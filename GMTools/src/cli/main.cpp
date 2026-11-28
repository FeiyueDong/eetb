#include "gmtools/gmtools.hpp"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <set>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
using namespace gmtools;
using Args = std::vector<std::string>;

std::atomic<bool> stop_requested{false};

void request_stop(int) {
    stop_requested.store(true);
}

void usage(std::ostream& out) {
    out << "Usage:\n"
           "  gmtools formats\n"
           "  gmtools info INPUT [read options]\n"
           "  gmtools convert INPUT --to FORMAT -o PATH [read options] [--channel N] [--output-unit UNIT] "
           "[--overwrite]\n"
           "  gmtools convert INPUT --preset FILE -o PATH [options] [--overwrite]\n"
           "  gmtools batch INPUT... --to FORMAT -o DIR [options] [--overwrite]\n"
           "  gmtools batch --input-dir DIR [--filter PATTERN] --to FORMAT -o DIR [options] [--overwrite]\n"
           "Run gmtools formats for available targets, including the optional miniSEED 2 target.\n"
           "Read options:\n"
           "  --format auto|text|dat|csv|tsv|at2|vt2|dt2|knet-ascii|sac|mseed|modified-mseed\n"
           "  --profile FILE  apply a text JSON profile before explicit command options\n"
           "  --preset FILE  load a conversion preset; explicit options override its fields\n"
           "  --layout sequential|columnar  --delimiter auto|whitespace|comma|tab\n"
           "  --first-line N  --last-line N  --data-columns N[,N...]  --time-column N\n"
           "  --dt SECONDS | --fs HZ  --input-unit UNIT\n"
           "  --quantity acceleration|velocity|displacement|counts|unknown  --numeric-factor FACTOR\n"
           "  --calibration raw|source|manual  --calibration-factor UNIT_PER_COUNT --calibration-unit UNIT\n"
           "  --start-time UTC --relative-start SECONDS --network NET --station STA --location LOC | --clear-location\n"
           "  --channel-code CHA | --source-id FDSN:...   --sac-input-units keep|standard\n"
           "  --channel N selects a trace for identity edits and export. Empty metadata arguments clear fields.\n"
           "Writer options:\n"
           "  --text-layout values|time-values --text-header none|columns|metadata\n"
           "  --text-delimiter whitespace|tab|comma --precision 1..17\n"
           "  --sac-version 6|7 --sac-unit-mode keep|standard --mseed-encoding float64|float32|int32|steim2\n"
           "  --output-unit UNIT converts the written samples; --output-unit keep preserves the input unit.\n"
           "Batch options:\n"
           "  --input-dir DIR  collect files from one directory (not recursive)\n"
           "  --filter PATTERN  filename wildcard for --input-dir (default *)\n"
           "  --select-quantity QUANTITY  export only channels with that physical quantity\n"
           "  --select-all  export every channel (default)\n"
           "  -o/--output DIR must name a directory. --overwrite replaces existing outputs but never a source.\n"
           "Units: g, m/s2, cm/s2 (Gal), nm/s2; m/s, cm/s, mm/s, nm/s; m, cm, mm, nm; counts, unknown.\n"
           "Lines and columns are 1-based. Time columns use seconds.\n"
           "Exit codes: 0 success, 1 conversion/partial failure, 2 invalid arguments, 130 interrupted.\n";
}

enum class Action { Info, Convert, Batch };

struct Command {
    std::filesystem::path input;
    std::vector<std::filesystem::path> inputs;
    std::optional<std::filesystem::path> profile;
    std::optional<std::filesystem::path> preset;
    std::optional<std::filesystem::path> input_directory;
    ReadOptions read;
    ExportOptions output;
    std::optional<ChannelSelection> selection;
    std::string filter{"*"};
    std::set<std::string_view> seen;
    bool explicit_format{};
    bool explicit_delimiter{};
};

template<class T>
T require(std::optional<T> value, std::string_view option, std::string_view expected) {
    if (!value)
        throw std::invalid_argument(std::string{option} + " requires " + std::string{expected});
    return *value;
}

Command parse(const Args& args, Action action) {
    const bool convert = action == Action::Convert;
    const bool batch = action == Action::Batch;
    const bool writes = convert || batch;
    Command command;
    std::set<std::string_view> seen;
    command.output.writer.format = FormatId::Auto;
    bool end_options = false;
    for (std::size_t i = 2; i < args.size(); ++i) {
        std::string_view option = args[i];
        if (!end_options && option == "--") {
            end_options = true;
            continue;
        }
        if (end_options || !option.starts_with('-')) {
            if (batch) {
                command.inputs.push_back(path_from_utf8(option));
                continue;
            }
            if (!command.input.empty())
                throw std::invalid_argument("provide exactly one input file");
            command.input = path_from_utf8(option);
            continue;
        }
        if (option == "-o")
            option = "--output";
        if (!seen.insert(option).second)
            throw std::invalid_argument("repeated option " + std::string{option});
        if (option == "--clear-location") {
            if (batch)
                throw std::invalid_argument("batch does not accept per-file identity edits (--clear-location)");
            command.read.metadata.clear_location = true;
            continue;
        }
        if (option == "--overwrite") {
            if (!writes)
                throw std::invalid_argument("--overwrite requires convert or batch");
            command.output.overwrite = true;
            continue;
        }
        if (option == "--select-all") {
            if (!batch)
                throw std::invalid_argument("--select-all requires batch");
            command.selection = ChannelSelection{ChannelSelectionMode::All, Quantity::Unknown};
            continue;
        }
        if (++i == args.size())
            throw std::invalid_argument("missing value for " + std::string{option});
        const std::string_view value = args[i];
        auto& read = command.read;
        auto& text = read.text;
        auto& writer = command.output.writer;
        const auto choice = [&](std::initializer_list<std::string_view> values) {
            int index = 0;
            for (auto candidate : values) {
                if (candidate == value)
                    return index;
                ++index;
            }
            throw std::invalid_argument("invalid value for " + std::string{option});
        };
        if (option == "--format") {
            const auto format = require(format_from_name(value), option, "a known input format");
            if (batch && format != FormatId::Auto && !is_generic_text(format)) {
                const auto known = formats();
                const auto entry = std::ranges::find_if(known, [&](const auto& item) { return item.id == format; });
                if (entry != known.end() && !entry->readable)
                    throw std::invalid_argument("input format " + std::string{entry->name} +
                                                " is not readable in this build");
            }
            read.format = format;
        } else if (option == "--profile") {
            if (value.empty())
                throw std::invalid_argument("--profile requires a file path");
            command.profile = path_from_utf8(value);
        } else if (option == "--preset") {
            if (value.empty())
                throw std::invalid_argument("--preset requires a file path");
            if (!writes)
                throw std::invalid_argument("--preset requires convert or batch");
            command.preset = path_from_utf8(value);
        } else if (option == "--first-line")
            text.first_line = require(parse_index(value), option, "a positive line number");
        else if (option == "--last-line")
            text.last_line = require(parse_index(value), option, "a positive line number");
        else if (option == "--layout")
            text.layout = require(layout_from_name(value), option, "sequential or columnar");
        else if (option == "--delimiter")
            text.delimiter = require(delimiter_from_name(value), option, "auto, whitespace, comma or tab");
        else if (option == "--data-columns")
            text.data_columns = require(parse_indices(value), option, "comma-separated positive column numbers");
        else if (option == "--time-column")
            text.time_column = require(parse_index(value), option, "a positive column number");
        else if (option == "--dt")
            read.overrides.dt_seconds = require(parse_number(value), option, "a finite number");
        else if (option == "--fs")
            read.overrides.fs_hz = require(parse_number(value), option, "a finite number");
        else if (option == "--input-unit")
            read.overrides.unit = require(unit_from_name(value), option, "a known unit");
        else if (option == "--quantity")
            read.overrides.quantity = require(quantity_from_name(value), option, "a known physical quantity");
        else if (option == "--numeric-factor")
            text.numeric_factor = require(parse_number(value), option, "a finite number");
        else if (option == "--calibration")
            read.calibration = static_cast<Calibration>(1 + choice({"raw", "source", "manual"}));
        else if (option == "--calibration-factor")
            read.calibration_factor = require(parse_number(value), option, "a finite physical-unit/count ratio");
        else if (option == "--calibration-unit")
            read.calibration_unit = require(unit_from_name(value), option, "a physical unit");
        else if (option == "--sac-input-units")
            read.sac_standard_units = choice({"keep", "standard"}) == 1;
        else if (option == "--start-time" || option == "--relative-start" || option == "--network" ||
                 option == "--station" || option == "--location" || option == "--channel-code" ||
                 option == "--source-id" || option == "--channel") {
            if (batch)
                throw std::invalid_argument("batch does not accept per-file overrides such as " +
                                            std::string{option});
            if (option == "--start-time")
                read.metadata.start_utc = value;
            else if (option == "--relative-start")
                read.metadata.relative_start = value;
            else if (option == "--network")
                read.metadata.network = value;
            else if (option == "--station")
                read.metadata.station = value;
            else if (option == "--location")
                read.metadata.location = value;
            else if (option == "--channel-code")
                read.metadata.channel = value;
            else if (option == "--source-id")
                read.metadata.source_id = value;
            else {
                read.selected_channel = require(parse_index(value), option, "a positive channel number");
                command.output.channels = {*read.selected_channel};
            }
        } else if (writes && option == "--input-dir") {
            if (!batch)
                throw std::invalid_argument("--input-dir requires batch");
            command.input_directory = path_from_utf8(value);
        } else if (writes && option == "--filter") {
            if (!batch)
                throw std::invalid_argument("--filter requires batch");
            if (value.empty())
                throw std::invalid_argument("--filter requires a filename pattern");
            command.filter = std::string{value};
        } else if (writes && option == "--select-quantity") {
            if (!batch)
                throw std::invalid_argument("--select-quantity requires batch");
            const auto quantity = require(quantity_from_name(value), option, "a known physical quantity");
            if (quantity == Quantity::Unknown)
                throw std::invalid_argument("--select-quantity requires acceleration, velocity, displacement or counts");
            command.selection = ChannelSelection{ChannelSelectionMode::Quantity, quantity};
        } else if (writes && option == "--text-layout")
            writer.text_time_values = choice({"values", "time-values"}) == 1;
        else if (writes && option == "--text-header")
            writer.text_header = static_cast<TextHeader>(choice({"none", "columns", "metadata"}));
        else if (writes && option == "--text-delimiter")
            writer.text_delimiter = require(delimiter_from_name(value), option, "whitespace, comma or tab");
        else if (writes && option == "--precision")
            writer.precision = require(parse_index(value), option, "1..17");
        else if (writes && option == "--sac-version")
            writer.sac_version = 6 + choice({"6", "7"});
        else if (writes && option == "--sac-unit-mode")
            writer.sac_standard_units = choice({"keep", "standard"}) == 1;
        else if (writes && option == "--mseed-encoding")
            writer.mseed_encoding = static_cast<MseedEncoding>(choice({"float64", "float32", "int32", "steim2"}));
        else if (writes && option == "--to")
            command.output.writer.format = require(format_from_name(value), option, "a writable target format");
        else if (writes && option == "--output")
            command.output.output = path_from_utf8(value);
        else if (writes && option == "--output-unit") {
            if (value == "keep") {
                command.output.writer.output_unit.reset();
            } else {
                const auto unit = require(unit_from_name(value), option, "a physical unit or keep");
                if (!unit_factor(unit, unit))
                    throw std::invalid_argument("--output-unit requires a physical unit or keep");
                command.output.writer.output_unit = unit;
            }
        } else
            throw std::invalid_argument("unknown option " + std::string{option});
    }
    if (command.profile && command.preset)
        throw std::invalid_argument("convert accepts either --profile or --preset, not both");
    if (batch) {
        if (command.inputs.empty() && !command.input_directory)
            throw std::invalid_argument("batch requires input files or --input-dir DIR");
        if (!command.inputs.empty() && command.input_directory)
            throw std::invalid_argument("batch accepts either input files or --input-dir DIR, not both");
        if (seen.contains("--filter") && !command.input_directory)
            throw std::invalid_argument("--filter requires --input-dir DIR");
        if (command.output.output.empty())
            throw std::invalid_argument("batch requires --output DIR");
        if (!command.preset && command.output.writer.format == FormatId::Auto)
            throw std::invalid_argument("batch requires --to FORMAT or --preset FILE");
    } else if (action == Action::Convert) {
        if (command.input.empty())
            throw std::invalid_argument("an input file is required");
        if (command.output.output.empty() ||
            (!command.preset && command.output.writer.format == FormatId::Auto))
            throw std::invalid_argument("convert requires --to FORMAT and --output PATH");
    } else if (command.input.empty()) {
        throw std::invalid_argument("an input file is required");
    }
    command.seen = seen;
    command.explicit_format = seen.contains("--format");
    command.explicit_delimiter = seen.contains("--delimiter");
    command.read.extension_hint = path_to_utf8(command.input.extension());
    return command;
}

void print_diagnostics(const Diagnostics& diagnostics, const std::filesystem::path& file) {
    for (const auto& diagnostic : diagnostics)
        std::cerr << path_to_utf8(file) << ": " << describe(diagnostic) << '\n';
}

void print_suggestions(const TextSuggestion& suggestion, const std::filesystem::path& input) {
    for (const auto& evidence : suggestion.evidence)
        std::cerr << path_to_utf8(input) << ": suggestion: " << evidence << '\n';
    print_diagnostics(suggestion.diagnostics, input);
    const auto& options = suggestion.options;
    if (!options.text.first_line && !options.text.layout && !options.text.time_column &&
        !options.overrides.unit && !options.overrides.quantity)
        return;
    std::cerr << "Suggested input options";
    if (options.text.first_line) std::cerr << " --first-line " << *options.text.first_line;
    if (options.text.layout) std::cerr << " --layout " << name(*options.text.layout);
    if (options.text.time_column) std::cerr << " --time-column " << *options.text.time_column;
    if (options.overrides.quantity) std::cerr << " --quantity " << name(*options.overrides.quantity);
    if (options.overrides.unit) std::cerr << " --input-unit " << name(*options.overrides.unit);
    std::cerr << '\n';
}

void print_attributes(const Metadata& attributes, std::string_view indent) {
    for (const auto& [key, value] : attributes)
        std::cout << indent << key << " = " << value << '\n';
}

void print_info(const ReadResult& result, std::optional<std::size_t> selected) {
    std::cout << "Format: " << name(result.data.source_format) << '\n';
    std::cout << "Traces: " << result.data.traces.size() << '\n';
    if (result.first_data_line)
        std::cout << "First data line: " << result.first_data_line << '\n';
    print_attributes(result.data.attributes, "  ");
    for (std::size_t i = 0; i < result.data.traces.size(); ++i) {
        if (selected && *selected != i + 1)
            continue;
        const auto& trace = result.data.traces[i];
        std::cout << '[' << i + 1 << "] " << (trace.name.empty() ? "(unnamed)" : trace.name) << '\n';
        std::cout << "  samples = " << trace.samples.size() << '\n';
        if (trace.dt_seconds) {
            std::cout << "  dt = " << *trace.dt_seconds << " s\n";
            std::cout << "  first-to-last span = " << *trace.sample_span() << " s\n";
            std::cout << "  nominal record length = " << *trace.record_length() << " s\n";
        } else {
            std::cout << "  dt = unknown\n";
        }
        if (trace.start_time)
            std::cout << "  start = " << timestamp(*trace.start_time) << '\n';
        if (trace.relative_start_seconds)
            std::cout << "  relative start = " << *trace.relative_start_seconds << " s\n";
        std::cout << "  quantity = " << name(trace.quantity) << "\n  unit = " << name(trace.unit) << '\n';
        print_attributes(trace.attributes, "  ");
    }
}

void note(Diagnostics& diagnostics, std::string message) {
    diagnostics.push_back(Diagnostic{Severity::Warning, "preset", std::move(message), 0, 0, 0});
}

// Overlay explicitly provided command options onto a preset. Explicit settings
// win; changing the target format drops parameters that no longer apply.
Diagnostics overlay_preset(ConversionPreset& preset, const Command& command,
                           const std::set<std::string_view>& seen) {
    Diagnostics diagnostics;
    const auto given = [&](std::string_view option) { return seen.contains(option); };
    auto& read = preset.read;
    auto& writer = preset.write;
    const auto& explicit_read = command.read;

    if (given("--format")) {
        if (read.format != explicit_read.format)
            note(diagnostics, "explicit --format overrides the preset input format");
        read.format = explicit_read.format;
    }
    if (given("--first-line")) read.text.first_line = explicit_read.text.first_line;
    if (given("--last-line")) read.text.last_line = explicit_read.text.last_line;
    if (given("--layout")) read.text.layout = explicit_read.text.layout;
    if (given("--delimiter")) read.text.delimiter = explicit_read.text.delimiter;
    if (given("--data-columns")) read.text.data_columns = explicit_read.text.data_columns;
    if (given("--time-column")) read.text.time_column = explicit_read.text.time_column;
    if (given("--numeric-factor")) read.text.numeric_factor = explicit_read.text.numeric_factor;
    if (given("--dt") || given("--fs")) {
        read.overrides.dt_seconds = explicit_read.overrides.dt_seconds;
        read.overrides.fs_hz = explicit_read.overrides.fs_hz;
    }
    if (given("--input-unit")) read.overrides.unit = explicit_read.overrides.unit;
    if (given("--quantity")) read.overrides.quantity = explicit_read.overrides.quantity;
    if (given("--calibration")) read.calibration = explicit_read.calibration;
    if (given("--calibration-factor")) read.calibration_factor = explicit_read.calibration_factor;
    if (given("--calibration-unit")) read.calibration_unit = explicit_read.calibration_unit;
    if (given("--sac-input-units")) read.sac_standard_units = explicit_read.sac_standard_units;

    if (given("--to")) {
        const auto target = command.output.writer.format;
        if (writer.format != FormatId::Auto && writer.format != target) {
            note(diagnostics, "explicit --to overrides the preset target format");
            if (!is_generic_text(target)) {
                writer.text_time_values.reset();
                writer.text_header.reset();
                writer.text_delimiter.reset();
                writer.precision.reset();
            }
            if (target != FormatId::Sac) {
                writer.sac_version.reset();
                writer.sac_standard_units.reset();
            }
            if (target != FormatId::Mseed && target != FormatId::Mseed2)
                writer.mseed_encoding.reset();
        }
        writer.format = target;
    }
    if (given("--output-unit")) writer.output_unit = command.output.writer.output_unit;
    if (given("--text-layout")) writer.text_time_values = command.output.writer.text_time_values;
    if (given("--text-header")) writer.text_header = command.output.writer.text_header;
    if (given("--text-delimiter")) writer.text_delimiter = command.output.writer.text_delimiter;
    if (given("--precision")) writer.precision = command.output.writer.precision;
    if (given("--sac-version")) writer.sac_version = command.output.writer.sac_version;
    if (given("--sac-unit-mode")) writer.sac_standard_units = command.output.writer.sac_standard_units;
    if (given("--mseed-encoding")) writer.mseed_encoding = command.output.writer.mseed_encoding;
    if (command.selection)
        preset.selection = *command.selection;
    try {
        (void)encode_preset(preset);
    } catch (const std::exception& problem) {
        diagnostics.push_back(Diagnostic{Severity::Error, "preset", problem.what(), 0, 0, 0});
    }
    return diagnostics;
}

bool wildcard_match(std::string_view pattern, std::string_view name) {
    std::size_t p = 0, n = 0;
    std::size_t star = std::string_view::npos;
    std::size_t mark = 0;
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == name[n])) {
            ++p;
            ++n;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = n;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            n = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*')
        ++p;
    return p == pattern.size();
}

std::vector<std::filesystem::path> collect_inputs(const Command& command) {
    std::vector<std::filesystem::path> collected;
    if (command.input_directory) {
        std::error_code ec;
        std::vector<std::filesystem::path> entries;
        for (std::filesystem::directory_iterator it(*command.input_directory, ec), end; !ec && it != end;
             it.increment(ec)) {
            std::error_code type_error;
            if (!it->is_regular_file(type_error) || type_error)
                continue;
            const auto filename = path_to_utf8(it->path().filename());
            if (!wildcard_match(command.filter, filename))
                continue;
            entries.push_back(it->path());
        }
        if (ec) {
            std::cerr << path_to_utf8(*command.input_directory) << ": " << ec.message() << '\n';
            return {};
        }
        std::ranges::sort(entries, [](const auto& left, const auto& right) {
            return path_to_utf8(left.filename()) < path_to_utf8(right.filename());
        });
        collected = std::move(entries);
    } else {
        collected = command.inputs;
    }

    std::vector<std::filesystem::path> unique;
    for (const auto& candidate : collected) {
        bool duplicate = false;
        for (const auto& kept : unique) {
            std::error_code ec;
            if (std::filesystem::equivalent(kept, candidate, ec) && !ec) {
                duplicate = true;
                break;
            }
            if (kept.lexically_normal() == candidate.lexically_normal())
                duplicate = true;
        }
        if (duplicate) {
            std::cerr << path_to_utf8(candidate) << ": skipping duplicate input\n";
            continue;
        }
        unique.push_back(candidate);
    }
    return unique;
}

int run_batch(Command& command) {
    ConversionPreset preset;
    preset.write.format = FormatId::Auto;
    if (command.preset) {
        auto loaded = load_preset(*command.preset);
        print_diagnostics(loaded.diagnostics, *command.preset);
        if (has_errors(loaded.diagnostics))
            return 1;
        preset = std::move(loaded.preset);
    }
    auto merged = overlay_preset(preset, command, command.seen);
    for (const auto& diagnostic : merged) {
        if (command.preset && !command.preset->empty())
            std::cerr << path_to_utf8(*command.preset) << ": " << describe(diagnostic) << '\n';
        else
            std::cerr << "gmtools: " << describe(diagnostic) << '\n';
    }
    if (has_errors(merged))
        return 2;

    BatchOptions batch;
    batch.inputs = collect_inputs(command);
    if (batch.inputs.empty()) {
        std::cerr << "gmtools: no input files were collected\n";
        return 2;
    }
    batch.preset = std::move(preset);
    batch.output_directory = command.output.output;
    batch.overwrite = command.output.overwrite;

    std::set<std::size_t> printed;
    const auto progress = [&](const BatchResult& result) {
        for (std::size_t i = 0; i < result.files.size(); ++i) {
            const auto& file = result.files[i];
            if (file.state == BatchFileState::Pending || file.state == BatchFileState::Running ||
                printed.contains(i))
                continue;
            printed.insert(i);
            std::cout << path_to_utf8(file.input) << ": " << name(file.state);
            if (!file.written.empty())
                std::cout << ", " << file.written.size() << " file(s) written";
            if (file.state != BatchFileState::Succeeded) {
                const auto failure = std::ranges::find_if(file.diagnostics, [](const Diagnostic& diagnostic) {
                    return diagnostic.severity == Severity::Error;
                });
                if (failure != file.diagnostics.end())
                    std::cout << " (" << failure->message << ')';
            }
            std::cout << '\n';
            for (const auto& diagnostic : file.diagnostics)
                std::cerr << path_to_utf8(file.input) << ": " << describe(diagnostic) << '\n';
        }
    };
    const auto result = convert_batch(batch, progress, [] { return stop_requested.load(); });
    for (const auto& diagnostic : result.diagnostics)
        std::cerr << "gmtools: " << describe(diagnostic) << '\n';
    if (has_errors(result.diagnostics))
        return 2;

    const auto& summary = result.summary;
    std::cout << "Batch: " << summary.succeeded << " succeeded, " << summary.partial << " partial, "
              << summary.failed << " failed, " << summary.cancelled << " cancelled; "
              << summary.files_written << " file(s) written\n";
    if (summary.stopped) {
        std::cout << "Stopped after the current file; unfinished inputs were cancelled.\n";
        return 130;
    }
    return (summary.failed || summary.partial || summary.cancelled) ? 1 : 0;
}

int run(const Args& args) {
    std::cout.imbue(std::locale::classic());
    std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
    if (args.size() == 1 || (args.size() == 2 && (args[1] == "--help" || args[1] == "-h"))) {
        usage(std::cout);
        return 0;
    }
    const auto& action_name = args[1];
    if (action_name == "formats" && args.size() == 2) {
        for (const auto& format : formats()) {
            std::cout << format.name << '\t' << (format.readable ? "read" : "—") << '\t'
                      << (format.writable ? "write" : "—") << '\t' << format.notes << '\n';
        }
        return 0;
    }
    if (action_name != "info" && action_name != "convert" && action_name != "batch") {
        usage(std::cerr);
        return 2;
    }
    const Action action = action_name == "info" ? Action::Info
                         : action_name == "convert" ? Action::Convert
                                                    : Action::Batch;
    auto command = parse(args, action);
    if (action == Action::Batch)
        return run_batch(command);

    if (command.preset) {
        auto loaded = load_preset(*command.preset);
        print_diagnostics(loaded.diagnostics, *command.preset);
        if (has_errors(loaded.diagnostics))
            return 1;
        auto merged = overlay_preset(loaded.preset, command, command.seen);
        print_diagnostics(merged, *command.preset);
        if (has_errors(merged))
            return 1;
        // Identity edits and --channel are per-file settings: the preset never
        // carries them, so keep the ones given on this command line.
        const auto metadata = command.read.metadata;
        const auto selected = command.read.selected_channel;
        command.read = loaded.preset.read;
        command.read.metadata = metadata;
        command.read.selected_channel = selected;
        command.read.extension_hint = path_to_utf8(command.input.extension());
        command.output.writer = loaded.preset.write;
    }
    if (command.profile) {
        const auto profile = load_profile(*command.profile);
        print_diagnostics(profile.diagnostics, *command.profile);
        if (has_errors(profile.diagnostics))
            return 1;
        auto merged = merge_profile(profile.options, command.read, command.explicit_format, command.explicit_delimiter);
        print_diagnostics(merged.diagnostics, *command.profile);
        command.read = std::move(merged.options);
    }
    const auto input = load_file(command.input);
    if (has_errors(input.diagnostics)) {
        for (const auto& diagnostic : input.diagnostics)
            std::cerr << describe(diagnostic) << '\n';
        return 1;
    }
    const auto parsed = read(input.bytes, command.read);
    print_diagnostics(parsed.diagnostics, command.input);
    if (is_generic_text(parsed.data.source_format)) {
        auto suggested_read = command.read;
        suggested_read.format = parsed.data.source_format;
        print_suggestions(suggest_text(input.bytes, suggested_read), command.input);
    }
    if (has_errors(parsed.diagnostics))
        return 1;
    if (action == Action::Info) {
        print_info(parsed, command.read.selected_channel);
        return 0;
    }
    const auto output = export_file(parsed.data, command.input, command.output);
    print_diagnostics(output.diagnostics, command.input);
    for (const auto& path : output.written)
        std::cout << path_to_utf8(path) << '\n';
    return has_errors(output.diagnostics) ? 1 : 0;
}

int main_impl(const Args& args) {
    try {
        return run(args);
    } catch (const std::invalid_argument& problem) {
        std::cerr << "gmtools: " << problem.what() << '\n';
        return 2;
    } catch (const std::exception& problem) {
        std::cerr << "gmtools: " << problem.what() << '\n';
        return 1;
    }
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    std::signal(SIGINT, request_stop);
    Args args;
    for (int i = 0; i < argc; ++i) {
        const auto value = std::filesystem::path(argv[i]).u8string();
        args.emplace_back(value.begin(), value.end());
    }
    return main_impl(args);
}
#else
int main(int argc, char** argv) {
    std::signal(SIGINT, request_stop);
    return main_impl(Args(argv, argv + argc));
}
#endif
