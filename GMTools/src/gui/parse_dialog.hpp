#pragma once

#include "gmtools/gmtools.hpp"
#include "plot.hpp"

#include <memory>

class QWidget;

namespace gmtools::gui {

struct ParsedInput {
    ReadOptions options;
    std::shared_ptr<ReadResult> result;
    std::vector<PlotData> plots;
};

std::optional<ParsedInput> interpret_input(std::shared_ptr<const InputFile> input,
    const std::filesystem::path& path, const ReadOptions& original, QWidget* parent);

} // namespace gmtools::gui
