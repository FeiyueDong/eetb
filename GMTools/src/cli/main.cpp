#include "gmtools/gmtools.hpp"

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

void usage(std::ostream& out) {
    out << "Usage:\n"
           "  gmtools formats\n"
           "  gmtools info INPUT [read options]\n"
           "  gmtools convert INPUT --to FORMAT -o PATH [read options] [--channel N] [--output-unit UNIT] [--overwrite]\n"
           "Run gmtools formats for available targets, including the optional miniSEED 2 target.\n"
           "Read options:\n"
           "  --format auto|text|dat|csv|tsv|at2|sac|mseed\n"
           "  --layout sequential|columnar  --delimiter auto|whitespace|comma|tab\n"
           "  --first-line N  --last-line N  --data-columns N[,N...]  --time-column N\n"
           "  --dt SECONDS | --fs HZ  --input-unit g|m/s2|cm/s2|counts|unknown\n"
           "  --quantity acceleration|velocity|displacement|counts|unknown  --numeric-factor FACTOR\n"
           "Lines and columns are 1-based. Time columns use seconds.\n";
}

struct Command {
    std::filesystem::path input;
    ReadOptions read;
    ExportOptions output;
};

template<class T>
T require(std::optional<T> value, std::string_view option, std::string_view expected) {
    if (!value)
        throw std::invalid_argument(std::string{option} + " requires " + std::string{expected});
    return *value;
}

Delimiter parse_delimiter(std::string_view value) {
    if (value == "auto") return Delimiter::Auto;
    if (value == "whitespace") return Delimiter::Whitespace;
    if (value == "comma") return Delimiter::Comma;
    if (value == "tab") return Delimiter::Tab;
    throw std::invalid_argument("--delimiter requires auto, whitespace, comma or tab");
}

Command parse(const Args& args, bool convert) {
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
            if (!command.input.empty())
                throw std::invalid_argument("provide exactly one input file");
            command.input = path_from_utf8(option);
            continue;
        }
        if (option == "-o")
            option = "--output";
        if (!seen.insert(option).second)
            throw std::invalid_argument("repeated option " + std::string{option});
        if (convert && option == "--overwrite") {
            command.output.overwrite = true;
            continue;
        }
        if (++i == args.size())
            throw std::invalid_argument("missing value for " + std::string{option});
        const std::string_view value = args[i];
        auto& read = command.read;
        auto& text = read.text;
        if (option == "--format")
            read.format = require(format_from_name(value), option, "a known input format");
        else if (option == "--first-line")
            text.first_line = require(parse_index(value), option, "a positive line number");
        else if (option == "--last-line")
            text.last_line = require(parse_index(value), option, "a positive line number");
        else if (option == "--layout") {
            if (value == "sequential")
                text.layout = Layout::Sequential;
            else if (value == "columnar")
                text.layout = Layout::Columnar;
            else
                throw std::invalid_argument("--layout requires sequential or columnar");
        } else if (option == "--delimiter")
            text.delimiter = parse_delimiter(value);
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
        else if (convert && option == "--to")
            command.output.writer.format = require(format_from_name(value), option, "a writable target format");
        else if (convert && option == "--output")
            command.output.output = path_from_utf8(value);
        else if (convert && option == "--channel")
            command.output.channels = {require(parse_index(value), option, "a positive channel number")};
        else if (convert && option == "--output-unit") {
            const auto unit = require(unit_from_name(value), option, "g, m/s2 or cm/s2");
            if (unit != Unit::G && unit != Unit::Mps2 && unit != Unit::Cmps2)
                throw std::invalid_argument("--output-unit requires g, m/s2 or cm/s2");
            command.output.writer.output_unit = unit;
        }
        else
            throw std::invalid_argument("unknown option " + std::string{option});
    }
    if (command.input.empty())
        throw std::invalid_argument("an input file is required");
    if (convert && (command.output.writer.format == FormatId::Auto || command.output.output.empty()))
        throw std::invalid_argument("convert requires --to FORMAT and --output PATH");
    command.read.extension_hint = path_to_utf8(command.input.extension());
    return command;
}

void print_diagnostics(const Diagnostics& diagnostics, const std::filesystem::path& file) {
    for (const auto& diagnostic : diagnostics)
        std::cerr << path_to_utf8(file) << ": " << describe(diagnostic) << '\n';
}

void print_attributes(const Metadata& attributes, std::string_view indent) {
    for (const auto& [key, value] : attributes)
        std::cout << indent << key << " = " << value << '\n';
}

void print_info(const ReadResult& result) {
    std::cout << "Format: " << name(result.data.source_format) << '\n';
    std::cout << "Traces: " << result.data.traces.size() << '\n';
    if (result.first_data_line)
        std::cout << "First data line: " << result.first_data_line << '\n';
    print_attributes(result.data.attributes, "  ");
    for (std::size_t i = 0; i < result.data.traces.size(); ++i) {
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

int run(const Args& args) {
    std::cout.imbue(std::locale::classic());
    std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
    if (args.size() == 1 || (args.size() == 2 && (args[1] == "--help" || args[1] == "-h"))) {
        usage(std::cout);
        return 0;
    }
    const auto& action = args[1];
    if (action == "formats" && args.size() == 2) {
        for (const auto& format : formats()) {
            std::cout << format.name << '\t' << (format.readable ? "read" : "—") << '\t'
                      << (format.writable ? "write" : "—") << '\t' << format.notes << '\n';
        }
        return 0;
    }
    if (action != "info" && action != "convert") {
        usage(std::cerr);
        return 2;
    }

    const auto command = parse(args, action == "convert");
    const auto input = load_file(command.input);
    if (has_errors(input.diagnostics)) {
        for (const auto& diagnostic : input.diagnostics)
            std::cerr << describe(diagnostic) << '\n';
        return 1;
    }
    const auto parsed = read(input.bytes, command.read);
    print_diagnostics(parsed.diagnostics, command.input);
    if (has_errors(parsed.diagnostics))
        return 1;
    if (action == "info") {
        print_info(parsed);
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
    Args args;
    for (int i = 0; i < argc; ++i) {
        const auto value = std::filesystem::path(argv[i]).u8string();
        args.emplace_back(value.begin(), value.end());
    }
    return main_impl(args);
}
#else
int main(int argc, char** argv) {
    return main_impl(Args(argv, argv + argc));
}
#endif
