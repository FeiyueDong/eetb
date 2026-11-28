#pragma once

#include <QWidget>

#include <vector>

namespace gmtools::gui {

// One X/Y curve. X must be sorted ascending; the plot downsamples for display
// only, the stored vectors remain the exact analysis result.
struct CurveSeries {
    std::vector<double> x;
    std::vector<double> y;
};

// A reusable, dependency-free 2D curve widget: linear or logarithmic axes,
// wheel zoom around the cursor, drag to pan, double-click to restore the full
// range, and a nearest-sample readout.
class AnalysisPlot final : public QWidget {
    Q_OBJECT
public:
    explicit AnalysisPlot(QWidget* parent = nullptr);

    void set_series(std::vector<CurveSeries> series);
    void set_axis_titles(QString x_title, QString y_title);
    void set_log_x(bool enabled);
    void set_log_y(bool enabled);
    bool log_x() const { return log_x_; }
    bool log_y() const { return log_y_; }
    void set_empty_text(QString text);
    void reset_view();

    QSize minimumSizeHint() const override { return {480, 260}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    struct Bounds {
        double min{};
        double max{};
    };
    struct Point {
        double x{};
        double y{};
    };

    QRectF plot_rect() const;
    double to_pixel_x(double value, const QRectF& rect) const;
    double to_pixel_y(double value, const QRectF& rect) const;
    double from_pixel_x(double pixel, const QRectF& rect) const;
    double from_pixel_y(double pixel, const QRectF& rect) const;
    void data_range(Bounds& x, Bounds& y) const;
    void draw_series(QPainter&, const QRectF& rect);
    void draw_axes(QPainter&, const QRectF& rect);
    void draw_readout(QPainter&, const QRectF& rect);

    std::vector<CurveSeries> series_;
    QString x_title_;
    QString y_title_;
    QString empty_text_;
    bool log_x_{};
    bool log_y_{};
    Bounds x_range_{0.0, 1.0};
    Bounds y_range_{0.0, 1.0};
    bool full_view_{true};
    bool dragging_{};
    QPoint drag_start_;
    Bounds drag_x_{};
    Bounds drag_y_{};
    QPoint cursor_{-1, -1};
    bool cursor_inside_{};
};

} // namespace gmtools::gui
