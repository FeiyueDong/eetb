#pragma once

#include "gmtools/gmtools.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace gmtools::detail {

using Reader = ReadResult (*)(std::span<const std::byte>, FormatId, const TextParseOptions&);
using Writer = WriteResult (*)(const Trace&, const WriteOptions&);
using Detector = bool (*)(std::span<const std::byte>);
enum class TextSampleKind { FloatingPoint, IntegerCounts };

std::string_view trim(std::string_view value);
std::string lower(std::string_view value);
std::vector<std::string_view> lines(std::string_view text);
std::string number(double value);
std::vector<std::byte> bytes_of(std::string_view text);
bool positive(double value);
bool compatible(Quantity quantity, Unit unit);
void save_text_file(const std::filesystem::path& path, std::string_view text, bool overwrite,
                    const std::filesystem::path& protected_input, Diagnostics& diagnostics);
void import_sid(Metadata& attributes, std::string_view sid);
bool valid_sid(std::string_view sid, bool v2);
std::string metadata_value(const Metadata&, const std::string& key);
Diagnostics process_read(Trace&, const ReadOptions&);
bool detect_modified(std::span<const std::byte>);
ReadResult read_modified(std::span<const std::byte>, FormatId, const TextParseOptions&);
FormatId peer_family(std::span<const std::byte>);
void error(Diagnostics& diagnostics, std::string source, std::string message,
           std::size_t line = 0, std::size_t column = 0);
void warn(Diagnostics& diagnostics, std::string source, std::string message,
          std::size_t line = 0, std::size_t column = 0);

ReadResult read_text(std::span<const std::byte>, FormatId, const TextParseOptions&);
std::optional<FormatId> detect_text_table(std::string_view);
TextSuggestion suggest_text_options(std::string_view, FormatId, const ReadOptions&);
ReadResult parse_text(std::string_view, FormatId, const TextParseOptions&,
                      TextSampleKind = TextSampleKind::FloatingPoint);
WriteResult write_text(const Trace&, const WriteOptions&);
ReadResult read_peer(std::span<const std::byte>, FormatId, const TextParseOptions&);
WriteResult write_peer(const Trace&, const WriteOptions&);
bool detect_knet(std::span<const std::byte>);
ReadResult read_knet(std::span<const std::byte>, FormatId, const TextParseOptions&);
Diagnostics validate_knet_overrides(const Trace&, const Overrides&);
bool detect_sac(std::span<const std::byte>);
ReadResult read_sac(std::span<const std::byte>, FormatId, const TextParseOptions&);
WriteResult write_sac(const Trace&, const WriteOptions&);
bool detect_mseed(std::span<const std::byte>);
#ifdef GMTOOLS_HAVE_LIBMSEED
ReadResult read_mseed(std::span<const std::byte>, FormatId, const TextParseOptions&);
WriteResult write_mseed(const Trace&, const WriteOptions&);
#endif

// Shared output planning used by both single-file export and batch conversion.
struct OutputPlan {
    FormatId format{FormatId::Auto};
    bool named_file{};
    std::filesystem::path directory;
    std::vector<std::size_t> channels; // 1-based
    std::vector<std::filesystem::path> targets;
};

std::optional<OutputPlan> plan_outputs(const FileData& data, const std::filesystem::path& input,
                                       const ExportOptions& options, Diagnostics& diagnostics);
bool same_location(const std::filesystem::path& left, const std::filesystem::path& right);
bool validate_targets(const OutputPlan& plan, std::span<const std::filesystem::path> protected_paths,
                      bool overwrite, std::span<const std::filesystem::path> reserved,
                      Diagnostics& diagnostics,
                      std::string_view protected_reason = "target refers to the input file");
std::vector<std::filesystem::path> publish_outputs(const FileData& data, const OutputPlan& plan,
                                                   const WriteOptions& writer, bool overwrite,
                                                   Diagnostics& diagnostics,
                                                   std::string* stage = nullptr);

} // namespace gmtools::detail
