#pragma once

#include "gmtools/gmtools.hpp"
#include "override_fields.hpp"

#include <QDialog>

#include <atomic>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;

namespace gmtools::gui {

// One sequential batch per dialog. The dialog owns the immutable configuration
// snapshot while a batch runs and forwards progress to the GUI thread.
class BatchDialog final : public QDialog {
    Q_OBJECT

public:
    explicit BatchDialog(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    ConversionPreset current_preset() const;
    void apply_preset(const ConversionPreset& preset);
    void reload_presets();
    void choose_files();
    void choose_directory();
    void add_paths(const QStringList& files);
    void remove_selected();
    void clear_files();
    void choose_output_directory();
    void choose_writer_options();
    void update_format();
    void update_selection_mode();
    void start();
    void request_stop();
    void show_progress(const BatchResult& result);
    void finished(const BatchResult& result);
    void set_running(bool running);
    void refresh_ready();

    std::vector<std::filesystem::path> inputs_;
    WriteOptions writer_;
    bool running_{};
    bool writer_touched_{};
    std::shared_ptr<std::atomic<bool>> stop_flag_;
    std::vector<BatchFileState> states_;

    QTableWidget* table_{};
    QComboBox* preset_{};
    QComboBox* format_{};
    QComboBox* layout_{};
    QComboBox* delimiter_{};
    QLineEdit* first_line_{};
    QLineEdit* last_line_{};
    QLineEdit* data_columns_{};
    QLineEdit* time_column_{};
    QLineEdit* numeric_factor_{};
    QComboBox* calibration_{};
    QLineEdit* calibration_factor_{};
    QComboBox* calibration_unit_{};
    QCheckBox* sac_units_{};
    OverrideFields* overrides_{};
    QComboBox* select_mode_{};
    QComboBox* select_quantity_{};
    QComboBox* target_{};
    QComboBox* output_unit_{};
    QPushButton* format_options_{};
    QLineEdit* output_directory_{};
    QCheckBox* overwrite_{};
    QProgressBar* progress_{};
    QLabel* status_{};
    QLabel* summary_{};
    QPlainTextEdit* details_{};
    QPushButton* start_{};
    QPushButton* stop_{};
    QPushButton* add_files_{};
    QPushButton* add_directory_{};
    QPushButton* remove_{};
    QPushButton* clear_{};
};

} // namespace gmtools::gui
