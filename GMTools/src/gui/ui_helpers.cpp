#include "ui_helpers.hpp"

#include <QCoreApplication>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QStringList>

namespace gmtools::gui {

QString as_text(std::string_view value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::string as_utf8(const QString& value) {
    return value.toUtf8().toStdString();
}

QString real(double value) {
    return QString::number(value, 'g', 17);
}

QString displayed_number(double value) {
    return QString::number(value, 'g', 12);
}

QString diagnostic_text(const Diagnostics& diagnostics) {
    QStringList messages;
    for (const auto& diagnostic : diagnostics)
        messages << as_text(describe(diagnostic));
    return messages.join('\n');
}

QPlainTextEdit* read_only_text(QWidget* parent) {
    auto* view = new QPlainTextEdit(parent);
    view->setReadOnly(true);
    return view;
}

QString trace_title(const Trace& trace, std::size_t channel) {
    return QStringLiteral("%1   %2").arg(channel)
        .arg(trace.name.empty() ? QCoreApplication::translate("gmtools::gui::TraceInfo", "Unnamed") : as_text(trace.name));
}

QString timing_unit(const Trace& trace) {
    const auto dt = trace.dt_seconds ? displayed_number(*trace.dt_seconds) + QStringLiteral(" s")
                                     : QCoreApplication::translate("gmtools::gui::TraceInfo", "unknown");
    return QCoreApplication::translate("gmtools::gui::TraceInfo", "%1 samples, dt %2, %3")
        .arg(trace.samples.size()).arg(dt).arg(as_text(name(trace.unit)));
}

void append_metadata(QString& text, const Metadata& attributes) {
    for (const auto& [key, value] : attributes)
        text += '\n' + as_text(key) + QStringLiteral(" = ") + as_text(value);
}

std::optional<double> optional_number(const QLineEdit* edit, const char* field) {
    const auto text = edit->text().trimmed();
    if (text.isEmpty())
        return {};
    return require(parse_number(as_utf8(text)), field);
}

std::optional<std::size_t> optional_index(const QLineEdit* edit, const char* field) {
    const auto text = edit->text().trimmed();
    if (text.isEmpty())
        return {};
    return require(parse_index(as_utf8(text)), field);
}
} // namespace gmtools::gui
