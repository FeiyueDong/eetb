#pragma once

#include "gmtools/gmtools.hpp"

#include <QDialog>

#include <functional>
#include <optional>

class QListWidget;
class QPushButton;

namespace gmtools::gui {

// Manage the presets owned by this installation. The dialog only reads and
// writes explicit preset files; it never touches the input waveforms.
class PresetDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PresetDialog(std::function<ConversionPreset()> current, QWidget* parent = nullptr);
    std::optional<ConversionPreset> take_selected();

private:
    void reload();
    QString selected_file() const;
    void load_selected();
    void save_current();
    void rename_selected();
    void delete_selected();
    void import_preset();
    void export_selected();

    std::function<ConversionPreset()> current_;
    QListWidget* list_{};
    QPushButton* load_{};
    QPushButton* rename_{};
    QPushButton* remove_{};
    QPushButton* export_{};
    std::optional<ConversionPreset> selected_;
};

} // namespace gmtools::gui
