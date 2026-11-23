#pragma once

#include "gmtools/gmtools.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace gmtools::detail {

using Reader = ReadResult (*)(std::span<const std::byte>, FormatId, const TextParseOptions&);
using Writer = WriteResult (*)(const Trace&, FormatId);
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
std::optional<double> acceleration_factor(Unit input, Unit output);
void error(Diagnostics& diagnostics, std::string source, std::string message,
           std::size_t line = 0, std::size_t column = 0);
void warn(Diagnostics& diagnostics, std::string source, std::string message,
          std::size_t line = 0, std::size_t column = 0);

ReadResult read_text(std::span<const std::byte>, FormatId, const TextParseOptions&);
std::optional<FormatId> detect_text_table(std::string_view);
TextSuggestion suggest_text_options(std::string_view, FormatId, const ReadOptions&);
ReadResult parse_text(std::string_view, FormatId, const TextParseOptions&,
                      TextSampleKind = TextSampleKind::FloatingPoint);
WriteResult write_text(const Trace&, FormatId);
bool detect_peer(std::span<const std::byte>);
ReadResult read_peer(std::span<const std::byte>, FormatId, const TextParseOptions&);
WriteResult write_peer(const Trace&, FormatId);
bool detect_knet(std::span<const std::byte>);
ReadResult read_knet(std::span<const std::byte>, FormatId, const TextParseOptions&);
Diagnostics validate_knet_overrides(const Trace&, const Overrides&);
bool detect_sac(std::span<const std::byte>);
ReadResult read_sac(std::span<const std::byte>, FormatId, const TextParseOptions&);
WriteResult write_sac(const Trace&, FormatId);
bool detect_mseed(std::span<const std::byte>);
#ifdef GMTOOLS_HAVE_LIBMSEED
ReadResult read_mseed(std::span<const std::byte>, FormatId, const TextParseOptions&);
WriteResult write_mseed(const Trace&, FormatId);
#endif

} // namespace gmtools::detail
