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
    Entry{{FormatId::ModifiedMseed, "modified-mseed", "", true, false,
           "experimental: wc Steim-2 checked on recorded data; wt/ws device samples pending; optional calibration"},
          detail::read_modified,
          nullptr,
          detail::detect_modified},
    Entry{{FormatId::Mseed, "mseed", ".mseed",
#ifdef GMTOOLS_HAVE_LIBMSEED
           true, true, "experimental: standard miniSEED 2/3 read, miniSEED 3 FLOAT64/FLOAT32/INT32/Steim-2 write"},
          detail::read_mseed,
          detail::write_mseed,
          detail::detect_mseed},
    Entry{{FormatId::Mseed2, "mseed2", ".mseed", false, true,
           "experimental: miniSEED 2 FLOAT64/FLOAT32/INT32/Steim-2 export, with NSLC and timing constraints"},
          nullptr,
          detail::write_mseed,
          nullptr},
#else
           false, false, "miniSEED signatures recognized; libmseed 3 adapter unavailable in this build"},
          nullptr,
          nullptr,
          detail::detect_mseed},
#endif
    Entry{{FormatId::Sac, "sac", ".sac", true, true,
           "experimental: binary v6/v7 evenly spaced ITIME read, v6/v7 float32 write"},
          detail::read_sac,
          detail::write_sac,
          detail::detect_sac},
    Entry{{FormatId::KnetAscii, "knet-ascii", "", true, false,
           "experimental: single NIED ASCII component, integer counts calibrated by Scale Factor"},
          detail::read_knet,
          nullptr,
          detail::detect_knet},
    Entry{{FormatId::At2, "at2", ".at2", true, true, "PEER AT2 ASCII/UTF-8 acceleration records; output uses g"},
          detail::read_peer,
          detail::write_peer,
          nullptr},
    Entry{{FormatId::Vt2, "vt2", ".vt2", true, true, "PEER velocity; Tabas VT2 checked, output cm/s"},
          detail::read_peer,
          detail::write_peer,
          nullptr},
    Entry{{FormatId::Dt2, "dt2", ".dt2", true, true, "PEER displacement; Alhambra DT2 checked, output cm"},
          detail::read_peer,
          detail::write_peer,
          nullptr},
    Entry{{FormatId::Text, "text", ".txt", true, true, "numeric TXT; choose a layout for multiple values per row"},
          detail::read_text,
          detail::write_text,
          nullptr},
    Entry{{FormatId::Dat, "dat", ".dat", true, true, "numeric text DAT"},
          detail::read_text,
          detail::write_text,
          nullptr},
    Entry{{FormatId::Csv, "csv", ".csv", true, true, "plain comma-separated columns and optional header"},
          detail::read_text,
          detail::write_text,
          nullptr},
    Entry{{FormatId::Tsv, "tsv", ".tsv", true, true, "plain tab-separated columns and optional header"},
          detail::read_text,
          detail::write_text,
          nullptr},
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
    if (const auto peer = detail::peer_family(bytes); peer != FormatId::Auto)
        return peer;
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

bool is_generic_text(FormatId format) {
    return format == FormatId::Text || format == FormatId::Dat ||
           format == FormatId::Csv || format == FormatId::Tsv;
}

FormatId detect_input_format(std::span<const std::byte> bytes, std::string_view extension_hint) {
    return bytes.empty() ? FormatId::Auto : detect_format(bytes, extension_hint);
}

TextSuggestion suggest_text(std::span<const std::byte> bytes, const ReadOptions& options) {
    const auto format = options.format == FormatId::Auto ? detect_input_format(bytes, options.extension_hint)
                                                        : options.format;
    if (is_generic_text(format)) {
        if (const auto text = text_view(bytes); text && !text->starts_with("# GMTools waveform "))
            return detail::suggest_text_options(*text, format, options);
    }
    return {};
}

ReadResult read(std::span<const std::byte> bytes, const ReadOptions& options) {
    ReadResult result;
    if (bytes.empty()) {
        detail::error(result.diagnostics, "reader", "input file is empty");
        return result;
    }
    const auto id = options.format == FormatId::Auto ? detect_input_format(bytes, options.extension_hint)
                                                      : options.format;
    result.data.source_format = id;
    const auto entry = entry_for(id);
    if (!entry || !entry->reader) {
        detail::error(result.diagnostics, "reader", entry ? "format has no Reader: " + std::string{entry->info.notes} :
                      "unrecognized input; specify a format if you know the file type");
        return result;
    }

    auto text = options.text;
    text.numeric_factor.reset(); // legacy Profile v1 location; applied once after native decoding
    if (options.sac_standard_units && id != FormatId::Sac) {
        detail::error(result.diagnostics, "reader", "SAC unit interpretation requires SAC input");
        return result;
    }
    if ((id == FormatId::Sac || id == FormatId::Mseed || id == FormatId::ModifiedMseed) &&
        (text.first_line || text.last_line || text.layout || text.delimiter != Delimiter::Auto ||
         !text.data_columns.empty() || text.time_column)) {
        detail::error(result.diagnostics, "reader", "text layout options only apply to text formats");
        return result;
    }
    result = entry->reader(bytes, id, text);
    result.data.source_format = id;
    if (has_errors(result.diagnostics))
        return result;

    if ((options.metadata.has_identity() && result.data.traces.size() > 1 && !options.selected_channel) ||
        (options.selected_channel &&
         (!*options.selected_channel || *options.selected_channel > result.data.traces.size()))) {
        detail::error(result.diagnostics, "metadata", "select one existing --channel for an identity edit");
        return result;
    }
    for (std::size_t i = 0; i < result.data.traces.size(); ++i) {
        if (options.selected_channel && *options.selected_channel != i + 1)
            continue;
        auto& trace = result.data.traces[i];
        if (options.sac_standard_units.value_or(false)) {
            for (const auto unit : {Unit::Nm, Unit::Nmps, Unit::Nmps2})
                if (trace.quantity == quantity_for(unit)) {
                    trace.unit = unit;
                    trace.attributes["source.unit"] = "explicit SAC IDEP nanounit convention";
                }
        }
        auto diagnostics = detail::process_read(trace, options);
        if (!has_errors(diagnostics)) {
            auto edits = edit_metadata(trace, options.metadata);
            diagnostics.insert(diagnostics.end(), edits.begin(), edits.end());
        }
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
    const auto format = options.format;
    if (!is_generic_text(format) &&
        (options.text_time_values || options.text_header || options.precision || options.text_delimiter))
        detail::error(result.diagnostics, "writer", "text export options require a text target");
    if (format != FormatId::Sac && (options.sac_version || options.sac_standard_units))
        detail::error(result.diagnostics, "writer", "SAC export options require SAC target");
    if (format != FormatId::Mseed && format != FormatId::Mseed2 && options.mseed_encoding)
        detail::error(result.diagnostics, "writer", "miniSEED encoding requires a standard miniSEED target");
    if (format == FormatId::At2 || format == FormatId::Vt2 || format == FormatId::Dt2) {
        if (!trace.dt_seconds)
            detail::error(result.diagnostics, "peer", "PEER export requires dt");
        const auto required = format == FormatId::At2 ? Unit::G : format == FormatId::Vt2 ? Unit::Cmps : Unit::Cm;
        if (output_unit && output_unit != required)
            detail::error(result.diagnostics, "peer", "incompatible target unit");
        output_unit = required;
    }
    if (options.sac_standard_units.value_or(false)) {
        const auto standard = trace.quantity == Quantity::Acceleration   ? Unit::Nmps2
                              : trace.quantity == Quantity::Velocity     ? Unit::Nmps
                              : trace.quantity == Quantity::Displacement ? Unit::Nm
                                                                         : Unit::Unknown;
        if (output_unit && output_unit != standard)
            detail::error(result.diagnostics, "sac", "standard mode requires SAC nanounits");
        output_unit = standard;
    }
    if (has_errors(result.diagnostics))
        return result;
    if (!output_unit)
        return entry->writer(trace, options);
    const auto factor = unit_factor(trace.unit, *output_unit);
    if (!factor || trace.quantity != quantity_for(*output_unit))
        detail::error(result.diagnostics, "writer",
                      "unit conversion requires known compatible physical quantity and units");
    if (has_errors(result.diagnostics))
        return result;
    if (*output_unit == trace.unit)
        return entry->writer(trace, options);

    Trace converted = trace;
    if (options.text_header == TextHeader::Metadata)
        converted.attributes["output.converted_from"] = name(trace.unit);
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
    result = entry->writer(converted, options);
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
