#pragma once

#include "parse_dialog.hpp"

#include <QMainWindow>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSplitter;
class QStackedWidget;
class QTreeWidget;

namespace gmtools::gui {

class MainWindow final : public QMainWindow {
public:
    MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void choose_file();
    void open_file(const QString& file);
    void reparse();
    bool confirm_replacement();
    void accept_parse(ParsedInput parsed);
    void refresh();
    void show_trace(bool scroll = true);
    void check_channels(Qt::CheckState state);
    void update_selection();
    void update_format();
    void edit_trace();
    void choose_output();
    void send_export();
    void set_busy(bool busy, const QString& message = {});

    std::filesystem::path input_;
    std::shared_ptr<const InputFile> bytes_;
    ReadOptions last_options_;
    std::shared_ptr<ReadResult> parsed_;
    std::vector<TracePlot*> trace_plots_;
    bool edited_{};
    bool busy_{};
    QAction* open_action_{};
    QAction* reparse_action_{};
    QAction* export_action_{};
    QStackedWidget* workspace_{};
    QSplitter* panes_{};
    QLabel* source_label_{};
    QLabel* source_summary_{};
    QLabel* selection_label_{};
    QLabel* review_label_{};
    QListWidget* channels_{};
    QScrollArea* plots_{};
    QTreeWidget* properties_{};
    QPlainTextEdit* metadata_{};
    QPlainTextEdit* diagnostics_{};
    QPushButton* edit_{};
    QPushButton* export_{};
    QComboBox* target_{};
    QComboBox* output_unit_{};
    QPushButton* format_options_{};
    WriteOptions writer_;
    QCheckBox* overwrite_{};
    QLabel* format_help_{};
    QLineEdit* output_directory_{};
    QProgressBar* progress_{};
};

} // namespace gmtools::gui
