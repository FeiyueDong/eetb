#include "../core/detail.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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

ReadResult read_text(std::span<const std::byte> bytes, FormatId format, const TextParseOptions& options) {
    const auto text = text_view(bytes);
    if (text)
        return parse_text(*text, format, options);
    ReadResult result;
    error(result.diagnostics, "text", "expected UTF-8/ASCII text without binary control bytes");
    return result;
}

ReadResult parse_text(std::string_view text, FormatId format, const TextParseOptions& options) {
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
    const double factor = options.numeric_factor.value_or(1.0);
    if (!std::isfinite(factor) || factor == 0.0) {
        error(result.diagnostics, "text", "numeric factor must be finite and nonzero");
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
            const auto value = parse_number(values[c]);
            if (!value) {
                error(result.diagnostics, "text", first_row && !options.first_line
                    ? "expected a finite numeric value. If the file has a header, set the first data line"
                    : "expected a finite numeric value", row + 1, c + 1);
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
        const auto append = [&](Trace& trace, std::size_t column) {
            const double original = numbers[column];
            const double sample = original * factor;
            if (!std::isfinite(sample) || (original != 0.0 && sample == 0.0)) {
                error(result.diagnostics, "text", "sample is out of range after applying the numeric factor",
                      row + 1, column + 1);
                return false;
            }
            trace.samples.push_back(sample);
            return true;
        };
        if (layout == Layout::Sequential) {
            for (std::size_t c = 0; c < numbers.size(); ++c) {
                if (!append(traces.front(), c))
                    return result;
            }
        } else {
            for (std::size_t i = 0; i < data_columns.size(); ++i) {
                if (!append(traces[i], data_columns[i] - 1))
                    return result;
            }
        }
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
        if (options.numeric_factor) {
            trace.attributes["text.numeric_factor"] = number(factor);
            trace.attributes["source.numeric_factor"] = "user input; applied to sample values";
        }
    }
    result.data.traces = std::move(traces);
    return result;
}

WriteResult write_text(const Trace& trace, FormatId format) {
    WriteResult result;
    std::string output;
    if (format == FormatId::Csv || format == FormatId::Tsv) {
        const auto label = trim(trace.name);
        output = is_column_label(label) && label.find_first_of(",\t\r\n") == label.npos &&
                          valid_text(label)
                     ? std::string{label} : "amplitude";
        if (!trace.name.empty() && output != trace.name)
            warn(result.diagnostics, "writer", "channel name cannot be represented as a plain column label");
        output += '\n';
    }
    for (double sample : trace.samples) {
        output += number(sample);
        output += '\n';
    }
    if (trace.dt_seconds || trace.relative_start_seconds || trace.start_time ||
        trace.unit != Unit::Unknown || trace.quantity != Quantity::Unknown || !trace.attributes.empty() ||
        ((format == FormatId::Text || format == FormatId::Dat) && !trace.name.empty())) {
        warn(result.diagnostics, "writer", "plain text waveforms do not encode timing, quantity, unit or format metadata");
    }
    result.bytes = bytes_of(output);
    return result;
}

} // namespace detail
} // namespace gmtools
