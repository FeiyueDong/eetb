#include "../core/detail.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>

namespace gmtools::detail {
namespace {

struct Field {
    std::string_view value;
    std::size_t line{};
};

constexpr std::array signature{"Origin Time", "Station Code", "Sampling Freq(Hz)", "Dir.", "Scale Factor"};
constexpr std::size_t header_limit = 32;
constexpr auto pretrigger = std::chrono::seconds{15};
constexpr auto jst_offset = std::chrono::hours{9};

std::optional<std::string_view> field_value(std::string_view line, std::string_view key) {
    line = trim(line);
    if (!line.starts_with(key))
        return {};
    line.remove_prefix(key.size());
    if (!line.empty() && line.front() != ' ' && line.front() != '\t')
        return {};
    return trim(line);
}

std::optional<double> sampling_rate(std::string_view value) {
    if (lower(value).ends_with("hz"))
        value = trim(value.substr(0, value.size() - 2));
    const auto hz = parse_number(value);
    if (!hz || !positive(*hz) || !positive(1.0 / *hz))
        return {};
    return hz;
}

std::optional<double> scale_factor(std::string_view value) {
    const auto open = value.find('(');
    const auto close = value.find(')');
    if (open == value.npos || close == value.npos || open >= close)
        return {};
    const auto unit = unit_from_name(value.substr(open + 1, close - open - 1));
    const auto numerator = parse_number(value.substr(0, open));
    auto denominator = trim(value.substr(close + 1));
    if (!denominator.starts_with('/') || unit != Unit::Cmps2 || !numerator || !positive(*numerator))
        return {};
    const auto divisor = parse_number(denominator.substr(1));
    if (!divisor || !positive(*divisor) || !positive(*numerator / *divisor))
        return {};
    return *numerator / *divisor;
}

std::optional<TimePoint> first_sample_time(std::string_view value) {
    using namespace std::chrono;
    if (value.size() != 19 || value[4] != '/' || value[7] != '/' ||
        value[10] != ' ' || value[13] != ':' || value[16] != ':')
        return {};
    const auto digits = [&](std::size_t pos, std::size_t length) {
        if (value.substr(pos, length).find_first_not_of("0123456789") != value.npos)
            return -1;
        int result{};
        const auto first = value.data() + pos;
        const auto end = first + length;
        const auto parsed = std::from_chars(first, end, result);
        return parsed.ec == std::errc{} && parsed.ptr == end && result >= 0 ? result : -1;
    };
    const auto y = digits(0, 4);
    const auto month_number = digits(5, 2);
    const auto day_number = digits(8, 2);
    const auto hh = digits(11, 2);
    const auto mm = digits(14, 2);
    const auto ss = digits(17, 2);
    if (y < 1 || month_number < 1 || day_number < 1 ||
        hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59)
        return {};
    const auto date = year{y}/month{static_cast<unsigned>(month_number)}/day{static_cast<unsigned>(day_number)};
    if (!date.ok())
        return {};
    const auto utc = sys_days{date} + hours{hh} + minutes{mm} + seconds{ss} - jst_offset - pretrigger;
    constexpr std::int64_t ticks_per_second = 1'000'000'000;
    const auto ticks = utc.time_since_epoch().count();
    if (ticks < std::numeric_limits<std::int64_t>::min() / ticks_per_second ||
        ticks > std::numeric_limits<std::int64_t>::max() / ticks_per_second)
        return {};
    return TimePoint{duration_cast<nanoseconds>(utc.time_since_epoch())};
}

std::string_view channel_label(std::string_view direction) {
    constexpr std::array<std::string_view, 6> kik{"NS1", "EW1", "UD1", "NS2", "EW2", "UD2"};
    if (direction.size() == 1 && direction.front() >= '1' && direction.front() <= '6')
        return kik[static_cast<std::size_t>(direction.front() - '1')];
    if (std::ranges::find(kik, direction) != kik.end())
        return direction;
    if (direction == "NS" || direction == "N-S") return "NS";
    if (direction == "EW" || direction == "E-W") return "EW";
    if (direction == "UD" || direction == "U-D") return "UD";
    return {};
}

} // namespace

bool detect_knet(std::span<const std::byte> bytes) {
    if (bytes.empty())
        return false;
    std::string_view prefix{reinterpret_cast<const char*>(bytes.data()),
                            std::min<std::size_t>(bytes.size(), 4096)};
    if (prefix.starts_with("\xef\xbb\xbf"))
        prefix.remove_prefix(3);
    std::array<bool, signature.size()> matched{};
    const auto header = lines(prefix);
    for (std::size_t row = 0; row < std::min(header_limit, header.size()); ++row) {
        const auto line = header[row];
        if (line.find('\0') != line.npos || field_value(line, "Memo."))
            break;
        for (std::size_t i = 0; i < signature.size(); ++i)
            matched[i] |= field_value(line, signature[i]).has_value();
    }
    return std::ranges::count(matched, true) >= 4;
}

ReadResult read_knet(std::span<const std::byte> bytes, FormatId format, const TextParseOptions& options) {
    ReadResult result;
    const auto text = text_view(bytes);
    if (!text) {
        error(result.diagnostics, "knet", "expected a UTF-8/ASCII K-NET text record");
        return result;
    }
    constexpr std::array header_keys{
        "Origin Time", "Station Code", "Record Time", "Sampling Freq(Hz)", "Duration Time(s)",
        "Dir.", "Scale Factor", "Max. Acc. (gal)", "Lat.", "Lon.", "Long.", "Depth. (km)",
        "Mag.", "Station Lat.", "Station Lon.", "Station Long.", "Station Height(m)", "Last Correction"
    };
    const auto rows = lines(*text);
    std::map<std::string_view, Field> fields;
    std::size_t first{};
    for (std::size_t row = 0; row < std::min(header_limit, rows.size()); ++row) {
        const auto line = trim(rows[row]);
        if (line.empty())
            continue;
        if (field_value(line, "Memo.")) {
            first = row + 2;
            break;
        }
        bool known = false;
        for (const auto key : header_keys) {
            if (const auto value = field_value(line, key)) {
                known = true;
                if (!fields.try_emplace(key, Field{*value, row + 1}).second) {
                    error(result.diagnostics, "knet", "duplicate header field " + std::string{key}, row + 1);
                    return result;
                }
                break;
            }
        }
        if (!known) {
            error(result.diagnostics, "knet", "unrecognized header line before Memo.", row + 1);
            return result;
        }
    }
    if (!first) {
        error(result.diagnostics, "knet", "missing Memo. header terminator");
        return result;
    }
    if (first > rows.size()) {
        error(result.diagnostics, "knet", "no integer samples after Memo.", first);
        return result;
    }
    const auto get = [&](std::string_view key) -> Field {
        const auto it = fields.find(key);
        return it == fields.end() ? Field{} : it->second;
    };
    const auto rate = get("Sampling Freq(Hz)");
    const auto scale = get("Scale Factor");
    const auto direction = get("Dir.");
    const auto station = get("Station Code");
    const auto hz = sampling_rate(rate.value);
    const auto coeff = scale_factor(scale.value);
    const auto channel = channel_label(direction.value);
    if (!hz)
        error(result.diagnostics, "knet", "Sampling Freq(Hz) must specify a positive finite sampling rate", rate.line);
    if (!coeff)
        error(result.diagnostics, "knet", "Scale Factor must be a positive gal/count ratio, e.g. 3920(gal)/6182761",
              scale.line);
    if (station.value.empty() || station.value.find_first_of(" \t") != station.value.npos)
        error(result.diagnostics, "knet", "Station Code is missing or invalid", station.line);
    if (channel.empty())
        error(result.diagnostics, "knet", "Dir. is missing or unrecognized", direction.line);
    if ((options.first_line && options.first_line != first) ||
        (options.last_line && options.last_line != rows.size()) ||
        (options.layout && options.layout != Layout::Sequential) ||
        (options.delimiter != Delimiter::Auto && options.delimiter != Delimiter::Whitespace) ||
        options.time_column || !options.data_columns.empty()) {
        error(result.diagnostics, "knet", "text layout options conflict with the K-NET waveform block. Select a general text format to reinterpret it", first);
    }
    if (options.numeric_factor && (!std::isfinite(*options.numeric_factor) || *options.numeric_factor == 0.0))
        error(result.diagnostics, "knet", "explicit numeric factor must be finite and nonzero");
    if (has_errors(result.diagnostics))
        return result;

    TextParseOptions numeric;
    numeric.first_line = first;
    numeric.layout = Layout::Sequential;
    numeric.delimiter = Delimiter::Whitespace;
    result = parse_text(*text, format, numeric, TextSampleKind::IntegerCounts);
    if (has_errors(result.diagnostics))
        return result;

    auto& trace = result.data.traces.front();
    trace.name = channel;
    trace.dt_seconds = 1.0 / *hz;
    trace.quantity = Quantity::Acceleration;
    trace.unit = Unit::Cmps2;
    const auto factor = options.numeric_factor.value_or(1.0);
    for (std::size_t i = 0; i < trace.samples.size(); ++i) {
        const auto count = trace.samples[i];
        const auto calibrated = count * *coeff;
        const auto sample = calibrated * factor;
        if (!std::isfinite(calibrated) || !std::isfinite(sample) || (count != 0.0 && sample == 0.0)) {
            error(result.diagnostics, "knet", "sample exceeds numeric range after scaling at index " +
                  std::to_string(i + 1));
            result.data.traces.clear();
            return result;
        }
        trace.samples[i] = sample;
    }

    auto& metadata = trace.attributes;
    metadata["knet.station_code"] = station.value;
    metadata["knet.direction"] = direction.value;
    if (channel.ends_with('1') || channel.ends_with('2'))
        metadata["knet.sensor_position"] = channel.ends_with('1') ? "borehole" : "surface";
    metadata["knet.sampling_frequency_hz"] = number(*hz);
    metadata["knet.raw_unit"] = "counts";
    metadata["knet.scale_factor"] = scale.value;
    metadata["knet.scale_factor_cmps2_per_count"] = number(*coeff);
    metadata["source.dt"] = "NIED Sampling Freq(Hz)";
    metadata["source.name"] = "NIED Dir.";
    metadata["source.quantity"] = "NIED K-NET ASCII acceleration";
    metadata["source.unit"] = "NIED Scale Factor applied to counts";
    if (options.numeric_factor) {
        metadata["text.numeric_factor"] = number(factor);
        metadata["source.numeric_factor"] = "user input applied after NIED instrument calibration";
    }

    const auto record = get("Record Time");
    trace.start_time = first_sample_time(record.value);
    metadata["knet.time_zone"] = "JST (UTC+09:00)";
    metadata["knet.pretrigger_seconds"] = "15";
    if (trace.start_time)
        metadata["source.start_time"] = "NIED Record Time in JST minus 15 s pretrigger, expressed in UTC";
    else
        warn(result.diagnostics, "knet", "Record Time is missing, invalid or out of range. First-sample time is unknown",
             record.line);
    for (const auto& [key, attribute] : {std::pair{"Origin Time", "knet.origin_time_jst"},
            {"Record Time", "knet.record_time_jst"}, {"Duration Time(s)", "knet.duration_header_seconds"},
            {"Max. Acc. (gal)", "knet.offset_removed_max_gal"}, {"Station Lat.", "knet.station_latitude"},
            {"Station Height(m)", "knet.station_height_m"}, {"Last Correction", "knet.last_correction_jst"}}) {
        const auto field = get(key);
        if (!field.value.empty())
            metadata[attribute] = field.value;
    }
    auto longitude = get("Station Long.");
    if (longitude.value.empty())
        longitude = get("Station Lon.");
    if (!longitude.value.empty())
        metadata["knet.station_longitude"] = longitude.value;
    const auto duration = get("Duration Time(s)");
    if (const auto seconds = parse_number(duration.value); seconds && positive(*seconds)) {
        const auto expected = *seconds * *hz;
        if (!std::isfinite(expected) || std::abs(expected - static_cast<double>(trace.samples.size())) > 1.0)
            warn(result.diagnostics, "knet", "Duration Time(s) differs from the actual sample count. All samples retained",
                 duration.line);
    } else if (duration.line) {
        warn(result.diagnostics, "knet", "Duration Time(s) is invalid", duration.line);
    }
    return result;
}

Diagnostics validate_knet_overrides(const Trace& trace, const Overrides& options) {
    Diagnostics diagnostics;
    if (!trace.dt_seconds || !positive(*trace.dt_seconds) ||
        !std::isfinite(trace.record_length().value_or(0.0))) {
        error(diagnostics, "knet", "sampling interval or record length is out of range");
        return diagnostics;
    }
    if (options.dt_seconds && options.fs_hz)
        error(diagnostics, "override", "dt and fs are mutually exclusive");
    auto dt = options.dt_seconds;
    if (options.fs_hz)
        dt = positive(*options.fs_hz) ? 1.0 / *options.fs_hz : 0.0;
    if (dt && (!positive(*dt) || std::abs(*dt - *trace.dt_seconds) > *trace.dt_seconds * 1e-9))
        error(diagnostics, "knet", "sampling override conflicts with NIED Sampling Freq(Hz). Select a general text format to reinterpret it");
    if ((options.quantity && options.quantity != Quantity::Acceleration) ||
        (options.unit && options.unit != Unit::Cmps2))
        error(diagnostics, "knet", "input quantity or unit conflicts with the NIED Scale Factor. Convert with an output unit or select a general text format to reinterpret the counts");
    return diagnostics;
}

} // namespace gmtools::detail
