#include "gmtools/analysis.hpp"

#include "core/detail.hpp"

#include <Eigen/Dense>
#include <unsupported/Eigen/FFT>

#include <algorithm>
#include <cmath>
#include <complex>

namespace gmtools {
namespace {

constexpr double pi = 3.14159265358979323846;
// Hard limits keep a bad request from exhausting memory before Eigen allocates.
constexpr std::size_t maximum_fft_length = std::size_t{1} << 24;
constexpr std::size_t maximum_segments = 100000;

std::vector<double> window_values(WindowType type, std::size_t length) {
    std::vector<double> values(length, 1.0);
    if (length <= 1)
        return values;
    const double denominator = static_cast<double>(length - 1);
    for (std::size_t i = 0; i < length; ++i) {
        const double phase = 2.0 * pi * static_cast<double>(i) / denominator;
        switch (type) {
        case WindowType::Rectangular:
            values[i] = 1.0;
            break;
        case WindowType::Hann:
            values[i] = 0.5 - 0.5 * std::cos(phase);
            break;
        case WindowType::Hamming:
            values[i] = 0.54 - 0.46 * std::cos(phase);
            break;
        case WindowType::Blackman:
            values[i] = 0.42 - 0.5 * std::cos(phase) + 0.08 * std::cos(2.0 * phase);
            break;
        }
    }
    return values;
}

double sum(const std::vector<double>& values) {
    long double total = 0.0L;
    for (const double value : values)
        total += value;
    return static_cast<double>(total);
}

double sum_squares(const std::vector<double>& values) {
    long double total = 0.0L;
    for (const double value : values)
        total += static_cast<long double>(value) * value;
    return static_cast<double>(total);
}

std::string unit_text_for(SpectrumKind kind, Unit unit) {
    std::string base{name(unit)};
    switch (kind) {
    case SpectrumKind::Psd:
        return base + "^2/Hz";
    case SpectrumKind::Fas:
        return base + "*s";
    default:
        return base;
    }
}

}  // namespace

Spectrum calculate_spectrum(const Trace& trace, const SpectrumOptions& options, Diagnostics& diagnostics) {
    Spectrum result;
    result.kind = options.kind;
    result.window = options.window;
    result.samples = trace.samples.size();
    result.unit = trace.unit;
    result.quantity = trace.quantity;
    result.unit_text = unit_text_for(options.kind, trace.unit);
    if (trace.samples.empty()) {
        detail::error(diagnostics, trace.name, "an empty trace has no spectrum");
        return result;
    }
    for (std::size_t i = 0; i < trace.samples.size(); ++i) {
        if (!std::isfinite(trace.samples[i])) {
            detail::error(diagnostics, trace.name,
                          "sample " + std::to_string(i + 1) + " is not finite; the spectrum needs finite samples");
            return result;
        }
    }
    if (!trace.dt_seconds || !std::isfinite(*trace.dt_seconds) || *trace.dt_seconds <= 0) {
        detail::error(diagnostics, trace.name, "the spectrum needs a positive sampling interval");
        return result;
    }
    const double dt = *trace.dt_seconds;
    const double fs = 1.0 / dt;
    result.dt_seconds = dt;
    result.sampling_rate_hz = fs;
    const std::size_t n = trace.samples.size();
    // Same default as the GUI/CLI documentation: Welch removes each segment mean.
    const bool remove_mean = options.remove_mean.value_or(options.welch);

    std::size_t segment_length = n;
    std::size_t overlap = 0;
    std::size_t fft_length = options.nfft.value_or(n);
    std::size_t segments = 1;
    if (options.welch) {
        if (n < 2) {
            detail::error(diagnostics, trace.name, "Welch spectra need at least two samples");
            return result;
        }
        segment_length = options.segment_length ? options.segment_length : std::min<std::size_t>(4096, n);
        if (segment_length < 2) {
            detail::error(diagnostics, trace.name, "Welch segment length must be at least 2 samples");
            return result;
        }
        if (!(options.overlap_fraction >= 0.0 && options.overlap_fraction < 1.0)) {
            detail::error(diagnostics, trace.name, "Welch overlap must be in [0, 1)");
            return result;
        }
        overlap = static_cast<std::size_t>(options.overlap_fraction * static_cast<double>(segment_length));
        if (overlap >= segment_length)
            overlap = segment_length - 1;
        fft_length = options.fft_length ? options.fft_length : segment_length;
        if (fft_length < segment_length) {
            detail::error(diagnostics, trace.name, "Welch FFT length must not be shorter than the segment length");
            return result;
        }
        const std::size_t step = segment_length - overlap;
        segments = step > 0 ? 1 + (n - segment_length) / step : 0;
        if (segments == 0) {
            detail::error(diagnostics, trace.name,
                          "the record is shorter than one Welch segment (" + std::to_string(segment_length) +
                              " samples)");
            return result;
        }
        if (segments > maximum_segments) {
            detail::error(diagnostics, trace.name,
                          "Welch segmentation would create " + std::to_string(segments) +
                              " segments; increase the segment length or overlap");
            return result;
        }
    } else if (fft_length < n) {
        detail::error(diagnostics, trace.name,
                      "zero padding cannot shorten the record; the FFT length is " + std::to_string(fft_length) +
                          " but the record has " + std::to_string(n) + " samples");
        return result;
    }
    if (fft_length > maximum_fft_length) {
        detail::error(diagnostics, trace.name,
                      "the FFT length " + std::to_string(fft_length) + " exceeds the supported limit");
        return result;
    }

    auto window = window_values(options.window, segment_length);
    const double window_sum = sum(window);
    double window_energy = sum_squares(window);
    if (!(window_energy > 0.0) || !std::isfinite(window_energy)) {
        if (options.window != WindowType::Rectangular && options.welch && segment_length == 2) {
            detail::warn(diagnostics, trace.name,
                         "the selected window has zero energy at this segment length; using a rectangular window");
            result.window = WindowType::Rectangular;
            window = window_values(WindowType::Rectangular, segment_length);
            window_energy = sum_squares(window);
        } else {
            detail::error(diagnostics, trace.name, "the selected window has no spectral energy");
            return result;
        }
    }

    Eigen::FFT<double> fft;
    std::vector<std::complex<double>> transformed(fft_length);
    std::vector<double> buffer(fft_length, 0.0);
    const std::size_t bins = fft_length / 2 + 1;
    std::vector<double> power(bins, 0.0);
    double scaling = 0.0;

    auto transform = [&](std::size_t start, std::size_t length) {
        std::fill(buffer.begin(), buffer.end(), 0.0);
        long double mean = 0.0L;
        if (remove_mean) {
            for (std::size_t i = 0; i < length; ++i)
                mean += trace.samples[start + i];
            mean /= static_cast<long double>(length);
        }
        for (std::size_t i = 0; i < length; ++i)
            buffer[i] = (static_cast<long double>(trace.samples[start + i]) - mean) * window[i];
        fft.fwd(transformed, buffer);
    };

    if (!options.welch) {
        transform(0, n);
        for (std::size_t k = 0; k < bins; ++k)
            power[k] = std::norm(transformed[k]);
        result.segments = 1;
        scaling = 1.0 / (fs * window_energy);
    } else {
        const std::size_t step = segment_length - overlap;
        for (std::size_t segment = 0; segment < segments; ++segment) {
            transform(segment * step, segment_length);
            for (std::size_t k = 0; k < bins; ++k)
                power[k] += std::norm(transformed[k]);
        }
        result.segments = segments;
        scaling = 1.0 / (static_cast<double>(segments) * fs * window_energy);
    }

    result.window_length = segment_length;
    result.fft_length = fft_length;
    result.remove_mean = remove_mean;
    result.frequency_resolution_hz = fs / static_cast<double>(fft_length);
    result.frequency_hz.resize(bins);
    result.value.resize(bins);
    for (std::size_t k = 0; k < bins; ++k) {
        result.frequency_hz[k] = static_cast<double>(k) * fs / static_cast<double>(fft_length);
        const double magnitude = std::sqrt(power[k]);
        // One-sided folding: DC and an even-length Nyquist bin have no mirror.
        const bool mirror = k != 0 && !(fft_length % 2 == 0 && k == fft_length / 2);
        const double fold = mirror ? 2.0 : 1.0;
        double value = 0.0;
        switch (options.kind) {
        case SpectrumKind::Fft:
            value = magnitude;
            break;
        case SpectrumKind::Amplitude:
            value = window_sum > 0.0 ? fold * magnitude / window_sum : 0.0;
            break;
        case SpectrumKind::Fas:
            value = dt * magnitude;
            break;
        case SpectrumKind::Psd:
            value = fold * power[k] * scaling;
            break;
        }
        result.value[k] = value;
        if (value > result.peak_value) {
            result.peak_value = value;
            result.peak_frequency_hz = result.frequency_hz[k];
        }
    }
    result.available = true;
    return result;
}

}  // namespace gmtools
