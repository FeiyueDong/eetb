#pragma once

#include "gmtools/analysis.hpp"
#include "gmtools/gmtools.hpp"

#include <QDialog>

#include <atomic>
#include <memory>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QCheckBox;

namespace gmtools::gui {

class AnalysisPlot;

// A read-only analysis window for one channel of the loaded file. It never
// changes the Trace, the export selection or the conversion settings.
class AnalysisDialog final : public QDialog {
    Q_OBJECT

public:
    AnalysisDialog(std::shared_ptr<const ReadResult> parsed, std::size_t channel, QWidget* parent = nullptr);

protected:
    void reject() override;

private:
    enum class Tab { Time, Spectrum, Response, Parameters };

    void build_ui();
    void populate_channels(std::size_t channel);
    const Trace& trace() const;
    void channel_changed(int index);
    void refresh_time_history();
    void refresh_summary();
    void refresh_tab();
    void compute_spectrum();
    void compute_response();
    void compute_parameters();
    void set_busy(bool busy);
    void show_diagnostics(const Diagnostics& diagnostics);
    void export_data();
    void save_image();
    void update_headings();
    Tab current_tab() const;

    std::shared_ptr<const ReadResult> parsed_;
    std::size_t channel_{};
    std::shared_ptr<std::atomic<bool>> cancel_;

    QComboBox* channels_{};
    QLabel* heading_{};
    QTabWidget* tabs_{};
    QLabel* status_{};

    AnalysisPlot* time_plot_{};
    QLabel* time_summary_{};

    AnalysisPlot* spectrum_plot_{};
    QComboBox* spectrum_kind_{};
    QComboBox* spectrum_window_{};
    QCheckBox* remove_mean_{};
    QCheckBox* welch_{};
    QSpinBox* segment_length_{};
    QDoubleSpinBox* overlap_{};
    QSpinBox* nfft_{};
    QCheckBox* log_x_{};
    QCheckBox* log_y_{};
    QPushButton* spectrum_compute_{};
    QLabel* spectrum_summary_{};
    Spectrum spectrum_result_;
    bool spectrum_valid_{};

    AnalysisPlot* response_plot_{};
    QComboBox* response_kind_{};
    QDoubleSpinBox* damping_{};
    QDoubleSpinBox* period_min_{};
    QDoubleSpinBox* period_max_{};
    QDoubleSpinBox* period_step_{};
    QPushButton* response_compute_{};
    QLabel* response_summary_{};
    ResponseSpectrum response_result_;
    bool response_valid_{};

    QTableWidget* parameters_{};
    QComboBox* integration_{};
    QPushButton* parameters_refresh_{};
    QLabel* parameters_note_{};
    Statistics statistics_;
    IntensityMeasures intensity_;
    bool statistics_valid_{};
    bool intensity_valid_{};

    bool busy_{};
    QString last_directory_;
};

}  // namespace gmtools::gui
