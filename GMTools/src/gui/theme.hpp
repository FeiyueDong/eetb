#pragma once

#include "settings.hpp"

class QApplication;

namespace gmtools::gui {

void apply_theme(QApplication& app);
void apply_theme(QApplication& app, ThemeMode mode);

} // namespace gmtools::gui
