#include "text_provenance.hpp"
#include "../core/detail.hpp"

#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace gmtools::detail {
namespace {
using Json = nlohmann::json;

[[noreturn]] void invalid(const std::string& field) {
    throw std::invalid_argument("invalid waveform provenance: " + field);
}

const std::string& string_field(const Json& object, const char* key) {
    const auto field = object.find(key);
    if (field == object.end() || !field->is_string())
        invalid(std::string{key} + " must be a string");
    return field->get_ref<const std::string&>();
}

Unit checked_unit(const Json& object, const char* key) {
    if (const auto unit = unit_from_name(string_field(object, key)))
        return *unit;
    invalid(std::string{key} + " contains an unknown unit");
}

double checked_factor(const Json& step) {
    const auto v = step.find("factor");
    if (v == step.end() || !v->is_number() || !std::isfinite(v->get<double>()) || v->get<double>() == 0)
        invalid("factor must be a finite, nonzero number");
    return v->get<double>();
}

double recorded_factor(const Metadata& attrs, const char* key) {
    const auto value = parse_number(attrs.at(key));
    if (!value || *value == 0)
        invalid(key);
    return *value;
}
} // namespace

void read_text_provenance(const Json& p, Trace& trace) {
    if (!p.is_object())
        invalid("expected an object");
    auto& attrs = trace.attributes;
    Quantity origin = Quantity::Unknown;
    if (p.contains("original_quantity")) {
        const auto quantity = quantity_from_name(string_field(p, "original_quantity"));
        if (!quantity)
            invalid("unknown original_quantity");
        origin = *quantity;
        attrs["history.original_quantity"] = name(origin);
    }
    if (p.contains("original_unit")) {
        const Unit unit = checked_unit(p, "original_unit");
        if (!compatible(origin, unit))
            invalid("incompatible original quantity and unit");
        if (origin == Quantity::Unknown)
            origin = quantity_for(unit);
        attrs["history.original_unit"] = name(unit);
    }
    const auto steps = p.value("steps", Json::array());
    if (!steps.is_array())
        invalid("steps must be an array");
    Json known = Json::array();
    for (const auto& step : steps) {
        const auto& type = string_field(step, "type");
        if (type == "calibration") {
            const auto& source = string_field(step, "source");
            const auto unit = checked_unit(step, "unit");
            if (trim(source).empty() || !unit_factor(unit, unit))
                invalid("calibration requires a source and physical unit per count");
            if ((origin != Quantity::Counts && origin != Quantity::Unknown) ||
                attrs.contains("history.calibration.source") || attrs.contains("history.last_conversion_unit"))
                invalid("invalid or repeated calibration");
            const auto factor = checked_factor(step);
            known.push_back({{"type", type}, {"source", source}, {"factor", factor}, {"unit", name(unit)}});
            attrs["history.calibration.source"] = source;
            attrs["history.calibration.factor"] = number(factor);
            attrs["history.calibration.unit"] = name(unit);
        } else if (type == "scale") {
            const auto factor = checked_factor(step);
            known.push_back({{"type", type}, {"factor", factor}});
            attrs["history.last_numeric_factor"] = number(factor);
        } else if (type == "unit_conversion") {
            const auto from = checked_unit(step, "from");
            const auto to = checked_unit(step, "to");
            if (!unit_factor(from, to) || (origin == Quantity::Counts && !attrs.contains("history.calibration.source")))
                invalid("incompatible conversion units");
            known.push_back({{"type", type}, {"from", name(from)}, {"to", name(to)}});
            attrs["history.last_conversion_unit"] = name(to);
        }
    }
    const auto current = trace.quantity == Quantity::Unknown ? quantity_for(trace.unit) : trace.quantity;
    if (current == Quantity::Counts && (attrs.contains("history.calibration.source") ||
        attrs.contains("history.last_conversion_unit") || (origin != Quantity::Unknown && origin != Quantity::Counts)))
        invalid("physical history conflicts with stored counts");
    if (origin == Quantity::Counts && current != Quantity::Counts && current != Quantity::Unknown &&
        !attrs.contains("history.calibration.source"))
        invalid("stored physical samples with a counts origin require a calibration step");
    if (!known.empty())
        attrs["history.steps"] = known.dump();
}

void write_text_provenance(Json& header, const Trace& trace) {
    const auto& attrs = trace.attributes;
    Json p = Json::object();
    Json steps = attrs.contains("history.steps") ? Json::parse(attrs.at("history.steps")) : Json::array();
    // Only live attributes describe operations on this import/export. Previous steps are already in order.
    if (attrs.contains("calibration.applied_unit"))
        steps.push_back({{"type", "calibration"}, {"source", attrs.at("calibration.source")},
                         {"factor", recorded_factor(attrs, "calibration.factor")}, {"unit", attrs.at("calibration.applied_unit")}});
    if (attrs.contains("text.numeric_factor"))
        steps.push_back({{"type", "scale"}, {"factor", recorded_factor(attrs, "text.numeric_factor")}});
    if (attrs.contains("output.converted_from"))
        steps.push_back({{"type", "unit_conversion"}, {"from", attrs.at("output.converted_from")}, {"to", name(trace.unit)}});
    if (!steps.empty())
        p["steps"] = std::move(steps);

    // A prior record can legitimately have incomplete input labels. Never fill them from a later import.
    const bool prior = attrs.contains("history.steps") || attrs.contains("history.original_quantity") ||
                       attrs.contains("history.original_unit");
    for (const auto* field : {"original_quantity", "original_unit"}) {
        const auto it = attrs.find(std::string{prior ? "history." : "source."} + field);
        if (it != attrs.end())
            p[field] = it->second;
    }
    if (!prior && attrs.contains("output.converted_from") && !attrs.contains("text.numeric_factor") &&
        !p.contains("original_unit") && !p.contains("original_quantity")) {
        p["original_unit"] = attrs.at("output.converted_from");
        p["original_quantity"] = name(trace.quantity);
    }
    if (!p.empty())
        header["provenance"] = std::move(p);
}
} // namespace gmtools::detail
