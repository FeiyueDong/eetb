#include "settings.hpp"

#include <QSettings>

namespace gmtools::gui {

QString language_code(Language language) {
    switch (language) {
    case Language::Chinese: return QStringLiteral("zh_CN");
    case Language::English: return QStringLiteral("en_US");
    case Language::System: break;
    }
    return QStringLiteral("system");
}

Language language_from_code(const QString& code, Language fallback) {
    if (code == QStringLiteral("zh_CN"))
        return Language::Chinese;
    if (code == QStringLiteral("en_US"))
        return Language::English;
    if (code == QStringLiteral("system"))
        return Language::System;
    return fallback;
}

QString theme_code(ThemeMode theme) {
    switch (theme) {
    case ThemeMode::Light: return QStringLiteral("light");
    case ThemeMode::Dark: return QStringLiteral("dark");
    case ThemeMode::System: break;
    }
    return QStringLiteral("system");
}

ThemeMode theme_from_code(const QString& code, ThemeMode fallback) {
    if (code == QStringLiteral("light"))
        return ThemeMode::Light;
    if (code == QStringLiteral("dark"))
        return ThemeMode::Dark;
    if (code == QStringLiteral("system"))
        return ThemeMode::System;
    return fallback;
}

AppSettings AppSettings::load() {
    QSettings settings;
    settings.beginGroup(QStringLiteral("ui"));
    AppSettings result;
    result.language = language_from_code(settings.value(QStringLiteral("language")).toString());
    result.theme = theme_from_code(settings.value(QStringLiteral("theme")).toString());
    settings.endGroup();
    settings.beginGroup(QStringLiteral("preferences"));
    result.remember_directories =
        settings.value(QStringLiteral("rememberDirectories"), true).toBool();
    result.remember_conversion_settings =
        settings.value(QStringLiteral("rememberConversionSettings"), true).toBool();
    settings.endGroup();
    return result;
}

void AppSettings::save() const {
    QSettings settings;
    settings.beginGroup(QStringLiteral("ui"));
    settings.setValue(QStringLiteral("language"), language_code(language));
    settings.setValue(QStringLiteral("theme"), theme_code(theme));
    settings.endGroup();
    settings.beginGroup(QStringLiteral("preferences"));
    settings.setValue(QStringLiteral("rememberDirectories"), remember_directories);
    settings.setValue(QStringLiteral("rememberConversionSettings"), remember_conversion_settings);
    settings.endGroup();
}

} // namespace gmtools::gui
