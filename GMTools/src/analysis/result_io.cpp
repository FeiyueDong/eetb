#include "gmtools/analysis.hpp"

#include <nlohmann/json.hpp>

#include <sstream>

namespace gmtools {

std::string_view name(IntegrationScheme scheme) {
    switch (scheme) {
    case IntegrationScheme::None:
        return "none";
    case IntegrationScheme::Trapezoid:
        return "trapezoid";
    case IntegrationScheme::TrapezoidDetrend:
        return "trapezoid-detrend";
    }
    return "none";
}

std::string_view name(SpectrumKind kind) {
    switch (kind) {
    case SpectrumKind::Fft:
        return "fft";
    case SpectrumKind::Amplitude:
        return "amplitude";
    case SpectrumKind::Fas:
        return "fas";
    case SpectrumKind::Psd:
        return "psd";
    }
    return "fft";
}

std::string_view name(WindowType window) {
    switch (window) {
    case WindowType::Rectangular:
        return "rectangular";
    case WindowType::Hann:
        return "hann";
    case WindowType::Hamming:
        return "hamming";
    case WindowType::Blackman:
        return "blackman";
    }
    return "rectangular";
}

std::optional<IntegrationScheme> integration_scheme_from_name(std::string_view value) {
    if (value == "none")
        return IntegrationScheme::None;
    if (value == "trapezoid")
        return IntegrationScheme::Trapezoid;
    if (value == "trapezoid-detrend")
        return IntegrationScheme::TrapezoidDetrend;
    return std::nullopt;
}

std::optional<SpectrumKind> spectrum_kind_from_name(std::string_view value) {
    if (value == "fft")
        return SpectrumKind::Fft;
    if (value == "amplitude")
        return SpectrumKind::Amplitude;
    if (value == "fas")
        return SpectrumKind::Fas;
    if (value == "psd")
        return SpectrumKind::Psd;
    return std::nullopt;
}

std::optional<WindowType> window_from_name(std::string_view value) {
    if (value == "rectangular")
        return WindowType::Rectangular;
    if (value == "hann")
        return WindowType::Hann;
    if (value == "hamming")
        return WindowType::Hamming;
    if (value == "blackman")
        return WindowType::Blackman;
    return std::nullopt;
}

namespace {

using json = nlohmann::json;

json base_metadata(const Trace& trace) {
    json value;
    value["channel"] = trace.name;
    value["samples"] = trace.samples.size();
    value["quantity"] = std::string{name(trace.quantity)};
    value["unit"] = std::string{name(trace.unit)};
    if (trace.dt_seconds)
        value["dt_seconds"] = *trace.dt_seconds;
    return value;
}

json optional(const std::optional<double>& value) {
    return value ? json(*value) : json(nullptr);
}

json optional(const std::optional<std::size_t>& value) {
    return value ? json(*value) : json(nullptr);
}

}  // namespace

std::string statistics_json(const Trace& trace, const Statistics& statistics) {
    json value = base_metadata(trace);
    value["kind"] = "statistics";
    value["samples"] = statistics.samples;
    value["dt_seconds"] = optional(statistics.dt_seconds);
    value["sampling_rate_hz"] = optional(statistics.sampling_rate_hz);
    value["minimum"] = statistics.minimum;
    value["maximum"] = statistics.maximum;
    value["absolute_peak"] = statistics.absolute_peak;
    value["signed_peak"] = statistics.signed_peak;
    value["peak_index"] = statistics.peak_index + 1;  // 1-based, matching the rest of the project
    value["peak_time_seconds"] = optional(statistics.peak_time_seconds);
    value["mean"] = statistics.mean;
    value["standard_deviation"] = statistics.standard_deviation;
    value["rms"] = statistics.rms;
    value["duration_seconds"] = optional(statistics.duration_seconds);
    return value.dump(2) + "\n";
}

std::string intensity_json(const Trace& trace, const IntensityMeasures& measures) {
    json value = base_metadata(trace);
    value["kind"] = "intensity";
    value["available"] = measures.available;
    value["scheme"] = std::string{name(measures.scheme)};
    value["integrated"] = measures.integrated;
    value["units"] = {{"pga", "m/s^2"}, {"pgv", "m/s"}, {"pgd", "m"},
                      {"cav", "m/s"}, {"arias_intensity", "m/s"},
                      {"significant_duration_5_75", "s"}, {"significant_duration_5_95", "s"}};
    value["pga"] = optional(measures.pga);
    value["pgv"] = optional(measures.pgv);
    value["pgd"] = optional(measures.pgd);
    value["pgv_pgd_estimated"] = measures.integrated;
    value["cav"] = optional(measures.cav);
    value["arias_intensity"] = optional(measures.arias_intensity);
    value["significant_duration_5_75"] = optional(measures.significant_duration_5_75);
    value["significant_duration_5_95"] = optional(measures.significant_duration_5_95);
    value["peak_index"] = measures.peak_index ? json(*measures.peak_index + 1) : json(nullptr);
    value["peak_time_seconds"] = optional(measures.peak_time_seconds);
    value["method"] =
        "Arias intensity uses pi/(2*9.80665) times the cumulative squared acceleration; significant "
        "durations interpolate linearly in cumulative energy between 5% and 75%/95%.";
    return value.dump(2) + "\n";
}

std::string spectrum_csv(const Trace& trace, const Spectrum& spectrum) {
    json header = base_metadata(trace);
    header["kind"] = "spectrum";
    header["spectrum"] = std::string{name(spectrum.kind)};
    header["window"] = std::string{name(spectrum.window)};
    header["remove_mean"] = spectrum.remove_mean;
    header["segments"] = spectrum.segments;
    header["window_length"] = spectrum.window_length;
    header["fft_length"] = spectrum.fft_length;
    header["frequency_resolution_hz"] = optional(spectrum.frequency_resolution_hz);
    header["unit"] = spectrum.unit_text;
    std::ostringstream out;
    out << "# GMTools analysis 1\n# " << header.dump() << "\nfrequency_hz,value\n";
    out.precision(17);
    for (std::size_t i = 0; i < spectrum.frequency_hz.size(); ++i)
        out << spectrum.frequency_hz[i] << ',' << spectrum.value[i] << '\n';
    return out.str();
}

std::string response_spectrum_csv(const Trace& trace, const ResponseSpectrum& spectrum) {
    json header = base_metadata(trace);
    header["kind"] = "response_spectrum";
    header["damping_ratio"] = spectrum.damping_ratio;
    header["points"] = spectrum.points;
    header["units"] = {{"period", "s"}, {"sd", "m"}, {"sv", "m/s"},
                       {"sa", "m/s^2"}, {"psv", "m/s"}, {"psa", "m/s^2"}};
    header["method"] =
        "First-order-hold recurrence on the unit-mass SDOF oscillator; peaks are sampled at the "
        "input samples. psv = omega*sd, psa = omega^2*sd, sa is true absolute acceleration.";
    std::ostringstream out;
    out << "# GMTools analysis 1\n# " << header.dump() << "\nperiod_s,sd_m,sv_mps,sa_mps2,psv_mps,psa_mps2\n";
    out.precision(17);
    for (std::size_t i = 0; i < spectrum.period_seconds.size(); ++i)
        out << spectrum.period_seconds[i] << ',' << spectrum.sd[i] << ',' << spectrum.sv[i] << ','
            << spectrum.sa[i] << ',' << spectrum.psv[i] << ',' << spectrum.psa[i] << '\n';
    return out.str();
}

}  // namespace gmtools
