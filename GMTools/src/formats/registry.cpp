#include "../core/detail.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>

namespace gmtools {
namespace {

struct Entry {
    FormatInfo info;
    detail::Reader reader;
    detail::Writer writer;
    detail::Detector detect;
};

constexpr std::array entries{
    Entry{{FormatId::Mseed, "mseed", ".mseed",
#ifdef GMTOOLS_HAVE_LIBMSEED
           true, true, "experimental: standard miniSEED 2/3 read, miniSEED 3 float64 write"},
           detail::read_mseed, detail::write_mseed, detail::detect_mseed},
    Entry{{FormatId::Mseed2, "mseed2", ".mseed", false, true,
           "experimental: miniSEED 2 float64 export, with NSLC and timing constraints"},
           nullptr, detail::write_mseed, nullptr},
#else
           false, false, "miniSEED signatures recognized; libmseed 3 adapter unavailable in this build"},
           nullptr, nullptr, detail::detect_mseed},
#endif
    Entry{{FormatId::Sac, "sac", ".sac", true, true,
           "experimental: binary v6/v7 evenly spaced ITIME read, v7 float32 write"},
           detail::read_sac, detail::write_sac, detail::detect_sac},
    Entry{{FormatId::At2, "at2", ".at2", true, true,
           "PEER AT2 ASCII/UTF-8 acceleration records; output uses g"},
           detail::read_peer, detail::write_peer, detail::detect_peer},
    Entry{{FormatId::Text, "text", ".txt", true, true, "numeric TXT; choose a layout for multiple values per row"},
           detail::read_text, detail::write_text, nullptr},
    Entry{{FormatId::Dat, "dat", ".dat", true, true, "numeric text DAT"},
           detail::read_text, detail::write_text, nullptr},
    Entry{{FormatId::Csv, "csv", ".csv", true, true, "plain comma-separated columns and optional header"},
           detail::read_text, detail::write_text, nullptr},
    Entry{{FormatId::Tsv, "tsv", ".tsv", true, true, "plain tab-separated columns and optional header"},
           detail::read_text, detail::write_text, nullptr},
};

const Entry* entry_for(FormatId id) {
    const auto entry = std::ranges::find_if(entries, [id](const auto& item) { return item.info.id == id; });
    return entry == entries.end() ? nullptr : &*entry;
}

FormatId detect_format(std::span<const std::byte> bytes, std::string_view extension) {
    for (const auto& entry : entries) {
        if (entry.detect && entry.detect(bytes))
            return entry.info.id;
    }
    const auto text = text_view(bytes);
    if (!text)
        return FormatId::Auto;
    if (const auto table = detail::detect_text_table(*text))
        return *table;
    // The delimiter still needs to be present on a data line for a multi-column
    // parse. These suffixes select rules only after the bytes pass text decoding.
    const auto hint = format_from_name(extension);
    if (hint == FormatId::Csv || hint == FormatId::Tsv || hint == FormatId::Dat)
        return *hint;
    return FormatId::Text;
}

} // namespace

std::vector<FormatInfo> formats() {
    std::vector<FormatInfo> result;
    for (const auto& entry : entries)
        result.push_back(entry.info);
    return result;
}

std::optional<FormatId> format_from_name(std::string_view input) {
    auto word = detail::lower(detail::trim(input));
    if (word.starts_with('.'))
        word.erase(0, 1);
    if (word == "auto")
        return FormatId::Auto;
    if (word == "txt")
        return FormatId::Text;
    if (word == "peer")
        return FormatId::At2;
    if (word == "miniseed" || word == "msd" || word == "ms")
        return FormatId::Mseed;
    for (const auto& entry : entries) {
        if (word == entry.info.name)
            return entry.info.id;
    }
    return {};
}

std::string_view name(FormatId format) {
    const auto entry = entry_for(format);
    return entry ? entry->info.name : "auto";
}

ReadResult read(std::span<const std::byte> bytes, const ReadOptions& options) {
    ReadResult result;
    if (bytes.empty()) {
        detail::error(result.diagnostics, "reader", "input file is empty");
        return result;
    }
    const auto id = options.format == FormatId::Auto ? detect_format(bytes, options.extension_hint)
                                                      : options.format;
    result.data.source_format = id;
    const auto entry = entry_for(id);
    if (!entry || !entry->reader) {
        detail::error(result.diagnostics, "reader", entry ? "format has no Reader: " + std::string{entry->info.notes} :
                      "unrecognized input; specify a format if you know the file type");
        return result;
    }

    const auto& text = options.text;
    if ((id == FormatId::Sac || id == FormatId::Mseed) &&
        (text.first_line || text.last_line || text.layout || text.delimiter != Delimiter::Auto ||
         !text.data_columns.empty() || text.time_column || text.numeric_factor)) {
        detail::error(result.diagnostics, "reader", "text parsing options only apply to text and AT2");
        return result;
    }
    result = entry->reader(bytes, id, text);
    result.data.source_format = id;
    if (has_errors(result.diagnostics))
        return result;

    for (std::size_t i = 0; i < result.data.traces.size(); ++i) {
        auto diagnostics = apply_overrides(result.data.traces[i], options.overrides);
        for (auto& diagnostic : diagnostics) {
            diagnostic.channel = i + 1;
            result.diagnostics.push_back(std::move(diagnostic));
        }
    }
    return result;
}

WriteResult write(const Trace& trace, const WriteOptions& options) {
    WriteResult result;
    const auto entry = entry_for(options.format);
    if (!entry || !entry->writer) {
        detail::error(result.diagnostics, "writer", entry ? std::string{entry->info.notes} :
                      "an explicit, writable target format is required");
        return result;
    }
    if (trace.samples.empty())
        detail::error(result.diagnostics, "writer", "trace has no samples");
    else {
        const auto invalid = std::ranges::find_if(trace.samples, [](double sample) { return !std::isfinite(sample); });
        if (invalid != trace.samples.end())
            detail::error(result.diagnostics, "writer", "nonfinite sample at index " +
                          std::to_string(invalid - trace.samples.begin() + 1));
    }
    if (trace.dt_seconds && (!detail::positive(*trace.dt_seconds) || !std::isfinite(trace.record_length().value_or(0.0))))
        detail::error(result.diagnostics, "writer", "invalid sampling interval or record length");
    if (trace.relative_start_seconds && !std::isfinite(*trace.relative_start_seconds))
        detail::error(result.diagnostics, "writer", "invalid relative start time");
    if (!detail::compatible(trace.quantity, trace.unit))
        detail::error(result.diagnostics, "writer", "physical quantity and unit are incompatible");
    if (has_errors(result.diagnostics))
        return result;

    auto output_unit = options.output_unit;
    if (options.format == FormatId::At2) {
        if (!trace.dt_seconds)
            detail::error(result.diagnostics, "peer", "AT2 requires a known sampling interval (dt or fs)");
        if (output_unit && output_unit != Unit::G)
            detail::error(result.diagnostics, "writer", "AT2 output uses g");
        output_unit = Unit::G;
    }
    if (!output_unit)
        return entry->writer(trace, options.format);
    const auto factor = detail::acceleration_factor(trace.unit, *output_unit);
    if (trace.quantity != Quantity::Acceleration || !factor) {
        detail::error(result.diagnostics, "writer", "a known acceleration quantity and unit (g, m/s² or cm/s²) are required");
    }
    if (has_errors(result.diagnostics))
        return result;
    if (*output_unit == trace.unit)
        return entry->writer(trace, options.format);

    Trace converted = trace;
    converted.unit = *output_unit;
    for (std::size_t i = 0; i < converted.samples.size(); ++i) {
        const auto original = converted.samples[i];
        const auto sample = original * *factor;
        if (!std::isfinite(sample) || (original != 0.0 && sample == 0.0)) {
            detail::error(result.diagnostics, "writer", "unit conversion exceeds numeric range at sample " +
                          std::to_string(i + 1));
            return result;
        }
        converted.samples[i] = sample;
    }
    result = entry->writer(converted, options.format);
    if (!has_errors(result.diagnostics))
        detail::warn(result.diagnostics, "writer", "converted " + std::string{name(trace.unit)} +
                     " to " + std::string{name(converted.unit)});
    return result;
}

namespace detail {

bool detect_mseed(std::span<const std::byte> bytes) {
    const auto* data = reinterpret_cast<const char*>(bytes.data());
    if (bytes.size() >= 3 && data[0] == 'M' && data[1] == 'S' && data[2] == '\x03')
        return true;
    if (bytes.size() < 48)
        return false;
    const std::string_view seq{data, 6};
    constexpr std::string_view types{"DRQM"};
    constexpr std::string_view sequence_bytes{"0123456789 \0", 12};
    return seq.find_first_not_of(sequence_bytes) == seq.npos && types.find(data[6]) != types.npos &&
           (data[7] == ' ' || data[7] == '\0') &&
           static_cast<unsigned char>(data[24]) <= 23 &&
           static_cast<unsigned char>(data[25]) <= 59 &&
           static_cast<unsigned char>(data[26]) <= 60;
}

} // namespace detail
} // namespace gmtools
