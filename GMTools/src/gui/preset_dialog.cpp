#include "preset_dialog.hpp"

#include "preset_store.hpp"
#include "ui_helpers.hpp"

#include <QDir>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

namespace gmtools::gui {
namespace {

std::filesystem::path as_path(const QString& value) {
    const auto utf8 = value.toUtf8();
    return gmtools::path_from_utf8(std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
}

QString diagnostic_summary(const Diagnostics& diagnostics) {
    QStringList lines;
    for (const auto& diagnostic : diagnostics)
        lines.push_back(QString::fromStdString(describe(diagnostic)));
    return lines.join(QLatin1Char('\n'));
}

} // namespace

PresetDialog::PresetDialog(std::function<ConversionPreset()> current, QWidget* parent)
    : QDialog(parent), current_(std::move(current)) {
    setWindowTitle(tr("Conversion presets"));
    resize(520, 420);

    list_ = new QListWidget(this);
    list_->setAlternatingRowColors(true);
    connect(list_, &QListWidget::itemSelectionChanged, this, [this] {
        const auto enabled = !selected_file().isEmpty();
        load_->setEnabled(enabled);
        rename_->setEnabled(enabled);
        remove_->setEnabled(enabled);
        export_->setEnabled(enabled);
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] { load_selected(); });

    auto* save = new QPushButton(tr("Save current settings as…"), this);
    auto* import = new QPushButton(tr("Import…"), this);
    load_ = new QPushButton(tr("Load"), this);
    rename_ = new QPushButton(tr("Rename…"), this);
    remove_ = new QPushButton(tr("Delete"), this);
    export_ = new QPushButton(tr("Export…"), this);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, &PresetDialog::save_current);
    connect(import, &QPushButton::clicked, this, &PresetDialog::import_preset);
    connect(load_, &QPushButton::clicked, this, &PresetDialog::load_selected);
    connect(rename_, &QPushButton::clicked, this, &PresetDialog::rename_selected);
    connect(remove_, &QPushButton::clicked, this, &PresetDialog::delete_selected);
    connect(export_, &QPushButton::clicked, this, &PresetDialog::export_selected);

    auto* actions = new QVBoxLayout;
    actions->addWidget(save);
    actions->addWidget(import);
    actions->addSpacing(12);
    actions->addWidget(load_);
    actions->addWidget(rename_);
    actions->addWidget(remove_);
    actions->addWidget(export_);
    actions->addStretch();

    auto* body = new QHBoxLayout;
    body->addWidget(list_, 1);
    body->addLayout(actions);

    auto* hint = new QLabel(tr("Presets store reading, calibration and writing parameters only. "
                               "Paths, overwrite permission and channel identities are not included."),
                            this);
    hint->setWordWrap(true);
    hint->setProperty("role", "muted");

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(body, 1);
    layout->addWidget(hint);
    layout->addWidget(close);
    reload();
}

void PresetDialog::reload() {
    list_->clear();
    for (const auto& file : preset_files()) {
        auto* item = new QListWidgetItem(preset_name(file), list_);
        item->setData(Qt::UserRole, file);
        item->setToolTip(preset_tooltip(file));
    }
    if (list_->count() > 0)
        list_->setCurrentRow(0);
    else {
        load_->setEnabled(false);
        rename_->setEnabled(false);
        remove_->setEnabled(false);
        export_->setEnabled(false);
    }
}

QString PresetDialog::selected_file() const {
    const auto* item = list_->currentItem();
    if (!item || !item->isSelected())
        return {};
    return item->data(Qt::UserRole).toString();
}

std::optional<ConversionPreset> PresetDialog::take_selected() {
    auto result = std::move(selected_);
    selected_.reset();
    return result;
}

void PresetDialog::load_selected() {
    const auto file = selected_file();
    if (file.isEmpty())
        return;
    const auto loaded = gmtools::load_preset(as_path(file));
    if (gmtools::has_errors(loaded.diagnostics)) {
        QMessageBox::warning(this, tr("Preset cannot be loaded"), diagnostic_summary(loaded.diagnostics));
        return;
    }
    selected_ = loaded.preset;
    accept();
}

void PresetDialog::save_current() {
    if (!current_)
        return;
    ConversionPreset preset;
    try {
        preset = current_();
    } catch (const std::exception& problem) {
        QMessageBox::warning(this, tr("Settings are incomplete"), QString::fromUtf8(problem.what()));
        return;
    }
    bool accepted = false;
    auto name = QInputDialog::getText(this, tr("Save preset"), tr("Preset name:"), QLineEdit::Normal,
                                      QString::fromStdString(preset.name), &accepted);
    if (!accepted || name.trimmed().isEmpty())
        return;
    preset.name = name.trimmed().toStdString();
    const auto file = suggested_preset_path(name);
    bool overwrite = false;
    if (QFileInfo::exists(file)) {
        if (QMessageBox::question(this, tr("Replace preset"),
                                  tr("A preset with this name already exists. Replace it?")) != QMessageBox::Yes)
            return;
        overwrite = true;
    }
    const auto diagnostics = write_preset_file(file, preset, overwrite);
    if (gmtools::has_errors(diagnostics)) {
        QMessageBox::warning(this, tr("Preset cannot be saved"), diagnostic_summary(diagnostics));
        return;
    }
    reload();
    const auto matches = list_->findItems(QString::fromStdString(preset.name), Qt::MatchExactly);
    if (!matches.isEmpty())
        list_->setCurrentItem(matches.front());
}

void PresetDialog::rename_selected() {
    const auto file = selected_file();
    if (file.isEmpty())
        return;
    auto loaded = gmtools::load_preset(as_path(file));
    if (gmtools::has_errors(loaded.diagnostics))
        return;
    bool accepted = false;
    const auto name = QInputDialog::getText(this, tr("Rename preset"), tr("New name:"), QLineEdit::Normal,
                                            preset_name(file), &accepted);
    if (!accepted || name.trimmed().isEmpty())
        return;
    const auto target = suggested_preset_path(name);
    if (target != file && QFileInfo::exists(target)) {
        QMessageBox::warning(this, tr("Rename preset"), tr("Another preset already uses this name."));
        return;
    }
    loaded.preset.name = name.trimmed().toStdString();
    const auto diagnostics = write_preset_file(target, loaded.preset, target == file);
    if (gmtools::has_errors(diagnostics)) {
        QMessageBox::warning(this, tr("Preset cannot be saved"), diagnostic_summary(diagnostics));
        return;
    }
    if (target != file)
        remove_preset_file(file);
    reload();
}

void PresetDialog::delete_selected() {
    const auto file = selected_file();
    if (file.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Delete preset"),
                              tr("Delete “%1”?").arg(preset_name(file))) != QMessageBox::Yes)
        return;
    if (!remove_preset_file(file))
        QMessageBox::warning(this, tr("Delete preset"), tr("The preset file could not be removed."));
    reload();
}

void PresetDialog::import_preset() {
    const auto file = QFileDialog::getOpenFileName(this, tr("Import preset"), {},
                                                   tr("Conversion presets (*.json);;All files (*)"));
    if (file.isEmpty())
        return;
    const auto loaded = gmtools::load_preset(as_path(file));
    if (gmtools::has_errors(loaded.diagnostics)) {
        QMessageBox::warning(this, tr("Preset cannot be loaded"), diagnostic_summary(loaded.diagnostics));
        return;
    }
    auto preset = loaded.preset;
    auto name = QString::fromStdString(preset.name);
    if (name.isEmpty())
        name = QFileInfo(file).completeBaseName();
    const auto target = QDir(presets_directory()).filePath(preset_file_name(name));
    bool overwrite = false;
    if (QFileInfo::exists(target)) {
        if (QMessageBox::question(this, tr("Replace preset"),
                                  tr("A preset with this name already exists. Replace it?")) != QMessageBox::Yes)
            return;
        overwrite = true;
    }
    preset.name = name.toStdString();
    const auto diagnostics = write_preset_file(target, preset, overwrite);
    if (gmtools::has_errors(diagnostics))
        QMessageBox::warning(this, tr("Preset cannot be saved"), diagnostic_summary(diagnostics));
    reload();
}

void PresetDialog::export_selected() {
    const auto file = selected_file();
    if (file.isEmpty())
        return;
    const auto loaded = gmtools::load_preset(as_path(file));
    if (gmtools::has_errors(loaded.diagnostics))
        return;
    auto target = QFileDialog::getSaveFileName(this, tr("Export preset"), preset_name(file) + QStringLiteral(".json"),
                                               tr("Conversion presets (*.json)"));
    if (target.isEmpty())
        return;
    if (!target.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
        target += QStringLiteral(".json");
    bool overwrite = false;
    if (QFileInfo::exists(target)) {
        if (QMessageBox::question(this, tr("Replace file"),
                                  tr("The file already exists. Replace it?")) != QMessageBox::Yes)
            return;
        overwrite = true;
    }
    const auto diagnostics = write_preset_file(target, loaded.preset, overwrite);
    if (gmtools::has_errors(diagnostics))
        QMessageBox::warning(this, tr("Preset cannot be saved"), diagnostic_summary(diagnostics));
}

} // namespace gmtools::gui
