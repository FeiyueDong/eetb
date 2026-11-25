// Steim-2 decoder adapted from REST data. See licenses/REST-data-MIT.txt.
#include "../core/detail.hpp"
#include <bit>
#include <cmath>
#include <cstdint>
#include <array>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace gmtools::detail {
namespace {
std::uint32_t be_u32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

std::uint32_t le_u32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[3]) << 24) | (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[1]) << 8) | static_cast<std::uint32_t>(p[0]);
}

std::int32_t sign_extend(std::uint32_t value, unsigned bits) {
    if (bits == 0 || bits > 32) {
        throw std::runtime_error("invalid sign-extension width");
    }
    if (bits == 32) {
        return static_cast<std::int32_t>(value);
    }

    const std::uint32_t mask = (std::uint32_t{1} << bits) - 1u;
    value &= mask;
    const std::uint32_t sign = std::uint32_t{1} << (bits - 1u);
    if ((value & sign) != 0u) {
        value |= ~mask;
    }
    return static_cast<std::int32_t>(value);
}

void append_steim2_word(std::uint32_t word, std::uint8_t control_code, std::vector<std::int32_t>& diffs) {
    switch (control_code) {
    case 0:
        return;

    case 1: // 4 x 8-bit differences
        diffs.push_back(sign_extend(word >> 24, 8));
        diffs.push_back(sign_extend(word >> 16, 8));
        diffs.push_back(sign_extend(word >> 8, 8));
        diffs.push_back(sign_extend(word, 8));
        return;

    case 2: {
        const std::uint8_t dnib = static_cast<std::uint8_t>((word >> 30) & 0x3u);
        switch (dnib) {
        case 1: // 1 x 30-bit
            diffs.push_back(sign_extend(word, 30));
            return;
        case 2: // 2 x 15-bit
            diffs.push_back(sign_extend(word >> 15, 15));
            diffs.push_back(sign_extend(word, 15));
            return;
        case 3: // 3 x 10-bit
            diffs.push_back(sign_extend(word >> 20, 10));
            diffs.push_back(sign_extend(word >> 10, 10));
            diffs.push_back(sign_extend(word, 10));
            return;
        default:
            throw std::runtime_error("invalid Steim-2 dnib for control code 2");
        }
    }

    case 3: {
        const std::uint8_t dnib = static_cast<std::uint8_t>((word >> 30) & 0x3u);
        switch (dnib) {
        case 0: // 5 x 6-bit
            for (int shift : {24, 18, 12, 6, 0}) {
                diffs.push_back(sign_extend(word >> shift, 6));
            }
            return;
        case 1: // 6 x 5-bit
            for (int shift : {25, 20, 15, 10, 5, 0}) {
                diffs.push_back(sign_extend(word >> shift, 5));
            }
            return;
        case 2: // 7 x 4-bit; bits 29..28 are unused
            for (int shift : {24, 20, 16, 12, 8, 4, 0}) {
                diffs.push_back(sign_extend(word >> shift, 4));
            }
            return;
        default:
            throw std::runtime_error("invalid Steim-2 dnib for control code 3");
        }
    }

    default:
        throw std::runtime_error("invalid Steim-2 control code");
    }
}

struct Header {
    unsigned encoding, word_order, data_offset, sample_count;
};

std::vector<std::int32_t> decode_steim2(std::span<const std::uint8_t> record, const Header& h) {
    if (h.encoding != 11) {
        throw std::runtime_error("unsupported encoding: only Steim-2 (11) is implemented");
    }
    if (h.data_offset >= record.size()) {
        throw std::runtime_error("data offset is outside the record");
    }

    const std::size_t compressed_bytes = record.size() - h.data_offset;
    if ((compressed_bytes % 64u) != 0u) {
        throw std::runtime_error("Steim data region is not an integer number of 64-byte frames");
    }

    auto read_word = [&](const std::uint8_t* p) -> std::uint32_t {
        if (h.word_order == 1) {
            return be_u32(p);
        }
        if (h.word_order == 0) {
            return le_u32(p);
        }
        throw std::runtime_error("invalid Blockette 1000 word-order flag");
    };

    std::vector<std::int32_t> diffs;
    diffs.reserve(static_cast<std::size_t>(h.sample_count) + 32u);

    std::int32_t x0 = 0;
    std::int32_t xn = 0;
    bool got_constants = false;

    const std::size_t frame_count = compressed_bytes / 64u;
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        const std::uint8_t* frame_ptr = record.data() + h.data_offset + frame * 64u;
        std::array<std::uint32_t, 16> words{};
        for (std::size_t i = 0; i < 16; ++i) {
            words[i] = read_word(frame_ptr + i * 4u);
        }

        const std::uint32_t control = words[0];
        std::size_t first_data_word = 1;
        if (frame == 0) {
            x0 = static_cast<std::int32_t>(words[1]);
            xn = static_cast<std::int32_t>(words[2]);
            got_constants = true;
            first_data_word = 3;
        }

        for (std::size_t word_index = first_data_word; word_index < 16; ++word_index) {
            const unsigned shift = static_cast<unsigned>(30u - 2u * word_index);
            const auto code = static_cast<std::uint8_t>((control >> shift) & 0x3u);
            // Four 8-bit differences are in transmission order even for little-endian frames.
            const auto word = code == 1 ? be_u32(frame_ptr + word_index * 4) : words[word_index];
            append_steim2_word(word, code, diffs);
        }
    }

    if (!got_constants) {
        throw std::runtime_error("Steim-2 record has no frame 0");
    }

    std::vector<std::int32_t> samples;
    samples.reserve(h.sample_count);
    if (h.sample_count == 0) {
        return samples;
    }

    // In Steim, X0 is stored explicitly. The first packed difference is d0,
    // which connects the previous record to X0, so decompression of this
    // standalone record starts at diffs[1].
    if (h.sample_count > 1 && diffs.size() < h.sample_count) {
        throw std::runtime_error("not enough Steim-2 differences for declared sample count");
    }

    samples.push_back(x0);
    for (std::size_t i = 1; i < h.sample_count; ++i) {
        const std::int64_t next = static_cast<std::int64_t>(samples.back()) + static_cast<std::int64_t>(diffs[i]);
        if (next < std::numeric_limits<std::int32_t>::min() || next > std::numeric_limits<std::int32_t>::max()) {
            throw std::runtime_error("Steim-2 integration overflow");
        }
        samples.push_back(static_cast<std::int32_t>(next));
    }

    if (!samples.empty() && samples.back() != xn) {
        std::ostringstream oss;
        oss << "Steim-2 reverse integration constant mismatch: decoded " << samples.back() << ", header Xn " << xn;
        throw std::runtime_error(oss.str());
    }

    return samples;
}

unsigned u16(const std::uint8_t* p) { return unsigned(p[0]) * 256 + p[1]; }

bool layout(std::span<const std::uint8_t> b) {
    if (b.size() < 64 || b[0] != 'w' || (b[1] != 'c' && b[1] != 's' && b[1] != 't'))
        return false;
    const auto* p = b.data();
    return b[39] == 1 && u16(p + 46) == 48 && u16(p + 48) == 1000 && u16(p + 50) == 0 && b[54] == 5 + (b[2] >> 5) &&
           b[54] >= 7 && u16(p + 44) >= 64 && u16(p + 44) % 64 == 0 && u16(p + 44) < (1u << b[54]) && b[53] <= 1;
}

std::optional<TimePoint> record_start(const std::uint8_t* p) {
    using namespace std::chrono;
    const year y{static_cast<int>(u16(p + 20))};
    const auto doy = u16(p + 22), fraction = u16(p + 28);
    if (int(y) < 1900 || int(y) > 2200 || doy < 1 || doy > 365u + y.is_leap() || p[24] > 23 || p[25] > 59 ||
        p[26] > 59 || fraction > 9999)
        return {};
    const auto base = sys_days{y / January / 1} + days{doy - 1} + hours{p[24]} + minutes{p[25]} + seconds{p[26]};
    const auto correction = (p[36] & 2) ? 0 : std::bit_cast<std::int32_t>(be_u32(p + 40));
    return base + microseconds{(static_cast<std::int64_t>(fraction) + correction) * 100};
}

std::span<const std::uint8_t> octets(std::span<const std::byte> b) {
    return {reinterpret_cast<const std::uint8_t*>(b.data()), b.size()};
}
} // namespace

bool detect_modified(std::span<const std::byte> bytes) { return layout(octets(bytes)); }

ReadResult read_modified(std::span<const std::byte> bytes, FormatId id, const TextParseOptions&) {
    ReadResult result;
    result.data.source_format = id;
    auto b = octets(bytes);
    std::map<std::array<std::string, 4>, std::size_t> active;
    std::size_t record = 0;
    try {
        while (!b.empty()) {
            ++record;
            if (!layout(b))
                throw std::runtime_error("invalid fixed header, length or Blockette 1000 extension");
            const auto* p = b.data();
            const auto length = std::size_t{1} << p[54];
            if (b.size() < length)
                throw std::runtime_error("truncated packet");
            Header header{p[52], p[53], u16(p + 44), u16(p + 30)};
            if (!header.sample_count)
                throw std::runtime_error("empty waveform packet");
            const auto samples = decode_steim2(b.first(length), header);
            const auto text = [&](unsigned pos, unsigned width) {
                const std::string_view value{reinterpret_cast<const char*>(p + pos), width};
                return std::string{trim(value.substr(0, value.find('\0')))};
            };
            const std::array key{text(18, 2), text(8, 5), text(13, 2), text(15, 3)};
            const auto rate = [](unsigned bits) {
                const auto n = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(bits));
                return n < 0 ? -1.0 / n : double(n);
            };
            const double hz = rate(u16(p + 32)) * rate(u16(p + 34));
            if (!positive(hz))
                throw std::runtime_error("invalid sampling rate");
            const double dt = 1 / hz;
            const auto start = record_start(p);
            const auto raw = be_u32(p + 60);
            const auto raw_text = std::to_string(raw);
            const auto order = text(56, 3);
            const auto type = text(0, 2);
            const auto sequence = be_u32(p + 2) & 0x1fffffff;
            auto [it, first] = active.try_emplace(key);
            bool continuous = false;
            std::size_t segment = 1;
            if (!first) {
                const auto& previous = result.data.traces[it->second];
                const auto& attrs = previous.attributes;
                segment = *parse_index(attrs.at("modified.segment")) + 1;
                if (start && previous.start_time && dt == previous.dt_seconds &&
                    raw_text == attrs.at("modified.dimension_sensitivity") && order == attrs.at("modified.order") &&
                    type == attrs.at("modified.packet_type")) {
                    const long double elapsed = (static_cast<long double>(start->time_since_epoch().count()) -
                                                 previous.start_time->time_since_epoch().count()) *
                                                1e-9L;
                    continuous = std::abs(elapsed - previous.samples.size() * dt) <= std::min(0.0001, dt / 16);
                }
            }
            if (!continuous) {
                if (!first || !start)
                    warn(result.diagnostics, "modified-mseed",
                         "record " + std::to_string(record) +
                             (start ? ": new segment (timing or header changed)"
                                    : ": unknown BTime, isolated segment"));
                it->second = result.data.traces.size();
                auto& trace = result.data.traces.emplace_back();
                MetadataEdit identity;
                identity.network = key[0];
                identity.station = key[1];
                identity.location = key[2];
                identity.channel = key[3];
                const auto ds = edit_metadata(trace, identity);
                if (has_errors(ds))
                    throw std::runtime_error("invalid NSLC header");
                trace.name = key[0] + "." + key[1] + "." + key[2] + "." + key[3];
                trace.dt_seconds = dt;
                trace.start_time = start;
                trace.quantity = Quantity::Counts;
                trace.unit = Unit::Counts;
                auto& attrs = trace.attributes;
                attrs["source.identity"] = "Modified miniSEED fixed header";
                attrs["source.dt"] = "Modified miniSEED rate factor and multiplier";
                attrs["source.start_time"] = start ? "BTime and activity correction flag" : "unknown BTime";
                attrs["modified.record_bytes"] = std::to_string(length);
                attrs["modified.first_packet_word"] = std::to_string(be_u32(p + 2));
                attrs["modified.first_time_correction"] =
                    std::to_string(std::bit_cast<std::int32_t>(be_u32(p + 40)));
                attrs["source.sample_type"] = "Steim-2 int32 counts";
                attrs["modified.segment"] = std::to_string(segment);
                attrs["modified.first_sequence"] = std::to_string(sequence);
                attrs["modified.packet_type"] = type;
                attrs["modified.dimension_sensitivity"] = raw_text;
                attrs["modified.sensitivity"] = std::to_string(raw & 0x3fffffff);
                constexpr std::array si{Unit::Unknown, Unit::M, Unit::Mps, Unit::Mps2};
                attrs["modified.si_unit"] = name(si[raw >> 30]);
                attrs["modified.dimension"] = raw >> 30 ? name(quantity_for(si[raw >> 30])) : "dimensionless";
                attrs["modified.order"] = order;
                attrs["modified.extension_byte"] = std::to_string(p[59]);
            }
            auto& trace = result.data.traces[it->second];
            trace.attributes["modified.last_sequence"] = std::to_string(sequence);
            trace.samples.insert(trace.samples.end(), samples.begin(), samples.end());
            b = b.subspan(length);
        }
    } catch (const std::runtime_error& e) {
        result.data.traces.clear();
        error(result.diagnostics, "modified-mseed", "record " + std::to_string(record) + ": " + e.what());
    }
    return result;
}
} // namespace gmtools::detail
