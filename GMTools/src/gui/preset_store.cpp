#include "preset_store.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace gmtools::gui {
namespace {

std::filesystem::path as_path(const QString& value) {
    const auto utf8 = value.toUtf8();
    return gmtools::path_from_utf8(std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
}

QString regularized(const QString& name) {
    auto text = name.trimmed();
    if (text.isEmpty())
        return QStringLiteral("preset");
    text.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_-]+")), QStringLiteral("-"));
    return text.left(80);
}

} // namespace

QString presets_directory() {
    const auto root = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir directory(root);
    directory.mkpath(QStringLiteral("presets"));
    return directory.filePath(QStringLiteral("presets"));
}

QStringList preset_files() {
    const auto directory = QDir(presets_directory());
    auto files = directory.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    QStringList paths;
    paths.reserve(files.size());
    for (const auto& file : files)
        paths.push_back(directory.filePath(file));
    return paths;
}

QString preset_name(const QString& file) {
    const auto loaded = gmtools::load_preset(as_path(file));
    if (!gmtools::has_errors(loaded.diagnostics) && !loaded.preset.name.empty())
        return QString::fromStdString(loaded.preset.name);
    return QFileInfo(file).completeBaseName();
}

QString preset_tooltip(const QString& file) {
    const auto loaded = gmtools::load_preset(as_path(file));
    if (gmtools::has_errors(loaded.diagnostics))
        return QString::fromStdString(gmtools::describe(loaded.diagnostics.front()));
    const auto& preset = loaded.preset;
    return QStringLiteral("%1 → %2")
        .arg(QString::fromStdString(std::string(gmtools::name(preset.read.format))),
             QString::fromStdString(std::string(gmtools::name(preset.write.format))));
}

QString preset_file_name(const QString& name) {
    return regularized(name).toLower() + QStringLiteral(".json");
}

QString suggested_preset_path(const QString& name) {
    return QDir(presets_directory()).filePath(preset_file_name(name));
}

Diagnostics write_preset_file(const QString& file, const ConversionPreset& preset, bool overwrite) {
    const auto path = as_path(file);
    return gmtools::save_preset(path, preset, overwrite, {});
}

bool remove_preset_file(const QString& file) {
    const auto managed = QFileInfo(file).absoluteFilePath();
    const auto root = QFileInfo(presets_directory()).absoluteFilePath() + QLatin1Char('/');
    if (!managed.startsWith(root))
        return false;
    return QFile::remove(managed);
}

bool copy_file(const QString& from, const QString& to, bool overwrite) {
    if (!overwrite && QFileInfo::exists(to))
        return false;
    return QFile::copy(from, to);
}

} // namespace gmtools::gui
