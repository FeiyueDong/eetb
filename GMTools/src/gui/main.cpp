#include "main_window.hpp"
#include "theme.hpp"

#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("EETB"));
    app.setApplicationName(QStringLiteral("GMTools"));
    gmtools::gui::apply_theme(app);
    gmtools::gui::MainWindow window;
    window.show();
    return app.exec();
}
