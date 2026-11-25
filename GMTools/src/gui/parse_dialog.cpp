#include "parse_dialog.hpp"
#include "background_task.hpp"
#include "override_fields.hpp"
#include <QCheckBox>
#include "ui_helpers.hpp"

#include <QProgressBar>
#include <QTimer>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QSplitter>
#include <QStringList>
#include <QTextBlock>
#include <QTextFormat>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>

namespace gmtools::gui {
namespace {

struct ParsePreview {
    ReadResult parsed;
    TextSuggestion suggestions;
    std::vector<PlotData> plots;
};

class ParseDialog final : public QDialog {
public:
    ParseDialog(std::shared_ptr<const InputFile> bytes, std::filesystem::path input, const ReadOptions& original,
                QWidget* parent)
        : QDialog(parent), bytes_(std::move(bytes)), input_(std::move(input)),
          extension_(path_to_utf8(input_.extension())) {
        setWindowTitle(QStringLiteral("Import · %1").arg(as_text(path_to_utf8(input_.filename()))));
        resize(1040, 740);
        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(20, 18, 20, 18);
        outer->setSpacing(12);
        auto* title = new QLabel(QStringLiteral("Interpret your record"), this);
        title->setProperty("role", "heading");
        outer->addWidget(title);
        auto* intro = new QLabel(QStringLiteral("Check the source, preview the channels, then import. Line and column numbers start at 1."), this);
        intro->setProperty("role", "muted");
        intro->setWordWrap(true);
        outer->addWidget(intro);
        auto* panes = new QSplitter(Qt::Horizontal, this);
        outer->addWidget(panes, 1);

        auto* settings_scroll = new QScrollArea(panes);
        settings_scroll->setWidgetResizable(true);
        settings_scroll->setFrameShape(QFrame::NoFrame);
        settings_scroll->setMinimumWidth(330);
        parameters_ = new QWidget;
        auto* parameters = parameters_;
        settings_scroll->setWidget(parameters);
        auto* settings = new QVBoxLayout(parameters);
        settings->setContentsMargins(0, 0, 12, 0);
        settings->setSpacing(14);
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
        layout_->setToolTip(QStringLiteral("Sequential: values continue across each row into one channel.\n"
                                          "Columnar: each selected column is a separate channel."));
        text_form->addRow(QStringLiteral("Layout"), layout_);
        delimiter_ = new QComboBox;
        delimiter_->addItems({QStringLiteral("Automatic"), QStringLiteral("Whitespace"), QStringLiteral("Comma"), QStringLiteral("Tab")});
        text_form->addRow(QStringLiteral("Delimiter"), delimiter_);
        columns_ = line_field(text_form, QStringLiteral("Data columns"), QStringLiteral("All except time, e.g. 2,3"));
        time_ = line_field(text_form, QStringLiteral("Time column"), QStringLiteral("Values in seconds"));
        settings->addWidget(text_group_);

        auto* override_group = new QGroupBox(QStringLiteral("Source parameter overrides"));
        auto* override_form = new QFormLayout(override_group);
        overrides_ = std::make_unique<OverrideFields>(override_form, original.overrides);
        factor_ = line_field(override_form, QStringLiteral("Numeric factor"), QStringLiteral("Optional sample multiplier"));
        factor_->setToolTip("Multiply decoded samples after any instrument calibration.");
        calibration_ = new QComboBox;
        calibration_->addItems(
            {"Format default", "Raw counts / values", "Use file calibration", "Manual units per count"});
        calibration_->setCurrentIndex(static_cast<int>(original.calibration));
        override_form->addRow("Calibration", calibration_);
        calibration_factor_ = line_field(override_form, "Units per count", "Physical units per count");
        calibration_unit_ = new QComboBox;
        calibration_unit_->addItem("Choose physical unit");
        for (auto u : units())
            if (unit_factor(u, u))
                calibration_unit_->addItem(as_text(name(u)), static_cast<int>(u));
        override_form->addRow("Calibrated unit", calibration_unit_);
        if (original.calibration_factor)
            calibration_factor_->setText(real(*original.calibration_factor));
        if (original.calibration_unit)
            calibration_unit_->setCurrentIndex(
                calibration_unit_->findData(static_cast<int>(*original.calibration_unit)));
        auto manual_visibility = [this, override_form] {
            const bool manual = calibration_->currentIndex() == static_cast<int>(Calibration::Manual);
            for (QWidget* field :
                 {static_cast<QWidget*>(calibration_factor_), static_cast<QWidget*>(calibration_unit_)}) {
                field->setVisible(manual);
                override_form->labelForField(field)->setVisible(manual);
            }
        };
        connect(calibration_, &QComboBox::currentIndexChanged, this, manual_visibility);
        manual_visibility();
        sac_units_ = new QCheckBox("Interpret SAC IDEP using standard nanounits");
        sac_units_->setChecked(original.sac_standard_units.value_or(false));
        override_form->addRow(sac_units_);
        connect(sac_units_, &QCheckBox::toggled, this, [this] { invalidate_preview(); });
        load_text_options(original.text);
        settings->addWidget(override_group);
        profile_bar_ = new QWidget(parameters);
        auto* profile_buttons = new QHBoxLayout(profile_bar_);
        profile_buttons->setContentsMargins(0, 0, 0, 0);
        auto* load = new QPushButton(QStringLiteral("Load profile…"), profile_bar_);
        auto* save = new QPushButton(QStringLiteral("Save profile…"), profile_bar_);
        profile_buttons->addWidget(load);
        profile_buttons->addWidget(save);
        settings->addWidget(profile_bar_);
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
        panes->setChildrenCollapsible(false);
        panes->setSizes({355, 625});
        progress_ = new QProgressBar(this);
        progress_->setRange(0, 0);
        progress_->setTextVisible(false);
        progress_->setFixedHeight(4);
        progress_->hide();
        outer->addWidget(progress_);

        buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        auto* buttons = buttons_;
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Import channels"));
        buttons->button(QDialogButtonBox::Ok)->setProperty("role", "primary");
        apply_suggestions_ = buttons->addButton(QStringLiteral("Apply text suggestions"), QDialogButtonBox::ActionRole);
        auto* check = buttons->addButton(QStringLiteral("Preview parse"), QDialogButtonBox::ActionRole);
        outer->addWidget(buttons);
        connect(load, &QPushButton::clicked, this, [this] { choose_profile(); });
        connect(save, &QPushButton::clicked, this, [this] { store_profile(); });
        connect(apply_suggestions_, &QPushButton::clicked, this, [this] { apply_suggestions(); });
        connect(check, &QPushButton::clicked, this, [this] { preview_parse(false); });
        connect(buttons, &QDialogButtonBox::accepted, this, [this] { preview_parse(true); });
        connect(buttons, &QDialogButtonBox::rejected, this, &ParseDialog::reject);
        connect(format_, &QComboBox::currentIndexChanged, this, [this] { update_text_fields(); });
        connect(layout_, &QComboBox::currentIndexChanged, this, [this] { update_text_fields(); });
        for (auto* edit : parameters->findChildren<QLineEdit*>())
            connect(edit, &QLineEdit::textChanged, this, [this] { invalidate_preview(); });
        for (auto* combo : parameters->findChildren<QComboBox*>())
            connect(combo, &QComboBox::currentIndexChanged, this, [this] { invalidate_preview(); });
        connect(first_, &QLineEdit::editingFinished, this, [this] {
            show_raw(parse_index(as_utf8(first_->text())).value_or(0));
        });
        if (const auto* display = screen())
            resize(size().boundedTo(display->availableGeometry().size()));
        set_busy(true);
        feedback_->setPlainText(QStringLiteral("Inspecting the source file…"));
        QTimer::singleShot(0, this, [this] {
            run_background(this, [bytes = bytes_, extension = extension_] {
                return std::pair{text_view(bytes->bytes), detect_input_format(bytes->bytes, extension)};
            }, [this](const auto& task) {
                set_busy(false);
                if (!task.value) {
                    feedback_->setPlainText(task.error);
                    return;
                }
                raw_ = task.value->first;
                detected_ = task.value->second;
                update_text_fields();
                show_raw(0);
                preview_parse(false);
            });
        });
    }

    ParsedInput take_result() {
        return {used_, std::make_shared<ReadResult>(std::move(preview_->parsed)), std::move(preview_->plots)};
    }

    void reject() override {
        if (!busy_)
            QDialog::reject();
    }

private:
    static QLineEdit* line_field(QFormLayout* form, const QString& name, const QString& placeholder) {
        auto* edit = new QLineEdit;
        edit->setPlaceholderText(placeholder);
        form->addRow(name, edit);
        return edit;
    }

    void load_text_options(const TextParseOptions& options) {
        first_->setText(options.first_line ? QString::number(*options.first_line) : QString{});
        last_->setText(options.last_line ? QString::number(*options.last_line) : QString{});
        layout_->setCurrentIndex(options.layout ? (*options.layout == Layout::Sequential ? 1 : 2) : 0);
        delimiter_->setCurrentIndex(static_cast<int>(options.delimiter));
        time_->setText(options.time_column ? QString::number(*options.time_column) : QString{});
        factor_->setText(options.numeric_factor ? real(*options.numeric_factor) : QString{});
        QStringList columns;
        for (const auto column : options.data_columns)
            columns << QString::number(column);
        columns_->setText(columns.join(','));
    }

    void load_options(const ReadOptions& options) {
        format_->setCurrentIndex(format_->findData(static_cast<int>(options.format)));
        calibration_->setCurrentIndex(static_cast<int>(options.calibration));
        calibration_factor_->setText(options.calibration_factor ? real(*options.calibration_factor) : QString{});
        calibration_unit_->setCurrentIndex(
            options.calibration_unit ? calibration_unit_->findData(static_cast<int>(*options.calibration_unit))
                                     : 0);
        sac_units_->setChecked(options.sac_standard_units.value_or(false));
        load_text_options(options.text);
        overrides_->set_options(options.overrides);
        update_text_fields();
    }

    void update_text_fields() {
        auto format = static_cast<FormatId>(format_->currentData().toInt());
        if (format == FormatId::Auto)
            format = detected_;
        const bool generic = raw_ && is_generic_text(format);
        text_group_->setEnabled(raw_.has_value() && (generic || format == FormatId::At2 ||
                                                     format == FormatId::Dt2 || format == FormatId::Vt2));
        text_group_->setVisible(text_group_->isEnabled());
        sac_units_->setVisible(format == FormatId::Sac);
        profile_bar_->setVisible(generic);
        apply_suggestions_->setVisible(generic);
        const bool columnar = layout_->currentIndex() != 1;
        columns_->setEnabled(columnar);
        time_->setEnabled(columnar);
    }

    ReadOptions options() const {
        ReadOptions result;
        result.format = static_cast<FormatId>(format_->currentData().toInt());
        result.extension_hint = extension_;
        result.overrides = overrides_->options();
        result.calibration = static_cast<Calibration>(calibration_->currentIndex());
        if (result.calibration == Calibration::Manual) {
            result.calibration_factor =
                optional_number(calibration_factor_, "Physical units per count must be finite");
            if (calibration_unit_->currentData().isValid())
                result.calibration_unit = static_cast<Unit>(calibration_unit_->currentData().toInt());
        }
        if (!sac_units_->isHidden() && sac_units_->isChecked())
            result.sac_standard_units = true;
        if (factor_->isEnabled())
            result.text.numeric_factor = optional_number(factor_, "Numeric factor must be finite");
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
        return result;
    }

    void choose_profile() {
        const auto selected = QFileDialog::getOpenFileName(this, QStringLiteral("Load text profile"), {},
                                                           QStringLiteral("JSON profiles (*.json);;All files (*)"));
        if (selected.isEmpty())
            return;
        try {
            auto profile = load_profile(path_from_utf8(as_utf8(selected)));
            if (has_errors(profile.diagnostics)) {
                feedback_->setPlainText(diagnostic_text(profile.diagnostics));
                return;
            }
            // Loading a profile replaces the visible form, including empty fields.
            // Unlike CLI overlays, this cannot retain hidden settings from a prior profile.
            load_options(profile.options);
            preview_parse(false);
        } catch (const std::exception& problem) {
            feedback_->setPlainText(as_text(problem.what()));
        }
    }

    void store_profile() {
        QFileDialog dialog(this, QStringLiteral("Save text profile"));
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        dialog.setNameFilter(QStringLiteral("JSON profiles (*.json)"));
        dialog.setDefaultSuffix(QStringLiteral("json"));
        dialog.setOption(QFileDialog::DontConfirmOverwrite);
        if (dialog.exec() != QDialog::Accepted)
            return;
        try {
            const auto path = path_from_utf8(as_utf8(dialog.selectedFiles().front()));
            const bool overwrite = std::filesystem::exists(path);
            if (overwrite && QMessageBox::question(this, QStringLiteral("Replace profile"),
                    QStringLiteral("Replace the existing file %1?").arg(as_text(path_to_utf8(path)))) != QMessageBox::Yes)
                return;
            const auto diagnostics = save_profile(path, options(), overwrite, input_);
            if (has_errors(diagnostics))
                feedback_->setPlainText(diagnostic_text(diagnostics));
            else
                feedback_->appendPlainText(QStringLiteral("Saved profile %1").arg(as_text(path_to_utf8(path))));
        } catch (const std::exception& problem) {
            feedback_->setPlainText(as_text(problem.what()));
        }
    }

    static bool applicable(const TextSuggestion& suggestions) {
        const auto& proposed = suggestions.options;
        return proposed.text.first_line || proposed.text.layout || proposed.text.time_column ||
               proposed.overrides.quantity || proposed.overrides.unit;
    }

    void apply_suggestions() {
        try {
            const auto active = options();
            const auto suggestions = preview_ ? preview_->suggestions : TextSuggestion{};
            if (applicable(suggestions))
                load_options(merge_profile(suggestions.options, active).options);
            preview_parse(false);
        } catch (const std::exception& problem) {
            feedback_->setPlainText(as_text(problem.what()));
        }
    }

    void show_raw(std::size_t highlight) {
        if (!raw_) {
            raw_label_->setText(QStringLiteral("Record headers"));
            QString headers;
            if (preview_) {
                for (const auto& trace : preview_->parsed.data.traces) {
                    headers += as_text(trace.name) + "\n";
                    if (trace.start_time)
                        headers += as_text(timestamp(*trace.start_time)) + "\n";
                    for (const auto* key : {"modified.dimension", "modified.sensitivity", "calibration.factor",
                                            "mseed.encoding", "sac.reference_utc"})
                        if (trace.attributes.contains(key))
                            headers += as_text(key) + " = " + as_text(trace.attributes.at(key)) + "\n";
                    headers += "\n";
                }
            }
            raw_view_->setPlainText(headers.isEmpty()
                                        ? QStringLiteral("Preview the file to inspect decoded headers.")
                                        : headers.trimmed());
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

    void set_busy(bool busy) {
        busy_ = busy;
        parameters_->setEnabled(!busy);
        buttons_->setEnabled(!busy);
        progress_->setVisible(busy);
    }

    void invalidate_preview() {
        preview_.reset();
        if (!raw_)
            show_raw(0);
        apply_suggestions_->setEnabled(false);
        feedback_->setPlainText(QStringLiteral("Settings changed. Choose Preview parse or Import channels to apply them."));
    }

    void preview_parse(bool confirm) {
        if (busy_)
            return;
        if (confirm && preview_ && !has_errors(preview_->parsed.diagnostics)) {
            accept();
            return;
        }
        try {
            used_ = options();
            preview_.reset();
            set_busy(true);
            feedback_->setPlainText(QStringLiteral("Parsing records and preparing waveforms…"));
            run_background(this, [bytes = bytes_, options = used_, confirm] {
                ParsePreview result;
                result.parsed = read(bytes->bytes, options);
                if (!confirm && is_generic_text(result.parsed.data.source_format)) {
                    auto active = options;
                    active.format = result.parsed.data.source_format;
                    result.suggestions = suggest_text(bytes->bytes, active);
                }
                if (!has_errors(result.parsed.diagnostics)) {
                    for (const auto& trace : result.parsed.data.traces)
                        result.plots.push_back(prepare_plot(trace));
                }
                return result;
            }, [this, confirm](const auto& task) {
                set_busy(false);
                if (!task.value) {
                    feedback_->setPlainText(task.error);
                    return;
                }
                preview_ = task.value;
                const auto& parsed = preview_->parsed;
                const auto& suggestions = preview_->suggestions;
                auto description = diagnostic_text(parsed.diagnostics);
                if (!has_errors(parsed.diagnostics)) {
                    QString summary = QStringLiteral("Ready · %1 · %2 channel(s)")
                        .arg(as_text(name(parsed.data.source_format))).arg(parsed.data.traces.size());
                    for (std::size_t i = 0; i < parsed.data.traces.size(); ++i) {
                        const auto& trace = parsed.data.traces[i];
                        summary += '\n' + trace_title(trace, i + 1) + QStringLiteral(" — ") + timing_unit(trace);
                    }
                    description = summary + (description.isEmpty() ? QString{} : '\n' + description);
                }
                apply_suggestions_->setEnabled(applicable(suggestions));
                for (const auto& evidence : suggestions.evidence)
                    description += QStringLiteral("\nSuggestion · ") + as_text(evidence);
                if (!suggestions.diagnostics.empty())
                    description += '\n' + diagnostic_text(suggestions.diagnostics);
                feedback_->setPlainText(description);
                show_raw(used_.text.first_line.value_or(suggestions.options.text.first_line.value_or(parsed.first_data_line)));
                if (confirm && !has_errors(parsed.diagnostics))
                    accept();
            });
        } catch (const std::exception& problem) {
            set_busy(false);
            feedback_->setPlainText(as_text(problem.what()));
        }
    }

    std::shared_ptr<const InputFile> bytes_;
    std::filesystem::path input_;
    std::string extension_;
    std::optional<std::string_view> raw_;
    FormatId detected_{FormatId::Auto};
    ReadOptions used_;
    std::shared_ptr<ParsePreview> preview_;
    bool busy_{};
    QWidget* parameters_{};
    QDialogButtonBox* buttons_{};
    QProgressBar* progress_{};
    QComboBox* format_{};
    QGroupBox* text_group_{};
    QComboBox* layout_{};
    QComboBox* delimiter_{};
    QLineEdit* first_{};
    QLineEdit* last_{};
    QLineEdit* columns_{};
    QLineEdit* time_{};
    QLineEdit* factor_{};
    QComboBox* calibration_{};
    QComboBox* calibration_unit_{};
    QLineEdit* calibration_factor_{};
    QCheckBox* sac_units_{};
    std::unique_ptr<OverrideFields> overrides_;
    QLabel* raw_label_{};
    QPlainTextEdit* raw_view_{};
    QPlainTextEdit* feedback_{};
    QWidget* profile_bar_{};
    QPushButton* apply_suggestions_{};
};

} // namespace

std::optional<ParsedInput> interpret_input(std::shared_ptr<const InputFile> input,
    const std::filesystem::path& path, const ReadOptions& original, QWidget* parent) {
    ParseDialog dialog(std::move(input), path, original, parent);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    return dialog.take_result();
}

} // namespace gmtools::gui
