#include "detail.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>

namespace gmtools {
namespace detail {

std::string metadata_value(const Metadata& data, const std::string& key) {
    const auto it = data.find(key);
    return it == data.end() ? std::string{} : it->second;
}

bool valid_sid(std::string_view sid, bool v2) {
    if (!sid.starts_with("FDSN:") || sid.size() >= 64)
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
    if (codes[0].empty() || codes[1].empty() || codes[3].size() != 1 || codes[4].empty() || codes[2] == "--" ||
        codes[0].size() > 8 || codes[1].size() > 8 || codes[2].size() > 8)
        return false;
    if (v2 && (codes[0].size() > 2 || codes[1].size() > 5 || codes[2].size() > 2 || codes[4].size() != 1 ||
               codes[5].size() != 1))
        return false;
    for (std::size_t i = 0; i < codes.size(); ++i) {
        if (!std::ranges::all_of(codes[i], [allow_dash = i == 1 || i == 2](char c) {
                return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (allow_dash && c == '-');
            }))
            return false;
    }
    return true;
}

void import_sid(Metadata& attrs, std::string_view sid) {
    for (const auto* key : {"station.network", "station.code", "station.location", "channel.code"})
        attrs.erase(key);
    attrs["channel.source_id"] = sid;
    if (!valid_sid(sid, false))
        return;
    sid.remove_prefix(5);
    for (const auto* key : {"station.network", "station.code", "station.location"}) {
        const auto split = sid.find('_');
        attrs[key] = sid.substr(0, split);
        sid.remove_prefix(split + 1);
    }
    if ((sid.size() == 4 || sid.size() == 5) && sid[1] == '_' && sid[3] == '_') {
        std::string code{sid};
        std::erase(code, '_');
        attrs["channel.code"] = code;
    }
}

} // namespace detail

std::optional<TimePoint> parse_utc(std::string_view s) {
    using namespace std::chrono;
    if (s.size() < 20 || s.size() > 30 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' ||
        s[16] != ':' || s.back() != 'Z')
        return {};
    const auto digits = [&](std::size_t pos, std::size_t n) {
        const auto part = s.substr(pos, n);
        if (part.empty() || part.find_first_not_of("0123456789") != part.npos)
            return -1;
        int value{};
        std::from_chars(part.data(), part.data() + n, value);
        return value;
    };
    const int y = digits(0, 4), m = digits(5, 2), d = digits(8, 2);
    const int h = digits(11, 2), min = digits(14, 2), sec = digits(17, 2);
    const year_month_day date{year{y}, month{static_cast<unsigned>(m)}, day{static_cast<unsigned>(d)}};
    if (!date.ok() || h < 0 || h > 23 || min < 0 || min > 59 || sec < 0 || sec > 59)
        return {};
    int fraction = 0;
    if (s.size() > 20) {
        if (s[19] != '.' || s.size() == 21)
            return {};
        fraction = digits(20, s.size() - 21);
        if (fraction < 0)
            return {};
        for (auto i = s.size(); i < 30; ++i)
            fraction *= 10;
    }
    const auto base = (sys_days{date} + hours{h} + minutes{min} + seconds{sec}).time_since_epoch().count();
    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    constexpr auto min_ns = std::numeric_limits<std::int64_t>::min();
    if (base >= 0) {
        if (base > (max - fraction) / 1000000000)
            return {};
        return TimePoint{nanoseconds{base * 1000000000 + fraction}};
    }
    if (base + 1 < (min_ns + (1000000000 - fraction)) / 1000000000)
        return {};
    return TimePoint{nanoseconds{(base + 1) * 1000000000 - (1000000000 - fraction)}};
}

bool MetadataEdit::has_identity() const { return network || station || location || channel || source_id || clear_location; }

Diagnostics edit_metadata(Trace& trace, const MetadataEdit& edit) {
    Diagnostics ds;
    if (!edit.start_utc && !edit.relative_start && !edit.has_identity())
        return ds;
    auto start = trace.start_time;
    auto relative = trace.relative_start_seconds;
    auto attrs = trace.attributes;
    if (edit.start_utc) {
        start = parse_utc(*edit.start_utc);
        if (!start && !edit.start_utc->empty())
            detail::error(ds, "metadata", "expected YYYY-MM-DDTHH:MM:SS[.fffffffff]Z within nanosecond UTC range");
        if (start != trace.start_time && trace.start_time)
            attrs.try_emplace("source.original_start_utc", timestamp(*trace.start_time));
        attrs["source.start_time"] = "user input";
    }
    if (edit.relative_start) {
        relative = parse_number(*edit.relative_start);
        if (!relative && !edit.relative_start->empty())
            detail::error(ds, "metadata", "invalid relative start");
        if (relative != trace.relative_start_seconds && trace.relative_start_seconds)
            attrs.try_emplace("source.original_relative_start", detail::number(*trace.relative_start_seconds));
        attrs["source.relative_start"] = "user input";
    }
    if (edit.has_identity()) {
        if (edit.source_id && (edit.network || edit.station || edit.location || edit.clear_location || edit.channel))
            detail::error(ds, "metadata", "specify a SID or NSLC edits separately");
        else if (edit.clear_location && edit.location)
            detail::error(ds, "metadata", "cannot both set and clear a location code");
        else if (edit.source_id) {
            if (!edit.source_id->empty() && !detail::valid_sid(*edit.source_id, false))
                detail::error(ds, "metadata", "invalid FDSN source identifier");
            detail::import_sid(attrs, *edit.source_id);
            if (edit.source_id->empty())
                attrs.erase("channel.source_id");
        } else {
            for (const auto& [key, field] : {std::pair{"station.network", &edit.network},
                                             {"station.code", &edit.station},
                                             {"station.location", &edit.location},
                                             {"channel.code", &edit.channel}}) {
                if (!*field)
                    continue;
                const auto& value = **field;
                if (!std::ranges::all_of(value, [](char c) { return c > 32 && c < 127 && c != '_'; }))
                    detail::error(ds, "metadata", "invalid code for " + std::string{key});
                if (value.empty() && key != std::string_view{"station.location"})
                    attrs.erase(key);
                else
                    attrs[key] = value;
            }
            if (edit.clear_location)
                attrs.erase("station.location");
            std::string channel_part;
            const auto original = detail::metadata_value(attrs, "channel.source_id");
            if (!edit.channel && detail::valid_sid(original, false)) {
                auto split = original.find('_');
                split = original.find('_', split + 1);
                split = original.find('_', split + 1);
                channel_part = original.substr(split + 1);
            }
            attrs.erase("channel.source_id");
            const auto code = detail::metadata_value(attrs, "channel.code");
            if (channel_part.empty() && (code.size() == 3 || code.size() == 2))
                channel_part = code.substr(0, 1) + "_" + code.substr(1, 1) + "_" + code.substr(2);
            if (!channel_part.empty() && attrs.contains("station.network") && attrs.contains("station.code") &&
                attrs.contains("station.location")) {
                const auto sid = "FDSN:" + attrs.at("station.network") + "_" + attrs.at("station.code") + "_" +
                                 attrs.at("station.location") + "_" + channel_part;
                if (detail::valid_sid(sid, false))
                    attrs["channel.source_id"] = sid;
            }
        }
        for (const auto* key :
             {"station.network", "station.code", "station.location", "channel.code", "channel.source_id"})
            if (trace.attributes.contains(key) && detail::metadata_value(attrs, key) != trace.attributes.at(key))
                attrs.try_emplace(std::string{"original."} + key, trace.attributes.at(key));
        attrs["source.identity"] = "user input";
    }
    if (!has_errors(ds)) {
        trace.attributes = std::move(attrs);
        trace.start_time = start;
        trace.relative_start_seconds = relative;
    }
    return ds;
}
} // namespace gmtools
