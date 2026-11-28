#include "localization.hpp"
#include "main_window.hpp"
#include "settings.hpp"
#include "theme.hpp"

#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("EETB"));
    app.setApplicationName(QStringLiteral("GMTools"));
    const auto settings = gmtools::gui::AppSettings::load();
    gmtools::gui::install_translations(app, settings.language);
    gmtools::gui::apply_theme(app, settings.theme);
    gmtools::gui::MainWindow window;
    window.show();
    return app.exec();
}
