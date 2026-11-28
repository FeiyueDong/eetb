#pragma once

#include "settings.hpp"

class QApplication;

namespace gmtools::gui {

// Resolve the effective language for this start. English is the source
// language, so only Chinese needs a catalog.
Language effective_language(Language requested);

// Install the Qt and GMTools catalogs for `language`. Call before any window is
// created; a missing GMTools catalog falls back to English with a warning.
bool install_translations(QApplication& app, Language language);

} // namespace gmtools::gui
