#include "detail.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace gmtools {
namespace {

using Json = nlohmann::json;

[[noreturn]] void invalid(std::string_view field, std::string_view reason) {
    throw std::invalid_argument(std::string{field} + ": " + std::string{reason});
}

void fields(const Json& object, std::string_view path, std::initializer_list<std::string_view> allowed) {
    if (!object.is_object())
        invalid(path, "expected a JSON object");
    for (const auto& [key, value] : object.items()) {
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            invalid(std::string{path} + "." + key, "unknown field");
    }
}

template<class Parser>
auto optional_field(const Json& object, std::string_view path, const char* key, Parser parse) {
    using Value = decltype(parse(std::declval<const Json&>(), path));
    const auto entry = object.find(key);
    if (entry == object.end())
        return std::optional<Value>{};
    return std::optional<Value>{parse(*entry, std::string{path} + "." + key)};
}

std::string_view as_string(const Json& value, std::string_view field) {
    if (!value.is_string())
        invalid(field, "expected a string");
    return value.get_ref<const std::string&>();
}

std::string as_text(const Json& value, std::string_view field) {
    return std::string{as_string(value, field)};
}

template<class T>
T named(const Json& value, std::string_view field,
        std::optional<T> (*parse)(std::string_view), std::string_view valid) {
    const auto parsed = parse(as_string(value, field));
    if (!parsed)
        invalid(field, valid);
    return *parsed;
}

std::size_t as_index(const Json& value, std::string_view field) {
    if (!value.is_number_integer() ||
        (!value.is_number_unsigned() && value.get<std::int64_t>() <= 0))
        invalid(field, "expected a positive integer");
    const auto index = value.get<std::uint64_t>();
    if (index == 0 || index > std::numeric_limits<std::size_t>::max())
        invalid(field, "positive integer is out of range");
    return static_cast<std::size_t>(index);
}

double as_number(const Json& value, std::string_view field) {
    if (!value.is_number())
        invalid(field, "expected a finite JSON number");
    const auto number = value.get<double>();
    if (!std::isfinite(number))
        invalid(field, "number is not finite");
    return number;
}

bool as_bool(const Json& value, std::string_view field) {
    if (!value.is_boolean())
        invalid(field, "expected true or false");
    return value.get<bool>();
}

void validate_text(const TextParseOptions& text, std::string_view path) {
    if (text.first_line == 0 || text.last_line == 0 ||
        (text.first_line && text.last_line && text.first_line > text.last_line))
        invalid(std::string{path}, "first_line and last_line must form a positive 1-based range");
    if (text.layout == Layout::Sequential && (text.time_column || !text.data_columns.empty()))
        invalid(std::string{path} + ".layout", "time_column and data_columns require columnar layout");
    std::set<std::size_t> columns;
    if (text.time_column) {
        if (*text.time_column == 0)
            invalid(std::string{path} + ".time_column", "expected a positive integer");
        columns.insert(*text.time_column);
    }
    for (const auto index : text.data_columns) {
        if (index == 0 || !columns.insert(index).second)
            invalid(std::string{path} + ".data_columns", "columns must be positive, unique and different from time_column");
    }
    if (text.delimiter == Delimiter::Auto && !text.data_columns.empty()) {
        // Auto is a valid input delimiter; nothing to reject.
    }
    if (text.numeric_factor && (!std::isfinite(*text.numeric_factor) || *text.numeric_factor == 0.0))
        invalid(std::string{path} + ".numeric_factor", "expected a finite nonzero factor");
}

void validate_read(const ReadOptions& read) {
    const std::string path{"preset.read"};
    if (!read.metadata.start_utc && !read.metadata.relative_start && !read.metadata.network &&
        !read.metadata.station && !read.metadata.location && !read.metadata.channel &&
        !read.metadata.source_id && !read.metadata.clear_location)
        ;
    else
        invalid(path + ".metadata", "a conversion preset cannot carry per-file identity edits");
    if (read.selected_channel)
        invalid(path + ".selected_channel", "a conversion preset cannot select a channel by number");
    if (!read.extension_hint.empty())
        invalid(path + ".extension_hint", "a conversion preset cannot carry a path or extension hint");

    if (read.format != FormatId::Auto && !is_generic_text(read.format)) {
        const bool has_text = read.text.first_line || read.text.last_line || read.text.layout ||
                              read.text.delimiter != Delimiter::Auto || !read.text.data_columns.empty() ||
                              read.text.time_column || read.text.numeric_factor;
        if (has_text)
            invalid(path + ".text", "text options require a generic text input format");
    }
    validate_text(read.text, path + ".text");

    const auto& overrides = read.overrides;
    if (overrides.dt_seconds && overrides.fs_hz)
        invalid(path + ".overrides", "dt and fs are mutually exclusive");
    if (overrides.dt_seconds && !detail::positive(*overrides.dt_seconds))
        invalid(path + ".overrides.dt", "expected positive finite seconds");
    if (overrides.fs_hz && (!detail::positive(*overrides.fs_hz) || !detail::positive(1.0 / *overrides.fs_hz)))
        invalid(path + ".overrides.fs", "expected a positive finite frequency and period");
    if (overrides.quantity && overrides.unit && !detail::compatible(*overrides.quantity, *overrides.unit))
        invalid(path + ".overrides", "physical quantity and unit are incompatible");

    if (read.calibration == Calibration::Manual) {
        if (!read.calibration_factor || !read.calibration_unit)
            invalid(path, "manual calibration requires calibration_factor and calibration_unit");
        if (!detail::positive(*read.calibration_factor))
            invalid(path + ".calibration_factor", "expected finite physical units per count");
        if (*read.calibration_unit == Unit::Unknown || *read.calibration_unit == Unit::Counts)
            invalid(path + ".calibration_unit", "expected a physical unit per count");
    } else if (read.calibration_factor || read.calibration_unit) {
        invalid(path, "calibration_factor and calibration_unit require manual calibration");
    }
    if (read.sac_standard_units && read.format != FormatId::Sac)
        invalid(path + ".sac_standard_units", "SAC unit mode requires the SAC input format");
}

void validate_write(const WriteOptions& write) {
    const std::string path{"preset.write"};
    if (write.format == FormatId::Auto)
        invalid(path + ".format", "a conversion preset requires an explicit target format");
    const auto known = formats();
    const auto entry = std::ranges::find_if(known, [&](const auto& item) { return item.id == write.format; });
    if (entry == known.end())
        invalid(path + ".format", "unknown target format");
    if (write.output_unit && *write.output_unit == Unit::Unknown)
        invalid(path + ".output_unit", "unknown output unit");

    const bool generic = is_generic_text(write.format);
    const bool has_text = write.text_time_values || write.text_header || write.text_delimiter || write.precision;
    if (has_text && !generic)
        invalid(path, "text layout options require a generic text target format");
    if (generic && write.precision && (*write.precision == 0 || *write.precision > 17))
        invalid(path + ".precision", "expected 1 to 17 significant digits");
    if (write.text_delimiter == Delimiter::Auto)
        invalid(path + ".text_delimiter", "choose whitespace, comma or tab");
    if ((write.sac_version || write.sac_standard_units) && write.format != FormatId::Sac)
        invalid(path, "SAC options require the SAC target format");
    if (write.sac_version && *write.sac_version != 6 && *write.sac_version != 7)
        invalid(path + ".sac_version", "expected 6 or 7");
    if (write.mseed_encoding && write.format != FormatId::Mseed && write.format != FormatId::Mseed2)
        invalid(path + ".mseed_encoding", "miniSEED encodings require the miniSEED target format");
}

void validate(const ConversionPreset& preset) {
    if (preset.name.size() > 120)
        invalid("preset.name", "expected at most 120 characters");
    validate_read(preset.read);
    validate_write(preset.write);
    if (preset.selection.mode == ChannelSelectionMode::Quantity &&
        preset.selection.quantity == Quantity::Unknown)
        invalid("preset.selection.quantity", "selecting by quantity requires a physical quantity");
}

ConversionPreset decode(const Json& json) {
    fields(json, "preset", {"kind", "schema_version", "name", "read", "write", "selection"});
    const auto kind = json.find("kind");
    if (kind == json.end())
        invalid("preset.kind", "required field is missing");
    if (as_string(*kind, "preset.kind") != "gmtools-conversion-preset")
        invalid("preset.kind", "expected gmtools-conversion-preset");
    const auto version = json.find("schema_version");
    if (version == json.end())
        invalid("preset.schema_version", "required field is missing");
    if (!version->is_number_integer() || *version != 1)
        invalid("preset.schema_version", "unsupported version, expected integer 1");

    ConversionPreset result;
    if (const auto entry = json.find("name"); entry != json.end())
        result.name = as_text(*entry, "preset.name");

    if (const auto read = json.find("read"); read != json.end()) {
        constexpr std::string_view path = "preset.read";
        fields(*read, path, {"format", "text", "overrides", "calibration",
                             "calibration_factor", "calibration_unit", "sac_standard_units"});
        result.read.format = optional_field(*read, path, "format", [](const auto& value, auto field) {
            return named(value, field, format_from_name, "unknown input format");
        }).value_or(FormatId::Auto);
        result.read.calibration = optional_field(*read, path, "calibration", [](const auto& value, auto field) {
            return named(value, field, calibration_from_name,
                         "expected default, raw, source or manual");
        }).value_or(Calibration::Default);
        result.read.calibration_factor = optional_field(*read, path, "calibration_factor", as_number);
        result.read.calibration_unit = optional_field(*read, path, "calibration_unit", [](const auto& value, auto field) {
            return named(value, field, unit_from_name, "unknown unit per count");
        });
        result.read.sac_standard_units = optional_field(*read, path, "sac_standard_units", as_bool);

        if (const auto text = read->find("text"); text != read->end()) {
            constexpr std::string_view text_path = "preset.read.text";
            fields(*text, text_path, {"first_line", "last_line", "layout", "delimiter",
                                      "data_columns", "time_column", "numeric_factor"});
            auto& options = result.read.text;
            options.first_line = optional_field(*text, text_path, "first_line", as_index);
            options.last_line = optional_field(*text, text_path, "last_line", as_index);
            options.time_column = optional_field(*text, text_path, "time_column", as_index);
            options.numeric_factor = optional_field(*text, text_path, "numeric_factor", as_number);
            options.layout = optional_field(*text, text_path, "layout", [](const auto& value, auto field) {
                return named(value, field, layout_from_name, "expected sequential or columnar");
            });
            options.delimiter = optional_field(*text, text_path, "delimiter", [](const auto& value, auto field) {
                return named(value, field, delimiter_from_name,
                             "expected auto, whitespace, comma or tab");
            }).value_or(Delimiter::Auto);
            if (const auto columns = text->find("data_columns"); columns != text->end()) {
                if (!columns->is_array())
                    invalid("preset.read.text.data_columns", "expected an array of column numbers");
                for (std::size_t i = 0; i < columns->size(); ++i)
                    options.data_columns.push_back(
                        as_index((*columns)[i], "preset.read.text.data_columns[" + std::to_string(i) + "]"));
            }
        }
        if (const auto overrides = read->find("overrides"); overrides != read->end()) {
            constexpr std::string_view override_path = "preset.read.overrides";
            fields(*overrides, override_path, {"dt", "fs", "quantity", "unit"});
            auto& options = result.read.overrides;
            options.dt_seconds = optional_field(*overrides, override_path, "dt", as_number);
            options.fs_hz = optional_field(*overrides, override_path, "fs", as_number);
            options.quantity = optional_field(*overrides, override_path, "quantity", [](const auto& value, auto field) {
                return named(value, field, quantity_from_name, "unknown physical quantity");
            });
            options.unit = optional_field(*overrides, override_path, "unit", [](const auto& value, auto field) {
                return named(value, field, unit_from_name, "unknown input unit");
            });
        }
    }

    const auto write = json.find("write");
    if (write == json.end())
        invalid("preset.write", "required field is missing");
    {
        constexpr std::string_view path = "preset.write";
        fields(*write, path, {"format", "output_unit", "text_layout", "text_header", "text_delimiter",
                              "precision", "sac_version", "sac_unit_mode", "mseed_encoding"});
        const auto format = write->find("format");
        if (format == write->end())
            invalid("preset.write.format", "required field is missing");
        result.write.format = named(*format, "preset.write.format", format_from_name, "unknown target format");
        result.write.output_unit = optional_field(*write, path, "output_unit", [](const auto& value, auto field) {
            return named(value, field, unit_from_name, "unknown output unit");
        });
        result.write.text_time_values = optional_field(*write, path, "text_layout", [](const auto& value, auto field) {
            const auto name = as_string(value, field);
            if (name == "amplitude") return false;
            if (name == "elapsed-time") return true;
            invalid(field, "expected amplitude or elapsed-time");
        });
        result.write.text_header = optional_field(*write, path, "text_header", [](const auto& value, auto field) {
            return named(value, field, text_header_from_name, "expected none, columns or metadata");
        });
        result.write.text_delimiter = optional_field(*write, path, "text_delimiter", [](const auto& value, auto field) {
            return named(value, field, delimiter_from_name, "expected whitespace, comma or tab");
        });
        if (const auto precision = optional_field(*write, path, "precision", as_index))
            result.write.precision = *precision;
        if (const auto version = optional_field(*write, path, "sac_version", as_index)) {
            if (*version != 6 && *version != 7)
                invalid("preset.write.sac_version", "expected 6 or 7");
            result.write.sac_version = static_cast<int>(*version);
        }
        result.write.sac_standard_units = optional_field(*write, path, "sac_unit_mode", [](const auto& value, auto field) {
            const auto name = as_string(value, field);
            if (name == "keep") return false;
            if (name == "nano") return true;
            invalid(field, "expected keep or nano");
        });
        result.write.mseed_encoding = optional_field(*write, path, "mseed_encoding", [](const auto& value, auto field) {
            return named(value, field, mseed_encoding_from_name,
                         "expected float64, float32, int32 or steim2");
        });
    }

    if (const auto selection = json.find("selection"); selection != json.end()) {
        constexpr std::string_view path = "preset.selection";
        fields(*selection, path, {"mode", "quantity"});
        const auto mode = optional_field(*selection, path, "mode", [](const auto& value, auto field) {
            return named(value, field, selection_mode_from_name, "expected all or quantity");
        });
        result.selection.mode = mode.value_or(ChannelSelectionMode::All);
        result.selection.quantity = optional_field(*selection, path, "quantity", [](const auto& value, auto field) {
            return named(value, field, quantity_from_name, "unknown physical quantity");
        }).value_or(Quantity::Unknown);
    }
    validate(result);
    return result;
}

Json encode(const ConversionPreset& preset) {
    validate(preset);
    Json json{{"kind", "gmtools-conversion-preset"}, {"schema_version", 1}};
    if (!preset.name.empty())
        json["name"] = preset.name;

    const auto& read = preset.read;
    Json read_fields = Json::object();
    if (read.format != FormatId::Auto)
        read_fields["format"] = name(read.format);
    if (read.calibration != Calibration::Default)
        read_fields["calibration"] = name(read.calibration);
    if (read.calibration_factor)
        read_fields["calibration_factor"] = *read.calibration_factor;
    if (read.calibration_unit)
        read_fields["calibration_unit"] = name(*read.calibration_unit);
    if (read.sac_standard_units)
        read_fields["sac_standard_units"] = *read.sac_standard_units;

    const auto& text = read.text;
    Json text_fields = Json::object();
    if (text.first_line) text_fields["first_line"] = *text.first_line;
    if (text.last_line) text_fields["last_line"] = *text.last_line;
    if (text.layout) text_fields["layout"] = name(*text.layout);
    if (text.delimiter != Delimiter::Auto) text_fields["delimiter"] = name(text.delimiter);
    if (!text.data_columns.empty()) text_fields["data_columns"] = text.data_columns;
    if (text.time_column) text_fields["time_column"] = *text.time_column;
    if (text.numeric_factor) text_fields["numeric_factor"] = *text.numeric_factor;
    if (!text_fields.empty())
        read_fields["text"] = std::move(text_fields);

    const auto& overrides = read.overrides;
    Json override_fields = Json::object();
    if (overrides.dt_seconds) override_fields["dt"] = *overrides.dt_seconds;
    if (overrides.fs_hz) override_fields["fs"] = *overrides.fs_hz;
    if (overrides.quantity) override_fields["quantity"] = name(*overrides.quantity);
    if (overrides.unit) override_fields["unit"] = name(*overrides.unit);
    if (!override_fields.empty())
        read_fields["overrides"] = std::move(override_fields);
    json["read"] = std::move(read_fields);

    const auto& write = preset.write;
    Json write_fields = Json::object();
    write_fields["format"] = name(write.format);
    if (write.output_unit)
        write_fields["output_unit"] = name(*write.output_unit);
    if (write.text_time_values)
        write_fields["text_layout"] = *write.text_time_values ? "elapsed-time" : "amplitude";
    if (write.text_header)
        write_fields["text_header"] = name(*write.text_header);
    if (write.text_delimiter)
        write_fields["text_delimiter"] = name(*write.text_delimiter);
    if (write.precision)
        write_fields["precision"] = *write.precision;
    if (write.sac_version)
        write_fields["sac_version"] = *write.sac_version;
    if (write.sac_standard_units)
        write_fields["sac_unit_mode"] = *write.sac_standard_units ? "nano" : "keep";
    if (write.mseed_encoding)
        write_fields["mseed_encoding"] = name(*write.mseed_encoding);
    json["write"] = std::move(write_fields);

    if (preset.selection.mode != ChannelSelectionMode::All) {
        json["selection"] = Json{{"mode", name(preset.selection.mode)},
                                 {"quantity", name(preset.selection.quantity)}};
    }
    return json;
}

} // namespace

std::string_view name(Calibration calibration) {
    switch (calibration) {
    case Calibration::Default: return "default";
    case Calibration::Raw: return "raw";
    case Calibration::Source: return "source";
    case Calibration::Manual: return "manual";
    }
    return "default";
}

std::string_view name(TextHeader header) {
    switch (header) {
    case TextHeader::None: return "none";
    case TextHeader::Columns: return "columns";
    case TextHeader::Metadata: return "metadata";
    }
    return "none";
}

std::string_view name(MseedEncoding encoding) {
    switch (encoding) {
    case MseedEncoding::Float64: return "float64";
    case MseedEncoding::Float32: return "float32";
    case MseedEncoding::Int32: return "int32";
    case MseedEncoding::Steim2: return "steim2";
    }
    return "float64";
}

std::string_view name(ChannelSelectionMode mode) {
    return mode == ChannelSelectionMode::Quantity ? "quantity" : "all";
}

std::string_view name(BatchFileState state) {
    switch (state) {
    case BatchFileState::Pending: return "pending";
    case BatchFileState::Running: return "running";
    case BatchFileState::Succeeded: return "succeeded";
    case BatchFileState::Failed: return "failed";
    case BatchFileState::Partial: return "partial";
    case BatchFileState::Cancelled: return "cancelled";
    }
    return "pending";
}

std::optional<Calibration> calibration_from_name(std::string_view value) {
    for (const auto calibration : {Calibration::Default, Calibration::Raw, Calibration::Source, Calibration::Manual}) {
        if (value == name(calibration))
            return calibration;
    }
    return {};
}

std::optional<TextHeader> text_header_from_name(std::string_view value) {
    for (const auto header : {TextHeader::None, TextHeader::Columns, TextHeader::Metadata}) {
        if (value == name(header))
            return header;
    }
    return {};
}

std::optional<MseedEncoding> mseed_encoding_from_name(std::string_view value) {
    for (const auto encoding : {MseedEncoding::Float64, MseedEncoding::Float32, MseedEncoding::Int32,
                                MseedEncoding::Steim2}) {
        if (value == name(encoding))
            return encoding;
    }
    return {};
}

std::optional<ChannelSelectionMode> selection_mode_from_name(std::string_view value) {
    if (value == "all") return ChannelSelectionMode::All;
    if (value == "quantity") return ChannelSelectionMode::Quantity;
    return {};
}

PresetResult decode_preset(std::string_view json) {
    PresetResult result;
    try {
        std::vector<std::set<std::string>> keys;
        const auto reject_duplicates = [&](int, Json::parse_event_t event, Json& value) {
            if (event == Json::parse_event_t::object_start) {
                keys.emplace_back();
            } else if (event == Json::parse_event_t::object_end) {
                keys.pop_back();
            } else if (event == Json::parse_event_t::key) {
                const auto& key = value.get_ref<const std::string&>();
                if (!keys.back().insert(key).second)
                    invalid("preset", "duplicate JSON field " + key);
            }
            return true;
        };
        result.preset = decode(Json::parse(json.begin(), json.end(), reject_duplicates));
    } catch (const std::exception& problem) {
        detail::error(result.diagnostics, "preset", problem.what());
    }
    return result;
}

std::string encode_preset(const ConversionPreset& preset) {
    return encode(preset).dump(2) + "\n";
}

PresetResult load_preset(const std::filesystem::path& path) {
    PresetResult result;
    auto file = load_file(path);
    if (has_errors(file.diagnostics)) {
        result.diagnostics = std::move(file.diagnostics);
        return result;
    }
    const auto text = text_view(file.bytes);
    if (!text || text->empty()) {
        detail::error(result.diagnostics, "preset", "expected a nonempty UTF-8 JSON document");
        return result;
    }
    result = decode_preset(*text);
    return result;
}

Diagnostics save_preset(const std::filesystem::path& path, const ConversionPreset& preset, bool overwrite,
                        const std::filesystem::path& protected_input) {
    Diagnostics diagnostics;
    try {
        const auto json = encode_preset(preset);
        detail::save_text_file(path, json, overwrite, protected_input, diagnostics);
    } catch (const std::exception& problem) {
        detail::error(diagnostics, "preset", problem.what());
    }
    return diagnostics;
}

} // namespace gmtools
