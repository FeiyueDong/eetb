#include "settings_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

namespace gmtools::gui {

SettingsDialog::SettingsDialog(const AppSettings& current, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    setMinimumWidth(420);

    auto* appearance = new QGroupBox(tr("Appearance"), this);
    auto* form = new QFormLayout(appearance);
    language_ = new QComboBox(appearance);
    language_->addItem(tr("Follow system"), static_cast<int>(Language::System));
    language_->addItem(QStringLiteral("简体中文"), static_cast<int>(Language::Chinese));
    language_->addItem(QStringLiteral("English"), static_cast<int>(Language::English));
    language_->setCurrentIndex(language_->findData(static_cast<int>(current.language)));
    if (language_->currentIndex() < 0)
        language_->setCurrentIndex(0);
    form->addRow(tr("Language"), language_);

    theme_ = new QComboBox(appearance);
    theme_->addItem(tr("Follow system"), static_cast<int>(ThemeMode::System));
    theme_->addItem(tr("Light"), static_cast<int>(ThemeMode::Light));
    theme_->addItem(tr("Dark"), static_cast<int>(ThemeMode::Dark));
    theme_->setCurrentIndex(theme_->findData(static_cast<int>(current.theme)));
    if (theme_->currentIndex() < 0)
        theme_->setCurrentIndex(0);
    form->addRow(tr("Theme"), theme_);

    auto* memory = new QGroupBox(tr("Memory"), this);
    auto* memory_layout = new QVBoxLayout(memory);
    remember_directories_ = new QCheckBox(tr("Remember recent directories"), memory);
    remember_directories_->setChecked(current.remember_directories);
    remember_settings_ = new QCheckBox(tr("Remember the last conversion settings"), memory);
    remember_settings_->setChecked(current.remember_conversion_settings);
    memory_layout->addWidget(remember_directories_);
    memory_layout->addWidget(remember_settings_);

    auto* note = new QLabel(tr("Language and theme changes take effect after restarting GMTools."), this);
    note->setWordWrap(true);
    note->setProperty("role", "muted");

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(appearance);
    layout->addWidget(memory);
    layout->addWidget(note);
    layout->addStretch();
    layout->addWidget(buttons);
}

AppSettings SettingsDialog::settings() const {
    AppSettings result;
    result.language = static_cast<Language>(language_->currentData().toInt());
    result.theme = static_cast<ThemeMode>(theme_->currentData().toInt());
    result.remember_directories = remember_directories_->isChecked();
    result.remember_conversion_settings = remember_settings_->isChecked();
    return result;
}

} // namespace gmtools::gui
