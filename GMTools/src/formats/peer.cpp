#include "../core/detail.hpp"

#include <algorithm>
#include <cmath>
#include <regex>

namespace gmtools::detail {
namespace {

const std::regex& npts_field() {
    static const std::regex pattern(R"(\bNPTS\s*=\s*([^,\s]*))", std::regex::icase);
    return pattern;
}

const std::regex& dt_field() {
    static const std::regex pattern(R"(\bDT\s*=\s*([^,\s]*))", std::regex::icase);
    return pattern;
}

std::optional<std::string_view> field(std::string_view line, const std::regex& pattern) {
    std::match_results<std::string_view::const_iterator> match;
    if (!std::regex_search(line.begin(), line.end(), match, pattern))
        return {};
    return {line.substr(static_cast<std::size_t>(match.position(1)),
                        static_cast<std::size_t>(match.length(1)))};
}

bool numeric_row(std::string_view line) {
    while (!line.empty()) {
        const auto split = line.find_first_of(" \t,");
        if (!parse_number(line.substr(0, split)))
            return false;
        if (split == line.npos)
            return true;
        line.remove_prefix(split + 1);
        const auto next = line.find_first_not_of(" \t,");
        if (next == line.npos)
            return true;
        line.remove_prefix(next);
    }
    return false;
}

FormatId declared_family(std::string_view line) {
    if (line.find("time series") == line.npos && line.find("time history") == line.npos)
        return FormatId::Auto;
    if (line.starts_with("acceleration "))
        return FormatId::At2;
    if (line.starts_with("velocity "))
        return FormatId::Vt2;
    if (line.starts_with("displacement "))
        return FormatId::Dt2;
    return FormatId::Auto;
}
} // namespace

FormatId peer_family(std::span<const std::byte> bytes) {
    std::string_view prefix{reinterpret_cast<const char*>(bytes.data()), std::min<std::size_t>(bytes.size(), 8192)};
    FormatId family = FormatId::Auto;
    bool structural = false;
    for (auto line : lines(prefix)) {
        const auto words = lower(trim(line));
        if (const auto found = declared_family(words); found != FormatId::Auto)
            family = found;
        if (field(line, dt_field()) || field(line, npts_field()))
            structural = true;
        if (structural && family != FormatId::Auto)
            return family;
        if (numeric_row(trim(line)))
            break;
    }
    return FormatId::Auto;
}

ReadResult read_peer(std::span<const std::byte> bytes, FormatId format, const TextParseOptions& options) {
    ReadResult result;
    result.data.source_format = format;
    const auto text = text_view(bytes);
    if (!text) {
        error(result.diagnostics, "peer", "expected a UTF-8/ASCII PEER waveform file");
        return result;
    }

    const auto source_lines = lines(*text);
    std::optional<std::size_t> npts;
    std::optional<double> dt;
    Quantity quantity = Quantity::Unknown;
    Unit unit = Unit::Unknown;
    std::size_t dt_row{};
    std::size_t npts_row{};
    std::string description;
    for (std::size_t i = 0; i < source_lines.size(); ++i) {
        const auto line = source_lines[i];
        if (const auto declared = field(line, npts_field())) {
            npts = parse_index(*declared);
            npts_row = i + 1;
            if (!npts) {
                error(result.diagnostics, "peer", "NPTS must be a positive integer", npts_row);
                return result;
            }
        }
        if (const auto declared = field(line, dt_field())) {
            dt = parse_number(*declared);
            dt_row = i + 1;
            if (!dt || !positive(*dt)) {
                error(result.diagnostics, "peer", "DT must be positive and finite (seconds)", dt_row);
                return result;
            }
        }
        if (npts && dt)
            break;
        const auto words = lower(trim(line));
        const auto declared = declared_family(words);
        if (declared != FormatId::Auto) {
            if (declared != format) {
                error(result.diagnostics, "peer", "declared quantity conflicts with requested PEER family", i + 1);
                return result;
            }
            quantity = declared == FormatId::At2   ? Quantity::Acceleration
                       : declared == FormatId::Vt2 ? Quantity::Velocity
                                                   : Quantity::Displacement;
            if (const auto start = words.find("units of "); start != words.npos) {
                auto label = trim(std::string_view{words}.substr(start + 9));
                label = trim(label.substr(0, label.find_first_of(".,;")));
                unit = unit_from_name(label).value_or(Unit::Unknown);
            }
        } else if (!trim(line).empty() && words.find("strong motion database") == words.npos &&
                   words.find("strong-motion data") == words.npos && !npts_row && !dt_row) {
            description = trim(line);
        }
        // Once a recognizable numeric row begins, a missing header field cannot follow it.
        if (numeric_row(trim(line)))
            break;
    }
    if (quantity == Quantity::Unknown || (unit != Unit::Unknown && quantity != quantity_for(unit))) {
        error(result.diagnostics, "peer", "missing quantity declaration or incompatible header unit");
        return result;
    }
    if (!dt) {
        error(result.diagnostics, "peer", "missing PEER header field DT");
        return result;
    }

    TextParseOptions numeric = options;
    const auto declared_first_line = std::max(npts_row, dt_row) + 1;
    if (!numeric.first_line)
        numeric.first_line = declared_first_line;
    numeric.layout = numeric.layout.value_or(Layout::Sequential);
    if (numeric.delimiter == Delimiter::Auto)
        numeric.delimiter = Delimiter::Whitespace;
    result = parse_text(*text, format, numeric);
    if (has_errors(result.diagnostics))
        return result;
    if (result.data.traces.size() != 1) {
        result.data.traces.clear();
        error(result.diagnostics, "peer", "a PEER record must contain one trace; use Text for a channel matrix");
        return result;
    }
    if ((options.first_line && options.first_line != declared_first_line) ||
        (options.last_line && options.last_line != source_lines.size()) ||
        (options.layout && options.layout != Layout::Sequential) ||
        (options.delimiter != Delimiter::Auto && options.delimiter != Delimiter::Whitespace))
        warn(result.diagnostics, "peer", "explicit text settings differ from the PEER data block. Check the selected range and layout",
             declared_first_line);
    auto& trace = result.data.traces.front();
    if (!npts)
        warn(result.diagnostics, "peer", "missing NPTS. Read " + std::to_string(trace.samples.size()) +
             " samples from the data rows", dt_row);
    else if (trace.samples.size() != *npts)
        warn(result.diagnostics, "peer", "NPTS declares " + std::to_string(*npts) +
             " samples; read " + std::to_string(trace.samples.size()), npts_row);
    if (trace.dt_seconds && std::abs(*trace.dt_seconds - *dt) > *dt * 1e-6)
        warn(result.diagnostics, "peer", "time column disagrees with header DT; using header DT", dt_row);
    trace.dt_seconds = dt;
    trace.quantity = quantity;
    trace.unit = unit;
    trace.attributes["source.dt"] = "PEER header DT";
    if (npts)
        trace.attributes["peer.npts"] = std::to_string(*npts);
    trace.attributes["source.quantity"] = "PEER header";
    if (unit != Unit::Unknown)
        trace.attributes["source.unit"] = "PEER header";
    else
        warn(result.diagnostics, "peer", "engineering unit is missing or unrecognized");
    if (!description.empty())
        trace.attributes["peer.description"] = description;
    return result;
}

WriteResult write_peer(const Trace& trace, const WriteOptions& options) {
    WriteResult result;
    std::string description = "Converted by GMTools";
    if (const auto desc = trace.attributes.find("peer.description"); desc != trace.attributes.end())
        description = desc->second;
    else if (!trace.name.empty())
        description += " | " + trace.name;
    std::ranges::replace(description, '\n', ' ');
    std::ranges::replace(description, '\r', ' ');
    // Free-form descriptions must not look like structural header fields.
    if (!text_view(std::as_bytes(std::span{description})) || numeric_row(trim(description)) ||
        declared_family(lower(trim(description))) != FormatId::Auto || field(description, npts_field()) ||
        field(description, dt_field()))
        description = "Converted by GMTools";

    const std::string quantity = options.format == FormatId::At2   ? "ACCELERATION"
                                 : options.format == FormatId::Vt2 ? "VELOCITY"
                                                                   : "DISPLACEMENT";
    const std::string unit = options.format == FormatId::At2   ? "G"
                             : options.format == FormatId::Vt2 ? "CM/S"
                                                               : "CM";
    std::string output = "GMTools PEER NGA FORMAT " + quantity + " RECORD\n" + description + "\n" + quantity +
                         " TIME SERIES IN UNITS OF " + unit + "\nNPTS= " + std::to_string(trace.samples.size()) +
                         ", DT= " + number(*trace.dt_seconds) + " SEC\n";
    for (std::size_t i = 0; i < trace.samples.size(); ++i) {
        output += number(trace.samples[i]);
        output += (i % 5 == 4 || i + 1 == trace.samples.size()) ? '\n' : ' ';
    }
    if (trace.relative_start_seconds || trace.start_time || !trace.name.empty() || !trace.attributes.empty())
        warn(result.diagnostics, "peer",
             "PEER retains dt and target engineering unit; other timing and channel metadata may be lost");
    result.bytes = bytes_of(output);
    return result;
}

} // namespace gmtools::detail
