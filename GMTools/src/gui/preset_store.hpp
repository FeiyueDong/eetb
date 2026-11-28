#pragma once

#include "gmtools/gmtools.hpp"

#include <QString>
#include <QStringList>

namespace gmtools::gui {

// Presets managed by this installation live under QStandardPaths::AppConfigLocation.
QString presets_directory();
QStringList preset_files();
QString preset_name(const QString& file);
QString preset_tooltip(const QString& file);

// Save `preset` to an explicit file. Existing files are only replaced when
// `overwrite` is true; the core writer publishes atomically.
Diagnostics write_preset_file(const QString& file, const ConversionPreset& preset, bool overwrite);
bool remove_preset_file(const QString& file);
bool copy_file(const QString& from, const QString& to, bool overwrite);
QString suggested_preset_path(const QString& name);
QString preset_file_name(const QString& name);

} // namespace gmtools::gui
