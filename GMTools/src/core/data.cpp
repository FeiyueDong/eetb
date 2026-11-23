#include "detail.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <system_error>

namespace gmtools {
namespace {

Quantity quantity_for(Unit unit) {
    if (unit == Unit::Unknown)
        return Quantity::Unknown;
    return unit == Unit::Counts ? Quantity::Counts : Quantity::Acceleration;
}

} // namespace

std::optional<double> Trace::sample_span() const {
    if (dt_seconds && !samples.empty())
        return static_cast<double>(samples.size() - 1) * *dt_seconds;
    return {};
}

std::optional<double> Trace::record_length() const {
    if (dt_seconds && !samples.empty())
        return static_cast<double>(samples.size()) * *dt_seconds;
    return {};
}

std::string_view name(Quantity quantity) {
    switch (quantity) {
    case Quantity::Unknown: return "unknown";
    case Quantity::Acceleration: return "acceleration";
    case Quantity::Velocity: return "velocity";
    case Quantity::Displacement: return "displacement";
    case Quantity::Counts: return "counts";
    }
    return "unknown";
}

std::string_view name(Unit unit) {
    switch (unit) {
    case Unit::Unknown: return "unknown";
    case Unit::G: return "g";
    case Unit::Mps2: return "m/s²";
    case Unit::Cmps2: return "cm/s²";
    case Unit::Counts: return "counts";
    }
    return "unknown";
}

std::optional<Quantity> quantity_from_name(std::string_view value) {
    const auto word = detail::lower(detail::trim(value));
    for (const auto quantity : {Quantity::Unknown, Quantity::Acceleration, Quantity::Velocity,
                                Quantity::Displacement, Quantity::Counts}) {
        if (word == name(quantity))
            return quantity;
    }
    return {};
}

std::optional<Unit> unit_from_name(std::string_view value) {
    const auto word = detail::lower(detail::trim(value));
    if (word == "m/s2" || word == "m/s^2")
        return Unit::Mps2;
    if (word == "cm/s2" || word == "cm/s^2" || word == "gal")
        return Unit::Cmps2;
    for (const auto unit : {Unit::Unknown, Unit::G, Unit::Mps2, Unit::Cmps2, Unit::Counts}) {
        if (word == name(unit))
            return unit;
    }
    return {};
}

std::optional<double> parse_number(std::string_view word) {
    word = detail::trim(word);
    if (word.starts_with('+')) {
        word.remove_prefix(1);
        if (word.starts_with('-') || word.starts_with('+'))
            return {};
    }
    if (word.empty())
        return {};
    double value{};
    const auto end = word.data() + word.size();
    const auto parsed = std::from_chars(word.data(), end, value, std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != end || !std::isfinite(value))
        return {};
    return value;
}

std::optional<std::size_t> parse_index(std::string_view word) {
    word = detail::trim(word);
    if (word.empty())
        return {};
    std::size_t value{};
    const auto end = word.data() + word.size();
    const auto parsed = std::from_chars(word.data(), end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end || value == 0)
        return {};
    return value;
}

std::optional<std::vector<std::size_t>> parse_indices(std::string_view list) {
    std::vector<std::size_t> result;
    for (;;) {
        const auto comma = list.find(',');
        const auto value = parse_index(list.substr(0, comma));
        if (!value)
            return {};
        result.push_back(*value);
        if (comma == list.npos)
            return result;
        list.remove_prefix(comma + 1);
    }
}

bool has_errors(const Diagnostics& diagnostics) {
    return std::ranges::any_of(diagnostics, [](const auto& d) {
        return d.severity == Severity::Error;
    });
}

std::string describe(const Diagnostic& d) {
    std::string result = d.severity == Severity::Error ? "error" : "warning";
    result += " [" + d.source + "]";
    if (d.channel)
        result += " channel " + std::to_string(d.channel);
    if (d.line)
        result += " line " + std::to_string(d.line);
    if (d.column)
        result += " column " + std::to_string(d.column);
    return result + ": " + d.message;
}

std::string timestamp(TimePoint time) {
    using namespace std::chrono;
    const auto day = floor<days>(time);
    const year_month_day date{day};
    const hh_mm_ss clock{time - day};
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setfill('0') << std::setw(4) << int(date.year()) << '-'
        << std::setw(2) << unsigned(date.month()) << '-'
        << std::setw(2) << unsigned(date.day()) << 'T'
        << std::setw(2) << clock.hours().count() << ':'
        << std::setw(2) << clock.minutes().count() << ':'
        << std::setw(2) << clock.seconds().count() << '.'
        << std::setw(9) << clock.subseconds().count() << 'Z';
    return out.str();
}

Diagnostics apply_overrides(Trace& trace, const Overrides& user) {
    Diagnostics diagnostics;
    if (user.dt_seconds && user.fs_hz)
        detail::error(diagnostics, "override", "dt and fs are mutually exclusive");
    if (user.fs_hz && !detail::positive(*user.fs_hz))
        detail::error(diagnostics, "override", "fs must be positive and finite");
    auto dt = user.dt_seconds;
    if (user.fs_hz && detail::positive(*user.fs_hz))
        dt = 1.0 / *user.fs_hz;
    if (dt && !detail::positive(*dt))
        detail::error(diagnostics, "override", "dt must be positive and finite");
    const auto unit = user.unit.value_or(trace.unit);
    auto quantity = user.quantity.value_or(trace.quantity);
    const bool infer_quantity = !user.quantity && user.unit && quantity == Quantity::Unknown;
    if (infer_quantity)
        quantity = quantity_for(unit);
    if (!detail::compatible(quantity, unit))
        detail::error(diagnostics, "override", "physical quantity and unit are incompatible");
    const auto original_counts = [&](const char* key) {
        const auto current = trace.attributes.find(key);
        return current != trace.attributes.end() && current->second == "counts";
    };
    const bool from_counts = trace.quantity == Quantity::Counts || trace.unit == Unit::Counts ||
        original_counts("source.original_quantity") || original_counts("source.original_unit");
    if (from_counts && (quantity == Quantity::Acceleration || quantity == Quantity::Velocity ||
        quantity == Quantity::Displacement || quantity_for(unit) == Quantity::Acceleration) &&
        !trace.attributes.contains("text.numeric_factor")) {
        detail::error(diagnostics, "override", "known digital counts need an explicit calibration factor before assigning a physical quantity");
    }
    const auto effective_dt = dt ? dt : trace.dt_seconds;
    if (effective_dt && (!detail::positive(*effective_dt) ||
                         !std::isfinite(*effective_dt * static_cast<double>(trace.samples.size()))))
        detail::error(diagnostics, "sampling", "sampling interval or record length is out of range");
    if (has_errors(diagnostics))
        return diagnostics;

    if (dt) {
        if (trace.dt_seconds && std::abs(*dt - *trace.dt_seconds) >
                                    1e-9 * std::max(*dt, *trace.dt_seconds)) {
            detail::warn(diagnostics, "override", "dt changed from " +
                detail::number(*trace.dt_seconds) + " to " + detail::number(*dt) + " s");
        }
        if (trace.dt_seconds && !trace.attributes.contains("source.original_dt_seconds"))
            trace.attributes["source.original_dt_seconds"] = detail::number(*trace.dt_seconds);
        trace.dt_seconds = dt;
        trace.attributes["source.dt"] = user.fs_hz ? "user sampling rate" : "user sampling interval";
    }
    if (user.unit) {
        if (trace.unit != Unit::Unknown && trace.unit != unit)
            detail::warn(diagnostics, "override", "unit relabeled from " + std::string{name(trace.unit)} +
                                                  " to " + std::string{name(unit)} + "; samples unchanged");
        if (trace.unit != Unit::Unknown && !trace.attributes.contains("source.original_unit"))
            trace.attributes["source.original_unit"] = name(trace.unit);
        trace.unit = unit;
        trace.attributes["source.unit"] = "user input";
    }
    if (user.quantity || (infer_quantity && quantity != Quantity::Unknown)) {
        if (trace.quantity != Quantity::Unknown && trace.quantity != quantity)
            detail::warn(diagnostics, "override", "physical quantity relabeled from " +
                std::string{name(trace.quantity)} + " to " + std::string{name(quantity)});
        if (trace.quantity != Quantity::Unknown && !trace.attributes.contains("source.original_quantity"))
            trace.attributes["source.original_quantity"] = name(trace.quantity);
        trace.quantity = quantity;
        trace.attributes["source.quantity"] = user.quantity ? "user input" : "user input unit";
    }
    return diagnostics;
}

namespace detail {

std::string_view trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == value.npos)
        return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

std::string lower(std::string_view value) {
    std::string result{value};
    for (auto& c : result) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return result;
}

std::vector<std::string_view> lines(std::string_view text) {
    std::vector<std::string_view> result;
    while (!text.empty()) {
        const auto next = text.find_first_of("\r\n");
        result.push_back(text.substr(0, next));
        if (next == text.npos)
            break;
        const std::size_t step = text[next] == '\r' && text.substr(next, 2) == "\r\n" ? 2 : 1;
        text.remove_prefix(next + step);
    }
    return result;
}

std::string number(double value) {
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
    return {buffer, result.ptr};
}

std::vector<std::byte> bytes_of(std::string_view text) {
    const auto bytes = std::as_bytes(std::span{text});
    return {bytes.begin(), bytes.end()};
}

bool positive(double value) {
    return value > 0.0 && std::isfinite(value);
}

bool compatible(Quantity quantity, Unit unit) {
    if (quantity == Quantity::Unknown || unit == Unit::Unknown)
        return true;
    return unit == Unit::Counts ? quantity == Quantity::Counts : quantity == Quantity::Acceleration;
}

std::optional<double> acceleration_factor(Unit input, Unit output) {
    const auto in_si = [](Unit unit) -> std::optional<double> {
        switch (unit) {
        case Unit::G: return 9.80665;
        case Unit::Mps2: return 1.0;
        case Unit::Cmps2: return 0.01;
        default: return {};
        }
    };
    const auto from = in_si(input);
    const auto to = in_si(output);
    if (!from || !to)
        return {};
    return *from / *to;
}

void error(Diagnostics& diagnostics, std::string source, std::string message,
           std::size_t line, std::size_t column) {
    diagnostics.push_back({Severity::Error, std::move(source), std::move(message), line, column});
}

void warn(Diagnostics& diagnostics, std::string source, std::string message,
          std::size_t line, std::size_t column) {
    diagnostics.push_back({Severity::Warning, std::move(source), std::move(message), line, column});
}

} // namespace detail
} // namespace gmtools
