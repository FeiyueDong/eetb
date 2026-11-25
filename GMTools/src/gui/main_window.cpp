#include "main_window.hpp"
#include "background_task.hpp"
#include "export_options.hpp"
#include "override_fields.hpp"
#include "ui_helpers.hpp"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace gmtools::gui {
namespace {

QLabel* label(const QString& text, const char* role, QWidget* parent) {
    auto* result = new QLabel(text, parent);
    result->setProperty("role", role);
    result->setTextFormat(Qt::PlainText);
    result->setWordWrap(true);
    return result;
}

QFrame* card(QWidget* parent) {
    auto* result = new QFrame(parent);
    result->setProperty("role", "card");
    return result;
}

QToolButton* action_button(QAction* action, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setDefaultAction(action);
    return button;
}

QString channel_label(const Trace& trace, std::size_t channel) {
    return trace_title(trace, channel) + QStringLiteral("\n%1 samples · %2")
        .arg(trace.samples.size()).arg(as_text(name(trace.unit)));
}

} // namespace

MainWindow::MainWindow() {
    setWindowTitle(QStringLiteral("GMTools · Ground motion converter"));
    resize(1280, 820);
    setAcceptDrops(true);

    open_action_ = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), QStringLiteral("Open file…"), this);
    open_action_->setShortcut(QKeySequence::Open);
    reparse_action_ = new QAction(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("Import settings…"), this);
    export_action_ = new QAction(QStringLiteral("Export checked channels"), this);
    export_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    addActions({open_action_, reparse_action_, export_action_});
    connect(open_action_, &QAction::triggered, this, &MainWindow::choose_file);
    connect(reparse_action_, &QAction::triggered, this, &MainWindow::reparse);
    connect(export_action_, &QAction::triggered, this, &MainWindow::send_export);

    auto* root = new QWidget(this);
    setCentralWidget(root);
    auto* outer = new QVBoxLayout(root);
    outer->setContentsMargins(20, 16, 20, 8);
    outer->setSpacing(16);
    auto* header = new QHBoxLayout;
    auto* brand = new QVBoxLayout;
    brand->setSpacing(3);
    brand->addWidget(label(QStringLiteral("GMTools"), "heading", root));
    brand->addWidget(label(QStringLiteral("Ground motion converter"), "muted", root));
    header->addLayout(brand);
    header->addStretch();
    header->addWidget(action_button(reparse_action_, root));
    header->addWidget(action_button(open_action_, root));
    outer->addLayout(header);

    workspace_ = new QStackedWidget(root);
    outer->addWidget(workspace_, 1);
    auto* empty = new QWidget(workspace_);
    auto* welcome = new QVBoxLayout(empty);
    welcome->setSpacing(18);
    welcome->addStretch();
    auto* welcome_title = label(QStringLiteral("Convert a ground-motion record"), "title", empty);
    welcome_title->setAlignment(Qt::AlignCenter);
    welcome->addWidget(welcome_title);
    auto* hint = label(QStringLiteral("Open or drop one file to inspect its channels and export the records you need."), "muted", empty);
    hint->setAlignment(Qt::AlignCenter);
    welcome->addWidget(hint);
    auto* start = new QPushButton(style()->standardIcon(QStyle::SP_DialogOpenButton), QStringLiteral("Open a record…"), empty);
    start->setProperty("role", "primary");
    welcome->addWidget(start, 0, Qt::AlignHCenter);
    connect(start, &QPushButton::clicked, this, &MainWindow::choose_file);
    auto* steps = new QHBoxLayout;
    for (const auto& step : {std::pair{QStringLiteral("01  Import"), QStringLiteral("Identify the format and check the text layout.")},
                             std::pair{QStringLiteral("02  Review"), QStringLiteral("Inspect every channel, sampling interval and unit.")},
                             std::pair{QStringLiteral("03  Export"), QStringLiteral("Write each checked channel to its own file.")}}) {
        auto* panel = card(empty);
        auto* content = new QVBoxLayout(panel);
        content->setContentsMargins(20, 20, 20, 20);
        content->addWidget(label(step.first, "section", panel));
        content->addWidget(label(step.second, "muted", panel));
        steps->addWidget(panel, 1);
    }
    welcome->addSpacing(16);
    welcome->addLayout(steps);
    auto* formats_label = label(QStringLiteral("TXT / DAT    ·    CSV / TSV    ·    PEER AT2    ·    SAC    ·    K-NET"), "muted", empty);
    formats_label->setAlignment(Qt::AlignCenter);
    welcome->addWidget(formats_label);
    welcome->addStretch();
    workspace_->addWidget(empty);

    panes_ = new QSplitter(Qt::Horizontal, workspace_);
    panes_->setChildrenCollapsible(false);
    panes_->setHandleWidth(12);
    workspace_->addWidget(panes_);

    auto* sources = card(panes_);
    sources->setMinimumWidth(185);
    auto* source_box = new QVBoxLayout(sources);
    source_box->setContentsMargins(14, 16, 14, 14);
    source_box->setSpacing(12);
    source_box->addWidget(label(QStringLiteral("SOURCE FILE"), "muted", sources));
    source_label_ = label({}, "section", sources);
    source_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    source_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    source_box->addWidget(source_label_);
    source_summary_ = label({}, "muted", sources);
    source_box->addWidget(source_summary_);
    source_box->addSpacing(6);
    selection_label_ = label(QStringLiteral("Channels"), "section", sources);
    source_box->addWidget(selection_label_);
    channels_ = new QListWidget(sources);
    channels_->setAccessibleName(QStringLiteral("Channels to inspect and export"));
    channels_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    channels_->setTextElideMode(Qt::ElideRight);
    source_box->addWidget(channels_, 1);
    auto* select = new QHBoxLayout;
    auto* all = new QPushButton(QStringLiteral("Select all"), sources);
    auto* none = new QPushButton(QStringLiteral("Clear"), sources);
    select->addWidget(all);
    select->addWidget(none);
    source_box->addLayout(select);
    source_box->addWidget(label(QStringLiteral("Check channels to export. Select a row to inspect its waveform."), "muted", sources));

    auto* review = new QWidget(panes_);
    review->setMinimumWidth(300);
    auto* review_box = new QVBoxLayout(review);
    review_box->setContentsMargins(0, 0, 0, 0);
    review_box->setSpacing(10);
    review_label_ = label(QStringLiteral("Waveform overview"), "section", review);
    review_box->addWidget(review_label_);
    review_box->addWidget(label(QStringLiteral("All channels · elapsed time when sampling is known"), "muted", review));
    plots_ = new QScrollArea(review);
    plots_->setWidgetResizable(true);
    review_box->addWidget(plots_, 1);

    auto* sidebar = new QScrollArea(panes_);
    sidebar->setWidgetResizable(true);
    sidebar->setMinimumWidth(310);
    auto* output = new QWidget;
    sidebar->setWidget(output);
    auto* output_box = new QVBoxLayout(output);
    output_box->setContentsMargins(0, 0, 0, 0);
    output_box->setSpacing(14);

    auto* inspector = card(output);
    auto* inspector_box = new QVBoxLayout(inspector);
    inspector_box->setContentsMargins(14, 14, 14, 14);
    inspector_box->addWidget(label(QStringLiteral("Channel details"), "section", inspector));
    auto* tabs = new QTabWidget(inspector);
    properties_ = new QTreeWidget(tabs);
    properties_->setColumnCount(2);
    properties_->setHeaderHidden(true);
    properties_->setRootIsDecorated(false);
    properties_->setIndentation(0);
    properties_->setAlternatingRowColors(true);
    properties_->setSelectionMode(QAbstractItemView::NoSelection);
    properties_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    properties_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    properties_->setAccessibleName(QStringLiteral("Selected channel properties"));
    metadata_ = read_only_text(tabs);
    metadata_->setPlaceholderText(QStringLiteral("Source fields and parameter provenance"));
    diagnostics_ = read_only_text(tabs);
    tabs->addTab(properties_, QStringLiteral("Summary"));
    tabs->addTab(metadata_, QStringLiteral("Metadata"));
    tabs->addTab(diagnostics_, QStringLiteral("Diagnostics"));
    tabs->setMinimumHeight(260);
    inspector_box->addWidget(tabs);
    edit_ = new QPushButton(QStringLiteral("Edit channel parameters…"), inspector);
    inspector_box->addWidget(edit_);
    output_box->addWidget(inspector, 1);

    auto* export_card = card(output);
    auto* export_box = new QVBoxLayout(export_card);
    export_box->setContentsMargins(14, 14, 14, 14);
    export_box->setSpacing(10);
    export_box->addWidget(label(QStringLiteral("Export"), "section", export_card));
    auto* export_form = new QFormLayout;
    export_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    target_ = new QComboBox(export_card);
    for (const auto& item : formats()) {
        if (item.writable) {
            const auto experimental = item.notes.starts_with("experimental:");
            target_->addItem(as_text(item.name).toUpper() +
                (experimental ? QStringLiteral(" · experimental") : QString{}), static_cast<int>(item.id));
            target_->setItemData(target_->count() - 1, as_text(item.notes), Qt::ToolTipRole);
        }
    }
    target_->setCurrentIndex(target_->findData(static_cast<int>(FormatId::Csv)));
    export_form->addRow(QStringLiteral("Format"), target_);
    output_unit_ = new QComboBox(export_card);
    export_form->addRow(QStringLiteral("Unit"), output_unit_);
    format_options_ = new QPushButton("Format options…", export_card);
    export_form->addRow(format_options_);
    connect(format_options_, &QPushButton::clicked, this, [this] {
        if (choose_writer_options(writer_, this))
            update_format();
    });
    export_box->addLayout(export_form);
    format_help_ = label({}, "muted", export_card);
    export_box->addWidget(format_help_);
    export_box->addWidget(label(QStringLiteral("Output folder"), "section", export_card));
    auto* folder_row = new QHBoxLayout;
    output_directory_ = new QLineEdit(export_card);
    output_directory_->setReadOnly(true);
    output_directory_->setPlaceholderText(QStringLiteral("Choose a folder…"));
    output_directory_->setAccessibleName(QStringLiteral("Output folder"));
    folder_row->addWidget(output_directory_, 1);
    auto* browse = new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon), QString(), export_card);
    browse->setToolTip(QStringLiteral("Choose output folder"));
    browse->setAccessibleName(QStringLiteral("Choose output folder"));
    folder_row->addWidget(browse);
    export_box->addLayout(folder_row);
    overwrite_ = new QCheckBox(QStringLiteral("Replace existing output files"), export_card);
    export_box->addWidget(overwrite_);
    export_ = new QPushButton(QStringLiteral("Export channels"), export_card);
    export_->setProperty("role", "primary");
    export_->setToolTip(QStringLiteral("Write one file per checked channel · Ctrl+Shift+E"));
    export_box->addWidget(export_);
    output_box->addWidget(export_card);

    progress_ = new QProgressBar(this);
    progress_->setRange(0, 0);
    progress_->setTextVisible(false);
    progress_->setFixedSize(140, 4);
    progress_->hide();
    statusBar()->addPermanentWidget(progress_);
    statusBar()->setSizeGripEnabled(true);
    panes_->setSizes({220, 630, 350});
    panes_->setStretchFactor(1, 1);

    connect(channels_, &QListWidget::currentRowChanged, this, [this] { show_trace(); });
    connect(channels_, &QListWidget::itemChanged, this, [this] { update_selection(); });
    connect(all, &QPushButton::clicked, this, [this] { check_channels(Qt::Checked); });
    connect(none, &QPushButton::clicked, this, [this] { check_channels(Qt::Unchecked); });
    connect(edit_, &QPushButton::clicked, this, &MainWindow::edit_trace);
    connect(export_, &QPushButton::clicked, this, &MainWindow::send_export);
    connect(browse, &QPushButton::clicked, this, &MainWindow::choose_output);
    connect(target_, &QComboBox::currentIndexChanged, this, &MainWindow::update_format);
    connect(output_directory_, &QLineEdit::textChanged, this, [this](const auto& path) {
        output_directory_->setToolTip(path);
        update_selection();
    });
    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    panes_->restoreState(settings.value(QStringLiteral("window/panes")).toByteArray());
    if (const auto* display = screen()) {
        const auto available = display->availableGeometry();
        if (width() > available.width() || height() > available.height())
            resize(size().boundedTo(available.size()));
    }
    update_format();
    refresh();
    statusBar()->showMessage(QStringLiteral("Ready · open or drop a single record to begin"));
}

void MainWindow::choose_file() {
    if (busy_)
        return;
    const auto file = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open ground-motion record"),
        QSettings().value(QStringLiteral("paths/input")).toString(),
        QStringLiteral("Waveform files (*.txt *.dat *.csv *.tsv *.at2 *.AT2 *.vt2 *.VT2 *.dt2 *.DT2 *.sac *.SAC "
                       "*.mseed *.miniseed *.msd *.ms *.knet *.ns *.ew *.ud *.ns1 *.ew1 *.ud1 *.ns2 *.ew2 *.ud2 "
                       "*.NS *.EW *.UD *.NS1 *.EW1 *.UD1 *.NS2 *.EW2 *.UD2);;All files (*)"));
    if (!file.isEmpty())
        open_file(file);
}

bool MainWindow::confirm_replacement() {
    return !edited_ || QMessageBox::question(this, QStringLiteral("Replace current channels"),
        QStringLiteral("Continue and replace the current channel edits? The source file is unchanged."),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes;
}

void MainWindow::open_file(const QString& file) {
    if (busy_ || !confirm_replacement())
        return;
    const auto path = path_from_utf8(as_utf8(file));
    set_busy(true, QStringLiteral("Reading %1…").arg(as_text(path_to_utf8(path.filename()))));
    run_background(this, [path] { return load_file(path); }, [this, path](const auto& task) {
        set_busy(false);
        if (!task.value || has_errors(task.value->diagnostics)) {
            QMessageBox::warning(this, QStringLiteral("Cannot open file"), task.value
                ? diagnostic_text(task.value->diagnostics) : task.error);
            return;
        }
        if (auto parsed = interpret_input(task.value, path, {}, this)) {
            input_ = path;
            bytes_ = task.value;
            if (output_directory_->text().isEmpty()) {
                auto directory = QSettings().value(QStringLiteral("paths/output")).toString();
                std::error_code ec;
                if (directory.isEmpty() || !std::filesystem::is_directory(path_from_utf8(as_utf8(directory)), ec))
                    directory = as_text(path_to_utf8(path.parent_path()));
                output_directory_->setText(directory);
            }
            QSettings().setValue(QStringLiteral("paths/input"), as_text(path_to_utf8(path.parent_path())));
            accept_parse(std::move(*parsed));
        }
    });
}

void MainWindow::reparse() {
    if (busy_ || !bytes_ || !confirm_replacement())
        return;
    if (auto parsed = interpret_input(bytes_, input_, last_options_, this))
        accept_parse(std::move(*parsed));
}

void MainWindow::accept_parse(ParsedInput parsed) {
    last_options_ = std::move(parsed.options);
    parsed_ = std::move(parsed.result);
    edited_ = false;
    overwrite_->setChecked(false);
    // Plot widgets own bounded display data and never borrow the document's Traces.
    trace_plots_.clear();
    delete plots_->takeWidget();
    auto* container = new QWidget;
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 3, 0);
    layout->setSpacing(12);
    for (std::size_t i = 0; i < parsed_->data.traces.size(); ++i) {
        auto* plot = new TracePlot(parsed_->data.traces[i], std::move(parsed.plots[i]), i + 1, container);
        trace_plots_.push_back(plot);
        layout->addWidget(plot);
    }
    layout->addStretch();
    plots_->setWidget(container);
    refresh();
    statusBar()->showMessage(QStringLiteral("Imported %1 channel(s) · %2 diagnostic(s)")
        .arg(parsed_->data.traces.size()).arg(parsed_->diagnostics.size()));
}

void MainWindow::refresh() {
    const bool open = parsed_ && !parsed_->data.traces.empty();
    workspace_->setCurrentIndex(open ? 1 : 0);
    {
        const QSignalBlocker blocker(channels_);
        channels_->clear();
        if (open) {
            for (std::size_t i = 0; i < parsed_->data.traces.size(); ++i) {
                const auto& trace = parsed_->data.traces[i];
                auto* item = new QListWidgetItem(channel_label(trace, i + 1), channels_);
                item->setToolTip(trace_title(trace, i + 1) + '\n' + timing_unit(trace));
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Checked);
            }
            channels_->setCurrentRow(0);
        }
    }
    if (open) {
        const auto file = as_text(path_to_utf8(input_.filename()));
        setWindowTitle(file + QStringLiteral(" · GMTools"));
        source_label_->setText(file);
        source_label_->setToolTip(as_text(path_to_utf8(input_)));
        source_summary_->setText(QStringLiteral("%1 · %2 channel(s)")
            .arg(as_text(name(parsed_->data.source_format)).toUpper()).arg(parsed_->data.traces.size()) +
            (parsed_->diagnostics.empty() ? QString{} : QStringLiteral("\n%1 import diagnostic(s)")
                .arg(parsed_->diagnostics.size())));
        diagnostics_->setPlainText(parsed_->diagnostics.empty()
            ? QStringLiteral("No import diagnostics.") : diagnostic_text(parsed_->diagnostics));
    }
    show_trace(false);
    set_busy(false);
}

void MainWindow::check_channels(Qt::CheckState state) {
    const QSignalBlocker blocker(channels_);
    for (int i = 0; i < channels_->count(); ++i)
        channels_->item(i)->setCheckState(state);
    update_selection();
}

void MainWindow::update_selection() {
    int count = 0;
    for (int i = 0; i < channels_->count(); ++i)
        count += channels_->item(i)->checkState() == Qt::Checked ? 1 : 0;
    selection_label_->setText(QStringLiteral("Channels · %1 / %2 checked").arg(count).arg(channels_->count()));
    export_->setText(count ? QStringLiteral("Export %1 channel(s)").arg(count) : QStringLiteral("Select channels to export"));
    const bool ready = !busy_ && count > 0 && !output_directory_->text().isEmpty();
    export_->setEnabled(ready);
    export_action_->setEnabled(ready);
    update_format();
}

void MainWindow::show_trace(bool scroll) {
    const auto row = channels_->currentRow();
    edit_->setEnabled(!busy_ && row >= 0);
    properties_->clear();
    metadata_->clear();
    for (std::size_t i = 0; i < trace_plots_.size(); ++i)
        trace_plots_[i]->set_selected(static_cast<int>(i) == row);
    if (row < 0 || !parsed_)
        return;
    const auto index = static_cast<std::size_t>(row);
    const auto& trace = parsed_->data.traces[index];
    review_label_->setText(QStringLiteral("Waveforms · channel %1 selected").arg(row + 1));
    const auto property = [this](const QString& key, const QString& value) {
        auto* item = new QTreeWidgetItem(properties_, {key, value});
        item->setToolTip(1, value);
    };
    property(QStringLiteral("Name"), trace.name.empty() ? QStringLiteral("Unnamed") : as_text(trace.name));
    property(QStringLiteral("Samples"), QString::number(trace.samples.size()));
    property(QStringLiteral("Interval · dt"), trace.dt_seconds ? displayed_number(*trace.dt_seconds) + QStringLiteral(" s") : QStringLiteral("Unknown"));
    property(QStringLiteral("Span · (N−1)dt"), trace.sample_span() ? displayed_number(*trace.sample_span()) + QStringLiteral(" s") : QStringLiteral("Unknown"));
    property(QStringLiteral("Length · Ndt"), trace.record_length() ? displayed_number(*trace.record_length()) + QStringLiteral(" s") : QStringLiteral("Unknown"));
    property(QStringLiteral("Quantity"), as_text(name(trace.quantity)));
    property(QStringLiteral("Input unit"), as_text(name(trace.unit)));
    if (trace.relative_start_seconds)
        property(QStringLiteral("Relative start"), displayed_number(*trace.relative_start_seconds) + QStringLiteral(" s"));
    if (trace.start_time)
        property(QStringLiteral("Start · UTC"), as_text(timestamp(*trace.start_time)));
    for (const auto& [key, label] : {std::pair{"modified.sensitivity", "Encoded sensitivity"},
                                     {"modified.dimension", "Sensor quantity"},
                                     {"modified.segment", "Segment"},
                                     {"mseed.segment", "Segment"},
                                     {"channel.source_id", "Source ID"},
                                     {"calibration.source", "Calibration on import"},
                                     {"history.calibration.source", "Earlier calibration"},
                                     {"history.last_numeric_factor", "Earlier scaling (last)"},
                                     {"text.numeric_factor", "Scaling on import"},
                                     {"calibration.factor", "Units per count"}})
        if (trace.attributes.contains(key))
            property(label, as_text(trace.attributes.at(key)));
    if (trace.attributes.contains("history.calibration.factor"))
        property("Earlier factor", as_text(trace.attributes.at("history.calibration.factor") + " " +
                                           trace.attributes.at("history.calibration.unit") + "/count"));
    if (trace.unit == Unit::Counts || trace.quantity == Quantity::Counts) {
        if (trace.attributes.contains("text.numeric_factor") || trace.attributes.contains("history.last_numeric_factor"))
            property("Values", "Scaled counts");
        else if (trace.attributes.contains("modified.sensitivity"))
            property("Values", "Raw counts");
    }
    QString metadata;
    if (!parsed_->data.attributes.empty()) {
        metadata = QStringLiteral("FILE FIELDS");
        append_metadata(metadata, parsed_->data.attributes);
    }
    if (!trace.attributes.empty()) {
        metadata += QStringLiteral("\n\nCHANNEL FIELDS");
        append_metadata(metadata, trace.attributes);
    }
    metadata_->setPlainText(metadata.trimmed());
    if (scroll && index < trace_plots_.size())
        plots_->ensureWidgetVisible(trace_plots_[index], 0, 12);
}

void MainWindow::update_format() {
    const auto format = static_cast<FormatId>(target_->currentData().toInt());
    if (writer_.format != format) {
        writer_ = WriteOptions{};
        writer_.format = format;
    }
    const bool peer = format == FormatId::At2 || format == FormatId::Vt2 || format == FormatId::Dt2;
    const bool standard = writer_.sac_standard_units.value_or(false);
    const auto previous = output_unit_->currentData();
    output_unit_->clear();
    const auto required = format == FormatId::At2 ? Unit::G : format == FormatId::Vt2 ? Unit::Cmps : Unit::Cm;
    output_unit_->addItem(peer       ? as_text(name(required))
                          : standard ? QStringLiteral("Standard nanounits")
                                     : QStringLiteral("Keep input unit"));
    std::optional<Quantity> common;
    if (!peer && !standard && parsed_) {
        for (int i = 0; i < channels_->count(); ++i) {
            if (channels_->item(i)->checkState() != Qt::Checked)
                continue;
            const auto& trace = parsed_->data.traces.at(static_cast<std::size_t>(i));
            if (!unit_factor(trace.unit, trace.unit) || (common && common != trace.quantity)) {
                common.reset();
                break;
            }
            common = trace.quantity;
        }
        for (auto u : units())
            if (common == quantity_for(u) && unit_factor(u, u))
                output_unit_->addItem(as_text(name(u)), static_cast<int>(u));
        output_unit_->setCurrentIndex(std::max(0, output_unit_->findData(previous)));
    }
    output_unit_->setEnabled(output_unit_->count() > 1);
    format_options_->setVisible(!peer);
    if (is_generic_text(format))
        format_help_->setText(writer_.text_header == TextHeader::Metadata
                                  ? "Stores basic waveform metadata and supported processing history."
                                  : "Choose a metadata header to retain timing, units and channel identifiers.");
    else if (peer)
        format_help_->setText("Requires a sampling interval and compatible physical units.");
    else if (format == FormatId::Sac)
        format_help_->setText(QString("SAC v%1 · float32 samples").arg(writer_.sac_version.value_or(7)));
    else
        format_help_->setText("Requires UTC, sampling interval and a representable source identifier.");
}

void MainWindow::edit_trace() {
    const auto row = channels_->currentRow();
    if (busy_ || row < 0 || !parsed_)
        return;
    auto& trace = parsed_->data.traces[static_cast<std::size_t>(row)];
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Edit channel parameters"));
    dialog.setMinimumWidth(430);
    auto* form = new QFormLayout(&dialog);
    form->setContentsMargins(20, 20, 20, 20);
    form->setSpacing(12);
    auto* channel_name = new QLineEdit(as_text(trace.name), &dialog);
    form->addRow(QStringLiteral("Channel name"), channel_name);
    form->addRow(label(timing_unit(trace), "muted", &dialog));
    const OverrideFields fields(form, {}, QStringLiteral("Keep current"));
    form->addRow(label(QStringLiteral("Input units describe the existing values. To convert values, choose an output unit in Export."), "muted", &dialog));
    MetadataEdit edits;
    using Field = std::optional<std::string> MetadataEdit::*;
    struct Editor {
        Field field;
        QLineEdit* widget;
        QString original;
    };
    std::vector<Editor> editors;
    auto add_edit = [&](const char* label, Field field, std::string initial) {
        auto* line = new QLineEdit(as_text(initial), &dialog);
        line->setPlaceholderText(field == &MetadataEdit::start_utc ? "YYYY-MM-DDTHH:MM:SS[.fraction]Z" : "Unknown");
        editors.push_back({field, line, line->text()});
        form->addRow(label, line);
    };
    add_edit("First sample · UTC", &MetadataEdit::start_utc, trace.start_time ? timestamp(*trace.start_time) : "");
    add_edit("Relative start (s)", &MetadataEdit::relative_start,
             trace.relative_start_seconds ? as_utf8(real(*trace.relative_start_seconds)) : "");
    auto value = [&](const char* key) {
        return trace.attributes.contains(key) ? trace.attributes.at(key) : std::string{};
    };
    add_edit("Network", &MetadataEdit::network, value("station.network"));
    add_edit("Station", &MetadataEdit::station, value("station.code"));
    const auto location = value("station.location");
    add_edit("Location", &MetadataEdit::location,
             location.empty() && trace.attributes.contains("station.location") ? "--" : location);
    editors.back().widget->setPlaceholderText("Unknown · use -- for an empty code");
    add_edit("Channel code", &MetadataEdit::channel, value("channel.code"));
    add_edit("Source ID", &MetadataEdit::source_id, value("channel.source_id"));
    form->addRow(label("Leave a field unchanged to keep it. Erase its contents to clear it. Edit the source ID or "
                       "individual codes.",
                       "muted", &dialog));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setProperty("role", "primary");
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Apply changes"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            const auto options = fields.options();
            const auto new_name = as_utf8(channel_name->text().trimmed());
            for (const auto& e : editors) {
                auto& field = edits.*e.field;
                field.reset();
                if (e.widget->text() != e.original)
                    field = as_utf8(e.widget->text().trimmed());
            }
            edits.clear_location = edits.location && edits.location->empty();
            if (edits.clear_location)
                edits.location.reset();
            else if (edits.location == "--")
                edits.location = "";
            Trace metadata;
            metadata.attributes = trace.attributes;
            metadata.start_time = trace.start_time;
            metadata.relative_start_seconds = trace.relative_start_seconds;
            auto validation = edit_metadata(metadata, edits);
            if (has_errors(validation)) {
                QMessageBox::warning(&dialog, "Invalid metadata", diagnostic_text(validation));
                return;
            }
            const bool changed = edits.start_utc || edits.relative_start || edits.has_identity() ||
                                 options.dt_seconds || options.fs_hz || options.quantity || options.unit ||
                                 new_name != trace.name;
            auto diagnostics = apply_overrides(trace, options);
            if (has_errors(diagnostics)) {
                QMessageBox::warning(&dialog, QStringLiteral("Invalid parameters"), diagnostic_text(diagnostics));
                return;
            }
            for (auto& diagnostic : diagnostics) {
                diagnostic.channel = static_cast<std::size_t>(row) + 1;
                parsed_->diagnostics.push_back(std::move(diagnostic));
            }
            if (new_name != trace.name) {
                if (!trace.name.empty() && !trace.attributes.contains("source.original_name"))
                    trace.attributes["source.original_name"] = trace.name;
                trace.name = new_name;
                trace.attributes["source.name"] = "user input";
            }
            edit_metadata(trace, edits);
            edited_ |= changed;
            dialog.accept();
        } catch (const std::exception& problem) {
            QMessageBox::warning(&dialog, QStringLiteral("Invalid parameters"), as_text(problem.what()));
        }
    });
    if (dialog.exec() == QDialog::Accepted) {
        auto* item = channels_->item(row);
        item->setText(channel_label(trace, static_cast<std::size_t>(row) + 1));
        item->setToolTip(trace_title(trace, static_cast<std::size_t>(row) + 1) + '\n' + timing_unit(trace));
        diagnostics_->setPlainText(parsed_->diagnostics.empty() ? QStringLiteral("No import diagnostics.") : diagnostic_text(parsed_->diagnostics));
        trace_plots_[static_cast<std::size_t>(row)]->set_metadata(trace);
        show_trace(false);
        update_format();
    }
}

void MainWindow::choose_output() {
    const auto directory = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose output folder"), output_directory_->text());
    if (!directory.isEmpty())
        output_directory_->setText(directory);
}

void MainWindow::send_export() {
    if (busy_ || !parsed_)
        return;
    ExportOptions options;
    for (int row = 0; row < channels_->count(); ++row) {
        if (channels_->item(row)->checkState() == Qt::Checked)
            options.channels.push_back(static_cast<std::size_t>(row) + 1);
    }
    if (options.channels.empty() || output_directory_->text().isEmpty())
        return;
    options.writer = writer_;
    if (output_unit_->currentData().isValid())
        options.writer.output_unit = static_cast<Unit>(output_unit_->currentData().toInt());
    options.output = path_from_utf8(as_utf8(output_directory_->text()));
    options.overwrite = overwrite_->isChecked();
    const bool all_channels = options.channels.size() == parsed_->data.traces.size();
    const auto directory = output_directory_->text();
    set_busy(true, QStringLiteral("Exporting %1 channel(s)…").arg(options.channels.size()));
    // Editing/importing is disabled until this worker releases the shared document.
    run_background(this, [parsed = parsed_, input = input_, options] {
        return export_file(parsed->data, input, options);
    }, [this, directory, all_channels](const auto& task) {
        set_busy(false);
        if (!task.value) {
            QMessageBox::warning(this, QStringLiteral("Export failed"), task.error);
            return;
        }
        const auto& result = *task.value;
        const bool failed = has_errors(result.diagnostics);
        if (!failed) {
            if (all_channels)
                edited_ = false;
            QSettings().setValue(QStringLiteral("paths/output"), directory);
        }
        QMessageBox report(this);
        report.setWindowTitle(QStringLiteral("Export results"));
        report.setTextFormat(Qt::PlainText);
        report.setIcon(failed ? QMessageBox::Warning : QMessageBox::Information);
        report.setText(failed ? QStringLiteral("Export incomplete · %1 file(s) written").arg(result.written.size())
                              : QStringLiteral("Exported %1 file(s)").arg(result.written.size()));
        const auto issue = std::ranges::find_if(result.diagnostics, [](const auto& diagnostic) {
            return diagnostic.severity == Severity::Error;
        });
        if (issue != result.diagnostics.end())
            report.setInformativeText(as_text(describe(*issue)));
        else
            report.setInformativeText(directory + (result.diagnostics.empty() ? QString{} :
                QStringLiteral("\n%1 warning(s); expand Details for metadata and conversion notes.").arg(result.diagnostics.size())));
        QStringList details;
        for (const auto& path : result.written)
            details << as_text(path_to_utf8(path));
        if (!result.diagnostics.empty())
            details << diagnostic_text(result.diagnostics);
        report.setDetailedText(details.join('\n'));
        report.addButton(QMessageBox::Ok);
        auto* folder = result.written.empty() ? nullptr : report.addButton(QStringLiteral("Open folder"), QMessageBox::ActionRole);
        statusBar()->showMessage(report.text());
        report.exec();
        if (folder && report.clickedButton() == folder)
            QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
    });
}

void MainWindow::set_busy(bool busy, const QString& message) {
    busy_ = busy;
    workspace_->setEnabled(!busy);
    open_action_->setEnabled(!busy);
    reparse_action_->setEnabled(!busy && bytes_ != nullptr);
    edit_->setEnabled(!busy && channels_->currentRow() >= 0);
    progress_->setVisible(busy);
    update_selection();
    statusBar()->showMessage(message.isEmpty() ? QStringLiteral("Ready") : message);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (busy_) {
        statusBar()->showMessage(QStringLiteral("Please wait for the current operation to finish before closing."));
        event->ignore();
        return;
    }
    if (edited_ && QMessageBox::question(this, QStringLiteral("Close GMTools"),
            QStringLiteral("Some channel edits have not been exported together. Close this record? The source file is unchanged."),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/panes"), panes_->saveState());
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (!busy_ && urls.size() == 1 && urls.front().isLocalFile())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (!busy_ && urls.size() == 1 && urls.front().isLocalFile()) {
        event->acceptProposedAction();
        open_file(urls.front().toLocalFile());
    }
}

} // namespace gmtools::gui
