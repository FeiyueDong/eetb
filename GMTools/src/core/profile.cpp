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

void validate(const ReadOptions& options) {
    if (options.format != FormatId::Auto && !is_generic_text(options.format))
        invalid("profile.format", "use auto, text, dat, csv or tsv for a text profile");
    const auto& text = options.text;
    if (text.first_line == 0 || text.last_line == 0 ||
        (text.first_line && text.last_line && text.first_line > text.last_line))
        invalid("profile.text", "first_line and last_line must form a positive 1-based range");
    if (text.layout == Layout::Sequential && (text.time_column || !text.data_columns.empty()))
        invalid("profile.text.layout", "time_column and data_columns require columnar layout");
    std::set<std::size_t> columns;
    if (text.time_column) {
        if (*text.time_column == 0)
            invalid("profile.text.time_column", "expected a positive integer");
        columns.insert(*text.time_column);
    }
    for (const auto index : text.data_columns) {
        if (index == 0 || !columns.insert(index).second)
            invalid("profile.text.data_columns", "columns must be positive, unique and different from time_column");
    }
    if (text.numeric_factor && (!std::isfinite(*text.numeric_factor) || *text.numeric_factor == 0.0))
        invalid("profile.text.numeric_factor", "expected a finite nonzero factor");

    const auto& overrides = options.overrides;
    if (overrides.dt_seconds && overrides.fs_hz)
        invalid("profile.overrides", "dt and fs are mutually exclusive");
    if (overrides.dt_seconds && !detail::positive(*overrides.dt_seconds))
        invalid("profile.overrides.dt", "expected positive finite seconds");
    if (overrides.fs_hz && (!detail::positive(*overrides.fs_hz) || !detail::positive(1.0 / *overrides.fs_hz)))
        invalid("profile.overrides.fs", "expected a positive finite frequency and period");
    if (overrides.quantity && overrides.unit && !detail::compatible(*overrides.quantity, *overrides.unit))
        invalid("profile.overrides", "physical quantity and unit are incompatible");
}

ReadOptions decode(const Json& json) {
    fields(json, "profile", {"schema_version", "format", "text", "overrides"});
    const auto version = json.find("schema_version");
    if (version == json.end())
        invalid("profile.schema_version", "required field is missing");
    if (!version->is_number_integer() || *version != 1)
        invalid("profile.schema_version", "unsupported version, expected integer 1");

    ReadOptions result;
    result.format = optional_field(json, "profile", "format", [](const auto& value, auto field) {
        return named(value, field, format_from_name, "unknown input format");
    }).value_or(FormatId::Auto);

    if (const auto text = json.find("text"); text != json.end()) {
        constexpr std::string_view path = "profile.text";
        fields(*text, path, {"first_line", "last_line", "layout", "delimiter",
                             "data_columns", "time_column", "numeric_factor"});
        auto& options = result.text;
        options.first_line = optional_field(*text, path, "first_line", as_index);
        options.last_line = optional_field(*text, path, "last_line", as_index);
        options.time_column = optional_field(*text, path, "time_column", as_index);
        options.numeric_factor = optional_field(*text, path, "numeric_factor", as_number);
        options.layout = optional_field(*text, path, "layout", [](const auto& value, auto field) {
            return named(value, field, layout_from_name, "expected sequential or columnar");
        });
        options.delimiter = optional_field(*text, path, "delimiter", [](const auto& value, auto field) {
            return named(value, field, delimiter_from_name, "expected auto, whitespace, comma or tab");
        }).value_or(Delimiter::Auto);
        if (const auto columns = text->find("data_columns"); columns != text->end()) {
            if (!columns->is_array())
                invalid("profile.text.data_columns", "expected an array of column numbers");
            for (std::size_t i = 0; i < columns->size(); ++i)
                options.data_columns.push_back(as_index((*columns)[i], "profile.text.data_columns[" + std::to_string(i) + "]"));
        }
    }
    if (const auto overrides = json.find("overrides"); overrides != json.end()) {
        constexpr std::string_view path = "profile.overrides";
        fields(*overrides, path, {"dt", "fs", "quantity", "unit"});
        auto& options = result.overrides;
        options.dt_seconds = optional_field(*overrides, path, "dt", as_number);
        options.fs_hz = optional_field(*overrides, path, "fs", as_number);
        options.quantity = optional_field(*overrides, path, "quantity", [](const auto& value, auto field) {
            return named(value, field, quantity_from_name, "unknown physical quantity");
        });
        options.unit = optional_field(*overrides, path, "unit", [](const auto& value, auto field) {
            return named(value, field, unit_from_name, "unknown input unit");
        });
    }
    validate(result);
    return result;
}

Json encode(const ReadOptions& options) {
    validate(options);
    Json json{{"schema_version", 1}};
    if (options.format != FormatId::Auto)
        json["format"] = name(options.format);
    const auto& text = options.text;
    Json text_fields = Json::object();
    if (text.first_line) text_fields["first_line"] = *text.first_line;
    if (text.last_line) text_fields["last_line"] = *text.last_line;
    if (text.layout) text_fields["layout"] = name(*text.layout);
    if (text.delimiter != Delimiter::Auto) text_fields["delimiter"] = name(text.delimiter);
    if (!text.data_columns.empty()) text_fields["data_columns"] = text.data_columns;
    if (text.time_column) text_fields["time_column"] = *text.time_column;
    if (text.numeric_factor) text_fields["numeric_factor"] = *text.numeric_factor;
    if (!text_fields.empty())
        json["text"] = std::move(text_fields);

    const auto& overrides = options.overrides;
    Json override_fields = Json::object();
    if (overrides.dt_seconds) override_fields["dt"] = *overrides.dt_seconds;
    if (overrides.fs_hz) override_fields["fs"] = *overrides.fs_hz;
    if (overrides.quantity) override_fields["quantity"] = name(*overrides.quantity);
    if (overrides.unit) override_fields["unit"] = name(*overrides.unit);
    if (!override_fields.empty())
        json["overrides"] = std::move(override_fields);
    return json;
}

} // namespace

std::string_view name(Layout layout) {
    return layout == Layout::Sequential ? "sequential" : "columnar";
}

std::string_view name(Delimiter delimiter) {
    switch (delimiter) {
    case Delimiter::Auto: return "auto";
    case Delimiter::Whitespace: return "whitespace";
    case Delimiter::Comma: return "comma";
    case Delimiter::Tab: return "tab";
    }
    return "auto";
}

std::optional<Layout> layout_from_name(std::string_view value) {
    if (value == "sequential") return Layout::Sequential;
    if (value == "columnar") return Layout::Columnar;
    return {};
}

std::optional<Delimiter> delimiter_from_name(std::string_view value) {
    for (const auto delimiter : {Delimiter::Auto, Delimiter::Whitespace, Delimiter::Comma, Delimiter::Tab}) {
        if (value == name(delimiter))
            return delimiter;
    }
    return {};
}

ProfileResult load_profile(const std::filesystem::path& path) {
    ProfileResult result;
    auto file = load_file(path);
    if (has_errors(file.diagnostics)) {
        result.diagnostics = std::move(file.diagnostics);
        return result;
    }
    const auto text = text_view(file.bytes);
    if (!text || text->empty()) {
        detail::error(result.diagnostics, "profile", "expected a nonempty UTF-8 JSON document");
        return result;
    }
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
                    invalid("profile", "duplicate JSON field " + key);
            }
            return true;
        };
        result.options = decode(Json::parse(text->begin(), text->end(), reject_duplicates));
    } catch (const std::exception& problem) {
        detail::error(result.diagnostics, "profile", problem.what());
    }
    return result;
}

Diagnostics save_profile(const std::filesystem::path& path, const ReadOptions& options, bool overwrite,
                         const std::filesystem::path& protected_input) {
    Diagnostics diagnostics;
    try {
        const auto json = encode(options).dump(2) + "\n";
        detail::save_text_file(path, json, overwrite, protected_input, diagnostics);
    } catch (const std::exception& problem) {
        detail::error(diagnostics, "profile", problem.what());
    }
    return diagnostics;
}

ProfileResult merge_profile(const ReadOptions& profile, const ReadOptions& explicit_options,
                            bool explicit_format, bool explicit_delimiter) {
    ProfileResult result{profile, {}};
    auto& merged = result.options;
    const auto changed = [&](std::string_view field) {
        detail::warn(result.diagnostics, "profile", "explicit input overrides profile field " + std::string{field});
    };
    const auto overlay = [&](auto& output, const auto& input, std::string_view field) {
        if (input) {
            if (output && output != input)
                changed(field);
            output = input;
        }
    };

    if (explicit_format || explicit_options.format != FormatId::Auto) {
        if (merged.format != FormatId::Auto && merged.format != explicit_options.format)
            changed("format");
        merged.format = explicit_options.format;
    }
    merged.extension_hint = explicit_options.extension_hint;
    auto& text = merged.text;
    const auto& input = explicit_options.text;
    overlay(text.first_line, input.first_line, "text.first_line");
    overlay(text.last_line, input.last_line, "text.last_line");
    overlay(text.layout, input.layout, "text.layout");
    overlay(text.time_column, input.time_column, "text.time_column");
    overlay(text.numeric_factor, input.numeric_factor, "text.numeric_factor");
    if (!input.data_columns.empty()) {
        if (!text.data_columns.empty() && text.data_columns != input.data_columns)
            changed("text.data_columns");
        text.data_columns = input.data_columns;
    }
    if (explicit_delimiter || input.delimiter != Delimiter::Auto) {
        if (text.delimiter != Delimiter::Auto && text.delimiter != input.delimiter)
            changed("text.delimiter");
        text.delimiter = input.delimiter;
    }

    auto& overrides = merged.overrides;
    const auto& specified = explicit_options.overrides;
    if (specified.dt_seconds || specified.fs_hz) {
        if ((overrides.dt_seconds || overrides.fs_hz) &&
            (overrides.dt_seconds != specified.dt_seconds || overrides.fs_hz != specified.fs_hz))
            changed("overrides.dt/fs");
        overrides.dt_seconds = specified.dt_seconds;
        overrides.fs_hz = specified.fs_hz;
    }
    overlay(overrides.quantity, specified.quantity, "overrides.quantity");
    overlay(overrides.unit, specified.unit, "overrides.unit");
    return result;
}

} // namespace gmtools
