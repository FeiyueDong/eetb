#pragma once

#include "gmtools/gmtools.hpp"

#include <QString>
#include <stdexcept>

class QLineEdit;
class QPlainTextEdit;
class QWidget;

namespace gmtools::gui {

QString as_text(std::string_view value);
std::string as_utf8(const QString& value);
QString real(double value);
QString displayed_number(double value);
QString diagnostic_text(const Diagnostics& diagnostics);
QPlainTextEdit* read_only_text(QWidget* parent);
QString trace_title(const Trace& trace, std::size_t channel);
QString timing_unit(const Trace& trace);
void append_metadata(QString& text, const Metadata& attributes);
std::optional<double> optional_number(const QLineEdit* edit, const char* field);
std::optional<std::size_t> optional_index(const QLineEdit* edit, const char* field);

template<class T>
T require(std::optional<T> value, const char* message) {
    if (!value)
        throw std::invalid_argument(message);
    return *value;
}

} // namespace gmtools::gui
