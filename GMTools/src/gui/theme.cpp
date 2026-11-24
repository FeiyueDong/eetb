#include "theme.hpp"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace gmtools::gui {

void apply_theme(QApplication& app) {
    const bool dark = app.palette().color(QPalette::Window).lightness() < 128;
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    auto palette = app.palette();
    const QColor background(dark ? "#17202d" : "#f3f6fb");
    const QColor surface(dark ? "#202c3b" : "#ffffff");
    const QColor text(dark ? "#e7edf6" : "#20324a");
    const QColor muted(dark ? "#acbbce" : "#5e7088");
    const QColor border(dark ? "#3c4e64" : "#d9e2ef");
    const QColor accent(dark ? "#77b3ff" : "#245fc4");
    palette.setColor(QPalette::Window, background);
    palette.setColor(QPalette::Base, surface);
    palette.setColor(QPalette::AlternateBase, background);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, surface);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::PlaceholderText, muted);
    palette.setColor(QPalette::Mid, border);
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText, dark ? QColor("#102138") : QColor("#ffffff"));
    palette.setColor(QPalette::ToolTipBase, surface);
    palette.setColor(QPalette::ToolTipText, text);
    for (const auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, muted);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, border);
    app.setPalette(palette);
    app.setStyleSheet(QStringLiteral(R"(
        QMainWindow, QDialog { background: palette(window); }
        QLabel { background: transparent; }
        QLabel[role="heading"] { font-size: 21px; font-weight: 600; }
        QLabel[role="title"] { font-size: 27px; font-weight: 600; }
        QLabel[role="section"] { font-weight: 600; }
        QLabel[role="muted"] { color: palette(placeholder-text); }
        QFrame[role="card"] {
            background: palette(base); border: 1px solid palette(mid); border-radius: 10px;
        }
        QPushButton, QToolButton {
            padding: 7px 12px; border: 1px solid palette(mid); border-radius: 6px;
            background: palette(base); color: palette(button-text);
        }
        QPushButton:hover, QToolButton:hover { border-color: palette(highlight); }
        QPushButton:pressed, QToolButton:pressed { background: palette(alternate-base); }
        QPushButton:focus, QToolButton:focus { border: 1px solid palette(highlight); }
        QPushButton[role="primary"] {
            background: palette(highlight); color: palette(highlighted-text);
            border-color: palette(highlight); font-weight: 600;
        }
        QPushButton:disabled, QToolButton:disabled {
            background: palette(window); color: palette(placeholder-text); border-color: palette(mid);
        }
        QLineEdit, QComboBox, QPlainTextEdit, QTreeWidget, QListWidget {
            background: palette(base); color: palette(text); border: 1px solid palette(mid);
            border-radius: 6px; selection-background-color: palette(highlight);
            selection-color: palette(highlighted-text);
        }
        QLineEdit, QComboBox { padding: 6px; min-height: 20px; }
        QLineEdit:focus, QComboBox:focus { border-color: palette(highlight); }
        QPlainTextEdit { padding: 6px; }
        QListWidget { padding: 4px; }
        QListWidget::item { padding: 10px 5px; border-radius: 4px; }
        QListWidget::item:selected { background: palette(highlight); color: palette(highlighted-text); }
        QTreeWidget { padding: 4px; }
        QTreeWidget::item { padding: 5px 0; }
        QGroupBox {
            border: 1px solid palette(mid); border-radius: 8px;
            margin-top: 12px; padding: 16px 12px 12px 12px; font-weight: 600;
        }
        QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 5px; }
        QTabWidget::pane { border: 0; padding-top: 8px; }
        QTabBar::tab { padding: 8px 10px; border-bottom: 2px solid transparent; }
        QTabBar::tab:selected { border-bottom-color: palette(highlight); color: palette(highlight); }
        QSplitter::handle { background: transparent; }
        QSplitter::handle:hover { background: palette(mid); }
        QScrollArea { border: 0; background: transparent; }
        QProgressBar { border: 0; background: palette(mid); border-radius: 2px; }
        QProgressBar::chunk { background: palette(highlight); }
        QStatusBar { color: palette(placeholder-text); }
        QToolTip { border: 1px solid palette(mid); padding: 5px; }
    )"));
}

} // namespace gmtools::gui
