#pragma once

#include "settings.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;

namespace gmtools::gui {

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const AppSettings& current, QWidget* parent = nullptr);
    AppSettings settings() const;

private:
    QComboBox* language_{};
    QComboBox* theme_{};
    QCheckBox* remember_directories_{};
    QCheckBox* remember_settings_{};
};

} // namespace gmtools::gui
