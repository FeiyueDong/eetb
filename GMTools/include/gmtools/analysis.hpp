#pragma once

#include "gmtools/gmtools.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace gmtools {

// Time-domain descriptive statistics. Values that need a sampling interval are
// optional so that a record with unknown dt still reports its amplitudes.
struct Statistics {
    std::size_t samples{};
    std::optional<double> dt_seconds;
    std::optional<double> sampling_rate_hz;
    double minimum{};
    double maximum{};
    double absolute_peak{};
    double signed_peak{};
    std::size_t peak_index{};
    std::optional<double> peak_time_seconds;
    double mean{};
    double standard_deviation{};
    double rms{};
    std::optional<double> duration_seconds;
};

// How engineering peak values are obtained. None means the current quantity is
// used as-is; the trapezoid schemes integrate acceleration to velocity and
// displacement (the second one removes the linear trend after each step).
enum class IntegrationScheme { None, Trapezoid, TrapezoidDetrend };

struct IntensityOptions {
    IntegrationScheme integration{IntegrationScheme::None};
};

// Engineering peak and energy measures. Peak values are always in SI units
// (m/s², m/s, m) and only exist when the current samples are accelerations with
// a known engineering unit and a valid sampling interval.
struct IntensityMeasures {
    bool available{};
    Unit source_unit{Unit::Unknown};
    bool integrated{};
    IntegrationScheme scheme{IntegrationScheme::None};
    std::optional<double> pga;
    std::optional<double> pgv;
    std::optional<double> pgd;
    std::optional<double> cav;
    std::optional<double> arias_intensity;
    std::optional<double> significant_duration_5_75;
    std::optional<double> significant_duration_5_95;
    std::optional<std::size_t> peak_index;
    std::optional<double> peak_time_seconds;
    // Normalised cumulative energy (Arias) at every sample, when available.
    std::vector<double> cumulative_energy;
    // Present only when an integration scheme was requested.
    std::vector<double> velocity_time;
    std::vector<double> displacement_time;
};

enum class SpectrumKind { Fft, Amplitude, Fas, Psd };
enum class WindowType { Rectangular, Hann, Hamming, Blackman };

struct SpectrumOptions {
    SpectrumKind kind{SpectrumKind::Fft};
    std::optional<std::size_t> nfft;  // padded length; defaults to the sample count
    WindowType window{WindowType::Rectangular};
    std::optional<bool> remove_mean;  // defaults: Welch removes the mean, single FFT does not
    bool welch{};
    std::size_t segment_length{};      // 0 = min(4096, N)
    double overlap_fraction{0.5};
    std::size_t fft_length{};          // 0 = segment length
    std::function<bool()> stop;        // optional cooperative cancellation
};

struct Spectrum {
    bool available{};
    SpectrumKind kind{SpectrumKind::Fft};
    WindowType window{WindowType::Rectangular};
    bool remove_mean{};
    std::size_t samples{};
    std::size_t window_length{};
    std::size_t fft_length{};
    std::size_t segments{};
    std::optional<double> dt_seconds;
    std::optional<double> sampling_rate_hz;
    std::optional<double> frequency_resolution_hz;
    double peak_frequency_hz{};
    double peak_value{};
    Unit unit{Unit::Unknown};
    Quantity quantity{Quantity::Unknown};
    std::string unit_text;  // e.g. "m/s^2", "m/s^2*s", "m/s^2^2/Hz"
    std::vector<double> frequency_hz;
    std::vector<double> value;
};

struct ResponseSpectrumOptions {
    double damping_ratio{0.05};
    double period_min_seconds{};
    double period_max_seconds{5.0};
    double period_step_seconds{0.02};
    std::function<bool()> stop;        // optional cooperative cancellation
};

struct ResponseSpectrum {
    bool available{};
    double damping_ratio{0.05};
    double dt_seconds{};
    std::size_t samples{};
    std::size_t points{};
    Unit source_unit{Unit::Unknown};
    std::vector<double> period_seconds;
    std::vector<double> sd;
    std::vector<double> sv;
    std::vector<double> sa;
    std::vector<double> psv;
    std::vector<double> psa;
};

std::string_view name(IntegrationScheme scheme);
std::string_view name(SpectrumKind kind);
std::string_view name(WindowType window);
std::optional<IntegrationScheme> integration_scheme_from_name(std::string_view value);
std::optional<SpectrumKind> spectrum_kind_from_name(std::string_view value);
std::optional<WindowType> window_from_name(std::string_view value);

// Each function appends diagnostics and returns a value with available=false on
// failure; the input Trace is never modified.
Statistics calculate_statistics(const Trace& trace, Diagnostics& diagnostics);
IntensityMeasures calculate_intensity(const Trace& trace, const IntensityOptions& options,
                                      Diagnostics& diagnostics);
Spectrum calculate_spectrum(const Trace& trace, const SpectrumOptions& options, Diagnostics& diagnostics);
ResponseSpectrum calculate_response_spectrum(const Trace& trace, const ResponseSpectrumOptions& options,
                                             Diagnostics& diagnostics);

// Result serialisation shared by the CLI and the GUI export paths.
std::string statistics_json(const Trace& trace, const Statistics& statistics);
std::string intensity_json(const Trace& trace, const IntensityMeasures& measures);
std::string spectrum_csv(const Trace& trace, const Spectrum& spectrum);
std::string response_spectrum_csv(const Trace& trace, const ResponseSpectrum& spectrum);

}  // namespace gmtools
