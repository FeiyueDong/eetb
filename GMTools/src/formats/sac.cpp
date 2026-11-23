#include "../core/detail.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace gmtools::detail {
namespace {

// Word offsets and enum values from the SAC binary format specification.
constexpr std::size_t header_words = 158;
constexpr std::size_t footer_words = 22;
constexpr int undefined_int = -12345;
constexpr float undefined_float = -12345.0f;
constexpr std::size_t delta_word = 0;
constexpr std::size_t begin_word = 5;
constexpr std::size_t end_word = 6;
constexpr std::size_t version_word = 76;
constexpr std::size_t npts_word = 79;
constexpr std::size_t type_word = 85;
constexpr std::size_t dependent_word = 86;
constexpr std::size_t even_word = 105;
constexpr std::size_t channel_word = 150;
constexpr int time_series = 1;
constexpr int unknown_dependent = 5;

struct LabelField {
    std::string_view key;
    std::size_t word;
    std::size_t width;
};

constexpr std::array label_fields{
    LabelField{"sac.kstnm", 110, 8}, LabelField{"sac.kevnm", 112, 16},
    LabelField{"sac.khole", 116, 8}, LabelField{"sac.knetwk", 152, 8},
    LabelField{"sac.kinst", 156, 8},
};

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);

template<class UInt>
UInt get(std::span<const std::byte> bytes, std::size_t offset, bool little) {
    UInt value{};
    for (std::size_t i = 0; i < sizeof(UInt); ++i) {
        const auto index = little ? sizeof(UInt) - 1 - i : i;
        value = (value << 8) | std::to_integer<unsigned>(bytes[offset + index]);
    }
    return value;
}

std::int32_t integer(std::span<const std::byte> bytes, std::size_t word, bool little) {
    return std::bit_cast<std::int32_t>(get<std::uint32_t>(bytes, word * 4, little));
}

float real(std::span<const std::byte> bytes, std::size_t word, bool little) {
    return std::bit_cast<float>(get<std::uint32_t>(bytes, word * 4, little));
}

template<class UInt>
void put(std::span<std::byte> bytes, std::size_t offset, UInt value) {
    for (std::size_t i = 0; i < sizeof(UInt); ++i) {
        bytes[offset + i] = static_cast<std::byte>(value & 0xff);
        value >>= 8;
    }
}

std::optional<bool> byte_order(std::span<const std::byte> bytes) {
    if (bytes.size() < header_words * 4)
        return {};
    for (const bool little : {true, false}) {
        const auto version = integer(bytes, version_word, little);
        if (version == 6 || version == 7)
            return little;
    }
    return {};
}

std::string header_text(std::span<const std::byte> bytes, std::size_t word, std::size_t width = 8) {
    std::string_view value{reinterpret_cast<const char*>(bytes.data() + word * 4), width};
    value = trim(value.substr(0, value.find('\0')));
    if (value == "-12345")
        return {};
    // Standard SAC character headers are ASCII.
    std::string result;
    for (const auto c : value)
        result += c >= 0x20 && c <= 0x7e ? c : '?';
    return result;
}

bool representable_label(std::string_view label, std::size_t width) {
    return !label.empty() && label.size() <= width && label != "-12345" &&
           std::ranges::all_of(label, [](char c) { return c >= 0x20 && c <= 0x7e; });
}

Quantity dependent_quantity(int dependent) {
    switch (dependent) {
    case 6: return Quantity::Displacement;
    case 7: return Quantity::Velocity;
    case 8: return Quantity::Acceleration;
    default: return Quantity::Unknown;
    }
}

std::optional<TimePoint> sac_start(std::span<const std::byte> bytes, bool little, double begin) {
    using namespace std::chrono;
    const auto y = integer(bytes, 70, little);
    const auto jday = integer(bytes, 71, little);
    const auto h = integer(bytes, 72, little);
    const auto m = integer(bytes, 73, little);
    const auto s = integer(bytes, 74, little);
    const auto ms = integer(bytes, 75, little);
    if (y < 1 || y > 9999 || jday < 1 || jday > 365 + int(year{y}.is_leap()) ||
        h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59 || ms < 0 || ms > 999)
        return {};
    const auto reference = sys_days{year{y}/January/1} + days{jday - 1} +
        hours{h} + minutes{m} + seconds{s} + milliseconds{ms};
    constexpr auto per_ms = 1'000'000;
    const auto reference_ms = reference.time_since_epoch().count();
    const auto begin_ns = std::round(static_cast<long double>(begin) * 1e9L);
    const auto limit = std::ldexp(1.0L, 63);
    if (reference_ms < std::numeric_limits<std::int64_t>::min() / per_ms ||
        reference_ms > std::numeric_limits<std::int64_t>::max() / per_ms ||
        begin_ns < -limit || begin_ns >= limit)
        return {};
    const auto reference_ns = reference_ms * per_ms;
    const auto offset = static_cast<std::int64_t>(begin_ns);
    if ((offset > 0 && reference_ns > std::numeric_limits<std::int64_t>::max() - offset) ||
        (offset < 0 && reference_ns < std::numeric_limits<std::int64_t>::min() - offset))
        return {};
    return TimePoint{nanoseconds{reference_ns + offset}};
}

} // namespace

bool detect_sac(std::span<const std::byte> bytes) {
    const auto order = byte_order(bytes);
    if (!order)
        return false;
    const auto type = integer(bytes, type_word, *order);
    const auto even = integer(bytes, even_word, *order);
    return (type == 1 || type == 2 || type == 3 || type == 4 || type == 51) &&
           (even == 0 || even == 1);
}

ReadResult read_sac(std::span<const std::byte> bytes, FormatId format, const TextParseOptions&) {
    ReadResult result;
    result.data.source_format = format;
    const auto order = byte_order(bytes);
    if (!order) {
        error(result.diagnostics, "sac", "expected a complete binary SAC header with NVHDR 6 or 7");
        return result;
    }
    const auto little = *order;
    if (integer(bytes, type_word, little) != time_series || integer(bytes, even_word, little) != 1) {
        error(result.diagnostics, "sac", "only evenly spaced ITIME records are supported (IFTYPE=1, LEVEN=1)");
        return result;
    }
    const auto version = integer(bytes, version_word, little);
    const auto npts = integer(bytes, npts_word, little);
    const std::size_t overhead = header_words * 4 + (version == 7 ? footer_words * 8 : 0);
    if (npts <= 0 || bytes.size() < overhead ||
        static_cast<std::uint64_t>(npts) * 4 != bytes.size() - overhead) {
        error(result.diagnostics, "sac", "NPTS does not match file size (including the v7 footer, if present)");
        return result;
    }
    const auto footer = bytes.size() - (version == 7 ? footer_words * 8 : 0);
    const auto precise = [&](std::size_t word, std::size_t footer_word) -> double {
        if (version == 7)
            return std::bit_cast<double>(get<std::uint64_t>(bytes, footer + footer_word * 8, little));
        return real(bytes, word, little);
    };
    const double dt = precise(delta_word, 0);
    const double begin = precise(begin_word, 1);
    const double end = precise(end_word, 2);
    if (!positive(dt)) {
        error(result.diagnostics, "sac", "DELTA must be positive and finite");
        return result;
    }
    if (begin != undefined_float && !std::isfinite(begin)) {
        error(result.diagnostics, "sac", "B must be finite or undefined");
        return result;
    }

    Trace trace;
    trace.dt_seconds = dt;
    trace.attributes["source.dt"] = "SAC DELTA";
    trace.name = header_text(bytes, channel_word);
    if (!trace.name.empty())
        trace.attributes["source.name"] = "SAC KCMPNM";
    if (begin != undefined_float) {
        trace.relative_start_seconds = begin;
        trace.start_time = sac_start(bytes, little, begin);
        trace.attributes["source.relative_start"] = "SAC B";
        if (trace.start_time)
            trace.attributes["source.start_time"] = "SAC reference time plus B";
        else if (integer(bytes, 70, little) != undefined_int)
            warn(result.diagnostics, "sac", "SAC reference date/time is invalid or outside the supported range");
        const auto expected = std::fma(npts - 1.0, dt, begin);
        // SAC stores this independently, and v6 rounds the three header fields to float32.
        const double epsilon = version == 6 ? std::numeric_limits<float>::epsilon()
                                            : std::numeric_limits<double>::epsilon();
        const auto tolerance = 8.0 * epsilon * std::max({std::abs(expected), std::abs(begin), dt});
        if (end != undefined_float && (!std::isfinite(end) || std::abs(end - expected) > tolerance))
            warn(result.diagnostics, "sac", "E disagrees with B, DELTA and NPTS");
    }

    const auto dependent = integer(bytes, dependent_word, little);
    trace.quantity = dependent_quantity(dependent);
    if (trace.quantity != Quantity::Unknown) {
        trace.attributes["source.quantity"] = "SAC IDEP";
        warn(result.diagnostics, "sac", "IDEP identifies the quantity; confirm the engineering unit explicitly");
    }
    for (const auto& [key, word, width] : label_fields) {
        const auto value = header_text(bytes, word, width);
        if (!value.empty())
            trace.attributes[std::string{key}] = value;
    }
    result.data.attributes["sac.nvhdr"] = std::to_string(version);
    result.data.attributes["sac.byte_order"] = little ? "little-endian" : "big-endian";
    const auto scale = real(bytes, 3, little);
    if (std::isfinite(scale) && scale != undefined_float) {
        trace.attributes["sac.scale"] = number(scale);
        if (scale != 1.0)
            warn(result.diagnostics, "sac", "SCALE recorded as metadata; samples were read unchanged");
    }

    trace.samples.reserve(static_cast<std::size_t>(npts));
    for (std::size_t i = 0; i < static_cast<std::size_t>(npts); ++i) {
        const auto sample = real(bytes, header_words + i, little);
        if (!std::isfinite(sample)) {
            error(result.diagnostics, "sac", "nonfinite sample at index " + std::to_string(i + 1));
            return result;
        }
        trace.samples.push_back(sample);
    }
    result.data.traces.push_back(std::move(trace));
    return result;
}

WriteResult write_sac(const Trace& trace, FormatId) {
    using namespace std::chrono;
    WriteResult result;
    if (!trace.dt_seconds)
        error(result.diagnostics, "sac", "SAC requires a known sampling interval");
    if (trace.samples.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) ||
        trace.samples.size() > (result.bytes.max_size() - header_words * 4 - footer_words * 8) / 4)
        error(result.diagnostics, "sac", "sample count exceeds the SAC file limit");
    if (has_errors(result.diagnostics))
        return result;

    double begin = trace.relative_start_seconds.value_or(0.0);
    std::optional<sys_time<milliseconds>> reference;
    if (trace.start_time) {
        reference = time_point_cast<milliseconds>(*trace.start_time);
        const auto residual = *trace.start_time - *reference;
        begin = duration<double>(residual).count();
        if (trace.relative_start_seconds && *trace.relative_start_seconds != begin)
            warn(result.diagnostics, "sac", "relative origin adjusted to retain the absolute first-sample time");
    }
    const auto dt = *trace.dt_seconds;
    const auto end = std::fma(static_cast<double>(trace.samples.size() - 1), dt, begin);
    const auto float_max = std::numeric_limits<float>::max();
    if (dt > float_max || !positive(static_cast<float>(dt)) || std::abs(begin) > float_max ||
        !std::isfinite(end) || std::abs(end) > float_max || begin == undefined_float ||
        end == undefined_float) {
        error(result.diagnostics, "sac", "timing cannot be represented in the SAC header");
        return result;
    }

    const auto footer = (header_words + trace.samples.size()) * 4;
    auto& bytes = result.bytes;
    bytes.resize(footer + footer_words * 8);
    const auto set_int = [&](std::size_t word, std::int32_t value) {
        put(std::span{bytes}, word * 4, std::bit_cast<std::uint32_t>(value));
    };
    const auto set_float = [&](std::size_t word, float value) {
        put(std::span{bytes}, word * 4, std::bit_cast<std::uint32_t>(value));
    };
    for (std::size_t word = 0; word < 70; ++word)
        set_float(word, undefined_float);
    for (std::size_t word = 70; word < 110; ++word)
        set_int(word, undefined_int);
    auto set_text = [&](std::size_t word, std::size_t width, std::string_view value) {
        for (std::size_t i = 0; i < width; ++i)
            bytes[word * 4 + i] = static_cast<std::byte>(i < value.size() ? value[i] : ' ');
    };
    for (std::size_t word = 110; word < header_words; word += word == 112 ? 4 : 2)
        set_text(word, word == 112 ? 16 : 8, "-12345");

    set_int(version_word, 7);
    set_int(npts_word, static_cast<std::int32_t>(trace.samples.size()));
    set_int(type_word, time_series);
    set_int(dependent_word, unknown_dependent);
    set_int(even_word, 1);
    for (std::size_t word = 106; word <= 108; ++word)
        set_int(word, 0);
    set_float(delta_word, static_cast<float>(dt));
    set_float(begin_word, static_cast<float>(begin));
    set_float(end_word, static_cast<float>(end));

    if (reference) {
        const auto day = floor<days>(*reference);
        const year_month_day date{day};
        const hh_mm_ss clock{*reference - day};
        set_int(70, int(date.year()));
        set_int(71, static_cast<std::int32_t>((day - sys_days{date.year()/January/1}).count() + 1));
        set_int(72, static_cast<std::int32_t>(clock.hours().count()));
        set_int(73, static_cast<std::int32_t>(clock.minutes().count()));
        set_int(74, static_cast<std::int32_t>(clock.seconds().count()));
        set_int(75, static_cast<std::int32_t>(clock.subseconds().count()));
    }
    const auto label = trim(trace.name);
    if (representable_label(label, 8)) {
        set_text(channel_word, 8, label);
    } else if (!trace.name.empty()) {
        warn(result.diagnostics, "sac", "KCMPNM requires an ASCII channel name of at most eight bytes");
    }
    std::size_t retained_attributes{};
    for (const auto& [key, word, width] : label_fields) {
        const auto attribute = trace.attributes.find(std::string{key});
        if (attribute != trace.attributes.end() && representable_label(attribute->second, width)) {
            set_text(word, width, attribute->second);
            ++retained_attributes;
        }
    }

    bool rounded = false;
    for (std::size_t i = 0; i < trace.samples.size(); ++i) {
        const auto original = trace.samples[i];
        if (std::abs(original) > float_max || (original != 0.0 && static_cast<float>(original) == 0.0f)) {
            bytes.clear();
            error(result.diagnostics, "sac", "sample is outside float32 range at index " + std::to_string(i + 1));
            return result;
        }
        const auto sample = static_cast<float>(original);
        rounded |= sample != original;
        set_float(header_words + i, sample);
    }
    for (std::size_t i = 0; i < footer_words; ++i)
        put(std::span{bytes}, footer + i * 8, std::bit_cast<std::uint64_t>(double{undefined_float}));
    put(std::span{bytes}, footer, std::bit_cast<std::uint64_t>(dt));
    put(std::span{bytes}, footer + 8, std::bit_cast<std::uint64_t>(begin));
    put(std::span{bytes}, footer + 16, std::bit_cast<std::uint64_t>(end));

    if (rounded)
        warn(result.diagnostics, "sac", "SAC waveform samples were rounded to float32");
    if (trace.quantity != Quantity::Unknown || trace.unit != Unit::Unknown)
        warn(result.diagnostics, "sac", "physical quantity and engineering unit are not written; sample magnitudes retain their original unit");
    if (retained_attributes < trace.attributes.size())
        warn(result.diagnostics, "sac", "some source attributes are not written");
    return result;
}

} // namespace gmtools::detail
