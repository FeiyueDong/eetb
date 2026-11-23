#include "../core/detail.hpp"

#include <libmseed.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>

namespace gmtools::detail {
namespace {

struct Record {
    MS3Record* value{};
    ~Record() { msr3_free(&value); }
};

struct Packer {
    MS3RecordPacker* value{};
    ~Packer() { msr3_pack_free(&value, nullptr); }
};

struct Segment {
    std::size_t index{};
    std::size_t sequence{};
    std::uint8_t version{};
    std::uint8_t publication{};
    int encoding{};
    std::uint64_t time_resolution{};
    std::uint64_t max_timing_deviation{};
};

bool known_time(nstime_t value) {
    return value != NSTERROR && value != NSTUNSET;
}

std::uint64_t distance(nstime_t a, nstime_t b) {
    return static_cast<std::uint64_t>(std::max(a, b)) - static_cast<std::uint64_t>(std::min(a, b));
}

std::uint64_t timestamp_resolution(const MS3Record& record) {
    if (record.formatversion == 3)
        return 1;
    // libmseed maps the v2 Blockette 1001 timing quality into this field.
    return record.extra && mseh_exists(&record, "/FDSN/Time/Quality") ? 1'000 : 100'000;
}

double period(const MS3Record& record) {
    return record.samprate < 0.0 ? -record.samprate : 1.0 / record.samprate;
}

std::uint64_t sample_capacity(int encoding, std::uint32_t payload) {
    switch (encoding) {
    case DE_INT16: return payload / 2;
    case DE_INT32:
    case DE_FLOAT32: return payload / 4;
    case DE_FLOAT64: return payload / 8;
    case DE_STEIM1: return std::uint64_t{payload / 64} * 60 + 1;
    case DE_STEIM2: return std::uint64_t{payload / 64} * 105 + 1;
    default: return 0;
    }
}

template<class T>
bool append_samples(Trace& trace, const MS3Record& record) {
    const auto* input = static_cast<const T*>(record.datasamples);
    for (std::int64_t i = 0; i < record.numsamples; ++i) {
        const auto sample = static_cast<double>(input[i]);
        if (!std::isfinite(sample))
            return false;
        trace.samples.push_back(sample);
    }
    return true;
}

bool append_samples(Trace& trace, const MS3Record& record) {
    if (!record.datasamples)
        return false;
    switch (record.sampletype) {
    case 'i': return append_samples<std::int32_t>(trace, record);
    case 'f': return append_samples<float>(trace, record);
    case 'd': return append_samples<double>(trace, record);
    default: return false;
    }
}

bool valid_sid(std::string_view sid, bool v2) {
    if (!sid.starts_with("FDSN:") || sid.size() >= LM_SIDLEN)
        return false;
    sid.remove_prefix(5);
    std::array<std::string_view, 6> codes;
    for (std::size_t i = 0; i < codes.size(); ++i) {
        const auto split = sid.find('_');
        if ((split == sid.npos) != (i + 1 == codes.size()))
            return false;
        codes[i] = sid.substr(0, split);
        if (split != sid.npos)
            sid.remove_prefix(split + 1);
    }
    if (codes[0].empty() || codes[1].empty() || codes[3].size() != 1 ||
        codes[4].empty() || codes[2] == "--" || codes[0].size() > 8 ||
        codes[1].size() > 8 || codes[2].size() > 8)
        return false;
    if (v2 && (codes[0].size() > 2 || codes[1].size() > 5 || codes[2].size() > 2 ||
               codes[4].size() != 1 || codes[5].size() != 1))
        return false;
    for (std::size_t i = 0; i < codes.size(); ++i) {
        if (!std::ranges::all_of(codes[i], [allow_dash = i == 1 || i == 2](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (allow_dash && c == '-');
        }))
            return false;
    }
    return true;
}

std::string output_sid(const Trace& trace) {
    if (trace.name.starts_with("FDSN:"))
        return trace.name;
    const auto value = [&](std::string_view key) {
        const auto item = trace.attributes.find(std::string{key});
        return item == trace.attributes.end() ? std::string{} : item->second;
    };
    const auto station = value("sac.kstnm");
    const auto network = value("sac.knetwk");
    if (!station.empty() && !network.empty() && trace.name.size() == 3) {
        return "FDSN:" + network + '_' + station + '_' + value("sac.khole") + '_' +
               trace.name[0] + '_' + trace.name[1] + '_' + trace.name[2];
    }
    return {};
}

} // namespace

ReadResult read_mseed(std::span<const std::byte> bytes, FormatId format, const TextParseOptions&) {
    ReadResult result;
    result.data.source_format = format;
    std::vector<Trace> traces;
    std::map<std::string, Segment> active;
    Record record;
    for (std::size_t offset = 0, sequence = 1; offset < bytes.size(); ++sequence) {
        const auto prefix = "record " + std::to_string(sequence) + " (byte " + std::to_string(offset + 1) + ")";
        const auto* raw = reinterpret_cast<const char*>(bytes.data() + offset);
        const auto remaining = bytes.size() - offset;
        const auto status = msr3_parse(raw, remaining, &record.value, MSF_VALIDATECRC | MSF_ATENDOFFILE, 0);
        if (status || !record.value) {
            error(result.diagnostics, "miniseed", prefix + ": " +
                (status < 0 ? ms_errorstr(status) : "incomplete record or unknown record length"));
            return result;
        }
        auto& rec = *record.value;
        if (rec.reclen <= 0 || static_cast<std::size_t>(rec.reclen) > remaining) {
            error(result.diagnostics, "miniseed", prefix + ": invalid record length");
            return result;
        }
        offset += static_cast<std::size_t>(rec.reclen);
        if (rec.samplecnt == 0) {
            warn(result.diagnostics, "miniseed", prefix + ": omitted a header-only record");
            continue;
        }

        const auto dt = period(rec);
        if (!positive(dt) || !known_time(rec.starttime) || rec.sid[0] == '\0') {
            error(result.diagnostics, "miniseed", prefix + ": invalid sampling interval, start time or source identifier");
            return result;
        }
        // The raw header carries the leap-second label; the common UTC
        // representation cannot distinguish :60 from the next minute.
        const auto raw_second = static_cast<unsigned char>(raw[rec.formatversion == 3 ? 14 : 26]);
        if (raw_second == 60) {
            error(result.diagnostics, "miniseed", prefix + ": a leap-second start is outside the supported UTC time model");
            return result;
        }
        std::uint32_t payload_offset{}, payload_size{};
        if (rec.samplecnt < 0 || msr3_data_bounds(&rec, &payload_offset, &payload_size) ||
            static_cast<std::uint64_t>(rec.samplecnt) > sample_capacity(rec.encoding, payload_size)) {
            error(result.diagnostics, "miniseed", prefix + ": unsupported encoding " + std::to_string(rec.encoding) +
                  " or sample count inconsistent with the payload");
            return result;
        }
        if (msr3_unpack_data(&rec, 0) != rec.samplecnt) {
            error(result.diagnostics, "miniseed", prefix + ": could not decode all declared samples");
            return result;
        }

        const std::string sid{rec.sid};
        const auto resolution = timestamp_resolution(rec);
        auto [previous, first] = active.try_emplace(sid);
        auto& segment = previous->second;
        std::string split_reason;
        if (!first) {
            auto& trace = traces[segment.index];
            if (dt != *trace.dt_seconds || rec.formatversion != segment.version ||
                rec.pubversion != segment.publication || rec.encoding != segment.encoding) {
                split_reason = "sampling interval, version, publication or encoding changed";
            } else {
                const auto expected = ms_sampletime(trace.start_time->time_since_epoch().count(),
                    static_cast<std::int64_t>(trace.samples.size()), -dt);
                const auto residual = distance(expected, rec.starttime);
                const auto tolerance = std::max(segment.time_resolution, resolution);
                if (!known_time(expected))
                    split_reason = "expected sample time is out of range";
                else if (residual > tolerance || static_cast<double>(residual) > dt * 1e9 / 16.0)
                    split_reason = rec.starttime < expected ? "overlapping or out-of-order record" : "time gap";
                else if (residual > segment.max_timing_deviation) {
                    if (segment.max_timing_deviation == 0) {
                        warn(result.diagnostics, "miniseed", prefix + ": " + sid +
                             " joined within the timestamp precision; samples retained");
                        result.diagnostics.back().channel = segment.index + 1;
                    }
                    segment.max_timing_deviation = residual;
                    trace.attributes["mseed.max_start_residual_ns"] = std::to_string(residual);
                }
            }
        }
        if (first || !split_reason.empty()) {
            segment = {traces.size(), segment.sequence + 1, rec.formatversion, rec.pubversion,
                       rec.encoding, resolution, 0};
            if (!split_reason.empty()) {
                warn(result.diagnostics, "miniseed", prefix + ": " + sid + " starts a separate trace (" + split_reason + ")");
                result.diagnostics.back().channel = segment.index + 1;
            }
            auto& trace = traces.emplace_back();
            trace.name = sid;
            trace.start_time = TimePoint{std::chrono::nanoseconds{rec.starttime}};
            trace.dt_seconds = dt;
            trace.attributes["source.dt"] = "miniSEED rate or period";
            trace.attributes["source.name"] = "miniSEED source identifier";
            trace.attributes["source.start_time"] = "miniSEED record time";
            trace.attributes["mseed.version"] = std::to_string(rec.formatversion);
            trace.attributes["mseed.publication"] = std::to_string(rec.pubversion);
            trace.attributes["mseed.encoding"] = std::to_string(rec.encoding);
            trace.attributes["mseed.segment"] = std::to_string(segment.sequence);
            trace.attributes["mseed.first_record_resolution_ns"] = std::to_string(resolution);
            if (rec.extra && rec.extralength) {
                const std::string_view extra{rec.extra, rec.extralength};
                if (text_view(std::as_bytes(std::span{extra})))
                    trace.attributes["mseed.first_record_extra"] = extra;
            }
        }
        if (!append_samples(traces[segment.index], rec)) {
            error(result.diagnostics, "miniseed", prefix + ": unsupported decoded sample type or nonfinite sample");
            return result;
        }
    }
    if (traces.empty())
        error(result.diagnostics, "miniseed", "no waveform samples");
    result.data.traces = std::move(traces);
    return result;
}

WriteResult write_mseed(const Trace& trace, FormatId format) {
    WriteResult result;
    const bool v2 = format == FormatId::Mseed2;
    const auto sid = output_sid(trace);
    if (!valid_sid(sid, v2))
        error(result.diagnostics, "miniseed", v2 ? "miniSEED 2 requires an FDSN source identifier that fits SEED 2 NSLC codes" :
                                                  "miniSEED 3 requires an FDSN source identifier");
    if (!trace.start_time || !known_time(trace.start_time->time_since_epoch().count()))
        error(result.diagnostics, "miniseed", "miniSEED output requires a known absolute first-sample time");
    if (!trace.dt_seconds)
        error(result.diagnostics, "miniseed", "miniSEED output requires a known sampling interval");
    if (has_errors(result.diagnostics))
        return result;
    const auto start = trace.start_time->time_since_epoch().count();
    if (*trace.dt_seconds < 1e-9 ||
        trace.samples.size() > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()) ||
        !known_time(ms_sampletime(start, static_cast<std::int64_t>(trace.samples.size()), -*trace.dt_seconds))) {
        error(result.diagnostics, "miniseed", "sampling interval or record end exceeds the supported nanosecond range");
        return result;
    }

    MS3Record record{};
    record.reclen = 4096;
    record.formatversion = v2 ? 2 : 3;
    record.starttime = start;
    record.samprate = -*trace.dt_seconds;
    record.encoding = DE_FLOAT64;
    record.sampletype = 'd';
    record.datasamples = const_cast<double*>(trace.samples.data()); // libmseed reads from this buffer.
    record.numsamples = record.samplecnt = static_cast<std::int64_t>(trace.samples.size());
    std::ranges::copy(sid, record.sid);

    Packer packer{msr3_pack_init(&record, MSF_FLUSHDATA, 0)};
    if (!packer.value) {
        error(result.diagnostics, "miniseed", "could not initialize miniSEED encoding");
        return result;
    }
    char* chunk{};
    std::int32_t size{};
    int status{};
    std::int64_t samples_in_v2_headers{};
    std::uint64_t max_v2_residual{};
    Record encoded_header;
    while ((status = msr3_pack_next(packer.value, &chunk, &size)) == 1) {
        if (v2) {
            // V2 quantizes both start time and rate. Verify their encoded
            // representation and reject exports that would alter the time grid.
            const auto parsed = msr3_parse(chunk, static_cast<std::uint64_t>(size),
                                           &encoded_header.value, MSF_ATENDOFFILE, 0);
            const auto expected = ms_sampletime(start, samples_in_v2_headers, record.samprate);
            if (parsed || !encoded_header.value || !known_time(expected)) {
                result.bytes.clear();
                error(result.diagnostics, "miniseed", "could not verify the encoded miniSEED 2 timing header");
                return result;
            }
            const auto& header = *encoded_header.value;
            const auto encoded_dt = period(header);
            const auto dt = *trace.dt_seconds;
            const auto residual = distance(header.starttime, expected);
            if (header.sid != sid || !positive(encoded_dt) ||
                std::abs(encoded_dt - dt) > 8.0 * std::numeric_limits<double>::epsilon() * dt ||
                !known_time(header.starttime) ||
                (samples_in_v2_headers == 0 && residual != 0) ||
                residual > timestamp_resolution(header) || static_cast<double>(residual) > dt * 1e9 / 16.0) {
                result.bytes.clear();
                error(result.diagnostics, "miniseed", "miniSEED 2 cannot preserve the source identifier or required timing; use miniSEED 3");
                return result;
            }
            max_v2_residual = std::max(max_v2_residual, residual);
            samples_in_v2_headers += header.samplecnt;
        }
        const auto bytes = std::as_bytes(std::span{chunk, static_cast<std::size_t>(size)});
        result.bytes.insert(result.bytes.end(), bytes.begin(), bytes.end());
    }
    std::int64_t packed{};
    msr3_pack_free(&packer.value, &packed);
    if (status != 0 || packed != record.samplecnt || result.bytes.empty() ||
        (v2 && samples_in_v2_headers != packed)) {
        result.bytes.clear();
        error(result.diagnostics, "miniseed", "could not encode all waveform samples");
        return result;
    }
    if (max_v2_residual != 0)
        warn(result.diagnostics, "miniseed", "miniSEED 2 rounded a record timestamp by up to " +
             std::to_string(max_v2_residual) + " ns");
    if (trace.unit != Unit::Unknown || trace.quantity != Quantity::Unknown || !trace.attributes.empty() ||
        trace.relative_start_seconds)
        warn(result.diagnostics, "miniseed", "engineering units, relative origin and extra metadata are not written");
    return result;
}

} // namespace gmtools::detail
