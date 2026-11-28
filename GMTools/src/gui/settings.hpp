#pragma once

#include <QString>

namespace gmtools::gui {

enum class Language { System, Chinese, English };
enum class ThemeMode { System, Light, Dark };

// Lightweight application preferences. They live in QSettings and never carry
// waveform data, per-file identity edits or export permissions.
struct AppSettings {
    Language language{Language::System};
    ThemeMode theme{ThemeMode::System};
    bool remember_directories{true};
    bool remember_conversion_settings{true};

    static AppSettings load();
    void save() const;
};

QString language_code(Language language);
Language language_from_code(const QString& code, Language fallback = Language::System);
QString theme_code(ThemeMode theme);
ThemeMode theme_from_code(const QString& code, ThemeMode fallback = ThemeMode::System);

} // namespace gmtools::gui
