#include "gmtools/analysis.hpp"

#include "core/detail.hpp"

#include <Eigen/Dense>
#include <unsupported/Eigen/MatrixFunctions>

#include <algorithm>
#include <cmath>

namespace gmtools {
namespace {

constexpr double pi = 3.14159265358979323846;
// Guard against a period grid that would take an unreasonable amount of time.
constexpr std::size_t maximum_periods = 100000;

struct Coefficients {
    Eigen::Matrix2d transition;
    Eigen::Vector2d from_previous;
    Eigen::Vector2d from_next;
    double scale{1.0};
};

// First-order-hold recurrence: the ground acceleration is linear between two
// samples. The augmented 4x4 matrix exponent is evaluated once per period.
Coefficients coefficients(double omega, double damping, double dt) {
    const double scale = std::max(1.0, omega);
    Eigen::Matrix4d augmented = Eigen::Matrix4d::Zero();
    augmented(0, 1) = scale * dt;
    augmented(1, 0) = -omega * (omega / scale) * dt;
    augmented(1, 1) = -2.0 * damping * omega * dt;
    augmented(1, 2) = -dt;
    augmented(2, 3) = 1.0;
    const Eigen::Matrix4d exponential = augmented.exp();
    Coefficients result;
    result.transition = exponential.block<2, 2>(0, 0);
    result.from_next = exponential.block<2, 1>(0, 3);
    result.from_previous = exponential.block<2, 1>(0, 2) - result.from_next;
    result.scale = scale;
    return result;
}

}  // namespace

ResponseSpectrum calculate_response_spectrum(const Trace& trace, const ResponseSpectrumOptions& options,
                                             Diagnostics& diagnostics) {
    ResponseSpectrum result;
    result.damping_ratio = options.damping_ratio;
    result.samples = trace.samples.size();
    result.source_unit = trace.unit;
    if (trace.samples.size() < 2) {
        detail::error(diagnostics, trace.name, "a response spectrum needs at least two samples");
        return result;
    }
    if (trace.quantity != Quantity::Acceleration) {
        detail::error(diagnostics, trace.name,
                      "a response spectrum requires acceleration samples; the channel quantity is " +
                          std::string{name(trace.quantity)});
        return result;
    }
    const auto factor = unit_factor(trace.unit, Unit::Mps2);
    if (!factor || !(*factor > 0.0) || !std::isfinite(*factor)) {
        detail::error(diagnostics, trace.name,
                      "a response spectrum needs a known acceleration unit; the channel unit is " +
                          std::string{name(trace.unit)});
        return result;
    }
    if (!trace.dt_seconds || !std::isfinite(*trace.dt_seconds) || *trace.dt_seconds <= 0) {
        detail::error(diagnostics, trace.name, "a response spectrum needs a positive sampling interval");
        return result;
    }
    for (std::size_t i = 0; i < trace.samples.size(); ++i) {
        if (!std::isfinite(trace.samples[i])) {
            detail::error(diagnostics, trace.name,
                          "sample " + std::to_string(i + 1) + " is not finite; a response spectrum needs finite samples");
            return result;
        }
    }
    if (!(options.damping_ratio >= 0.0) || !std::isfinite(options.damping_ratio)) {
        detail::error(diagnostics, trace.name, "the damping ratio must be a finite, non-negative number");
        return result;
    }
    if (!(options.period_min_seconds >= 0.0) || !std::isfinite(options.period_min_seconds) ||
        !std::isfinite(options.period_max_seconds) || options.period_max_seconds < options.period_min_seconds) {
        detail::error(diagnostics, trace.name, "the period range is invalid");
        return result;
    }
    if (!(options.period_step_seconds > 0.0) || !std::isfinite(options.period_step_seconds)) {
        detail::error(diagnostics, trace.name, "the period step must be a positive number");
        return result;
    }
    const double span = options.period_max_seconds - options.period_min_seconds;
    const auto intervals = static_cast<std::size_t>(std::floor(span / options.period_step_seconds + 1e-12));
    const std::size_t points = intervals + 1;
    if (points > maximum_periods) {
        detail::error(diagnostics, trace.name,
                      "the period grid would contain " + std::to_string(points) + " points; increase the step");
        return result;
    }

    const double dt = *trace.dt_seconds;
    result.dt_seconds = dt;
    std::vector<double> acceleration(trace.samples.size());
    for (std::size_t i = 0; i < trace.samples.size(); ++i)
        acceleration[i] = trace.samples[i] * *factor;
    double pga = 0.0;
    for (const double value : acceleration)
        pga = std::max(pga, std::abs(value));
    if (points > 1 && options.period_step_seconds < dt)
        detail::warn(diagnostics, trace.name,
                     "period steps finer than the sample interval are not resolved by the sampled record");

    result.period_seconds.resize(points);
    result.sd.assign(points, 0.0);
    result.sv.assign(points, 0.0);
    result.sa.assign(points, 0.0);
    result.psv.assign(points, 0.0);
    result.psa.assign(points, 0.0);
    for (std::size_t p = 0; p < points; ++p) {
        if (options.stop && options.stop()) {
            detail::error(diagnostics, trace.name, "the response spectrum calculation was cancelled");
            return result;
        }
        // Integer index arithmetic keeps the last point on the requested grid.
        const double period = options.period_min_seconds + static_cast<double>(p) * options.period_step_seconds;
        result.period_seconds[p] = period;
        if (period <= 0.0) {
            result.sd[p] = 0.0;
            result.sv[p] = 0.0;
            result.sa[p] = pga;
            result.psv[p] = 0.0;
            result.psa[p] = pga;
            continue;
        }
        const double omega = 2.0 * pi / period;
        if (!std::isfinite(omega) || omega <= 0.0) {
            detail::error(diagnostics, trace.name, "the period grid produced a non-finite angular frequency");
            return result;
        }
        const auto coefficients_for_period = coefficients(omega, options.damping_ratio, dt);
        Eigen::Vector2d state = Eigen::Vector2d::Zero();
        double sd = 0.0;
        double sv = 0.0;
        double sa = 0.0;
        for (std::size_t i = 0; i + 1 < acceleration.size(); ++i) {
            state = coefficients_for_period.transition * state +
                coefficients_for_period.from_previous * acceleration[i] +
                coefficients_for_period.from_next * acceleration[i + 1];
            const double displacement = state(0) / coefficients_for_period.scale;
            const double velocity = state(1);
            const double absolute = -omega * omega * displacement - 2.0 * options.damping_ratio * omega * velocity;
            if (!std::isfinite(displacement) || !std::isfinite(velocity) || !std::isfinite(absolute)) {
                detail::error(diagnostics, trace.name,
                              "the response exceeded the numeric range at period " +
                                  std::to_string(period) + " s");
                return result;
            }
            sd = std::max(sd, std::abs(displacement));
            sv = std::max(sv, std::abs(velocity));
            sa = std::max(sa, std::abs(absolute));
        }
        result.sd[p] = sd;
        result.sv[p] = sv;
        result.sa[p] = sa;
        result.psv[p] = omega * sd;
        result.psa[p] = omega * omega * sd;
    }
    result.points = points;
    result.available = true;
    return result;
}

}  // namespace gmtools
