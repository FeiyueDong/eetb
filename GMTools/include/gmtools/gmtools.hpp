#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gmtools {

enum class FormatId { Auto, Text, Dat, Csv, Tsv, At2, Sac, Mseed, Mseed2 };
enum class Quantity { Unknown, Acceleration, Velocity, Displacement, Counts };
enum class Unit { Unknown, G, Mps2, Cmps2, Counts };
enum class Severity { Warning, Error };
enum class Layout { Sequential, Columnar };
enum class Delimiter { Auto, Whitespace, Comma, Tab };

using TimePoint = std::chrono::sys_time<std::chrono::nanoseconds>;
using Metadata = std::map<std::string, std::string>;

struct Diagnostic {
    Severity severity;
    std::string source;
    std::string message;
    std::size_t line{};
    std::size_t column{};
    std::size_t channel{};
};
using Diagnostics = std::vector<Diagnostic>;

struct Trace {
    std::string name;
    std::vector<double> samples;
    std::optional<double> dt_seconds;
    std::optional<double> relative_start_seconds;
    std::optional<TimePoint> start_time;
    Quantity quantity{Quantity::Unknown};
    Unit unit{Unit::Unknown};
    Metadata attributes;

    std::optional<double> sample_span() const;
    std::optional<double> record_length() const;
};

struct FileData {
    FormatId source_format{FormatId::Auto};
    std::vector<Trace> traces;
    Metadata attributes;
};

struct TextParseOptions {
    std::optional<std::size_t> first_line;
    std::optional<std::size_t> last_line;
    std::optional<Layout> layout;
    Delimiter delimiter{Delimiter::Auto};
    std::vector<std::size_t> data_columns;
    std::optional<std::size_t> time_column;
    std::optional<double> numeric_factor;
};

struct Overrides {
    std::optional<double> dt_seconds;
    std::optional<double> fs_hz;
    std::optional<Quantity> quantity;
    std::optional<Unit> unit;
};

struct ReadOptions {
    FormatId format{FormatId::Auto};
    TextParseOptions text;
    Overrides overrides;
    std::string extension_hint;
};

struct ReadResult {
    FileData data;
    Diagnostics diagnostics;
    std::size_t first_data_line{};
};

struct WriteOptions {
    FormatId format{FormatId::Text};
    std::optional<Unit> output_unit;
};

struct WriteResult {
    std::vector<std::byte> bytes;
    Diagnostics diagnostics;
};

struct InputFile {
    std::vector<std::byte> bytes;
    Diagnostics diagnostics;
};

struct ExportOptions {
    WriteOptions writer;
    std::filesystem::path output;
    std::vector<std::size_t> channels; // 1-based; empty selects every Trace.
    bool overwrite{};
};

struct ExportResult {
    std::vector<std::filesystem::path> written;
    Diagnostics diagnostics;
};

struct FormatInfo {
    FormatId id;
    std::string_view name;
    std::string_view extension;
    bool readable;
    bool writable;
    std::string_view notes;
};

std::vector<FormatInfo> formats();
std::optional<FormatId> format_from_name(std::string_view name);
std::string_view name(FormatId format);
std::string_view name(Quantity quantity);
std::string_view name(Unit unit);
std::optional<Quantity> quantity_from_name(std::string_view value);
std::optional<Unit> unit_from_name(std::string_view value);
std::optional<double> parse_number(std::string_view word);
std::optional<std::size_t> parse_index(std::string_view word);
std::optional<std::vector<std::size_t>> parse_indices(std::string_view list);
bool has_errors(const Diagnostics& diagnostics);
std::string describe(const Diagnostic& diagnostic);
std::string timestamp(TimePoint time);

InputFile load_file(const std::filesystem::path& path);
std::optional<std::string_view> text_view(std::span<const std::byte> bytes);
ReadResult read(std::span<const std::byte> bytes, const ReadOptions& options = {});
Diagnostics apply_overrides(Trace& trace, const Overrides& overrides);
WriteResult write(const Trace& trace, const WriteOptions& options);
ExportResult export_file(const FileData& data, const std::filesystem::path& input,
                         const ExportOptions& options);

std::filesystem::path path_from_utf8(std::string_view value);
std::string path_to_utf8(const std::filesystem::path& value);

} // namespace gmtools
