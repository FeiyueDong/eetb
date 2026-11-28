#include "gmtools/analysis.hpp"

#include "core/detail.hpp"

#include <cmath>
#include <limits>

namespace gmtools {
namespace {

void check_samples(const Trace& trace, Diagnostics& diagnostics) {
    for (std::size_t i = 0; i < trace.samples.size(); ++i) {
        if (!std::isfinite(trace.samples[i])) {
            detail::error(diagnostics, trace.name,
                          "sample " + std::to_string(i + 1) + " is not finite; statistics need finite samples");
            return;
        }
    }
}

}  // namespace

Statistics calculate_statistics(const Trace& trace, Diagnostics& diagnostics) {
    Statistics result;
    result.samples = trace.samples.size();
    if (trace.samples.empty()) {
        detail::error(diagnostics, trace.name, "an empty trace has no statistics");
        return result;
    }
    check_samples(trace, diagnostics);
    if (has_errors(diagnostics))
        return result;

    if (trace.dt_seconds && std::isfinite(*trace.dt_seconds) && *trace.dt_seconds > 0) {
        result.dt_seconds = *trace.dt_seconds;
        result.sampling_rate_hz = 1.0 / *trace.dt_seconds;
        if (result.samples > 1)
            result.duration_seconds = static_cast<double>(result.samples - 1) * *trace.dt_seconds;
        else
            result.duration_seconds = 0.0;
    } else if (trace.dt_seconds) {
        detail::warn(diagnostics, trace.name,
                     "the sampling interval is not positive; time-dependent statistics are unavailable");
    }

    long double sum = 0.0L;
    long double squares = 0.0L;
    double absolute_peak = -1.0;
    for (std::size_t i = 0; i < trace.samples.size(); ++i) {
        const double x = trace.samples[i];
        sum += x;
        squares += static_cast<long double>(x) * x;
        if (i == 0) {
            result.minimum = result.maximum = result.signed_peak = x;
            result.absolute_peak = std::abs(x);
            result.peak_index = 0;
            absolute_peak = std::abs(x);
            continue;
        }
        result.minimum = std::min(result.minimum, x);
        result.maximum = std::max(result.maximum, x);
        const double magnitude = std::abs(x);
        if (magnitude > absolute_peak) {
            absolute_peak = magnitude;
            result.absolute_peak = magnitude;
            result.signed_peak = x;
            result.peak_index = i;
        }
    }
    const auto count = static_cast<long double>(trace.samples.size());
    result.mean = static_cast<double>(sum / count);
    long double variance = 0.0L;
    for (const double x : trace.samples) {
        const long double deviation = static_cast<long double>(x) - result.mean;
        variance += deviation * deviation;
    }
    result.standard_deviation = static_cast<double>(std::sqrt(variance / count));
    result.rms = static_cast<double>(std::sqrt(squares / count));
    if (result.dt_seconds)
        result.peak_time_seconds = static_cast<double>(result.peak_index) * *result.dt_seconds;
    return result;
}

}  // namespace gmtools
