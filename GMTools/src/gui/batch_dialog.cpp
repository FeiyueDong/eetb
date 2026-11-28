#include "batch_dialog.hpp"

#include "background_task.hpp"
#include "export_options.hpp"
#include "preset_dialog.hpp"
#include "preset_store.hpp"
#include "ui_helpers.hpp"

#include <QCheckBox>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QRegularExpression>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <atomic>
#include <set>

namespace gmtools::gui {
namespace {

std::filesystem::path as_path(const QString& value) {
    const auto utf8 = value.toUtf8();
    return gmtools::path_from_utf8(std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
}

QString state_text(BatchFileState state) {
    switch (state) {
    case BatchFileState::Pending: return BatchDialog::tr("Pending");
    case BatchFileState::Running: return BatchDialog::tr("Converting…");
    case BatchFileState::Succeeded: return BatchDialog::tr("Succeeded");
    case BatchFileState::Failed: return BatchDialog::tr("Failed");
    case BatchFileState::Partial: return BatchDialog::tr("Partial");
    case BatchFileState::Cancelled: return BatchDialog::tr("Cancelled");
    }
    return {};
}

bool matches_pattern(const QString& pattern, const QString& name) {
    auto escaped = QRegularExpression::escape(pattern);
    escaped.replace(QStringLiteral("\\*"), QStringLiteral(".*"));
    escaped.replace(QStringLiteral("\\?"), QStringLiteral("."));
    return QRegularExpression(QRegularExpression::anchoredPattern(escaped)).match(name).hasMatch();
}

QString failure_text(const BatchFileResult& file) {
    for (const auto& diagnostic : file.diagnostics) {
        if (diagnostic.severity == Severity::Error)
            return QString::fromStdString(describe(diagnostic));
    }
    return {};
}

} // namespace

BatchDialog::BatchDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Batch conversion"));
    resize(1120, 760);
    setAcceptDrops(true);

    table_ = new QTableWidget(0, 3, this);
    table_->setHorizontalHeaderLabels({tr("Input file"), tr("Status"), tr("Details")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);
    connect(table_, &QTableWidget::itemSelectionChanged, this, &BatchDialog::refresh_ready);

    add_files_ = new QPushButton(tr("Add files…"), this);
    add_directory_ = new QPushButton(tr("Add directory…"), this);
    remove_ = new QPushButton(tr("Remove"), this);
    clear_ = new QPushButton(tr("Clear"), this);
    connect(add_files_, &QPushButton::clicked, this, &BatchDialog::choose_files);
    connect(add_directory_, &QPushButton::clicked, this, &BatchDialog::choose_directory);
    connect(remove_, &QPushButton::clicked, this, &BatchDialog::remove_selected);
    connect(clear_, &QPushButton::clicked, this, &BatchDialog::clear_files);

    auto* file_buttons = new QHBoxLayout;
    file_buttons->addWidget(add_files_);
    file_buttons->addWidget(add_directory_);
    file_buttons->addStretch();
    file_buttons->addWidget(remove_);
    file_buttons->addWidget(clear_);

    preset_ = new QComboBox(this);
    preset_->setMinimumWidth(220);
    auto* manage = new QPushButton(tr("Manage presets…"), this);
    connect(preset_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index <= 0) {
            if (!running_)
                writer_touched_ = true;
            return;
        }
        const auto file = preset_->itemData(index).toString();
        const auto loaded = gmtools::load_preset(as_path(file));
        if (gmtools::has_errors(loaded.diagnostics)) {
            QMessageBox::warning(this, tr("Preset cannot be loaded"),
                                 QString::fromStdString(describe(loaded.diagnostics.front())));
            preset_->setCurrentIndex(0);
            return;
        }
        apply_preset(loaded.preset);
    });
    connect(manage, &QPushButton::clicked, this, [this] {
        PresetDialog dialog([this] { return current_preset(); }, this);
        if (dialog.exec() == QDialog::Accepted) {
            if (const auto selected = dialog.take_selected()) {
                reload_presets();
                apply_preset(*selected);
                for (int i = 0; i < preset_->count(); ++i) {
                    if (preset_name(preset_->itemData(i).toString()) == QString::fromStdString(selected->name)) {
                        preset_->setCurrentIndex(i);
                        break;
                    }
                }
            } else {
                reload_presets();
            }
        }
    });

    auto* preset_row = new QHBoxLayout;
    preset_row->addWidget(new QLabel(tr("Preset"), this));
    preset_row->addWidget(preset_, 1);
    preset_row->addWidget(manage);

    // ---- Input parameters -------------------------------------------------
    format_ = new QComboBox(this);
    format_->addItem(tr("Automatic"), static_cast<int>(FormatId::Auto));
    for (const auto& entry : formats()) {
        if (entry.readable)
            format_->addItem(as_text(entry.name), static_cast<int>(entry.id));
    }
    layout_ = new QComboBox(this);
    layout_->addItem(tr("Format default"), -1);
    layout_->addItem(tr("Sequential"), static_cast<int>(Layout::Sequential));
    layout_->addItem(tr("Columnar"), static_cast<int>(Layout::Columnar));
    delimiter_ = new QComboBox(this);
    delimiter_->addItem(tr("Automatic"), static_cast<int>(Delimiter::Auto));
    delimiter_->addItem(tr("Whitespace"), static_cast<int>(Delimiter::Whitespace));
    delimiter_->addItem(tr("Comma"), static_cast<int>(Delimiter::Comma));
    delimiter_->addItem(tr("Tab"), static_cast<int>(Delimiter::Tab));
    first_line_ = new QLineEdit(this);
    last_line_ = new QLineEdit(this);
    data_columns_ = new QLineEdit(this);
    data_columns_->setPlaceholderText(tr("All except time, e.g. 2,3"));
    time_column_ = new QLineEdit(this);
    time_column_->setPlaceholderText(tr("Values in seconds"));
    numeric_factor_ = new QLineEdit(this);
    calibration_ = new QComboBox(this);
    calibration_->addItem(tr("Format default"), static_cast<int>(Calibration::Default));
    calibration_->addItem(tr("Raw counts"), static_cast<int>(Calibration::Raw));
    calibration_->addItem(tr("Use file calibration"), static_cast<int>(Calibration::Source));
    calibration_->addItem(tr("Manual units per count"), static_cast<int>(Calibration::Manual));
    calibration_factor_ = new QLineEdit(this);
    calibration_unit_ = new QComboBox(this);
    calibration_unit_->addItem(tr("Unchanged"), -1);
    for (auto unit : units()) {
        if (unit == Unit::Unknown)
            continue;
        calibration_unit_->addItem(as_text(name(unit)), static_cast<int>(unit));
    }
    sac_units_ = new QCheckBox(tr("Interpret SAC IDEP using standard nanounits"), this);

    auto* input_group = new QGroupBox(tr("Input parameters"), this);
    auto* input_form = new QFormLayout(input_group);
    input_form->addRow(tr("Input format"), format_);
    input_form->addRow(tr("First data line"), first_line_);
    input_form->addRow(tr("Last data line"), last_line_);
    input_form->addRow(tr("Layout"), layout_);
    input_form->addRow(tr("Delimiter"), delimiter_);
    input_form->addRow(tr("Data columns"), data_columns_);
    input_form->addRow(tr("Time column"), time_column_);
    input_form->addRow(tr("Numeric factor"), numeric_factor_);
    input_form->addRow(tr("Calibration"), calibration_);
    input_form->addRow(tr("Units per count"), calibration_factor_);
    input_form->addRow(tr("Calibrated unit"), calibration_unit_);
    input_form->addRow(QString{}, sac_units_);
    overrides_ = new OverrideFields(input_form, {}, tr("From source"));

    // ---- Channel selection ------------------------------------------------
    select_mode_ = new QComboBox(this);
    select_mode_->addItem(tr("All channels"), static_cast<int>(ChannelSelectionMode::All));
    select_mode_->addItem(tr("Matching quantity"), static_cast<int>(ChannelSelectionMode::Quantity));
    select_quantity_ = new QComboBox(this);
    for (const auto quantity : {Quantity::Acceleration, Quantity::Velocity, Quantity::Displacement, Quantity::Counts})
        select_quantity_->addItem(as_text(name(quantity)), static_cast<int>(quantity));
    connect(select_mode_, &QComboBox::currentIndexChanged, this, &BatchDialog::update_selection_mode);
    auto* selection_group = new QGroupBox(tr("Channels"), this);
    auto* selection_form = new QFormLayout(selection_group);
    selection_form->addRow(tr("Export"), select_mode_);
    selection_form->addRow(tr("Quantity"), select_quantity_);

    // ---- Output -----------------------------------------------------------
    target_ = new QComboBox(this);
    for (const auto& entry : formats()) {
        if (!entry.writable)
            continue;
        const auto label = QString::fromStdString(std::string{entry.name}).toUpper() +
                           (std::string_view{entry.notes}.starts_with("experimental:") ? tr(" · experimental") : QString{});
        target_->addItem(label, static_cast<int>(entry.id));
    }
    if (const auto index = target_->findData(static_cast<int>(FormatId::Csv)); index >= 0)
        target_->setCurrentIndex(index);
    connect(target_, &QComboBox::currentIndexChanged, this, &BatchDialog::update_format);
    output_unit_ = new QComboBox(this);
    output_unit_->addItem(tr("Keep input unit"), -1);
    for (auto unit : units()) {
        if (unit == Unit::Unknown)
            continue;
        output_unit_->addItem(as_text(name(unit)), static_cast<int>(unit));
    }
    format_options_ = new QPushButton(tr("Format options…"), this);
    connect(format_options_, &QPushButton::clicked, this, &BatchDialog::choose_writer_options);
    output_directory_ = new QLineEdit(this);
    output_directory_->setReadOnly(true);
    auto* browse = new QPushButton(tr("Browse…"), this);
    connect(browse, &QPushButton::clicked, this, &BatchDialog::choose_output_directory);
    overwrite_ = new QCheckBox(tr("Replace existing output files"), this);

    auto* output_group = new QGroupBox(tr("Output"), this);
    auto* output_form = new QFormLayout(output_group);
    output_form->addRow(tr("Target format"), target_);
    output_form->addRow(tr("Output unit"), output_unit_);
    output_form->addRow(QString{}, format_options_);
    auto* directory_row = new QHBoxLayout;
    directory_row->addWidget(output_directory_, 1);
    directory_row->addWidget(browse);
    output_form->addRow(tr("Output directory"), directory_row);
    output_form->addRow(QString{}, overwrite_);

    auto* form = new QVBoxLayout;
    form->addWidget(input_group);
    form->addWidget(selection_group);
    form->addWidget(output_group);
    form->addStretch();
    auto* form_host = new QWidget;
    form_host->setLayout(form);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(form_host);
    scroll->setMinimumWidth(430);

    auto* left = new QVBoxLayout;
    left->addWidget(table_, 1);
    left->addLayout(file_buttons);
    auto* body = new QHBoxLayout;
    body->addLayout(left, 1);
    body->addWidget(scroll);

    progress_ = new QProgressBar(this);
    progress_->setRange(0, 1);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    status_ = new QLabel(tr("Ready."), this);
    status_->setProperty("role", "muted");
    summary_ = new QLabel(this);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    details_ = read_only_text(this);
    details_->setMaximumHeight(130);
    details_->setVisible(false);

    start_ = new QPushButton(tr("Start conversion"), this);
    start_->setProperty("role", "primary");
    stop_ = new QPushButton(tr("Stop"), this);
    stop_->setEnabled(false);
    auto* close = new QPushButton(tr("Close"), this);
    connect(start_, &QPushButton::clicked, this, &BatchDialog::start);
    connect(stop_, &QPushButton::clicked, this, &BatchDialog::request_stop);
    connect(close, &QPushButton::clicked, this, &QDialog::close);

    auto* footer = new QHBoxLayout;
    footer->addWidget(status_, 1);
    footer->addWidget(start_);
    footer->addWidget(stop_);
    footer->addWidget(close);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(preset_row);
    layout->addLayout(body, 1);
    layout->addWidget(progress_);
    layout->addWidget(summary_);
    layout->addWidget(details_);
    layout->addLayout(footer);

    for (auto* combo : {format_, layout_, delimiter_, calibration_, select_mode_, select_quantity_, target_})
        connect(combo, &QComboBox::currentIndexChanged, this, [this] {
            if (!running_)
                writer_touched_ = true;
            refresh_ready();
        });
    for (auto* edit : {first_line_, last_line_, data_columns_, time_column_, numeric_factor_, calibration_factor_})
        connect(edit, &QLineEdit::textChanged, this, [this] { refresh_ready(); });

    update_selection_mode();
    update_format();
    reload_presets();
    refresh_ready();
}

ConversionPreset BatchDialog::current_preset() const {
    ConversionPreset preset;
    auto& read = preset.read;
    read.format = static_cast<FormatId>(format_->currentData().toInt());
    if (const auto index = static_cast<int>(layout_->currentData().toInt()); index >= 0)
        read.text.layout = static_cast<Layout>(index);
    read.text.delimiter = static_cast<Delimiter>(delimiter_->currentData().toInt());
    read.text.first_line = optional_index(first_line_, "first line");
    read.text.last_line = optional_index(last_line_, "last line");
    read.text.time_column = optional_index(time_column_, "time column");
    read.text.numeric_factor = optional_number(numeric_factor_, "numeric factor");
    if (!data_columns_->text().trimmed().isEmpty())
        read.text.data_columns = require(parse_indices(as_utf8(data_columns_->text())), "invalid data columns");
    read.overrides = overrides_->options();
    read.calibration = static_cast<Calibration>(calibration_->currentData().toInt());
    read.calibration_factor = optional_number(calibration_factor_, "calibration factor");
    if (calibration_unit_->currentData().toInt() >= 0)
        read.calibration_unit = static_cast<Unit>(calibration_unit_->currentData().toInt());
    if (sac_units_->isChecked())
        read.sac_standard_units = true;

    preset.write = writer_;
    preset.write.format = static_cast<FormatId>(target_->currentData().toInt());
    if (output_unit_->currentData().toInt() >= 0)
        preset.write.output_unit = static_cast<Unit>(output_unit_->currentData().toInt());
    else
        preset.write.output_unit.reset();

    preset.selection.mode = static_cast<ChannelSelectionMode>(select_mode_->currentData().toInt());
    preset.selection.quantity = static_cast<Quantity>(select_quantity_->currentData().toInt());
    return preset;
}

void BatchDialog::apply_preset(const ConversionPreset& preset) {
    const auto index_of = [](QComboBox* combo, int value) {
        const auto index = combo->findData(value);
        if (index >= 0)
            combo->setCurrentIndex(index);
    };
    index_of(format_, static_cast<int>(preset.read.format));
    index_of(layout_, preset.read.text.layout ? static_cast<int>(*preset.read.text.layout) : -1);
    index_of(delimiter_, static_cast<int>(preset.read.text.delimiter));
    first_line_->setText(preset.read.text.first_line ? QString::number(*preset.read.text.first_line) : QString{});
    last_line_->setText(preset.read.text.last_line ? QString::number(*preset.read.text.last_line) : QString{});
    time_column_->setText(preset.read.text.time_column ? QString::number(*preset.read.text.time_column) : QString{});
    numeric_factor_->setText(preset.read.text.numeric_factor ? real(*preset.read.text.numeric_factor) : QString{});
    QStringList columns;
    for (const auto column : preset.read.text.data_columns)
        columns.push_back(QString::number(column));
    data_columns_->setText(columns.join(QLatin1Char(',')));
    overrides_->set_options(preset.read.overrides);
    index_of(calibration_, static_cast<int>(preset.read.calibration));
    calibration_factor_->setText(preset.read.calibration_factor ? real(*preset.read.calibration_factor) : QString{});
    calibration_unit_->setCurrentIndex(preset.read.calibration_unit
                                           ? std::max(0, calibration_unit_->findData(static_cast<int>(*preset.read.calibration_unit)))
                                           : 0);
    sac_units_->setChecked(preset.read.sac_standard_units.value_or(false));

    writer_ = preset.write;
    index_of(target_, static_cast<int>(preset.write.format));
    output_unit_->setCurrentIndex(preset.write.output_unit
                                      ? std::max(0, output_unit_->findData(static_cast<int>(*preset.write.output_unit)))
                                      : 0);
    index_of(select_mode_, static_cast<int>(preset.selection.mode));
    index_of(select_quantity_, static_cast<int>(preset.selection.quantity));
    writer_touched_ = true;
    update_selection_mode();
    update_format();
    refresh_ready();
}

void BatchDialog::reload_presets() {
    const auto current = preset_->currentData().toString();
    preset_->blockSignals(true);
    preset_->clear();
    preset_->addItem(tr("Custom settings"), QString{});
    for (const auto& file : preset_files()) {
        preset_->addItem(preset_name(file), file);
        preset_->setItemData(preset_->count() - 1, preset_tooltip(file), Qt::ToolTipRole);
    }
    const auto index = current.isEmpty() ? 0 : preset_->findData(current);
    preset_->setCurrentIndex(index >= 0 ? index : 0);
    preset_->blockSignals(false);
}

void BatchDialog::choose_files() {
    const auto files = QFileDialog::getOpenFileNames(this, tr("Add input files"), {});
    add_paths(files);
}

void BatchDialog::choose_directory() {
    const auto directory = QFileDialog::getExistingDirectory(this, tr("Add a directory"));
    if (directory.isEmpty())
        return;
    bool accepted = false;
    const auto pattern = QInputDialog::getText(this, tr("Directory filter"), tr("Filename pattern:"),
                                               QLineEdit::Normal, QStringLiteral("*"), &accepted);
    if (!accepted || pattern.trimmed().isEmpty())
        return;
    QStringList matches;
    const QDir dir(directory);
    for (const auto& name : dir.entryList(QDir::Files, QDir::Name)) {
        if (matches_pattern(pattern.trimmed(), name))
            matches.push_back(dir.filePath(name));
    }
    if (matches.isEmpty()) {
        QMessageBox::information(this, tr("Add a directory"), tr("No files match this pattern."));
        return;
    }
    if (QMessageBox::question(this, tr("Add a directory"),
                              tr("Add %1 file(s) from\n%2?").arg(matches.size()).arg(directory)) != QMessageBox::Yes)
        return;
    add_paths(matches);
}

void BatchDialog::add_paths(const QStringList& files) {
    std::set<std::filesystem::path> known(inputs_.begin(), inputs_.end());
    std::size_t added = 0;
    for (const auto& file : files) {
        const auto path = as_path(file);
        if (!known.insert(path).second)
            continue;
        inputs_.push_back(path);
        const auto row = table_->rowCount();
        table_->insertRow(row);
        auto* item = new QTableWidgetItem(QDir::toNativeSeparators(file));
        item->setToolTip(file);
        table_->setItem(row, 0, item);
        table_->setItem(row, 1, new QTableWidgetItem(state_text(BatchFileState::Pending)));
        table_->setItem(row, 2, new QTableWidgetItem(QString{}));
        ++added;
    }
    if (added < static_cast<std::size_t>(files.size()))
        status_->setText(tr("Skipped %1 duplicate input(s).").arg(files.size() - static_cast<int>(added)));
    else
        status_->setText(tr("Added %1 file(s).").arg(added));
    refresh_ready();
}

void BatchDialog::remove_selected() {
    auto rows = table_->selectionModel() ? table_->selectionModel()->selectedRows() : QModelIndexList{};
    std::vector<int> indices;
    for (const auto& index : rows)
        indices.push_back(index.row());
    std::ranges::sort(indices, std::greater<>{});
    for (const auto row : indices) {
        if (row < 0 || row >= static_cast<int>(inputs_.size()))
            continue;
        inputs_.erase(inputs_.begin() + row);
        table_->removeRow(row);
    }
    refresh_ready();
}

void BatchDialog::clear_files() {
    inputs_.clear();
    table_->setRowCount(0);
    refresh_ready();
}

void BatchDialog::choose_output_directory() {
    const auto directory = QFileDialog::getExistingDirectory(this, tr("Choose the output directory"),
                                                             output_directory_->text());
    if (!directory.isEmpty())
        output_directory_->setText(directory);
    refresh_ready();
}

void BatchDialog::choose_writer_options() {
    writer_.format = static_cast<FormatId>(target_->currentData().toInt());
    if (gmtools::gui::choose_writer_options(writer_, this))
        writer_touched_ = true;
}

void BatchDialog::update_format() {
    const auto format = static_cast<FormatId>(target_->currentData().toInt());
    if (writer_.format != format) {
        const auto output_unit = writer_.output_unit;
        writer_ = WriteOptions{};
        writer_.format = format;
        writer_.output_unit = output_unit;
    }
    const bool text = is_generic_text(format);
    const bool sac = format == FormatId::Sac;
    format_options_->setEnabled(text || sac || format == FormatId::Mseed || format == FormatId::Mseed2);
    // PEER targets fix their engineering unit; keep the unit control consistent.
    if (format == FormatId::At2 || format == FormatId::Vt2 || format == FormatId::Dt2) {
        const auto target_unit = format == FormatId::At2 ? Unit::G
                                : format == FormatId::Vt2 ? Unit::Cmps
                                                          : Unit::Cm;
        output_unit_->setCurrentIndex(std::max(0, output_unit_->findData(static_cast<int>(target_unit))));
    }
    refresh_ready();
}

void BatchDialog::update_selection_mode() {
    const auto mode = static_cast<ChannelSelectionMode>(select_mode_->currentData().toInt());
    select_quantity_->setEnabled(mode == ChannelSelectionMode::Quantity);
}

void BatchDialog::set_running(bool running) {
    running_ = running;
    for (auto* widget : std::initializer_list<QWidget*>{add_files_, add_directory_, remove_, clear_, preset_,
                                                        format_, layout_, delimiter_, calibration_, target_,
                                                        output_unit_, format_options_, sac_units_, overwrite_})
        widget->setEnabled(!running);
    for (auto* edit : {first_line_, last_line_, data_columns_, time_column_, numeric_factor_, calibration_factor_})
        edit->setEnabled(!running);
    overrides_->set_enabled(!running);
    select_mode_->setEnabled(!running);
    select_quantity_->setEnabled(!running && static_cast<ChannelSelectionMode>(select_mode_->currentData().toInt()) ==
                                                ChannelSelectionMode::Quantity);
    start_->setEnabled(!running);
    stop_->setEnabled(running);
}

void BatchDialog::refresh_ready() {
    start_->setEnabled(!running_ && !inputs_.empty() && !output_directory_->text().isEmpty());
}

void BatchDialog::start() {
    if (running_ || inputs_.empty())
        return;
    const auto directory = output_directory_->text().trimmed();
    if (directory.isEmpty())
        return;
    BatchOptions options;
    try {
        options.preset = current_preset();
    } catch (const std::exception& problem) {
        QMessageBox::warning(this, tr("Incomplete settings"), QString::fromUtf8(problem.what()));
        return;
    }
    if (options.preset.write.format == FormatId::Auto) {
        QMessageBox::warning(this, tr("Incomplete settings"), tr("Choose a target format."));
        return;
    }
    options.inputs = inputs_;
    options.output_directory = as_path(directory);
    options.overwrite = overwrite_->isChecked();
    if (QMessageBox::question(this, tr("Start batch conversion"),
                              tr("Convert %1 file(s) into\n%2\n\n"
                                 "Output names are derived from each input file. "
                                 "Source files are never replaced.")
                                  .arg(inputs_.size())
                                  .arg(QDir::toNativeSeparators(directory))) != QMessageBox::Yes)
        return;

    stop_flag_ = std::make_shared<std::atomic<bool>>(false);
    states_.assign(inputs_.size(), BatchFileState::Pending);
    shown_states_.assign(inputs_.size(), BatchFileState::Pending);
    for (int row = 0; row < table_->rowCount(); ++row) {
        table_->item(row, 1)->setText(state_text(BatchFileState::Pending));
        table_->item(row, 2)->setText(QString{});
    }
    details_->clear();
    details_->setVisible(false);
    summary_->clear();
    progress_->setRange(0, static_cast<int>(inputs_.size()));
    progress_->setValue(0);
    status_->setText(tr("Starting…"));
    set_running(true);

    QPointer<BatchDialog> self(this);
    auto progress = [self](const BatchResult& snapshot) {
        auto copy = std::make_shared<BatchResult>(snapshot);
        if (!self)
            return;
        QMetaObject::invokeMethod(
            self.data(), [self, copy] { if (self) self->show_progress(*copy); },
            Qt::QueuedConnection);
    };
    auto stop = stop_flag_;
    run_background(this,
                   [options, progress, stop] {
                       return convert_batch(options, progress, [stop] { return stop->load(); });
                   },
                   [this, self](TaskResult<BatchResult> result) {
                       if (!self)
                           return;
                       if (!result.error.isEmpty()) {
                           set_running(false);
                           status_->setText(tr("Batch conversion failed."));
                           QMessageBox::critical(this, tr("Batch conversion failed"), result.error);
                           return;
                       }
                       if (result.value)
                           finished(*result.value);
                   });
}

void BatchDialog::request_stop() {
    if (!stop_flag_)
        return;
    stop_flag_->store(true);
    status_->setText(tr("Stopping after the current file…"));
}

void BatchDialog::show_progress(const BatchResult& result) {
    std::size_t done = 0;
    for (std::size_t i = 0; i < result.files.size() && i < static_cast<std::size_t>(table_->rowCount()); ++i) {
        const auto& file = result.files[i];
        const bool changed = i >= shown_states_.size() || shown_states_[i] != file.state;
        states_[i] = file.state;
        if (changed) {
            if (i < shown_states_.size())
                shown_states_[i] = file.state;
            table_->item(static_cast<int>(i), 1)->setText(state_text(file.state));
        }
        const auto failure = failure_text(file);
        if (changed && !failure.isEmpty())
            table_->item(static_cast<int>(i), 2)->setText(failure);
        else if (changed && !file.written.empty())
            table_->item(static_cast<int>(i), 2)->setText(
                tr("%1 file(s) written").arg(file.written.size()));
        if (file.state != BatchFileState::Pending && file.state != BatchFileState::Running)
            ++done;
        else if (file.state == BatchFileState::Running)
            status_->setText(tr("Converting %1 (%2/%3)…")
                                 .arg(QDir::toNativeSeparators(QString::fromStdString(path_to_utf8(file.input))))
                                 .arg(i + 1)
                                 .arg(result.files.size()));
    }
    progress_->setValue(static_cast<int>(done));
}

void BatchDialog::finished(const BatchResult& result) {
    set_running(false);
    show_progress(result);
    const auto& summary = result.summary;
    if (has_errors(result.diagnostics)) {
        // A batch-level problem stopped the run before any file was read: report
        // it as "not started", not as a finished batch with zero failures.
        status_->setText(tr("Batch was not started."));
        summary_->setText(tr("Not started · %1 problem(s)").arg(result.diagnostics.size()));
        details_->setPlainText(diagnostic_text(result.diagnostics).trimmed());
        details_->setVisible(true);
        return;
    }
    summary_->setText(tr("%1 succeeded · %2 partial · %3 failed · %4 cancelled · %5 file(s) written")
                          .arg(summary.succeeded)
                          .arg(summary.partial)
                          .arg(summary.failed)
                          .arg(summary.cancelled)
                          .arg(summary.files_written));
    QString text;
    for (const auto& file : result.files) {
        if (file.state == BatchFileState::Succeeded && file.diagnostics.empty())
            continue;
        text += QDir::toNativeSeparators(QString::fromStdString(path_to_utf8(file.input))) + QLatin1Char('\n');
        text += diagnostic_text(file.diagnostics).trimmed() + QLatin1Char('\n');
    }
    details_->setPlainText(text.trimmed());
    details_->setVisible(!text.trimmed().isEmpty());
    status_->setText(summary.stopped ? tr("Stopped after the current file.") : tr("Batch finished."));
}

void BatchDialog::keyPressEvent(QKeyEvent* event) {
    if (running_ && event->key() == Qt::Key_Escape) {
        request_stop();
        return;
    }
    QDialog::keyPressEvent(event);
}

void BatchDialog::dragEnterEvent(QDragEnterEvent* event) {
    if (running_ || !event->mimeData()->hasUrls())
        return;
    event->acceptProposedAction();
}

void BatchDialog::dropEvent(QDropEvent* event) {
    if (running_)
        return;
    QStringList files;
    for (const auto& url : event->mimeData()->urls()) {
        if (!url.isLocalFile())
            continue;
        const QFileInfo info(url.toLocalFile());
        if (info.isDir()) {
            for (const auto& name : QDir(info.absoluteFilePath()).entryList(QDir::Files, QDir::Name))
                files.push_back(QDir(info.absoluteFilePath()).filePath(name));
        } else {
            files.push_back(info.absoluteFilePath());
        }
    }
    if (files.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Add dropped files"),
            tr("Add %1 file(s) to this batch?").arg(files.size())) != QMessageBox::Yes)
        return;
    add_paths(files);
    event->acceptProposedAction();
}

void BatchDialog::closeEvent(QCloseEvent* event) {
    if (running_) {
        status_->setText(tr("Stop the batch before closing this window."));
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

} // namespace gmtools::gui
