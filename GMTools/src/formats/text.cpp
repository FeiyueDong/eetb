#include "../core/detail.hpp"
#include "text_provenance.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>

namespace gmtools {
namespace {

// Accept ASCII and well-formed UTF-8. Text controls other than line endings
// and tabs cannot be part of the supported numeric text formats.
bool valid_text(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i++]);
        if (lead < 0x80) {
            if ((lead < 0x20 && lead != '\r' && lead != '\n' && lead != '\t') || lead == 0x7f)
                return false;
            continue;
        }
        unsigned tails{};
        unsigned code{};
        unsigned minimum{};
        if (lead >= 0xc2 && lead <= 0xdf) {
            tails = 1; code = lead & 0x1f; minimum = 0x80;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            tails = 2; code = lead & 0x0f; minimum = 0x800;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            tails = 3; code = lead & 0x07; minimum = 0x10000;
        } else {
            return false;
        }
        if (text.size() - i < tails)
            return false;
        for (unsigned j = 0; j < tails; ++j) {
            const auto tail = static_cast<unsigned char>(text[i++]);
            if ((tail & 0xc0) != 0x80)
                return false;
            code = (code << 6) | (tail & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
            return false;
    }
    return true;
}

} // namespace

std::optional<std::string_view> text_view(std::span<const std::byte> bytes) {
    if (bytes.empty())
        return std::string_view{};
    std::string_view text{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    if (text.starts_with("\xef\xbb\xbf"))
        text.remove_prefix(3);
    if (!valid_text(text))
        return {};
    return text;
}

namespace detail {
namespace {

std::vector<std::string_view> cells(std::string_view line, Delimiter delimiter) {
    std::vector<std::string_view> result;
    if (delimiter == Delimiter::Whitespace) {
        line = trim(line);
        while (!line.empty()) {
            const auto split = line.find_first_of(" \t");
            result.push_back(line.substr(0, split));
            if (split == line.npos)
                break;
            line = trim(line.substr(split + 1));
        }
    } else {
        const char separator = delimiter == Delimiter::Comma ? ',' : '\t';
        for (;;) {
            const auto split = line.find(separator);
            result.push_back(trim(line.substr(0, split)));
            if (split == line.npos)
                break;
            line.remove_prefix(split + 1);
        }
    }
    return result;
}

bool is_column_label(std::string_view cell) {
    if (cell.empty())
        return false;
    const auto first = static_cast<unsigned char>(cell.front());
    if (!(first >= 0x80 || first == '_' || (first >= 'A' && first <= 'Z') ||
          (first >= 'a' && first <= 'z')))
        return false;
    const auto word = lower(cell);
    return word != "nan" && word != "inf" && word != "infinity" &&
           !word.starts_with("nan(") && cell.find('"') == cell.npos;
}

std::optional<double> integer_sample(std::string_view cell) {
    if (cell.starts_with('+')) {
        cell.remove_prefix(1);
        if (cell.starts_with('-'))
            return {};
    }
    if (cell.empty())
        return {};
    std::int32_t value{};
    const auto end = cell.data() + cell.size();
    const auto parsed = std::from_chars(cell.data(), end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end)
        return {};
    return static_cast<double>(value);
}

Delimiter delimiter_for(FormatId format, std::string_view first, Delimiter requested) {
    if (requested != Delimiter::Auto)
        return requested;
    if (format == FormatId::Csv)
        return Delimiter::Comma;
    if (format == FormatId::Tsv)
        return Delimiter::Tab;
    if (first.find(',') != first.npos)
        return Delimiter::Comma;
    if (first.find('\t') != first.npos) {
        const auto tab_cells = cells(first, Delimiter::Tab);
        if (std::ranges::none_of(tab_cells, [](auto cell) {
                return cell.find(' ') != cell.npos;
            }))
            return Delimiter::Tab;
    }
    return Delimiter::Whitespace;
}

std::vector<std::string> table_labels(std::string_view line, Delimiter delimiter, std::size_t width) {
    auto pieces = cells(line, delimiter);
    if (pieces.size() != width && delimiter == Delimiter::Tab && line.find('\t') == line.npos)
        pieces = cells(line, Delimiter::Whitespace);
    std::vector<std::string> labels;
    for (const auto piece : pieces) {
        if (!labels.empty() && (piece.starts_with('(') || piece.starts_with('[')))
            labels.back() += " " + std::string{piece};
        else
            labels.emplace_back(piece);
    }
    if (labels.size() != width || !std::ranges::all_of(labels, is_column_label))
        labels.clear();
    return labels;
}

struct LabelMeaning {
    bool time_seconds{};
    std::optional<Unit> physical_unit;
};

LabelMeaning meaning(std::string_view label) {
    const auto lower_label = lower(label);
    const std::string_view word{lower_label};
    const auto open = word.find_first_of("[(");
    if (open == word.npos || word.size() <= open + 2 ||
        word.back() != (word[open] == '[' ? ']' : ')'))
        return {};
    const auto kind = trim(word.substr(0, open));
    const auto unit = trim(word.substr(open + 1, word.size() - open - 2));
    if ((kind == "time" || kind == "t") && unit == "s")
        return {true, {}};
    if (auto quantity = quantity_from_name(kind == "accel" ? "acceleration" : kind)) {
        const auto parsed = unit_from_name(unit);
        if (parsed && unit_factor(*parsed, *parsed) && quantity_for(*parsed) == quantity)
            return {false, parsed};
    }
    return {};
}

struct TimeColumn {
    std::vector<std::pair<double, std::size_t>> rows;

    std::optional<double> step(Diagnostics& diagnostics, std::size_t column) const {
        if (rows.size() < 2)
            return {};
        const auto first = rows.front().first;
        // Use the full baseline to reduce cancellation for large time origins.
        // Accept this interval only if every point lies on one uniform grid.
        const auto candidate = (rows.back().first - first) / static_cast<double>(rows.size() - 1);
        if (!positive(candidate)) {
            error(diagnostics, "text", "time range or interval is out of range", rows.back().second, column);
            return {};
        }
        for (std::size_t i = 1; i < rows.size(); ++i) {
            const auto value = rows[i].first;
            const auto previous = rows[i - 1].first;
            const auto scale = std::max({std::abs(first), std::abs(previous), std::abs(value)});
            const auto tolerance = candidate * 1e-6 + std::min(candidate * 0.005,
                8.0 * std::numeric_limits<double>::epsilon() * scale);
            const auto elapsed = value - first;
            const auto expected = static_cast<double>(i) * candidate;
            if (value <= previous || std::abs((value - previous) - candidate) > 2.0 * tolerance ||
                std::abs(elapsed - expected) > tolerance) {
                error(diagnostics, "text", "time must increase at a uniform interval (seconds)",
                      rows[i].second, column);
                return {};
            }
        }
        return candidate;
    }
};

} // namespace

std::optional<FormatId> detect_text_table(std::string_view text) {
    std::string_view header;
    while (!text.empty()) {
        const auto end = text.find_first_of("\r\n");
        const auto line = text.substr(0, end);
        text = end == text.npos ? std::string_view{} : text.substr(end + 1);
        if (trim(line).empty())
            continue;
        if (header.empty()) {
            header = line;
            if (line.find_first_of(",\t") == line.npos)
                return {};
            continue;
        }
        for (const auto& [delimiter, format] : {std::pair{Delimiter::Comma, FormatId::Csv},
                                              {Delimiter::Tab, FormatId::Tsv}}) {
            const auto labels = cells(header, delimiter);
            if (labels.size() > 1 && std::ranges::all_of(labels, is_column_label)) {
                const auto numbers = cells(line, delimiter);
                if (numbers.size() == labels.size() && std::ranges::all_of(numbers, [](auto value) {
                        return parse_number(value).has_value();
                    }))
                    return format;
            }
        }
        break;
    }
    return {};
}

TextSuggestion suggest_text_options(std::string_view text, FormatId format, const ReadOptions& current) {
    TextSuggestion result;
    const auto source = lines(text);
    const auto& set = current.text;
    const auto last = set.last_line.value_or(source.size());
    const auto requested_first = set.first_line.value_or(1);
    if (requested_first == 0 || requested_first > last || last > source.size())
        return result;

    constexpr std::size_t max_header_lines = 24;
    const auto search_end = set.first_line ? last : std::min(last, max_header_lines + 1);
    std::size_t header_line{};
    if (set.first_line) {
        for (auto i = requested_first - 1; i > 0; --i) {
            if (!trim(source[i - 1]).empty()) {
                header_line = i;
                break;
            }
        }
    }
    std::size_t first{};
    std::size_t width{};
    Delimiter delimiter{Delimiter::Auto};
    for (std::size_t row = requested_first; row <= search_end; ++row) {
        const auto line = trim(source[row - 1]);
        if (line.empty())
            continue;
        delimiter = delimiter_for(format, source[row - 1], set.delimiter);
        const auto values = cells(source[row - 1], delimiter);
        if (std::ranges::all_of(values, [](auto token) { return parse_number(token).has_value(); })) {
            first = row;
            width = values.size();
            break;
        }
        // A malformed numeric-looking row is not evidence of a text preamble.
        const auto lead = line.front();
        if (set.first_line || (lead >= '0' && lead <= '9') || lead == '+' || lead == '-' || lead == '.')
            return result;
        header_line = row;
    }
    if (!first)
        return result;

    const auto labels = header_line ? table_labels(source[header_line - 1], delimiter, width)
                                    : std::vector<std::string>{};
    std::vector<LabelMeaning> semantics;
    std::vector<std::size_t> time_labels;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        semantics.push_back(meaning(labels[i]));
        if (semantics.back().time_seconds)
            time_labels.push_back(i + 1);
    }
    const bool columnar = set.layout != Layout::Sequential;
    std::optional<std::size_t> candidate_time;
    if (columnar && !set.time_column) {
        if (time_labels.size() == 1 && width > 1 &&
            std::ranges::find(set.data_columns, time_labels.front()) == set.data_columns.end()) {
            candidate_time = time_labels.front();
            result.evidence.push_back("Time column " + std::to_string(*candidate_time) + " from header " +
                                      labels[*candidate_time - 1]);
        } else if (!time_labels.empty()) {
            for (const auto column : time_labels)
                result.evidence.push_back("Candidate time column " + std::to_string(column) + " from header " +
                                          labels[column - 1]);
            warn(result.diagnostics, "suggestion", "time labels conflict with each other or the selected data columns",
                 header_line);
        }
    }
    if (semantics.empty() && std::none_of(source.begin() + static_cast<std::ptrdiff_t>(first),
            source.begin() + static_cast<std::ptrdiff_t>(last), [](auto line) { return !trim(line).empty(); }))
        return result;
    if (!set.first_line && header_line)
        result.evidence.push_back("First data line " + std::to_string(first) + " after the text preamble");

    // Use the same reader to verify the entire proposed data block and time
    // grid. Treat the shape used for validation as an internal choice when
    // the column meanings are not stated by the source.
    if (result.evidence.empty() && semantics.empty())
        return result;
    auto proposed = set;
    proposed.first_line = first;
    proposed.layout = set.layout.value_or(Layout::Columnar);
    if (candidate_time)
        proposed.time_column = candidate_time;
    const auto verified = parse_text(text, format, proposed);
    if (has_errors(verified.diagnostics)) {
        for (const auto& diagnostic : verified.diagnostics)
            warn(result.diagnostics, "suggestion", "candidate could not be verified: " + diagnostic.message,
                 diagnostic.line, diagnostic.column);
        return result;
    }
    if (!set.first_line && header_line)
        result.options.text.first_line = first;
    if (candidate_time && verified.data.traces.front().dt_seconds)
        result.options.text.time_column = candidate_time;
    else if (candidate_time)
        warn(result.diagnostics, "suggestion", "at least two time values are needed to verify the time grid",
             first, *candidate_time);

    if (!columnar || semantics.empty())
        return result;
    const auto time_column = set.time_column ? set.time_column : result.options.text.time_column;
    std::vector<std::size_t> selected = set.data_columns;
    if (selected.empty()) {
        for (std::size_t i = 1; i <= width; ++i) {
            if (i != time_column)
                selected.push_back(i);
        }
    }
    const bool unresolved_time_labels = !time_labels.empty() &&
        (!time_column || time_labels.size() != 1 || time_labels.front() != *time_column);
    const auto shared_unit = selected.empty() ? std::nullopt : semantics[selected.front() - 1].physical_unit;
    const bool uniform_units = shared_unit && std::ranges::all_of(selected, [&](auto index) {
                                   return semantics[index - 1].physical_unit == shared_unit;
                               });
    if (!current.overrides.unit && !uniform_units &&
        std::ranges::any_of(selected, [&](auto index) { return semantics[index - 1].physical_unit.has_value(); }))
        warn(result.diagnostics, "suggestion", "selected data columns do not share one declared physical unit",
             header_line);
    if (!set.layout && !unresolved_time_labels && (result.options.text.time_column || uniform_units))
        result.options.text.layout = Layout::Columnar;
    if (uniform_units && !unresolved_time_labels &&
        (!current.overrides.quantity || current.overrides.quantity == quantity_for(*shared_unit))) {
        if (!current.overrides.unit) {
            result.options.overrides.unit = shared_unit;
            result.evidence.push_back("Input unit " + std::string{name(*shared_unit)} + " from data column labels");
        }
        if (!current.overrides.quantity &&
            (!current.overrides.unit || compatible(quantity_for(*shared_unit), *current.overrides.unit)))
            result.options.overrides.quantity = quantity_for(*shared_unit);
    }
    return result;
}

ReadResult read_text(std::span<const std::byte> bytes, FormatId format, const TextParseOptions& options) {
    const auto text = text_view(bytes);
    if (text && text->starts_with("# GMTools waveform ")) {
        ReadResult result;
        try {
            auto pop_line = [](std::string_view& tail) {
                const auto end = tail.find('\n');
                const auto line = trim(tail.substr(0, end));
                tail = end == tail.npos ? std::string_view{} : tail.substr(end + 1);
                return line;
            };
            auto tail = *text;
            if (pop_line(tail) != "# GMTools waveform 1")
                throw std::runtime_error("unknown waveform header version");
            const auto header = pop_line(tail);
            if (!header.starts_with("# "))
                throw std::runtime_error("missing waveform metadata");
            const auto meta = nlohmann::json::parse(header.substr(2));
            TextParseOptions data;
            const bool timed = meta.at("time_basis") == "elapsed";
            if (!timed && meta.at("time_basis") != "values")
                throw std::runtime_error("invalid time basis");
            data.layout = Layout::Columnar;
            data.first_line = 1;
            auto delimiter = delimiter_from_name(meta.at("delimiter").get<std::string>());
            if (!delimiter || *delimiter == Delimiter::Auto)
                throw std::runtime_error("invalid delimiter");
            data.delimiter = *delimiter;
            if (timed)
                data.time_column = 1;
            if (pop_line(tail) != meta.at("columns").get<std::string>())
                throw std::runtime_error("column header mismatch");
            if (options.first_line || options.last_line || options.layout || options.time_column ||
                !options.data_columns.empty() || options.delimiter != Delimiter::Auto)
                throw std::runtime_error("explicit text layout conflicts with self-describing input");
            result = parse_text(tail, format, data);
            result.first_data_line = 4;
            for (auto& d : result.diagnostics)
                if (d.line)
                    d.line += 3;
            if (has_errors(result.diagnostics))
                return result;
            if (result.data.traces.size() != 1)
                throw std::runtime_error("expected one amplitude column");
            auto& trace = result.data.traces.front();
            if (!meta.at("npts").is_number_unsigned() || meta.at("npts") != trace.samples.size())
                throw std::runtime_error("NPTS disagrees with waveform");
            if (timed && (trace.relative_start_seconds != 0 || !meta.contains("dt")))
                throw std::runtime_error("elapsed time must start at zero with declared dt");
            if (meta.contains("dt")) {
                const auto dt = meta.at("dt").get<double>();
                if (!positive(dt) || (trace.dt_seconds && std::abs(*trace.dt_seconds - dt) > dt * 1e-9))
                    throw std::runtime_error("invalid dt or disagreement with time column");
                trace.dt_seconds = dt;
            }
            trace.relative_start_seconds.reset();
            if (meta.contains("relative_start"))
                trace.relative_start_seconds = meta.at("relative_start").get<double>();
            if (meta.contains("start_utc")) {
                trace.start_time = parse_utc(meta.at("start_utc").get<std::string>());
                if (!trace.start_time)
                    throw std::runtime_error("invalid start UTC");
            }
            trace.name = meta.at("name").get<std::string>();
            const auto unit = unit_from_name(meta.at("unit").get<std::string>());
            const auto quantity = quantity_from_name(meta.at("quantity").get<std::string>());
            if (!unit || !quantity || !compatible(*quantity, *unit))
                throw std::runtime_error("invalid quantity or unit");
            trace.unit = *unit;
            trace.quantity = *quantity;
            trace.attributes.clear();
            if (meta.contains("provenance"))
                read_text_provenance(meta.at("provenance"), trace);
            for (const auto* key :
                 {"station.network", "station.code", "station.location", "channel.code", "channel.source_id"})
                if (meta.contains(key))
                    trace.attributes[key] = meta.at(key).get<std::string>();
            if (trace.attributes.contains("channel.source_id")) {
                Metadata derived;
                import_sid(derived, trace.attributes.at("channel.source_id"));
                for (const auto& [key, value] : derived) {
                    if (trace.attributes.contains(key) && trace.attributes.at(key) != value)
                        throw std::runtime_error("SID and channel fields disagree");
                    trace.attributes[key] = value;
                }
            }
        } catch (const std::exception& e) {
            result.data.traces.clear();
            error(result.diagnostics, "text", e.what());
        }
        return result;
    }
    if (text)
        return parse_text(*text, format, options);
    ReadResult result;
    error(result.diagnostics, "text", "expected UTF-8/ASCII text without binary control bytes");
    return result;
}

ReadResult parse_text(std::string_view text, FormatId format, const TextParseOptions& options,
                      TextSampleKind kind) {
    ReadResult result;
    result.data.source_format = format;
    const auto source_lines = lines(text);
    const auto first = options.first_line.value_or(1);
    const auto last = options.last_line.value_or(source_lines.size());
    if (first == 0 || last < first || last > source_lines.size()) {
        error(result.diagnostics, "text", source_lines.empty() ? "no text data" :
              "data line range must be within the file (1–" + std::to_string(source_lines.size()) + ")");
        return result;
    }

    std::set<std::size_t> selected(options.data_columns.begin(), options.data_columns.end());
    if (selected.contains(0) || selected.size() != options.data_columns.size() ||
        (options.time_column && (*options.time_column == 0 || selected.contains(*options.time_column)))) {
        error(result.diagnostics, "text", "column indices must be distinct and positive; time cannot be a data column");
        return result;
    }
    const bool delimited_table = format == FormatId::Csv || format == FormatId::Tsv;
    const auto layout = options.layout.value_or(Layout::Columnar);
    if (layout == Layout::Sequential && (!selected.empty() || options.time_column)) {
        error(result.diagnostics, "text", "data columns and a time column require Columnar layout");
        return result;
    }
    std::size_t row = first - 1;
    while (row < last && trim(source_lines[row]).empty())
        ++row;
    if (row == last) {
        error(result.diagnostics, "text", "no numeric samples in the selected lines", first);
        return result;
    }

    const auto delimiter = delimiter_for(format, source_lines[row], options.delimiter);
    std::vector<std::string_view> labels;
    if (delimited_table && layout == Layout::Columnar) {
        if (!options.first_line) {
            auto candidate = cells(source_lines[row], delimiter);
            if (std::ranges::all_of(candidate, is_column_label)) {
                labels = std::move(candidate);
                ++row;
            }
        } else if (row > 0) {
            auto candidate = cells(source_lines[row - 1], delimiter);
            if (!candidate.empty() && std::ranges::all_of(candidate, is_column_label))
                labels = std::move(candidate);
        }
    }

    std::vector<std::size_t> data_columns = options.data_columns;
    std::vector<Trace> traces;
    TimeColumn time;
    std::size_t width{};
    std::size_t short_row{};
    for (; row < last; ++row) {
        const auto line = source_lines[row];
        if (trim(line).empty())
            continue;
        const auto values = cells(line, delimiter);
        const bool first_row = width == 0;
        if (first_row) {
            result.first_data_line = row + 1;
            width = values.size();
            if (delimited_table && !options.first_line && !labels.empty() && labels.size() != width) {
                error(result.diagnostics, "text", "column header width differs from data width", row + 1);
                return result;
            }
            if (layout == Layout::Sequential) {
                traces.resize(1);
            } else {
                if (data_columns.empty()) {
                    for (std::size_t i = 1; i <= width; ++i) {
                        if (i != options.time_column)
                            data_columns.push_back(i);
                    }
                }
                if (data_columns.empty() || options.time_column.value_or(0) > width ||
                    std::ranges::any_of(data_columns, [width](auto index) { return index > width; })) {
                    error(result.diagnostics, "text", "column selection is outside the data row or leaves no data columns", row + 1);
                    return result;
                }
                traces.resize(data_columns.size());
                if (labels.size() == width) {
                    for (std::size_t i = 0; i < traces.size(); ++i) {
                        traces[i].name = labels[data_columns[i] - 1];
                        traces[i].attributes["source.name"] = "text column header";
                    }
                }
            }
        }

        if (short_row || values.size() > width ||
            (layout == Layout::Columnar && values.size() != width)) {
            error(result.diagnostics, "text", "row width differs from the first data row",
                  short_row ? short_row : row + 1);
            return result;
        }
        if (values.size() < width)
            short_row = row + 1; // Only the final Sequential row may be short.

        std::vector<double> numbers;
        numbers.reserve(values.size());
        for (std::size_t c = 0; c < values.size(); ++c) {
            const auto value = kind == TextSampleKind::IntegerCounts ? integer_sample(values[c])
                                                                     : parse_number(values[c]);
            if (!value) {
                const auto message = kind == TextSampleKind::IntegerCounts ? "expected a signed integer count"
                    : first_row && !options.first_line
                        ? "expected a finite numeric value. If the file has a header, set the first data line"
                        : "expected a finite numeric value";
                error(result.diagnostics, "text", message, row + 1, c + 1);
                return result;
            }
            numbers.push_back(*value);
        }
        if (first_row && !options.layout && !options.time_column && selected.empty() &&
            !delimited_table && width > 1) {
            error(result.diagnostics, "text", "multiple values per row: choose Sequential or Columnar layout", row + 1);
            return result;
        }
        if (options.time_column)
            time.rows.emplace_back(numbers[*options.time_column - 1], row + 1);
        if (layout == Layout::Sequential)
            traces.front().samples.insert(traces.front().samples.end(), numbers.begin(), numbers.end());
        else
            for (std::size_t i = 0; i < data_columns.size(); ++i)
                traces[i].samples.push_back(numbers[data_columns[i] - 1]);
    }
    if (traces.empty()) {
        error(result.diagnostics, "text", "no numeric samples after the header", first);
        return result;
    }
    const auto step = options.time_column ? time.step(result.diagnostics, *options.time_column) : std::nullopt;
    if (has_errors(result.diagnostics))
        return result;
    for (auto& trace : traces) {
        if (options.time_column) {
            trace.relative_start_seconds = time.rows.front().first;
            trace.dt_seconds = step;
            trace.attributes["source.relative_start"] = "text time column (seconds)";
            if (step)
                trace.attributes["source.dt"] = "text time column (seconds)";
        }
    }
    result.data.traces = std::move(traces);
    return result;
}

WriteResult write_text(const Trace& trace, const WriteOptions& options) {
    WriteResult result;
    const auto format = options.format;
    const auto fixed = format == FormatId::Csv   ? Delimiter::Comma
                       : format == FormatId::Tsv ? Delimiter::Tab
                                                 : Delimiter::Whitespace;
    const bool table = format == FormatId::Csv || format == FormatId::Tsv;
    const auto delimiter = options.text_delimiter.value_or(fixed);
    const bool timed = options.text_time_values.value_or(false);
    const auto header = options.text_header.value_or(table ? TextHeader::Columns : TextHeader::None);
    if ((timed && !trace.dt_seconds) || delimiter == Delimiter::Auto || (table && fixed != delimiter) ||
        (options.precision && (!*options.precision || *options.precision > 17))) {
        error(result.diagnostics, "text",
              "time columns require dt, CSV/TSV use their native delimiter, precision must be 1..17");
        return result;
    }
    const char separator = delimiter == Delimiter::Comma ? ',' : delimiter == Delimiter::Tab ? '\t' : ' ';
    std::string label = "amplitude";
    const auto original = trim(trace.name);
    if (is_column_label(original) && original.find_first_of(",\t\r\n") == original.npos && valid_text(original))
        label = std::string{original};
    if (timed || options.text_header || options.output_unit) {
        label = trace.quantity == Quantity::Unknown ? "amplitude" : std::string{name(trace.quantity)};
        if (trace.unit != Unit::Unknown)
            label += "[" + std::string{name(trace.unit)} + "]";
    }
    const std::string columns = timed ? "Time[s]" + std::string(1, separator) + label : label;
    std::string output;
    if (header == TextHeader::Metadata) {
        nlohmann::json meta = {{"name", trace.name},
                               {"npts", trace.samples.size()},
                               {"unit", name(trace.unit)},
                               {"quantity", name(trace.quantity)},
                               {"delimiter", name(delimiter)},
                               {"columns", columns},
                               {"time_basis", timed ? "elapsed" : "values"}};
        if (trace.dt_seconds)
            meta["dt"] = *trace.dt_seconds;
        if (trace.relative_start_seconds)
            meta["relative_start"] = *trace.relative_start_seconds;
        if (trace.start_time)
            meta["start_utc"] = timestamp(*trace.start_time);
        for (const auto* key :
             {"station.network", "station.code", "station.location", "channel.code", "channel.source_id"})
            if (trace.attributes.contains(key))
                meta[key] = trace.attributes.at(key);
        try {
            write_text_provenance(meta, trace);
            output = "# GMTools waveform 1\n# " + meta.dump() + "\n";
        } catch (const std::exception& e) {
            error(result.diagnostics, "text", e.what());
            return result;
        }
        if (std::ranges::any_of(trace.attributes, [](const auto& attr) {
                const auto& key = attr.first;
                return key.starts_with("sac.") || key.starts_with("mseed.") || key.starts_with("modified.") ||
                       key.starts_with("knet.") || key.starts_with("peer.");
            }))
            warn(result.diagnostics, "text", "waveform header omits format-specific source fields");
    } else if (trace.dt_seconds || trace.start_time || trace.relative_start_seconds ||
               trace.unit != Unit::Unknown || !trace.attributes.empty()) {
        warn(result.diagnostics, "text",
             "plain output omits source metadata; use metadata header to retain timing and units");
    }
    if (header != TextHeader::None)
        output += columns + "\n";
    for (std::size_t i = 0; i < trace.samples.size(); ++i) {
        if (timed)
            output += number(static_cast<double>(i) * *trace.dt_seconds) + separator;
        if (options.precision) {
            char buffer[64];
            auto end = std::to_chars(buffer, buffer + sizeof buffer, trace.samples[i], std::chars_format::general,
                                     static_cast<int>(*options.precision));
            output.append(buffer, end.ptr);
        } else
            output += number(trace.samples[i]);
        output += '\n';
    }
    result.bytes = bytes_of(output);
    return result;
}

} // namespace detail
} // namespace gmtools
