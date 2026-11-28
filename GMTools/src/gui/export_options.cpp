#include "export_options.hpp"
#include "ui_helpers.hpp"

#include <QCoreApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>

namespace gmtools::gui {

bool choose_writer_options(WriteOptions& options, QWidget* parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("gmtools::gui::ExportOptions", "%1 export options")
                              .arg(as_text(name(options.format)).toUpper()));
    QFormLayout form(&dialog);
    auto choice = [&](const QString& label, const QStringList& values, int initial) {
        auto* box = new QComboBox(&dialog);
        box->addItems(values);
        box->setCurrentIndex(initial);
        form.addRow(label, box);
        return box;
    };
    QComboBox *layout{}, *header{}, *delimiter{}, *version{}, *unit_mode{}, *encoding{};
    QSpinBox* precision{};
    if (is_generic_text(options.format)) {
        layout =
            choice("Columns", {"Amplitude", "Elapsed time + amplitude"}, options.text_time_values.value_or(false));
        header = choice("Header", {"Format default", "None", "Column names", "Waveform metadata"},
                        options.text_header ? static_cast<int>(*options.text_header) + 1 : 0);
        if (options.format == FormatId::Text || options.format == FormatId::Dat)
            delimiter = choice("Separator", {"Space", "Comma", "Tab"},
                               static_cast<int>(options.text_delimiter.value_or(Delimiter::Whitespace)) - 1);
        precision = new QSpinBox(&dialog);
        precision->setRange(0, 17);
        precision->setSpecialValueText("Full precision");
        precision->setValue(static_cast<int>(options.precision.value_or(0)));
        form.addRow("Amplitude digits", precision);
    } else if (options.format == FormatId::Sac) {
        version = choice("SAC version", {"7 · double timing footer", "6 · float timing"}, options.sac_version == 6);
        unit_mode = choice("Sample scale", {"Keep selected unit", "Standard nanounits"},
                           options.sac_standard_units.value_or(false));
    } else {
        encoding = choice("Encoding", {"FLOAT64", "FLOAT32", "INT32", "Steim-2"},
                          static_cast<int>(options.mseed_encoding.value_or(MseedEncoding::Float64)));
    }
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form.addRow(&buttons);
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    if (layout) {
        options.text_time_values = layout->currentIndex() == 1;
        options.text_header.reset();
        if (header->currentIndex())
            options.text_header = static_cast<TextHeader>(header->currentIndex() - 1);
        if (delimiter)
            options.text_delimiter = static_cast<Delimiter>(delimiter->currentIndex() + 1);
        options.precision.reset();
        if (precision->value())
            options.precision = static_cast<std::size_t>(precision->value());
    } else if (version) {
        options.sac_version = version->currentIndex() ? 6 : 7;
        options.sac_standard_units = unit_mode->currentIndex() == 1;
    } else
        options.mseed_encoding = static_cast<MseedEncoding>(encoding->currentIndex());
    return true;
}
} // namespace gmtools::gui
