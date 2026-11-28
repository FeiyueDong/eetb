#include "localization.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

#include <cstdio>

namespace gmtools::gui {
namespace {

QString catalog_path(QStringView name) {
    const auto local = QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/translations"))
                           .filePath(name.toString() + QStringLiteral(".qm"));
    if (QFileInfo::exists(local))
        return local;
    const auto installed = QDir(QLibraryInfo::path(QLibraryInfo::TranslationsPath))
                               .filePath(name.toString() + QStringLiteral(".qm"));
    return QFileInfo::exists(installed) ? installed : QString{};
}

} // namespace

Language effective_language(Language requested) {
    if (requested != Language::System)
        return requested;
    const auto locale = QLocale::system();
    if (locale.language() == QLocale::Chinese)
        return Language::Chinese;
    return Language::English;
}

bool install_translations(QApplication& app, Language language) {
    const auto effective = effective_language(language);
    if (effective != Language::Chinese)
        return true;
    bool catalog_loaded = false;
    // Application-owned translators live for the whole process.
    const auto install = [&app](const QString& file, bool optional) {
        if (file.isEmpty())
            return false;
        auto* translator = new QTranslator(&app);
        if (!translator->load(file)) {
            delete translator;
            return false;
        }
        app.installTranslator(translator);
        return true;
    };
    install(catalog_path(QStringLiteral("qtbase_zh_CN")), true);
    catalog_loaded = install(catalog_path(QStringLiteral("gmtools_zh_CN")), false);
    if (!catalog_loaded)
        std::fprintf(stderr, "gmtools: translations/gmtools_zh_CN.qm is missing; using English\n");
    return catalog_loaded;
}

} // namespace gmtools::gui
