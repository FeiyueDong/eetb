#pragma once

#include "gmtools/gmtools.hpp"

#include <QWidget>

namespace gmtools::gui {

struct PlotBucket {
    double first;
    double last;
    double minimum;
    double maximum;
};

struct PlotData {
    std::vector<PlotBucket> buckets;
    std::size_t sample_count{};
    double minimum{};
    double maximum{};
};

// A bounded display envelope, computed in the parsing worker. The source
// samples remain untouched and are the only data used by the Writers.
PlotData prepare_plot(const Trace& trace);

class TracePlot final : public QWidget {
public:
    TracePlot(const Trace& trace, PlotData data, std::size_t channel, QWidget* parent = nullptr);
    void set_metadata(const Trace& trace);
    void set_selected(bool selected);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    PlotData data_;
    std::size_t channel_;
    QString name_;
    QString unit_;
    std::optional<double> span_;
    bool selected_{};
};

} // namespace gmtools::gui
