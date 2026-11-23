#include "gmtools/gmtools.hpp"
#include "plot.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QTextBlock>
#include <QTextFormat>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
using namespace gmtools;

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
        .arg(trace.name.empty() ? QStringLiteral("Unnamed") : as_text(trace.name));
}

QString timing_unit(const Trace& trace) {
    const auto dt = trace.dt_seconds ? displayed_number(*trace.dt_seconds) + QStringLiteral(" s")
                                     : QStringLiteral("unknown");
    return QStringLiteral("%1 samples, dt %2, %3")
        .arg(trace.samples.size()).arg(dt).arg(as_text(name(trace.unit)));
}

void append_metadata(QString& text, const Metadata& attributes) {
    for (const auto& [key, value] : attributes)
        text += '\n' + as_text(key) + QStringLiteral(" = ") + as_text(value);
}

template<class T>
T require(std::optional<T> value, const char* message) {
    if (!value)
        throw std::invalid_argument(message);
    return *value;
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

class OverrideFields {
public:
    explicit OverrideFields(QFormLayout* form, const Overrides& initial = {},
                            const QString& keep = QStringLiteral("From source")) {
        sampling_ = new QLineEdit;
        sampling_->setPlaceholderText(keep);
        sampling_kind_ = new QComboBox;
        sampling_kind_->addItems({QStringLiteral("dt (s)"), QStringLiteral("fs (Hz)")});
        if (initial.fs_hz) {
            sampling_kind_->setCurrentIndex(1);
            sampling_->setText(real(*initial.fs_hz));
        } else if (initial.dt_seconds) {
            sampling_->setText(real(*initial.dt_seconds));
        }
        auto* timing = new QHBoxLayout;
        timing->addWidget(sampling_kind_);
        timing->addWidget(sampling_);
        form->addRow(QStringLiteral("Sampling"), timing);

        quantity_ = new QComboBox;
        quantity_->addItem(keep);
        for (const auto value : {Quantity::Unknown, Quantity::Acceleration, Quantity::Velocity,
                                 Quantity::Displacement, Quantity::Counts})
            quantity_->addItem(as_text(name(value)), static_cast<int>(value));
        if (initial.quantity)
            quantity_->setCurrentIndex(quantity_->findData(static_cast<int>(*initial.quantity)));
        form->addRow(QStringLiteral("Quantity"), quantity_);

        unit_ = new QComboBox;
        unit_->addItem(keep);
        for (const auto value : {Unit::Unknown, Unit::G, Unit::Mps2, Unit::Cmps2, Unit::Counts})
            unit_->addItem(as_text(name(value)), static_cast<int>(value));
        if (initial.unit)
            unit_->setCurrentIndex(unit_->findData(static_cast<int>(*initial.unit)));
        form->addRow(QStringLiteral("Input unit"), unit_);
    }

    Overrides options() const {
        Overrides result;
        const auto sampling = optional_number(sampling_, "Sampling requires a finite number");
        if (sampling_kind_->currentIndex() == 0)
            result.dt_seconds = sampling;
        else
            result.fs_hz = sampling;
        if (quantity_->currentData().isValid())
            result.quantity = static_cast<Quantity>(quantity_->currentData().toInt());
        if (unit_->currentData().isValid())
            result.unit = static_cast<Unit>(unit_->currentData().toInt());
        return result;
    }

private:
    QLineEdit* sampling_{};
    QComboBox* sampling_kind_{};
    QComboBox* quantity_{};
    QComboBox* unit_{};
};

class ParseDialog final : public QDialog {
public:
    ParseDialog(std::span<const std::byte> bytes, std::string extension, const ReadOptions& original,
                QWidget* parent)
        : QDialog(parent), bytes_(bytes), extension_(std::move(extension)), raw_(text_view(bytes)) {
        setWindowTitle(QStringLiteral("Interpret input file"));
        resize(850, 590);
        auto* outer = new QVBoxLayout(this);
        auto* panes = new QSplitter(Qt::Horizontal, this);
        outer->addWidget(panes, 1);

        auto* parameters = new QWidget(panes);
        auto* settings = new QVBoxLayout(parameters);
        auto* preset = new QFormLayout;
        format_ = new QComboBox;
        format_->addItem(QStringLiteral("Automatic"), static_cast<int>(FormatId::Auto));
        for (const auto& item : formats()) {
            if (item.readable)
                format_->addItem(as_text(item.name), static_cast<int>(item.id));
        }
        format_->setCurrentIndex(format_->findData(static_cast<int>(original.format)));
        preset->addRow(QStringLiteral("Input format"), format_);
        settings->addLayout(preset);

        text_group_ = new QGroupBox(QStringLiteral("Text data"));
        auto* text_form = new QFormLayout(text_group_);
        first_ = line_field(text_form, QStringLiteral("First data line"), QStringLiteral("Automatic"));
        last_ = line_field(text_form, QStringLiteral("Last data line"), QStringLiteral("End of file"));
        layout_ = new QComboBox;
        layout_->addItems({QStringLiteral("Format default"), QStringLiteral("Sequential"), QStringLiteral("Columnar")});
        text_form->addRow(QStringLiteral("Layout"), layout_);
        delimiter_ = new QComboBox;
        delimiter_->addItems({QStringLiteral("Automatic"), QStringLiteral("Whitespace"), QStringLiteral("Comma"), QStringLiteral("Tab")});
        text_form->addRow(QStringLiteral("Delimiter"), delimiter_);
        columns_ = line_field(text_form, QStringLiteral("Data columns"), QStringLiteral("All except time, e.g. 2,3"));
        time_ = line_field(text_form, QStringLiteral("Time column"), QStringLiteral("Optional; values in seconds"));
        factor_ = line_field(text_form, QStringLiteral("Numeric factor"), QStringLiteral("Optional; scales sample values"));
        load_text_options(original.text);
        settings->addWidget(text_group_);

        auto* override_group = new QGroupBox(QStringLiteral("Source parameter overrides"));
        overrides_ = std::make_unique<OverrideFields>(new QFormLayout(override_group), original.overrides);
        settings->addWidget(override_group);
        settings->addStretch();

        auto* preview = new QWidget(panes);
        auto* preview_layout = new QVBoxLayout(preview);
        raw_label_ = new QLabel(preview);
        preview_layout->addWidget(raw_label_);
        raw_view_ = read_only_text(preview);
        raw_view_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        raw_view_->setLineWrapMode(QPlainTextEdit::NoWrap);
        preview_layout->addWidget(raw_view_, 3);
        preview_layout->addWidget(new QLabel(QStringLiteral("Parsing feedback"), preview));
        feedback_ = read_only_text(preview);
        preview_layout->addWidget(feedback_, 2);
        panes->setStretchFactor(1, 1);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Use parsed traces"));
        auto* check = buttons->addButton(QStringLiteral("Preview parse"), QDialogButtonBox::ActionRole);
        outer->addWidget(buttons);
        connect(check, &QPushButton::clicked, this, [this] { preview_parse(false); });
        connect(buttons, &QDialogButtonBox::accepted, this, [this] { preview_parse(true); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(format_, &QComboBox::currentIndexChanged, this, [this] { update_text_fields(); });
        connect(layout_, &QComboBox::currentIndexChanged, this, [this] { update_text_fields(); });
        connect(first_, &QLineEdit::editingFinished, this, [this] {
            show_raw(parse_index(as_utf8(first_->text())).value_or(0));
        });
        update_text_fields();
        preview_parse(false);
    }

    ReadResult take_result() { return std::move(result_); }
    const ReadOptions& last_options() const { return used_; }

private:
    static QLineEdit* line_field(QFormLayout* form, const QString& name, const QString& placeholder) {
        auto* edit = new QLineEdit;
        edit->setPlaceholderText(placeholder);
        form->addRow(name, edit);
        return edit;
    }

    void load_text_options(const TextParseOptions& options) {
        if (options.first_line)
            first_->setText(QString::number(*options.first_line));
        if (options.last_line)
            last_->setText(QString::number(*options.last_line));
        if (options.layout)
            layout_->setCurrentIndex(*options.layout == Layout::Sequential ? 1 : 2);
        delimiter_->setCurrentIndex(static_cast<int>(options.delimiter));
        if (options.time_column)
            time_->setText(QString::number(*options.time_column));
        if (options.numeric_factor)
            factor_->setText(real(*options.numeric_factor));
        QStringList columns;
        for (const auto column : options.data_columns)
            columns << QString::number(column);
        columns_->setText(columns.join(','));
    }

    void update_text_fields() {
        const auto format = static_cast<FormatId>(format_->currentData().toInt());
        text_group_->setEnabled(raw_.has_value() && format != FormatId::Sac && format != FormatId::Mseed);
        const bool columnar = layout_->currentIndex() != 1;
        columns_->setEnabled(columnar);
        time_->setEnabled(columnar);
    }

    ReadOptions options() const {
        ReadOptions result;
        result.format = static_cast<FormatId>(format_->currentData().toInt());
        result.extension_hint = extension_;
        result.overrides = overrides_->options();
        if (!text_group_->isEnabled())
            return result;
        auto& text = result.text;
        text.first_line = optional_index(first_, "First data line must be a positive integer");
        text.last_line = optional_index(last_, "Last data line must be a positive integer");
        if (layout_->currentIndex() != 0)
            text.layout = layout_->currentIndex() == 1 ? Layout::Sequential : Layout::Columnar;
        text.delimiter = static_cast<Delimiter>(delimiter_->currentIndex());
        if (time_->isEnabled()) {
            text.time_column = optional_index(time_, "Time column must be a positive integer");
            const auto columns = columns_->text().trimmed();
            if (!columns.isEmpty())
                text.data_columns = require(parse_indices(as_utf8(columns)), "Use comma-separated column numbers");
        }
        text.numeric_factor = optional_number(factor_, "Numeric factor must be finite");
        return result;
    }

    void show_raw(std::size_t highlight) {
        if (!raw_) {
            raw_label_->setText(QStringLiteral("Raw preview"));
            raw_view_->setPlainText(QStringLiteral("Binary input or unsupported text encoding"));
            return;
        }
        // Keep the view bounded while allowing navigation to any selected input line.
        const auto first = highlight > 15 ? highlight - 15 : 1;
        constexpr std::size_t limit = 220;
        constexpr std::size_t line_limit = 1000;
        QStringList visible;
        auto rest = *raw_;
        std::size_t row = 1;
        for (; !rest.empty() && visible.size() < static_cast<qsizetype>(limit); ++row) {
            const auto end = rest.find_first_of("\r\n");
            const auto line = rest.substr(0, end);
            if (row >= first) {
                auto visible_size = std::min(line.size(), line_limit);
                while (visible_size < line.size() &&
                       (static_cast<unsigned char>(line[visible_size]) & 0xc0) == 0x80)
                    --visible_size;
                auto content = as_text(line.substr(0, visible_size));
                if (visible_size < line.size())
                    content += QStringLiteral("…");
                visible << QStringLiteral("%1  %2").arg(row, 6).arg(content);
            }
            if (end == rest.npos)
                rest = {};
            else
                rest.remove_prefix(end + (rest.substr(end, 2) == "\r\n" ? 2 : 1));
        }
        raw_view_->setPlainText(visible.join('\n'));
        const auto visible_lines = static_cast<std::size_t>(visible.size());
        raw_label_->setText(visible.isEmpty() ? QStringLiteral("Raw preview")
            : QStringLiteral("Raw preview · lines %1–%2").arg(first).arg(first + visible_lines - 1));

        if (!visible.isEmpty() && highlight >= first &&
            highlight - first < visible_lines) {
            QTextEdit::ExtraSelection selection;
            selection.cursor = QTextCursor(raw_view_->document()->findBlockByNumber(static_cast<int>(highlight - first)));
            selection.format.setBackground(raw_view_->palette().highlight());
            selection.format.setForeground(raw_view_->palette().highlightedText());
            selection.format.setProperty(QTextFormat::FullWidthSelection, true);
            raw_view_->setExtraSelections({selection});
        } else {
            raw_view_->setExtraSelections({});
        }
    }

    void preview_parse(bool confirm) {
        try {
            used_ = options();
            result_ = read(bytes_, used_);
            auto description = diagnostic_text(result_.diagnostics);
            if (!has_errors(result_.diagnostics)) {
                QString summary = QStringLiteral("%1 · %2 trace(s)")
                    .arg(as_text(name(result_.data.source_format))).arg(result_.data.traces.size());
                for (std::size_t i = 0; i < result_.data.traces.size(); ++i) {
                    const auto& trace = result_.data.traces[i];
                    summary += '\n' + trace_title(trace, i + 1) + QStringLiteral(" — ") + timing_unit(trace);
                }
                description = summary + (description.isEmpty() ? QString{} : '\n' + description);
            }
            feedback_->setPlainText(description);
            show_raw(used_.text.first_line.value_or(result_.first_data_line));
            if (confirm && !has_errors(result_.diagnostics))
                accept();
        } catch (const std::exception& problem) {
            feedback_->setPlainText(as_text(problem.what()));
            show_raw(0);
        }
    }

    std::span<const std::byte> bytes_;
    std::string extension_;
    std::optional<std::string_view> raw_;
    ReadOptions used_;
    ReadResult result_;
    QComboBox* format_{};
    QGroupBox* text_group_{};
    QComboBox* layout_{};
    QComboBox* delimiter_{};
    QLineEdit* first_{};
    QLineEdit* last_{};
    QLineEdit* columns_{};
    QLineEdit* time_{};
    QLineEdit* factor_{};
    std::unique_ptr<OverrideFields> overrides_;
    QLabel* raw_label_{};
    QPlainTextEdit* raw_view_{};
    QPlainTextEdit* feedback_{};
};

class Window final : public QMainWindow {
public:
    Window() {
        setWindowTitle(QStringLiteral("GMTools"));
        resize(1180, 760);
        auto* panes = new QSplitter(Qt::Horizontal, this);
        setCentralWidget(panes);

        auto* sources = new QWidget(panes);
        auto* source_box = new QVBoxLayout(sources);
        auto* open = new QPushButton(QStringLiteral("Open file…"), sources);
        source_box->addWidget(open);
        reparse_ = new QPushButton(QStringLiteral("Interpret again…"), sources);
        source_box->addWidget(reparse_);
        source_label_ = new QLabel(QStringLiteral("No file open"), sources);
        source_label_->setWordWrap(true);
        source_label_->setTextFormat(Qt::PlainText);
        source_box->addWidget(source_label_);
        source_box->addWidget(new QLabel(QStringLiteral("Channels to export"), sources));
        channels_ = new QListWidget(sources);
        source_box->addWidget(channels_, 1);
        auto* selection = new QHBoxLayout;
        auto* all = new QPushButton(QStringLiteral("All"), sources);
        auto* none = new QPushButton(QStringLiteral("None"), sources);
        selection->addWidget(all);
        selection->addWidget(none);
        source_box->addLayout(selection);

        plots_ = new QScrollArea(panes);
        plots_->setWidgetResizable(true);
        plots_->setMinimumWidth(280);

        auto* output = new QWidget(panes);
        auto* output_box = new QVBoxLayout(output);
        output_box->addWidget(new QLabel(QStringLiteral("Channel information"), output));
        info_ = read_only_text(output);
        output_box->addWidget(info_, 2);
        edit_ = new QPushButton(QStringLiteral("Edit selected channel…"), output);
        output_box->addWidget(edit_);
        output_box->addWidget(new QLabel(QStringLiteral("Import diagnostics"), output));
        diagnostics_ = read_only_text(output);
        output_box->addWidget(diagnostics_, 1);
        auto* export_form = new QFormLayout;
        target_ = new QComboBox(output);
        for (const auto& item : formats()) {
            if (item.writable) {
                const auto label = as_text(item.name) +
                    (item.id == FormatId::Sac || item.id == FormatId::Mseed || item.id == FormatId::Mseed2
                        ? QStringLiteral(" (experimental)") : QString{});
                target_->addItem(label, static_cast<int>(item.id));
            }
        }
        target_->setCurrentIndex(target_->findData(static_cast<int>(FormatId::Csv)));
        export_form->addRow(QStringLiteral("Target format"), target_);
        output_unit_ = new QComboBox(output);
        output_unit_->addItem(QStringLiteral("Format default"));
        for (const auto unit : {Unit::G, Unit::Mps2, Unit::Cmps2})
            output_unit_->addItem(as_text(name(unit)), static_cast<int>(unit));
        export_form->addRow(QStringLiteral("Output unit"), output_unit_);
        connect(target_, &QComboBox::currentIndexChanged, this, [this] {
            const bool at2 = static_cast<FormatId>(target_->currentData().toInt()) == FormatId::At2;
            if (at2)
                output_unit_->setCurrentIndex(0);
            output_unit_->setEnabled(!at2);
            output_unit_->setItemText(0, at2 ? QStringLiteral("g (AT2)") : QStringLiteral("Format default"));
        });
        overwrite_ = new QCheckBox(QStringLiteral("Overwrite existing output files"), output);
        export_form->addRow(overwrite_);
        output_box->addLayout(export_form);
        export_ = new QPushButton(QStringLiteral("Export checked channels…"), output);
        output_box->addWidget(export_);

        panes->setSizes({205, 620, 330});
        panes->setStretchFactor(1, 1);
        connect(open, &QPushButton::clicked, this, [this] { open_file(); });
        connect(reparse_, &QPushButton::clicked, this, [this] { reparse(); });
        connect(channels_, &QListWidget::currentRowChanged, this, [this] { show_trace(); });
        connect(all, &QPushButton::clicked, this, [this] { check_channels(Qt::Checked); });
        connect(none, &QPushButton::clicked, this, [this] { check_channels(Qt::Unchecked); });
        connect(edit_, &QPushButton::clicked, this, [this] { edit_trace(); });
        connect(export_, &QPushButton::clicked, this, [this] { send_export(); });
        refresh();
    }

    ~Window() override { clear_plots(); }

private:
    void open_file() {
        const auto file = QFileDialog::getOpenFileName(this, QStringLiteral("Open ground motion"), {},
            QStringLiteral("Waveforms (*.txt *.dat *.csv *.tsv *.at2 *.sac *.mseed *.miniseed);;All files (*)"));
        if (file.isEmpty())
            return;
        try {
            const auto path = path_from_utf8(as_utf8(file));
            auto loaded = load_file(path);
            if (has_errors(loaded.diagnostics)) {
                QMessageBox::warning(this, QStringLiteral("Cannot open file"), diagnostic_text(loaded.diagnostics));
                return;
            }
            ParseDialog dialog(loaded.bytes, path_to_utf8(path.extension()), {}, this);
            if (dialog.exec() == QDialog::Accepted) {
                input_ = path;
                bytes_ = std::move(loaded.bytes);
                accept_parse(dialog);
            }
        } catch (const std::exception& problem) {
            QMessageBox::warning(this, QStringLiteral("Cannot open file"), as_text(problem.what()));
        }
    }

    void reparse() {
        // Reinterpreting source bytes replaces all per-channel edits and export selections.
        if (edited_ && QMessageBox::question(this, QStringLiteral("Interpret again"),
                QStringLiteral("Replace your channel edits by parsing the source again?")) != QMessageBox::Yes)
            return;
        try {
            ParseDialog dialog(bytes_, path_to_utf8(input_.extension()), last_options_, this);
            if (dialog.exec() == QDialog::Accepted)
                accept_parse(dialog);
        } catch (const std::exception& problem) {
            QMessageBox::warning(this, QStringLiteral("Interpret again"), as_text(problem.what()));
        }
    }

    void accept_parse(ParseDialog& dialog) {
        clear_plots();
        last_options_ = dialog.last_options();
        parsed_ = dialog.take_result();
        edited_ = false;
        refresh();
    }

    void check_channels(Qt::CheckState state) {
        for (int i = 0; i < channels_->count(); ++i)
            channels_->item(i)->setCheckState(state);
    }

    void clear_plots() {
        delete plots_->takeWidget();
    }

    void refresh_plots() {
        clear_plots();
        auto* container = new QWidget;
        auto* layout = new QVBoxLayout(container);
        for (std::size_t i = 0; i < parsed_.data.traces.size(); ++i)
            layout->addWidget(new TracePlot(parsed_.data.traces[i], i + 1, container));
        if (parsed_.data.traces.empty())
            layout->addWidget(new QLabel(QStringLiteral("Open a ground-motion file to view its traces."), container));
        layout->addStretch();
        plots_->setWidget(container);
    }

    void refresh() {
        const bool open = !parsed_.data.traces.empty();
        reparse_->setEnabled(open);
        export_->setEnabled(open);
        channels_->clear();
        for (std::size_t i = 0; i < parsed_.data.traces.size(); ++i) {
            auto* item = new QListWidgetItem(trace_title(parsed_.data.traces[i], i + 1), channels_);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
        }
        if (open) {
            source_label_->setText(as_text(path_to_utf8(input_.filename())) +
                                   QStringLiteral(" · ") + as_text(name(parsed_.data.source_format)));
            source_label_->setToolTip(as_text(path_to_utf8(input_)));
            channels_->setCurrentRow(0);
        }
        diagnostics_->setPlainText(diagnostic_text(parsed_.diagnostics));
        refresh_plots();
        show_trace();
    }

    void show_trace() {
        const auto row = channels_->currentRow();
        edit_->setEnabled(row >= 0);
        if (row < 0) {
            info_->clear();
            return;
        }
        const auto& trace = parsed_.data.traces[static_cast<std::size_t>(row)];
        auto text = trace_title(trace, static_cast<std::size_t>(row) + 1) + '\n' + timing_unit(trace);
        text += QStringLiteral("\nQuantity: ") + as_text(name(trace.quantity));
        if (const auto span = trace.sample_span()) {
            text += QStringLiteral("\nFirst-to-last span: %1 s\nNominal record length: %2 s")
                .arg(displayed_number(*span), displayed_number(*trace.record_length()));
        }
        if (trace.relative_start_seconds)
            text += QStringLiteral("\nRelative start: %1 s").arg(displayed_number(*trace.relative_start_seconds));
        if (trace.start_time)
            text += QStringLiteral("\nAbsolute start: ") + as_text(timestamp(*trace.start_time));
        append_metadata(text, parsed_.data.attributes);
        append_metadata(text, trace.attributes);
        info_->setPlainText(text);
    }

    void edit_trace() {
        const auto row = channels_->currentRow();
        if (row < 0)
            return;
        auto& trace = parsed_.data.traces[static_cast<std::size_t>(row)];
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Edit channel parameters"));
        auto* form = new QFormLayout(&dialog);
        auto* channel_name = new QLineEdit(as_text(trace.name), &dialog);
        form->addRow(QStringLiteral("Channel name"), channel_name);
        form->addRow(new QLabel(timing_unit(trace), &dialog));
        const OverrideFields fields(form, {}, QStringLiteral("Keep current"));
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            try {
                auto diagnostics = apply_overrides(trace, fields.options());
                if (has_errors(diagnostics)) {
                    QMessageBox::warning(&dialog, QStringLiteral("Invalid parameters"), diagnostic_text(diagnostics));
                    return;
                }
                for (auto& diagnostic : diagnostics) {
                    diagnostic.channel = static_cast<std::size_t>(row) + 1;
                    parsed_.diagnostics.push_back(std::move(diagnostic));
                }
                const auto new_name = as_utf8(channel_name->text().trimmed());
                if (new_name != trace.name) {
                    if (!trace.name.empty() && !trace.attributes.contains("source.original_name"))
                        trace.attributes["source.original_name"] = trace.name;
                    trace.name = new_name;
                    trace.attributes["source.name"] = "user input";
                }
                edited_ = true;
                dialog.accept();
            } catch (const std::exception& problem) {
                QMessageBox::warning(&dialog, QStringLiteral("Invalid parameters"), as_text(problem.what()));
            }
        });
        if (dialog.exec() == QDialog::Accepted) {
            channels_->item(row)->setText(trace_title(trace, static_cast<std::size_t>(row) + 1));
            diagnostics_->setPlainText(diagnostic_text(parsed_.diagnostics));
            refresh_plots();
            show_trace();
        }
    }

    void send_export() {
        ExportOptions options;
        for (int row = 0; row < channels_->count(); ++row) {
            if (channels_->item(row)->checkState() == Qt::Checked)
                options.channels.push_back(static_cast<std::size_t>(row) + 1);
        }
        if (options.channels.empty()) {
            QMessageBox::information(this, QStringLiteral("Export"), QStringLiteral("Check at least one channel."));
            return;
        }
        const auto directory = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose output directory"),
            as_text(path_to_utf8(input_.parent_path())));
        if (directory.isEmpty())
            return;
        try {
            options.writer.format = static_cast<FormatId>(target_->currentData().toInt());
            if (output_unit_->currentData().isValid())
                options.writer.output_unit = static_cast<Unit>(output_unit_->currentData().toInt());
            options.output = path_from_utf8(as_utf8(directory));
            options.overwrite = overwrite_->isChecked();
            const auto result = export_file(parsed_.data, input_, options);
            QMessageBox report(this);
            report.setWindowTitle(QStringLiteral("Export"));
            report.setTextFormat(Qt::PlainText);
            report.setIcon(has_errors(result.diagnostics) ? QMessageBox::Warning : QMessageBox::Information);
            report.setText(has_errors(result.diagnostics)
                ? QStringLiteral("Export could not complete. %1 file(s) written.").arg(result.written.size())
                : QStringLiteral("Exported %1 file(s).").arg(result.written.size()));
            const auto issue = std::ranges::find_if(result.diagnostics, [](const auto& diagnostic) {
                return diagnostic.severity == Severity::Error;
            });
            if (issue != result.diagnostics.end())
                report.setInformativeText(as_text(describe(*issue)));
            QStringList details;
            for (const auto& path : result.written)
                details << as_text(path_to_utf8(path));
            if (!result.diagnostics.empty())
                details << diagnostic_text(result.diagnostics);
            report.setDetailedText(details.join('\n'));
            report.exec();
        } catch (const std::exception& problem) {
            QMessageBox::warning(this, QStringLiteral("Export"), as_text(problem.what()));
        }
    }

    std::filesystem::path input_;
    std::vector<std::byte> bytes_;
    ReadOptions last_options_;
    ReadResult parsed_;
    bool edited_{};
    QPushButton* reparse_{};
    QLabel* source_label_{};
    QListWidget* channels_{};
    QScrollArea* plots_{};
    QPlainTextEdit* info_{};
    QPlainTextEdit* diagnostics_{};
    QPushButton* edit_{};
    QPushButton* export_{};
    QComboBox* target_{};
    QComboBox* output_unit_{};
    QCheckBox* overwrite_{};
};

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("GMTools"));
    Window window;
    window.show();
    return app.exec();
}
