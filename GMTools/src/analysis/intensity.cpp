#include "gmtools/analysis.hpp"

#include "core/detail.hpp"

#include <algorithm>
#include <cmath>

namespace gmtools {
namespace {

constexpr double gravity = 9.80665;

std::vector<double> trapezoid(const std::vector<double>& values, double dt) {
    std::vector<double> integral(values.size(), 0.0);
    for (std::size_t i = 1; i < values.size(); ++i)
        integral[i] = integral[i - 1] + 0.5 * dt * (values[i - 1] + values[i]);
    return integral;
}

// Least-squares line through (i*dt, values[i]) subtracted from the series. Both
// the mean and the slope are removed, matching the usual baseline correction.
void remove_linear_trend(std::vector<double>& values, double dt) {
    const auto n = static_cast<long double>(values.size());
    if (values.size() < 2)
        return;
    long double sum_t = 0.0L, sum_v = 0.0L, sum_tt = 0.0L, sum_tv = 0.0L;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const long double t = static_cast<long double>(i) * dt;
        const long double v = values[i];
        sum_t += t;
        sum_v += v;
        sum_tt += t * t;
        sum_tv += t * v;
    }
    const long double denominator = n * sum_tt - sum_t * sum_t;
    if (denominator == 0.0L) {
        const long double mean = sum_v / n;
        for (double& v : values)
            v = static_cast<double>(static_cast<long double>(v) - mean);
        return;
    }
    const long double slope = (n * sum_tv - sum_t * sum_v) / denominator;
    const long double intercept = (sum_v - slope * sum_t) / n;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const long double t = static_cast<long double>(i) * dt;
        values[i] = static_cast<double>(static_cast<long double>(values[i]) - (intercept + slope * t));
    }
}

// Time at which the cumulative energy first reaches `target` (0..total),
// interpolated linearly inside the bracket. Returns nullopt when the energy is
// flat there and no meaningful bracket exists.
std::optional<double> crossing_time(const std::vector<double>& energy, double target, double dt) {
    if (target <= 0.0)
        return 0.0;
    for (std::size_t i = 1; i < energy.size(); ++i) {
        if (energy[i] < target)
            continue;
        const double lower = energy[i - 1];
        const double upper = energy[i];
        const double fraction = upper > lower ? (target - lower) / (upper - lower) : 0.0;
        return (static_cast<double>(i - 1) + std::clamp(fraction, 0.0, 1.0)) * dt;
    }
    return std::nullopt;
}

}  // namespace

IntensityMeasures calculate_intensity(const Trace& trace, const IntensityOptions& options,
                                      Diagnostics& diagnostics) {
    IntensityMeasures result;
    result.source_unit = trace.unit;
    result.scheme = options.integration;
    result.integrated = options.integration != IntegrationScheme::None;
    if (trace.samples.empty()) {
        detail::error(diagnostics, trace.name, "an empty trace has no intensity measures");
        return result;
    }
    if (trace.quantity != Quantity::Acceleration) {
        detail::error(diagnostics, trace.name,
                      "intensity measures require acceleration samples; the channel quantity is " +
                          std::string{name(trace.quantity)});
        return result;
    }
    const auto factor = unit_factor(trace.unit, Unit::Mps2);
    if (!factor || !(*factor > 0.0) || !std::isfinite(*factor)) {
        detail::error(diagnostics, trace.name,
                      "intensity measures need a known acceleration unit; the channel unit is " +
                          std::string{name(trace.unit)});
        return result;
    }
    if (!trace.dt_seconds || !std::isfinite(*trace.dt_seconds) || *trace.dt_seconds <= 0) {
        detail::error(diagnostics, trace.name, "intensity measures need a positive sampling interval");
        return result;
    }
    if (trace.samples.size() < 2) {
        detail::error(diagnostics, trace.name, "intensity measures need at least two samples");
        return result;
    }
    for (std::size_t i = 0; i < trace.samples.size(); ++i) {
        if (!std::isfinite(trace.samples[i])) {
            detail::error(diagnostics, trace.name,
                          "sample " + std::to_string(i + 1) + " is not finite; intensity measures need finite samples");
            return result;
        }
    }
    const double dt = *trace.dt_seconds;
    std::vector<double> acceleration(trace.samples.size());
    for (std::size_t i = 0; i < trace.samples.size(); ++i)
        acceleration[i] = trace.samples[i] * *factor;

    double pga = 0.0;
    std::size_t peak = 0;
    std::vector<double> absolute(acceleration.size());
    for (std::size_t i = 0; i < acceleration.size(); ++i) {
        absolute[i] = std::abs(acceleration[i]);
        if (absolute[i] > pga) {
            pga = absolute[i];
            peak = i;
        }
    }
    result.pga = pga;
    result.peak_index = peak;
    result.peak_time_seconds = static_cast<double>(peak) * dt;

    std::vector<double> squared(acceleration.size());
    for (std::size_t i = 0; i < acceleration.size(); ++i)
        squared[i] = acceleration[i] * acceleration[i];
    const auto energy = trapezoid(squared, dt);
    const double total_energy = energy.back();
    result.cav = trapezoid(absolute, dt).back();
    result.arias_intensity = total_energy > 0.0 ? 3.14159265358979323846 / (2.0 * gravity) * total_energy : 0.0;
    result.cumulative_energy.resize(energy.size());
    if (total_energy > 0.0)
        for (std::size_t i = 0; i < energy.size(); ++i)
            result.cumulative_energy[i] = energy[i] / total_energy;

    if (total_energy > 0.0) {
        const auto start_5 = crossing_time(energy, 0.05 * total_energy, dt);
        const auto start_75 = crossing_time(energy, 0.75 * total_energy, dt);
        const auto end_75 = crossing_time(energy, 0.95 * total_energy, dt);
        if (start_5 && end_75)
            result.significant_duration_5_95 = *end_75 - *start_5;
        if (start_5 && start_75)
            result.significant_duration_5_75 = *start_75 - *start_5;
    } else {
        detail::warn(diagnostics, trace.name,
                     "the record has zero acceleration energy; significant durations are unavailable");
    }

    if (options.integration != IntegrationScheme::None) {
        result.velocity_time = trapezoid(acceleration, dt);
        if (options.integration == IntegrationScheme::TrapezoidDetrend)
            remove_linear_trend(result.velocity_time, dt);
        result.displacement_time = trapezoid(result.velocity_time, dt);
        if (options.integration == IntegrationScheme::TrapezoidDetrend)
            remove_linear_trend(result.displacement_time, dt);
        double pgv = 0.0;
        double pgd = 0.0;
        for (std::size_t i = 0; i < result.velocity_time.size(); ++i) {
            pgv = std::max(pgv, std::abs(result.velocity_time[i]));
            pgd = std::max(pgd, std::abs(result.displacement_time[i]));
        }
        result.pgv = pgv;
        result.pgd = pgd;
    }
    result.available = true;
    return result;
}

}  // namespace gmtools
