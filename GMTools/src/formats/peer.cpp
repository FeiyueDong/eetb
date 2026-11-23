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

bool acceleration_header(std::string_view lower_line) {
    return lower_line.starts_with("acceleration") &&
        (lower_line.find("time series") != lower_line.npos ||
         lower_line.find("time history") != lower_line.npos);
}

} // namespace

bool detect_peer(std::span<const std::byte> bytes) {
    if (bytes.empty())
        return false;
    std::string_view prefix{reinterpret_cast<const char*>(bytes.data()),
                            std::min<std::size_t>(bytes.size(), 8192)};
    bool has_acceleration_header = false;
    bool has_count_or_dt = false;
    for (const auto line : lines(prefix)) {
        const auto lower_line = lower(trim(line));
        if (lower_line.find("peer nga") != lower_line.npos ||
            lower_line.find("pacific engineering and analysis strong-motion data") != lower_line.npos)
            return true;
        if (line.find('\0') != line.npos)
            break;
        const auto has_count = field(line, npts_field()).has_value();
        const auto has_dt = field(line, dt_field()).has_value();
        if (has_count && has_dt)
            return true;
        has_acceleration_header |= acceleration_header(lower_line);
        has_count_or_dt |= has_count || has_dt;
    }
    return has_acceleration_header && has_count_or_dt;
}

ReadResult read_peer(std::span<const std::byte> bytes, FormatId format, const TextParseOptions& options) {
    ReadResult result;
    result.data.source_format = format;
    const auto text = text_view(bytes);
    if (!text) {
        error(result.diagnostics, "peer", "expected a UTF-8/ASCII PEER AT2 file");
        return result;
    }

    const auto source_lines = lines(*text);
    std::optional<std::size_t> npts;
    std::optional<double> dt;
    Quantity quantity = Quantity::Unknown;
    Unit unit = Unit::Unknown;
    std::size_t header_row{};
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
        if (npts && dt) {
            header_row = i + 1;
            break;
        }
        const auto words = lower(trim(line));
        if (acceleration_header(words)) {
            quantity = Quantity::Acceleration;
            if (const auto start = words.find("units of "); start != words.npos) {
                auto label = trim(std::string_view{words}.substr(start + 9));
                if (label.ends_with('.'))
                    label.remove_suffix(1);
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
    if (!npts || !dt) {
        error(result.diagnostics, "peer", std::string{"missing PEER header field "} +
            (!npts && !dt ? "NPTS and DT" : (!npts ? "NPTS" : "DT")));
        return result;
    }

    TextParseOptions numeric = options;
    if (!numeric.first_line)
        numeric.first_line = header_row + 1;
    numeric.layout = numeric.layout.value_or(Layout::Sequential);
    if (numeric.delimiter == Delimiter::Auto)
        numeric.delimiter = Delimiter::Whitespace;
    result = parse_text(*text, format, numeric);
    if (has_errors(result.diagnostics))
        return result;
    if (result.data.traces.size() != 1) {
        result.data.traces.clear();
        error(result.diagnostics, "peer", "an AT2 record must contain one trace; use Text for a channel matrix");
        return result;
    }
    auto& trace = result.data.traces.front();
    if (trace.samples.size() != *npts)
        warn(result.diagnostics, "peer", "NPTS declares " + std::to_string(*npts) +
             " samples; read " + std::to_string(trace.samples.size()), npts_row);
    if (trace.dt_seconds && std::abs(*trace.dt_seconds - *dt) > *dt * 1e-6)
        warn(result.diagnostics, "peer", "time column disagrees with header DT; using header DT", dt_row);
    trace.dt_seconds = dt;
    trace.quantity = quantity;
    trace.unit = unit;
    trace.attributes["source.dt"] = "PEER header DT";
    trace.attributes["peer.npts"] = std::to_string(*npts);
    if (quantity != Quantity::Unknown)
        trace.attributes["source.quantity"] = "PEER header";
    else
        warn(result.diagnostics, "peer", "acceleration time-series label is missing");
    if (unit != Unit::Unknown)
        trace.attributes["source.unit"] = "PEER header";
    else
        warn(result.diagnostics, "peer", "acceleration unit is missing or unrecognized");
    if (!description.empty())
        trace.attributes["peer.description"] = description;
    return result;
}

WriteResult write_peer(const Trace& trace, FormatId) {
    WriteResult result;
    if (!trace.dt_seconds)
        error(result.diagnostics, "peer", "AT2 requires a known sampling interval; specify dt or fs");
    if (has_errors(result.diagnostics))
        return result;

    std::string description = "Converted by GMTools";
    if (const auto desc = trace.attributes.find("peer.description"); desc != trace.attributes.end())
        description = desc->second;
    else if (!trace.name.empty())
        description += " | " + trace.name;
    std::ranges::replace(description, '\n', ' ');
    std::ranges::replace(description, '\r', ' ');
    // Free-form descriptions must not look like structural header fields.
    if (!text_view(std::as_bytes(std::span{description})) || numeric_row(trim(description)) ||
        acceleration_header(lower(trim(description))) || field(description, npts_field()) ||
        field(description, dt_field()))
        description = "Converted by GMTools";

    std::string output = "GMTools PEER NGA FORMAT ACCELERATION RECORD\n" + description +
        "\nACCELERATION TIME SERIES IN UNITS OF G\nNPTS= " +
        std::to_string(trace.samples.size()) + ", DT= " + number(*trace.dt_seconds) + " SEC\n";
    for (std::size_t i = 0; i < trace.samples.size(); ++i) {
        output += number(trace.samples[i]);
        output += (i % 5 == 4 || i + 1 == trace.samples.size()) ? '\n' : ' ';
    }
    if (trace.relative_start_seconds || trace.start_time || !trace.name.empty() || !trace.attributes.empty())
        warn(result.diagnostics, "peer", "AT2 retains dt and acceleration in g; other timing and channel metadata may be lost");
    result.bytes = bytes_of(output);
    return result;
}

} // namespace gmtools::detail
