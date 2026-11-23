#include "detail.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <fstream>
#include <limits>
#include <set>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace gmtools {
namespace fs = std::filesystem;
namespace {

std::string numbered_channel(std::size_t index) {
    return "CH" + std::string(index < 10 ? "0" : "") + std::to_string(index);
}

std::string safe_component(std::string_view name, std::string_view fallback) {
    const bool utf8 = text_view(std::as_bytes(std::span{name})).has_value();
    std::string result;
    for (const char c : name) {
        const auto value = static_cast<unsigned char>(c);
        if (value >= 0x80 && utf8) {
            // Reserve room for a whole UTF-8 code point and keep each filename
            // below common per-component limits even after appending a suffix.
            if ((value & 0xc0) != 0x80 && result.size() >= 80)
                break;
            result += c;
        } else {
            if (result.size() >= 80)
                break;
            const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                               (c >= '0' && c <= '9') || c == '_' || c == '-';
            result += plain ? c : '_';
        }
    }
    if (result.find_first_not_of('_') == result.npos)
        return std::string{fallback};
    return result;
}

std::vector<std::string> channel_names(const FileData& data) {
    std::vector<std::string> names;
    std::set<std::string> used;
    for (std::size_t i = 0; i < data.traces.size(); ++i) {
        const auto sequence = numbered_channel(i + 1);
        const auto label = safe_component(data.traces[i].name, sequence);
        auto candidate = label;
        std::size_t suffix = 1;
        while (!used.insert(detail::lower(candidate)).second) {
            candidate = label + "_" + sequence;
            if (suffix > 1)
                candidate += "_" + std::to_string(suffix);
            ++suffix;
        }
        names.push_back(std::move(candidate));
    }
    return names;
}

bool writable_target(const fs::path& target, const fs::path& input, bool overwrite,
                     Diagnostics& diagnostics) {
    std::error_code ec;
    const auto status = fs::symlink_status(target, ec);
    if (status.type() == fs::file_type::not_found && (!ec || ec == std::errc::no_such_file_or_directory))
        return true;
    const auto label = path_to_utf8(target);
    if (ec) {
        detail::error(diagnostics, "output", label + ": " + ec.message());
        return false;
    }
    if (!fs::is_regular_file(status)) {
        detail::error(diagnostics, "output", label + ": target exists and is not a regular file");
        return false;
    }
    if (input == target || fs::equivalent(input, target, ec)) {
        detail::error(diagnostics, "output", label + ": target refers to the input file");
        return false;
    }
    if (!overwrite) {
        detail::error(diagnostics, "output", label + ": target already exists; choose a new path or enable overwrite");
        return false;
    }
    return true;
}

struct StagingDirectory {
    fs::path path;

    ~StagingDirectory() {
        if (!path.empty()) {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    }
};

bool make_stage(StagingDirectory& stage, const fs::path& directory, Diagnostics& diagnostics) {
    static std::atomic<unsigned long long> sequence{};
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 16; ++attempt) {
        const auto candidate = directory / (".gmtools-" + std::to_string(tick) + "-" + std::to_string(++sequence));
        std::error_code ec;
        if (fs::create_directory(candidate, ec)) {
            stage.path = candidate;
            return true;
        }
        if (ec && ec != std::errc::file_exists) {
            detail::error(diagnostics, "output", path_to_utf8(directory) + ": " + ec.message());
            return false;
        }
    }
    detail::error(diagnostics, "output", "could not reserve a temporary directory in " + path_to_utf8(directory));
    return false;
}

void publish(const fs::path& staged, const fs::path& target, bool overwrite, std::error_code& ec) {
    if (overwrite) {
        fs::rename(staged, target, ec);
        return;
    }
#ifdef _WIN32
    if (!MoveFileExW(staged.c_str(), target.c_str(), 0))
        ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
#ifdef SYS_renameat2
    constexpr unsigned rename_noreplace = 1;
    if (::syscall(SYS_renameat2, AT_FDCWD, staged.c_str(), AT_FDCWD, target.c_str(), rename_noreplace) == 0)
        return;
    if (errno != ENOSYS && errno != EINVAL && errno != EOPNOTSUPP) {
        ec = std::error_code(errno, std::generic_category());
        return;
    }
#endif
    fs::create_hard_link(staged, target, ec);
#endif
}

} // namespace

fs::path path_from_utf8(std::string_view value) {
    return fs::path(std::u8string{value.begin(), value.end()});
}

void detail::save_text_file(const fs::path& path, std::string_view text, bool overwrite,
                            const fs::path& protected_input, Diagnostics& diagnostics) {
    if (path.empty()) {
        error(diagnostics, "output", "a profile path is required");
        return;
    }
    if (!writable_target(path, protected_input, overwrite, diagnostics))
        return;
    StagingDirectory stage;
    const auto parent = path.parent_path();
    if (!make_stage(stage, parent.empty() ? fs::path{"."} : parent, diagnostics))
        return;
    const auto temporary = stage.path / "profile.json";
    std::ofstream stream(temporary, std::ios::binary);
    if (stream && text.size() <= static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        stream.close();
    } else {
        stream.setstate(std::ios::failbit);
    }
    if (!stream) {
        error(diagnostics, "output", path_to_utf8(path) + ": could not write the temporary profile");
        return;
    }
    if (!writable_target(path, protected_input, overwrite, diagnostics))
        return;
    std::error_code ec;
    publish(temporary, path, overwrite, ec);
    if (ec)
        error(diagnostics, "output", path_to_utf8(path) + ": " + ec.message());
}

std::string path_to_utf8(const fs::path& value) {
    const auto utf8 = value.generic_u8string();
    return {utf8.begin(), utf8.end()};
}

InputFile load_file(const fs::path& path) {
    InputFile result;
    const auto label = path_to_utf8(path);
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        detail::error(result.diagnostics, "input", label + ": expected a readable regular file");
        return result;
    }
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto size = input.tellg();
    if (!input || size < 0 || size > std::numeric_limits<std::streamsize>::max() ||
        static_cast<std::uintmax_t>(size) > result.bytes.max_size()) {
        detail::error(result.diagnostics, "input", label + ": cannot open file or file is too large");
        return result;
    }
    result.bytes.resize(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!input || (size != 0 && !input.read(reinterpret_cast<char*>(result.bytes.data()),
                                           static_cast<std::streamsize>(size))) ||
        input.peek() != std::char_traits<char>::eof() || input.bad()) {
        result.bytes.clear();
        detail::error(result.diagnostics, "input", label + ": reading failed or file size changed");
    }
    return result;
}

ExportResult export_file(const FileData& data, const fs::path& input, const ExportOptions& options) {
    ExportResult result;
    const auto known = formats();
    const auto target_format = std::ranges::find_if(known, [&](const auto& item) { return item.id == options.writer.format; });
    if (target_format == known.end() || !target_format->writable) {
        detail::error(result.diagnostics, "output", "target format is not writable in this build");
        return result;
    }
    if (options.output.empty() || data.traces.empty()) {
        detail::error(result.diagnostics, "output", "an output path and at least one trace are required");
        return result;
    }

    auto channels = options.channels;
    if (channels.empty()) {
        for (std::size_t i = 1; i <= data.traces.size(); ++i)
            channels.push_back(i);
    }
    std::set<std::size_t> distinct;
    for (const auto channel : channels) {
        if (channel == 0 || channel > data.traces.size() || !distinct.insert(channel).second) {
            detail::error(result.diagnostics, "output", "invalid or repeated channel index " + std::to_string(channel));
            return result;
        }
    }

    const auto output = options.output.lexically_normal();
    std::error_code ec;
    const bool output_is_directory = fs::is_directory(output, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
        detail::error(result.diagnostics, "output", path_to_utf8(output) + ": " + ec.message());
        return result;
    }
    const auto suffix = detail::lower(path_to_utf8(output.extension()));
    const bool named_file = !output_is_directory &&
        (suffix == target_format->extension || format_from_name(suffix) == options.writer.format);
    if (named_file && channels.size() != 1) {
        detail::error(result.diagnostics, "output", "multiple traces require an output directory");
        return result;
    }
    auto directory = named_file ? output.parent_path() : output;
    if (directory.empty())
        directory = fs::path{"."};

    std::vector<fs::path> paths;
    const auto labels = channel_names(data);
    const auto stem = safe_component(path_to_utf8(input.stem()), "record");
    for (const auto channel : channels) {
        paths.push_back(named_file ? output : directory / path_from_utf8(
            stem + "_" + labels[channel - 1] + std::string{target_format->extension}));
        if (!writable_target(paths.back(), input, options.overwrite, result.diagnostics))
            return result;
    }

    ec.clear();
    fs::create_directories(directory, ec);
    if (ec) {
        detail::error(result.diagnostics, "output", path_to_utf8(directory) + ": " + ec.message());
        return result;
    }
    StagingDirectory stage;
    if (!make_stage(stage, directory, result.diagnostics))
        return result;

    if (!data.attributes.empty())
        detail::warn(result.diagnostics, "output", "source file-level format attributes are not exported");

    std::vector<fs::path> staged;
    for (std::size_t i = 0; i < channels.size(); ++i) {
        const auto channel = channels[i];
        auto encoded = write(data.traces[channel - 1], options.writer);
        for (auto& diagnostic : encoded.diagnostics) {
            diagnostic.channel = channel;
            result.diagnostics.push_back(std::move(diagnostic));
        }
        if (has_errors(result.diagnostics))
            return result;

        staged.push_back(stage.path / std::to_string(i + 1));
        std::ofstream stream(staged.back(), std::ios::binary | std::ios::trunc);
        if (stream && encoded.bytes.size() <= static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
            stream.write(reinterpret_cast<const char*>(encoded.bytes.data()),
                         static_cast<std::streamsize>(encoded.bytes.size()));
            stream.close();
        } else {
            stream.setstate(std::ios::failbit);
        }
        if (!stream) {
            detail::error(result.diagnostics, "output", path_to_utf8(paths[i]) + ": could not stage the encoded file");
            return result;
        }
    }

    result.written.reserve(paths.size());
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (!writable_target(paths[i], input, options.overwrite, result.diagnostics))
            break;
        ec.clear();
        publish(staged[i], paths[i], options.overwrite, ec);
        if (ec) {
            detail::error(result.diagnostics, "output", path_to_utf8(paths[i]) + ": " + ec.message());
            break;
        }
        result.written.push_back(std::move(paths[i]));
    }
    if (result.written.size() != paths.size() && !result.written.empty()) {
        detail::error(result.diagnostics, "output", "publication stopped after writing " +
                      std::to_string(result.written.size()) + " of " + std::to_string(paths.size()) + " files");
    }
    return result;
}

} // namespace gmtools
