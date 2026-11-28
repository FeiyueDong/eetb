#include "detail.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <utility>

namespace gmtools {
namespace fs = std::filesystem;
namespace {

void notify(const BatchProgress& progress, const BatchResult& result) {
    if (progress)
        progress(result);
}

void summarize(BatchResult& result) {
    auto& summary = result.summary;
    summary.succeeded = summary.partial = summary.failed = summary.cancelled = 0;
    summary.files_written = 0;
    for (const auto& file : result.files) {
        switch (file.state) {
        case BatchFileState::Succeeded: ++summary.succeeded; break;
        case BatchFileState::Partial: ++summary.partial; break;
        case BatchFileState::Failed: ++summary.failed; break;
        case BatchFileState::Cancelled: ++summary.cancelled; break;
        case BatchFileState::Pending:
        case BatchFileState::Running: break;
        }
        summary.files_written += file.written.size();
    }
}

bool validate_options(const BatchOptions& options, Diagnostics& diagnostics) {
    if (options.inputs.empty())
        detail::error(diagnostics, "batch", "a batch requires at least one input file");
    if (options.output_directory.empty()) {
        detail::error(diagnostics, "batch", "a batch requires an output directory");
    } else {
        std::error_code ec;
        const bool exists = fs::exists(options.output_directory, ec);
        if (ec) {
            detail::error(diagnostics, "batch", path_to_utf8(options.output_directory) + ": " + ec.message());
        } else if (exists) {
            if (!fs::is_directory(options.output_directory, ec) || ec)
                detail::error(diagnostics, "batch",
                              path_to_utf8(options.output_directory) + ": the batch output must be a directory");
        } else {
            const auto known = formats();
            const auto entry = std::ranges::find_if(known, [&](const auto& item) {
                return item.id == options.preset.write.format;
            });
            const auto suffix = detail::lower(path_to_utf8(options.output_directory.extension()));
            if (entry != known.end() &&
                (suffix == entry->extension || format_from_name(suffix) == options.preset.write.format))
                detail::error(diagnostics, "batch", path_to_utf8(options.output_directory) +
                                  ": the batch output must be a directory, not a file path");
        }
    }

    const auto known = formats();
    const auto entry = std::ranges::find_if(known, [&](const auto& item) {
        return item.id == options.preset.write.format;
    });
    if (entry == known.end() || !entry->writable)
        detail::error(diagnostics, "batch", "target format is not writable in this build");
    if (options.preset.read.format != FormatId::Auto) {
        const auto reader = std::ranges::find_if(known, [&](const auto& item) {
            return item.id == options.preset.read.format;
        });
        if (reader != known.end() && !reader->readable)
            detail::error(diagnostics, "batch",
                          "input format " + std::string{reader->name} + " is not readable in this build");
    }
    try {
        (void)encode_preset(options.preset);
    } catch (const std::exception& problem) {
        detail::error(diagnostics, "batch", problem.what());
    }
    return !has_errors(diagnostics);
}

} // namespace

BatchResult convert_batch(const BatchOptions& options, const BatchProgress& progress, const BatchStop& stop) {
    BatchResult result;
    result.files.resize(options.inputs.size());
    for (std::size_t i = 0; i < options.inputs.size(); ++i)
        result.files[i].input = options.inputs[i];
    result.summary.total = options.inputs.size();

    if (!validate_options(options, result.diagnostics)) {
        summarize(result);
        return result;
    }

    std::vector<fs::path> originals = options.inputs;
    std::vector<fs::path> reserved;
    std::size_t next = 0;
    for (; next < result.files.size(); ++next) {
        if (stop && stop()) {
            result.summary.stopped = true;
            break;
        }
        auto& file = result.files[next];
        file.state = BatchFileState::Running;
        file.stage = "read";
        notify(progress, result);

        auto input = load_file(file.input);
        if (has_errors(input.diagnostics)) {
            file.diagnostics = std::move(input.diagnostics);
            file.state = BatchFileState::Failed;
            summarize(result);
            notify(progress, result);
            continue;
        }

        file.stage = "parse";
        notify(progress, result);
        auto read_options = options.preset.read;
        read_options.extension_hint = path_to_utf8(file.input.extension());
        auto parsed = read(input.bytes, read_options);
        file.diagnostics = std::move(parsed.diagnostics);
        file.channels_read = parsed.data.traces.size();
        if (has_errors(file.diagnostics)) {
            file.state = BatchFileState::Failed;
            summarize(result);
            notify(progress, result);
            continue;
        }

        file.stage = "select";
        const auto source = path_to_utf8(file.input);
        const auto channels =
            detail::select_channels(parsed.data, options.preset.selection, source, file.diagnostics);
        if (has_errors(file.diagnostics)) {
            file.state = BatchFileState::Failed;
            summarize(result);
            notify(progress, result);
            continue;
        }
        file.channels_selected = channels.size();

        file.stage = "plan";
        notify(progress, result);
        ExportOptions export_options;
        export_options.writer = options.preset.write;
        export_options.output = options.output_directory;
        export_options.channels = channels;
        export_options.overwrite = options.overwrite;
        auto plan = detail::plan_outputs(parsed.data, file.input, export_options, file.diagnostics);
        if (!plan) {
            file.state = BatchFileState::Failed;
            summarize(result);
            notify(progress, result);
            continue;
        }

        file.stage = "check";
        notify(progress, result);
        if (!detail::validate_targets(*plan, originals, options.overwrite, reserved, file.diagnostics,
                                      "target refers to a source file of this batch")) {
            file.state = BatchFileState::Failed;
            summarize(result);
            notify(progress, result);
            continue;
        }
        file.written = detail::publish_outputs(parsed.data, *plan, options.preset.write, options.overwrite,
                                               file.diagnostics, &file.stage, originals, reserved,
                                               "target refers to a source file of this batch");
        // Only paths that actually appeared stay claimed: a plan that failed before publishing
        // must not block a later file from using the same names.
        reserved.insert(reserved.end(), file.written.begin(), file.written.end());
        file.state = has_errors(file.diagnostics)
                         ? (file.written.empty() ? BatchFileState::Failed : BatchFileState::Partial)
                         : BatchFileState::Succeeded;
        summarize(result);
        notify(progress, result);
    }

    if (next < result.files.size()) {
        for (; next < result.files.size(); ++next)
            result.files[next].state = BatchFileState::Cancelled;
    }
    summarize(result);
    notify(progress, result);
    return result;
}

} // namespace gmtools
