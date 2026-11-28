#include "analysis_dialog.hpp"

#include "analysis_plot.hpp"
#include "background_task.hpp"
#include "core/detail.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace gmtools::gui {
namespace {

QString channel_name(const Trace& trace, std::size_t index) {
    if (!trace.name.empty())
        return QString::fromStdString(trace.name);
    return QStringLiteral("CH%1").arg(index + 1, 2, 10, QLatin1Char('0'));
}

QString quantity_text(Quantity quantity) {
    switch (quantity) {
    case Quantity::Acceleration:
        return QCoreApplication::translate("gmtools::gui::AnalysisDialog", "acceleration");
    case Quantity::Velocity:
        return QCoreApplication::translate("gmtools::gui::AnalysisDialog", "velocity");
    case Quantity::Displacement:
        return QCoreApplication::translate("gmtools::gui::AnalysisDialog", "displacement");
    case Quantity::Counts:
        return QCoreApplication::translate("gmtools::gui::AnalysisDialog", "counts");
    case Quantity::Unknown:
        break;
    }
    return QCoreApplication::translate("gmtools::gui::AnalysisDialog", "unknown quantity");
}

QString window_text(WindowType window) {
    switch (window) {
    case WindowType::Rectangular:
        return QCoreApplication::translate("gmtools::gui::AnalysisDialog", "Rectangular");
    case WindowType::Hann:
        return QCoreApplication::translate("gmtools::gui::AnalysisDialog", "Hann");
    case WindowType::Hamming:
        return QCoreApplication::translate("gmtools::gui::AnalysisDialog", "Hamming");
    case WindowType::Blackman:
        return QCoreApplication::translate("gmtools::gui::AnalysisDialog", "Blackman");
    }
    return {};
}

QString number(double value) {
    return QString::number(value, 'g', 6);
}

std::vector<CurveSeries> curve(const std::vector<double>& x, const std::vector<double>& y) {
    CurveSeries series;
    series.x = x;
    series.y = y;
    return {std::move(series)};
}

}  // namespace

AnalysisDialog::AnalysisDialog(std::shared_ptr<const ReadResult> parsed, std::size_t channel, QWidget* parent)
    : QDialog(parent), parsed_(std::move(parsed)), channel_(channel) {
    setWindowTitle(tr("Data analysis"));
    resize(1080, 780);
    build_ui();
    populate_channels(channel_);
}

void AnalysisDialog::build_ui() {
    auto* root = new QVBoxLayout(this);
    auto* header = new QHBoxLayout();
    header->addWidget(new QLabel(tr("Channel"), this));
    channels_ = new QComboBox(this);
    channels_->setMinimumWidth(220);
    header->addWidget(channels_);
    heading_ = new QLabel(this);
    heading_->setStyleSheet(QStringLiteral("color: palette(mid);"));
    header->addWidget(heading_, 1);
    root->addLayout(header);
    connect(channels_, &QComboBox::currentIndexChanged, this, &AnalysisDialog::channel_changed);

    tabs_ = new QTabWidget(this);
    root->addWidget(tabs_, 1);
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int) { refresh_tab(); });

    // Time history.
    {
        auto* page = new QWidget(tabs_);
        auto* layout = new QVBoxLayout(page);
        time_summary_ = new QLabel(page);
        time_summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(time_summary_);
        time_plot_ = new AnalysisPlot(page);
        layout->addWidget(time_plot_, 1);
        tabs_->addTab(page, tr("Time history"));
    }

    // Spectrum.
    {
        auto* page = new QWidget(tabs_);
        auto* layout = new QVBoxLayout(page);
        auto* controls = new QHBoxLayout();
        spectrum_kind_ = new QComboBox(page);
        spectrum_kind_->addItem(tr("FFT magnitude"), static_cast<int>(SpectrumKind::Fft));
        spectrum_kind_->addItem(tr("One-sided amplitude spectrum"), static_cast<int>(SpectrumKind::Amplitude));
        spectrum_kind_->addItem(tr("Fourier amplitude spectrum (FAS)"), static_cast<int>(SpectrumKind::Fas));
        spectrum_kind_->addItem(tr("Power spectral density (PSD)"), static_cast<int>(SpectrumKind::Psd));
        spectrum_kind_->setCurrentIndex(1);
        spectrum_window_ = new QComboBox(page);
        spectrum_window_->addItem(tr("Rectangular"), static_cast<int>(WindowType::Rectangular));
        spectrum_window_->addItem(tr("Hann"), static_cast<int>(WindowType::Hann));
        spectrum_window_->addItem(tr("Hamming"), static_cast<int>(WindowType::Hamming));
        spectrum_window_->addItem(tr("Blackman"), static_cast<int>(WindowType::Blackman));
        remove_mean_ = new QCheckBox(tr("Remove mean"), page);
        welch_ = new QCheckBox(tr("Welch segments"), page);
        segment_length_ = new QSpinBox(page);
        segment_length_->setRange(2, 1000000);
        segment_length_->setValue(4096);
        overlap_ = new QDoubleSpinBox(page);
        overlap_->setRange(0.0, 0.95);
        overlap_->setSingleStep(0.05);
        overlap_->setDecimals(2);
        overlap_->setValue(0.5);
        nfft_ = new QSpinBox(page);
        nfft_->setRange(0, 1 << 24);
        nfft_->setSpecialValueText(tr("Automatic"));
        log_x_ = new QCheckBox(tr("Log frequency"), page);
        log_y_ = new QCheckBox(tr("Log value"), page);
        spectrum_compute_ = new QPushButton(tr("Compute"), page);
        spectrum_compute_->setDefault(true);
        controls->addWidget(new QLabel(tr("Type"), page));
        controls->addWidget(spectrum_kind_);
        controls->addWidget(new QLabel(tr("Window"), page));
        controls->addWidget(spectrum_window_);
        controls->addWidget(remove_mean_);
        controls->addWidget(welch_);
        controls->addWidget(new QLabel(tr("Segment"), page));
        controls->addWidget(segment_length_);
        controls->addWidget(new QLabel(tr("Overlap"), page));
        controls->addWidget(overlap_);
        controls->addWidget(new QLabel(tr("FFT length"), page));
        controls->addWidget(nfft_);
        controls->addStretch(1);
        controls->addWidget(spectrum_compute_);
        layout->addLayout(controls);

        auto* second = new QHBoxLayout();
        second->addWidget(log_x_);
        second->addWidget(log_y_);
        second->addStretch(1);
        layout->addLayout(second);
        spectrum_plot_ = new AnalysisPlot(page);
        spectrum_plot_->set_empty_text(tr("Choose the parameters and press Compute."));
        spectrum_plot_->set_axis_titles(tr("Frequency (Hz)"), tr("Value"));
        layout->addWidget(spectrum_plot_, 1);
        spectrum_summary_ = new QLabel(page);
        spectrum_summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(spectrum_summary_);
        tabs_->addTab(page, tr("Spectrum"));

        connect(spectrum_kind_, &QComboBox::currentIndexChanged, this, [this](int) {
            const bool welch = spectrum_kind_->currentData().toInt() == static_cast<int>(SpectrumKind::Psd);
            welch_->setChecked(welch);
            if (welch)
                remove_mean_->setChecked(true);
            segment_length_->setEnabled(welch);
            overlap_->setEnabled(welch);
        });
        connect(welch_, &QCheckBox::toggled, this, [this](bool on) {
            segment_length_->setEnabled(on);
            overlap_->setEnabled(on);
            if (on)
                remove_mean_->setChecked(true);
        });
        connect(log_x_, &QCheckBox::toggled, spectrum_plot_, &AnalysisPlot::set_log_x);
        connect(log_y_, &QCheckBox::toggled, spectrum_plot_, &AnalysisPlot::set_log_y);
        connect(spectrum_compute_, &QPushButton::clicked, this, &AnalysisDialog::compute_spectrum);
    }

    // Response spectrum.
    {
        auto* page = new QWidget(tabs_);
        auto* layout = new QVBoxLayout(page);
        auto* controls = new QHBoxLayout();
        response_kind_ = new QComboBox(page);
        response_kind_->addItem(tr("True absolute acceleration (Sa)"), 0);
        response_kind_->addItem(tr("Pseudo-spectral acceleration (PSA)"), 1);
        response_kind_->addItem(tr("Relative velocity (Sv)"), 2);
        response_kind_->addItem(tr("Pseudo-velocity (PSV)"), 3);
        response_kind_->addItem(tr("Relative displacement (Sd)"), 4);
        damping_ = new QDoubleSpinBox(page);
        damping_->setRange(0.0, 1.0);
        damping_->setDecimals(3);
        damping_->setSingleStep(0.005);
        damping_->setValue(0.05);
        period_min_ = new QDoubleSpinBox(page);
        period_min_->setRange(0.0, 1000.0);
        period_min_->setDecimals(3);
        period_min_->setValue(0.0);
        period_max_ = new QDoubleSpinBox(page);
        period_max_->setRange(0.0, 10000.0);
        period_max_->setDecimals(3);
        period_max_->setValue(5.0);
        period_step_ = new QDoubleSpinBox(page);
        period_step_->setRange(0.0001, 100.0);
        period_step_->setDecimals(4);
        period_step_->setValue(0.02);
        response_compute_ = new QPushButton(tr("Compute"), page);
        controls->addWidget(new QLabel(tr("Damping ratio"), page));
        controls->addWidget(damping_);
        controls->addWidget(new QLabel(tr("Period from (s)"), page));
        controls->addWidget(period_min_);
        controls->addWidget(new QLabel(tr("to (s)"), page));
        controls->addWidget(period_max_);
        controls->addWidget(new QLabel(tr("Step (s)"), page));
        controls->addWidget(period_step_);
        controls->addStretch(1);
        controls->addWidget(response_compute_);
        layout->addLayout(controls);
        auto* second = new QHBoxLayout();
        second->addWidget(new QLabel(tr("Show"), page));
        second->addWidget(response_kind_);
        second->addStretch(1);
        layout->addLayout(second);
        response_plot_ = new AnalysisPlot(page);
        response_plot_->set_empty_text(tr("Choose the parameters and press Compute."));
        response_plot_->set_axis_titles(tr("Period (s)"), tr("Value"));
        layout->addWidget(response_plot_, 1);
        response_summary_ = new QLabel(page);
        response_summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(response_summary_);
        tabs_->addTab(page, tr("Response spectrum"));

        connect(response_compute_, &QPushButton::clicked, this, &AnalysisDialog::compute_response);
        connect(response_kind_, &QComboBox::currentIndexChanged, this, [this](int) {
            if (!response_valid_)
                return;
            std::vector<double> values;
            QString title;
            switch (response_kind_->currentData().toInt()) {
            case 0:
                values = response_result_.sa;
                title = tr("Sa (m/s²)");
                break;
            case 1:
                values = response_result_.psa;
                title = tr("PSA (m/s²)");
                break;
            case 2:
                values = response_result_.sv;
                title = tr("Sv (m/s)");
                break;
            case 3:
                values = response_result_.psv;
                title = tr("PSV (m/s)");
                break;
            default:
                values = response_result_.sd;
                title = tr("Sd (m)");
                break;
            }
            response_plot_->set_axis_titles(tr("Period (s)"), title);
            response_plot_->set_series(curve(response_result_.period_seconds, values));
        });
    }

    // Engineering parameters.
    {
        auto* page = new QWidget(tabs_);
        auto* layout = new QVBoxLayout(page);
        auto* controls = new QHBoxLayout();
        integration_ = new QComboBox(page);
        integration_->addItem(tr("Peak only (no integration)"), static_cast<int>(IntegrationScheme::None));
        integration_->addItem(tr("Trapezoid integration"), static_cast<int>(IntegrationScheme::Trapezoid));
        integration_->addItem(tr("Trapezoid with linear detrend"), static_cast<int>(IntegrationScheme::TrapezoidDetrend));
        parameters_refresh_ = new QPushButton(tr("Refresh"), page);
        controls->addWidget(new QLabel(tr("Integration"), page));
        controls->addWidget(integration_);
        controls->addWidget(parameters_refresh_);
        controls->addStretch(1);
        layout->addLayout(controls);
        parameters_ = new QTableWidget(page);
        parameters_->setColumnCount(2);
        parameters_->setHorizontalHeaderLabels({tr("Quantity"), tr("Value")});
        parameters_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        parameters_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        parameters_->verticalHeader()->setVisible(false);
        parameters_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        parameters_->setSelectionMode(QAbstractItemView::SingleSelection);
        layout->addWidget(parameters_, 1);
        parameters_note_ = new QLabel(page);
        parameters_note_->setWordWrap(true);
        parameters_note_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(parameters_note_);
        tabs_->addTab(page, tr("Engineering parameters"));
        connect(parameters_refresh_, &QPushButton::clicked, this, &AnalysisDialog::compute_parameters);
        connect(integration_, &QComboBox::currentIndexChanged, this, [this](int) { compute_parameters(); });
    }

    status_ = new QLabel(this);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(status_);

    auto* buttons = new QDialogButtonBox(this);
    auto* export_data_button = buttons->addButton(tr("Export data…"), QDialogButtonBox::ActionRole);
    auto* export_image_button = buttons->addButton(tr("Save image…"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    root->addWidget(buttons);
    connect(export_data_button, &QPushButton::clicked, this, &AnalysisDialog::export_data);
    connect(export_image_button, &QPushButton::clicked, this, &AnalysisDialog::save_image);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    last_directory_ = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

void AnalysisDialog::populate_channels(std::size_t channel) {
    channels_->blockSignals(true);
    channels_->clear();
    const auto& traces = parsed_->data.traces;
    for (std::size_t i = 0; i < traces.size(); ++i) {
        const auto& item = traces[i];
        QString label = QStringLiteral("%1 · %2 · %3").arg(channel_name(item, i), quantity_text(item.quantity),
                                                           QString::fromStdString(std::string{name(item.unit)}));
        channels_->addItem(label, static_cast<qulonglong>(i));
    }
    channel_ = std::clamp<std::size_t>(channel, 0, traces.empty() ? 0 : traces.size() - 1);
    channels_->setCurrentIndex(static_cast<int>(channel_));
    channels_->blockSignals(false);
    refresh_time_history();
    refresh_summary();
    channel_changed(static_cast<int>(channel_));
}

const Trace& AnalysisDialog::trace() const {
    return parsed_->data.traces[channel_];
}

void AnalysisDialog::channel_changed(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= parsed_->data.traces.size())
        return;
    channel_ = static_cast<std::size_t>(index);
    spectrum_valid_ = false;
    response_valid_ = false;
    statistics_valid_ = false;
    intensity_valid_ = false;
    spectrum_plot_->set_series({});
    response_plot_->set_series({});
    spectrum_summary_->clear();
    response_summary_->clear();
    parameters_->setRowCount(0);
    parameters_note_->clear();
    refresh_time_history();
    refresh_summary();
    update_headings();
    if (current_tab() == Tab::Parameters)
        compute_parameters();
}

AnalysisDialog::Tab AnalysisDialog::current_tab() const {
    switch (tabs_->currentIndex()) {
    case 1:
        return Tab::Spectrum;
    case 2:
        return Tab::Response;
    case 3:
        return Tab::Parameters;
    default:
        return Tab::Time;
    }
}

void AnalysisDialog::refresh_time_history() {
    const auto& item = trace();
    const std::size_t count = item.samples.size();
    std::vector<double> x(count);
    const double dt = item.dt_seconds.value_or(0.0);
    for (std::size_t i = 0; i < count; ++i)
        x[i] = dt > 0.0 ? static_cast<double>(i) * dt : static_cast<double>(i);
    time_plot_->set_axis_titles(dt > 0.0 ? tr("Elapsed time (s)") : tr("Sample index"),
                                QString::fromStdString(std::string{name(item.unit)}));
    time_plot_->set_series(curve(x, item.samples));

    double peak = 0.0;
    std::size_t peak_index = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (std::abs(item.samples[i]) > peak) {
            peak = std::abs(item.samples[i]);
            peak_index = i;
        }
    }
    QString summary = tr("%1 samples").arg(count);
    if (item.dt_seconds)
        summary += QStringLiteral(" · dt %1 s · %2 Hz").arg(number(*item.dt_seconds), number(1.0 / *item.dt_seconds));
    if (count > 0) {
        summary += QStringLiteral(" · ") + tr("peak %1 %2")
                       .arg(number(peak), QString::fromStdString(std::string{name(item.unit)}));
        if (item.dt_seconds)
            summary += tr(" at sample %1 (%2 s)").arg(peak_index + 1).arg(number(peak_index * *item.dt_seconds));
    }
    time_summary_->setText(summary);
}

void AnalysisDialog::refresh_summary() {
    const auto& item = trace();
    QString text = QStringLiteral("%1 · %2 · %3")
                       .arg(channel_name(item, channel_), quantity_text(item.quantity),
                            QString::fromStdString(std::string{name(item.unit)}));
    if (item.dt_seconds)
        text += QStringLiteral(" · dt %1 s").arg(number(*item.dt_seconds));
    if (!item.samples.empty())
        text += QStringLiteral(" · %1 samples").arg(item.samples.size());
    heading_->setText(text);
    status_->setText(tr("%1 channels in this record").arg(parsed_->data.traces.size()));
}

void AnalysisDialog::refresh_tab() {
    if (current_tab() == Tab::Parameters && !statistics_valid_)
        compute_parameters();
}

void AnalysisDialog::update_headings() {}

void AnalysisDialog::set_busy(bool busy) {
    busy_ = busy;
    spectrum_compute_->setEnabled(!busy);
    response_compute_->setEnabled(!busy);
    parameters_refresh_->setEnabled(!busy);
    channels_->setEnabled(!busy);
    spectrum_kind_->setEnabled(!busy);
    spectrum_window_->setEnabled(!busy);
    welch_->setEnabled(!busy);
    segment_length_->setEnabled(!busy && welch_->isChecked());
    overlap_->setEnabled(!busy && welch_->isChecked());
    nfft_->setEnabled(!busy);
}

void AnalysisDialog::show_diagnostics(const Diagnostics& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        const QString text = QString::fromStdString(describe(diagnostic));
        if (diagnostic.severity == Severity::Error) {
            status_->setStyleSheet(QStringLiteral("color: #b3261e;"));
            status_->setText(text);
        } else {
            status_->setStyleSheet(QString());
            status_->setText(text);
        }
    }
}

void AnalysisDialog::compute_spectrum() {
    if (busy_)
        return;
    SpectrumOptions options;
    options.kind = static_cast<SpectrumKind>(spectrum_kind_->currentData().toInt());
    options.window = static_cast<WindowType>(spectrum_window_->currentData().toInt());
    const bool welch = welch_->isChecked() || options.kind == SpectrumKind::Psd;
    options.welch = welch;
    options.remove_mean = remove_mean_->isChecked();
    options.segment_length = static_cast<std::size_t>(segment_length_->value());
    options.overlap_fraction = overlap_->value();
    if (nfft_->value() > 0) {
        if (welch)
            options.fft_length = static_cast<std::size_t>(nfft_->value());
        else
            options.nfft = static_cast<std::size_t>(nfft_->value());
    }
    Trace copy = trace();
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    const auto cancel = cancel_;
    options.stop = [cancel] { return cancel->load(); };
    set_busy(true);
    status_->setStyleSheet(QString());
    status_->setText(tr("Computing the spectrum…"));
    run_background(
        this,
        [copy = std::move(copy), options]() mutable {
            Diagnostics diagnostics;
            Spectrum spectrum = calculate_spectrum(copy, options, diagnostics);
            return std::pair<Spectrum, Diagnostics>{std::move(spectrum), std::move(diagnostics)};
        },
        [this](const TaskResult<std::pair<Spectrum, Diagnostics>>& result) {
            set_busy(false);
            if (!result.error.isEmpty()) {
                status_->setText(result.error);
                return;
            }
            const auto& spectrum = result.value->first;
            const auto& diagnostics = result.value->second;
            show_diagnostics(diagnostics);
            if (has_errors(diagnostics) || !spectrum.available) {
                spectrum_valid_ = false;
                spectrum_plot_->set_series({});
                return;
            }
            spectrum_result_ = spectrum;
            spectrum_valid_ = true;
            spectrum_plot_->set_axis_titles(tr("Frequency (Hz)"), QString::fromStdString(spectrum.unit_text));
            spectrum_plot_->set_series(curve(spectrum.frequency_hz, spectrum.value));
            spectrum_summary_->setText(
                tr("%1 samples · %2 segments · window: %3 · %4 Hz resolution · peak %5 Hz")
                    .arg(spectrum.samples)
                    .arg(spectrum.segments)
                    .arg(window_text(spectrum.window))
                    .arg(number(spectrum.frequency_resolution_hz.value_or(0.0)))
                    .arg(number(spectrum.peak_frequency_hz)));
            if (!has_errors(diagnostics) && diagnostics.empty())
                status_->setText(tr("Spectrum ready."));
        });
}

void AnalysisDialog::compute_response() {
    if (busy_)
        return;
    ResponseSpectrumOptions options;
    options.damping_ratio = damping_->value();
    options.period_min_seconds = period_min_->value();
    options.period_max_seconds = period_max_->value();
    options.period_step_seconds = period_step_->value();
    Trace copy = trace();
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    const auto cancel = cancel_;
    options.stop = [cancel] { return cancel->load(); };
    set_busy(true);
    status_->setStyleSheet(QString());
    status_->setText(tr("Computing the response spectrum…"));
    run_background(
        this,
        [copy = std::move(copy), options]() mutable {
            Diagnostics diagnostics;
            ResponseSpectrum spectrum = calculate_response_spectrum(copy, options, diagnostics);
            return std::pair<ResponseSpectrum, Diagnostics>{std::move(spectrum), std::move(diagnostics)};
        },
        [this](const TaskResult<std::pair<ResponseSpectrum, Diagnostics>>& result) {
            set_busy(false);
            if (!result.error.isEmpty()) {
                status_->setText(result.error);
                return;
            }
            const auto& spectrum = result.value->first;
            const auto& diagnostics = result.value->second;
            show_diagnostics(diagnostics);
            if (has_errors(diagnostics) || !spectrum.available) {
                response_valid_ = false;
                response_plot_->set_series({});
                return;
            }
            response_result_ = spectrum;
            response_valid_ = true;
            response_summary_->setText(tr("%1 periods · damping %2% · %3 samples · %4")
                                           .arg(spectrum.points)
                                           .arg(number(spectrum.damping_ratio * 100.0))
                                           .arg(spectrum.samples)
                                           .arg(QString::fromStdString(std::string{name(spectrum.source_unit)})));
            response_plot_->set_axis_titles(tr("Period (s)"), tr("Sa (m/s²)"));
            response_plot_->set_series(curve(spectrum.period_seconds, spectrum.sa));
            if (diagnostics.empty())
                status_->setText(tr("Response spectrum ready."));
        });
}

void AnalysisDialog::compute_parameters() {
    const Trace& item = trace();
    Diagnostics statistics_diagnostics;
    statistics_ = calculate_statistics(item, statistics_diagnostics);
    statistics_valid_ = !has_errors(statistics_diagnostics);

    IntensityOptions options;
    options.integration = static_cast<IntegrationScheme>(integration_->currentData().toInt());
    Diagnostics intensity_diagnostics;
    intensity_ = calculate_intensity(item, options, intensity_diagnostics);
    intensity_valid_ = intensity_.available && !has_errors(intensity_diagnostics);

    parameters_->setRowCount(0);
    const auto add = [this](const QString& label, const QString& value, const QString& tooltip = {}) {
        const int row = parameters_->rowCount();
        parameters_->insertRow(row);
        auto* name = new QTableWidgetItem(label);
        auto* content = new QTableWidgetItem(value);
        if (!tooltip.isEmpty())
            content->setToolTip(tooltip);
        parameters_->setItem(row, 0, name);
        parameters_->setItem(row, 1, content);
    };
    const auto add_optional = [&](const QString& label, const std::optional<double>& value, const QString& unit) {
        if (value)
            add(label, QStringLiteral("%1 %2").arg(number(*value), unit).trimmed());
        else
            add(label, tr("unavailable"));
    };
    if (statistics_valid_) {
        add(tr("Samples"), QString::number(statistics_.samples));
        add_optional(tr("Sampling interval"), statistics_.dt_seconds, QStringLiteral("s"));
        add_optional(tr("Sampling rate"), statistics_.sampling_rate_hz, QStringLiteral("Hz"));
        add_optional(tr("Duration"), statistics_.duration_seconds, QStringLiteral("s"));
        add(tr("Minimum"), number(statistics_.minimum));
        add(tr("Maximum"), number(statistics_.maximum));
        add(tr("Absolute peak"), number(statistics_.absolute_peak));
        add(tr("Signed peak"), number(statistics_.signed_peak));
        add(tr("Peak sample"), QString::number(statistics_.peak_index + 1));
        add_optional(tr("Peak time"), statistics_.peak_time_seconds, QStringLiteral("s"));
        add(tr("Mean"), number(statistics_.mean));
        add(tr("Standard deviation"), number(statistics_.standard_deviation));
        add(tr("RMS"), number(statistics_.rms));
    } else {
        show_diagnostics(statistics_diagnostics);
    }

    const auto scheme = static_cast<IntegrationScheme>(integration_->currentData().toInt());
    const QString suffix = scheme == IntegrationScheme::None ? QString() : tr(" (estimated by integration)");
    const auto value_or = [](const std::optional<double>& value, const QString& unit) {
        if (!value)
            return QCoreApplication::translate("gmtools::gui::AnalysisDialog", "unavailable");
        return QStringLiteral("%1 %2").arg(number(*value), unit).trimmed();
    };
    if (intensity_valid_) {
        add(tr("PGA") + suffix, value_or(intensity_.pga, QStringLiteral("m/s²")));
        add(tr("PGV") + suffix, value_or(intensity_.pgv, QStringLiteral("m/s")));
        add(tr("PGD") + suffix, value_or(intensity_.pgd, QStringLiteral("m")));
        add(tr("CAV") + suffix, value_or(intensity_.cav, QStringLiteral("m/s")));
        add(tr("Arias intensity"), value_or(intensity_.arias_intensity, QStringLiteral("m/s")));
        add(tr("Significant duration D5-75"), value_or(intensity_.significant_duration_5_75, QStringLiteral("s")));
        add(tr("Significant duration D5-95"), value_or(intensity_.significant_duration_5_95, QStringLiteral("s")));
    } else {
        add(tr("PGA"), value_or(intensity_.pga, QStringLiteral("m/s²")));
    }

    QString note;
    for (const auto& diagnostic : intensity_diagnostics) {
        if (!note.isEmpty())
            note += QStringLiteral("\n");
        note += QString::fromStdString(describe(diagnostic));
    }
    if (intensity_valid_)
        note += tr("Engineering values are computed on a temporary SI copy; the channel and its exported samples are unchanged.");
    parameters_note_->setText(note);
    if (intensity_valid_ && statistics_valid_)
        status_->setText(tr("Parameters ready."));
}

void AnalysisDialog::export_data() {
    QString text;
    QString suffix;
    if (current_tab() == Tab::Spectrum && spectrum_valid_) {
        text = QString::fromStdString(spectrum_csv(trace(), spectrum_result_));
        suffix = QStringLiteral("spectrum.csv");
    } else if (current_tab() == Tab::Response && response_valid_) {
        text = QString::fromStdString(response_spectrum_csv(trace(), response_result_));
        suffix = QStringLiteral("response.csv");
    } else if (current_tab() == Tab::Parameters && statistics_valid_) {
        const QString statistics = QString::fromStdString(statistics_json(trace(), statistics_)).trimmed();
        const QString intensity = QString::fromStdString(intensity_json(trace(), intensity_)).trimmed();
        text = QStringLiteral("{\n  \"statistics\": ") + statistics + QStringLiteral(",\n  \"intensity\": ") +
               intensity + QStringLiteral("\n}\n");
        suffix = QStringLiteral("parameters.json");
    } else {
        QMessageBox::information(this, tr("Export data"), tr("Compute a result on this tab before exporting."));
        return;
    }
    QString suggested = QString::fromStdString(trace().name);
    if (suggested.isEmpty())
        suggested = QStringLiteral("analysis");
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export analysis data"), last_directory_ + QStringLiteral("/") + suggested + QStringLiteral("-") + suffix,
        tr("Data files (*.csv *.json);;All files (*)"));
    if (path.isEmpty())
        return;
    last_directory_ = QFileInfo(path).absolutePath();
    if (QFileInfo::exists(path) &&
        QMessageBox::question(this, tr("Replace file"), tr("The file already exists. Replace it?")) !=
            QMessageBox::Yes)
        return;
    Diagnostics diagnostics;
    detail::save_text_file(gmtools::path_from_utf8(path.toStdString()), text.toStdString(), true, {}, diagnostics);
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == Severity::Error) {
            QMessageBox::warning(this, tr("Cannot export data"), QString::fromStdString(describe(diagnostic)));
            return;
        }
    }
    status_->setText(tr("Exported %1").arg(path));
}

void AnalysisDialog::save_image() {
    AnalysisPlot* plot = nullptr;
    switch (current_tab()) {
    case Tab::Spectrum:
        plot = spectrum_plot_;
        break;
    case Tab::Response:
        plot = response_plot_;
        break;
    case Tab::Parameters:
        break;
    default:
        plot = time_plot_;
        break;
    }
    if (!plot) {
        QMessageBox::information(this, tr("Save image"), tr("Choose the time history, spectrum or response spectrum tab."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Save image"),
                                                      last_directory_ + QStringLiteral("/analysis.png"),
                                                      tr("PNG images (*.png);;All files (*)"));
    if (path.isEmpty())
        return;
    last_directory_ = QFileInfo(path).absolutePath();
    if (QFileInfo::exists(path) &&
        QMessageBox::question(this, tr("Replace file"), tr("The file already exists. Replace it?")) !=
            QMessageBox::Yes)
        return;
    if (!plot->grab().save(path)) {
        QMessageBox::warning(this, tr("Cannot save image"), tr("The image could not be written to %1").arg(path));
        return;
    }
    status_->setText(tr("Saved %1").arg(path));
}

void AnalysisDialog::reject() {
    if (busy_) {
        QMessageBox::information(this, tr("Analysis running"),
                                 tr("Wait for the current calculation or cancel it before closing."));
        return;
    }
    QDialog::reject();
}

}  // namespace gmtools::gui
