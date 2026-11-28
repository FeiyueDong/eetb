#include "analysis_plot.hpp"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace gmtools::gui {
namespace {

double clamp_positive(double value) {
    return value > 0.0 ? value : std::numeric_limits<double>::min();
}

QString format_value(double value) {
    return QString::number(value, 'g', 6);
}

// 1-2-5 tick steps, extended to decades on logarithmic axes.
std::vector<double> nice_ticks(double low, double high, int target, bool logarithmic) {
    std::vector<double> ticks;
    if (!(high > low) || !std::isfinite(low) || !std::isfinite(high))
        return ticks;
    if (logarithmic) {
        const double first = std::floor(std::log10(clamp_positive(low)));
        const double last = std::ceil(std::log10(clamp_positive(high)));
        for (double exponent = first; exponent <= last; exponent += 1.0) {
            const double base = std::pow(10.0, exponent);
            for (const double factor : {1.0, 2.0, 5.0}) {
                const double value = static_cast<double>(factor) * base;
                if (value >= low && value <= high)
                    ticks.push_back(value);
            }
        }
        return ticks;
    }
    const double span = high - low;
    const double rough = span / std::max(1, target);
    const double magnitude = std::pow(10.0, std::floor(std::log10(rough)));
    double step = magnitude;
    for (const double factor : {1.0, 2.0, 2.5, 5.0, 10.0}) {
        if (magnitude * factor >= rough) {
            step = magnitude * factor;
            break;
        }
    }
    const double start = std::ceil(low / step) * step;
    for (double value = start; value <= high + step * 1e-9; value += step)
        ticks.push_back(value);
    if (ticks.size() > 200)
        ticks.resize(200);
    return ticks;
}

}  // namespace

AnalysisPlot::AnalysisPlot(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void AnalysisPlot::set_series(std::vector<CurveSeries> series) {
    series_ = std::move(series);
    full_view_ = true;
    reset_view();
    update();
}

void AnalysisPlot::set_axis_titles(QString x_title, QString y_title) {
    x_title_ = std::move(x_title);
    y_title_ = std::move(y_title);
    update();
}

void AnalysisPlot::set_log_x(bool enabled) {
    log_x_ = enabled;
    full_view_ = true;
    reset_view();
    update();
}

void AnalysisPlot::set_log_y(bool enabled) {
    log_y_ = enabled;
    full_view_ = true;
    reset_view();
    update();
}

void AnalysisPlot::set_empty_text(QString text) {
    empty_text_ = std::move(text);
    update();
}

void AnalysisPlot::reset_view() {
    Bounds x, y;
    data_range(x, y);
    x_range_ = x;
    y_range_ = y;
    full_view_ = true;
    update();
}

void AnalysisPlot::data_range(Bounds& x, Bounds& y) const {
    x = {std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest()};
    y = x;
    bool found = false;
    for (const auto& curve : series_) {
        for (std::size_t i = 0; i < curve.x.size() && i < curve.y.size(); ++i) {
            const double px = curve.x[i];
            const double py = curve.y[i];
            if (!std::isfinite(px) || !std::isfinite(py))
                continue;
            if (log_x_ && !(px > 0.0))
                continue;
            if (log_y_ && !(py > 0.0))
                continue;
            x.min = std::min(x.min, px);
            x.max = std::max(x.max, px);
            y.min = std::min(y.min, py);
            y.max = std::max(y.max, py);
            found = true;
        }
    }
    if (!found) {
        x = {0.0, 1.0};
        y = {0.0, 1.0};
    }
    if (x.max <= x.min)
        x.max = x.min + 1.0;
    if (y.max <= y.min) {
        const double pad = std::abs(y.min) * 0.05 + 1e-12;
        y.min -= pad;
        y.max += pad;
    } else {
        const double pad = (y.max - y.min) * 0.06;
        y.min -= pad;
        y.max += pad;
    }
}

QRectF AnalysisPlot::plot_rect() const {
    const double left = 74.0;
    const double right = 18.0;
    const double top = 14.0;
    const double bottom = 52.0;
    return QRectF(left, top, std::max(10.0, width() - left - right), std::max(10.0, height() - top - bottom));
}

double AnalysisPlot::to_pixel_x(double value, const QRectF& rect) const {
    const double low = log_x_ ? std::log10(clamp_positive(x_range_.min)) : x_range_.min;
    const double high = log_x_ ? std::log10(clamp_positive(x_range_.max)) : x_range_.max;
    const double position = log_x_ ? std::log10(clamp_positive(value)) : value;
    return rect.left() + (position - low) / (high - low) * rect.width();
}

double AnalysisPlot::to_pixel_y(double value, const QRectF& rect) const {
    const double low = log_y_ ? std::log10(clamp_positive(y_range_.min)) : y_range_.min;
    const double high = log_y_ ? std::log10(clamp_positive(y_range_.max)) : y_range_.max;
    const double position = log_y_ ? std::log10(clamp_positive(value)) : value;
    return rect.bottom() - (position - low) / (high - low) * rect.height();
}

double AnalysisPlot::from_pixel_x(double pixel, const QRectF& rect) const {
    const double low = log_x_ ? std::log10(clamp_positive(x_range_.min)) : x_range_.min;
    const double high = log_x_ ? std::log10(clamp_positive(x_range_.max)) : x_range_.max;
    const double position = low + (pixel - rect.left()) / rect.width() * (high - low);
    return log_x_ ? std::pow(10.0, position) : position;
}

double AnalysisPlot::from_pixel_y(double pixel, const QRectF& rect) const {
    const double low = log_y_ ? std::log10(clamp_positive(y_range_.min)) : y_range_.min;
    const double high = log_y_ ? std::log10(clamp_positive(y_range_.max)) : y_range_.max;
    const double position = low + (rect.bottom() - pixel) / rect.height() * (high - low);
    return log_y_ ? std::pow(10.0, position) : position;
}

void AnalysisPlot::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const auto palette = this->palette();
    painter.fillRect(rect(), palette.base());

    const QRectF area = plot_rect();
    painter.fillRect(area, palette.window());

    if (series_.empty()) {
        painter.setPen(palette.color(QPalette::Mid));
        painter.drawText(area, Qt::AlignCenter, empty_text_.isEmpty() ? tr("No analysis result yet") : empty_text_);
        draw_axes(painter, area);
        return;
    }
    draw_axes(painter, area);
    draw_series(painter, area);
    draw_readout(painter, area);
}

void AnalysisPlot::draw_axes(QPainter& painter, const QRectF& area) {
    const auto palette = this->palette();
    const auto text_color = palette.color(QPalette::WindowText);
    const auto grid_color = palette.color(QPalette::Mid);
    painter.setPen(QPen(grid_color, 1.0, Qt::DotLine));
    const auto xticks = nice_ticks(x_range_.min, x_range_.max, std::max(3, static_cast<int>(area.width() / 90.0)), log_x_);
    const auto yticks = nice_ticks(y_range_.min, y_range_.max, std::max(3, static_cast<int>(area.height() / 44.0)), log_y_);
    QFont small = font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.0));
    painter.setFont(small);
    const QFontMetricsF metrics(small);
    for (const double tick : xticks) {
        const double x = to_pixel_x(tick, area);
        if (x < area.left() - 1 || x > area.right() + 1)
            continue;
        painter.setPen(QPen(grid_color, 1.0, Qt::DotLine));
        painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        painter.setPen(text_color);
        const QString label = format_value(tick);
        painter.drawText(QPointF(x - metrics.horizontalAdvance(label) / 2.0, area.bottom() + metrics.height() + 2),
                         label);
    }
    for (const double tick : yticks) {
        const double y = to_pixel_y(tick, area);
        if (y < area.top() - 1 || y > area.bottom() + 1)
            continue;
        painter.setPen(QPen(grid_color, 1.0, Qt::DotLine));
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        painter.setPen(text_color);
        const QString label = format_value(tick);
        painter.drawText(QPointF(area.left() - metrics.horizontalAdvance(label) - 6, y + metrics.ascent() / 2.0), label);
    }
    painter.setPen(QPen(text_color, 1.0));
    painter.drawRect(area);
    painter.drawText(QPointF(area.center().x() - metrics.horizontalAdvance(x_title_) / 2.0, height() - 8), x_title_);
    painter.save();
    painter.translate(14, area.center().y() + metrics.horizontalAdvance(y_title_) / 2.0);
    painter.rotate(-90);
    painter.drawText(QPointF(0, 0), y_title_);
    painter.restore();
}

void AnalysisPlot::draw_series(QPainter& painter, const QRectF& area) {
    const auto palette = this->palette();
    const QColor primary = palette.color(QPalette::Highlight);
    const QColor secondary = palette.color(QPalette::Link);
    const double pixel_width = std::max(1.0, area.width());
    int color_index = 0;
    for (const auto& curve : series_) {
        const QColor color = color_index++ == 0 ? primary : secondary;
        painter.setPen(QPen(color, 1.4));
        const std::size_t count = std::min(curve.x.size(), curve.y.size());
        if (count < 2)
            continue;
        const double bucket_width = (x_range_.max - x_range_.min) / pixel_width;
        if (static_cast<double>(count) <= pixel_width * 4.0 || log_x_ || !(bucket_width > 0.0)) {
            QPainterPath path;
            bool started = false;
            for (std::size_t i = 0; i < count; ++i) {
                const double px = curve.x[i];
                const double py = curve.y[i];
                if (!std::isfinite(px) || !std::isfinite(py) || (log_x_ && !(px > 0)) || (log_y_ && !(py > 0))) {
                    started = false;
                    continue;
                }
                const QPointF point(to_pixel_x(px, area), to_pixel_y(py, area));
                if (!started) {
                    path.moveTo(point);
                    started = true;
                } else {
                    path.lineTo(point);
                }
            }
            painter.drawPath(path);
        } else {
            // Downsample: keep first/last/min/max per pixel bucket so the
            // envelope and the sample order remain faithful.
            std::size_t index = 0;
            while (index < count) {
                const double left_edge = curve.x[index];
                const double right_edge = left_edge + bucket_width;
                std::size_t lowest = index;
                std::size_t highest = index;
                std::size_t last = index;
                double lowest_y = std::numeric_limits<double>::max();
                double highest_y = std::numeric_limits<double>::lowest();
                for (std::size_t j = index; j < count && curve.x[j] <= right_edge; ++j) {
                    const double py = curve.y[j];
                    if (!std::isfinite(py))
                        continue;
                    if (py < lowest_y) {
                        lowest_y = py;
                        lowest = j;
                    }
                    if (py > highest_y) {
                        highest_y = py;
                        highest = j;
                    }
                    last = j;
                }
                const std::size_t middle = (lowest + highest) / 2;
                const std::size_t order[3] = {lowest, middle, highest};
                for (const std::size_t point_index : order) {
                    if (point_index < index || point_index >= count)
                        continue;
                    const double py = curve.y[point_index];
                    if (!std::isfinite(py))
                        continue;
                    const QPointF point(to_pixel_x(curve.x[point_index], area), to_pixel_y(py, area));
                    painter.drawLine(QPointF(point.x(), area.top()), QPointF(point.x(), area.bottom()));
                }
                index = last + 1;
            }
        }
    }
}

void AnalysisPlot::draw_readout(QPainter& painter, const QRectF& area) {
    if (!cursor_inside_ || series_.empty())
        return;
    const auto& curve = series_.front();
    if (curve.x.empty())
        return;
    const double target = from_pixel_x(cursor_.x(), area);
    const auto iterator = std::lower_bound(curve.x.begin(), curve.x.end(), target);
    std::size_t index = static_cast<std::size_t>(std::distance(curve.x.begin(), iterator));
    if (index >= curve.x.size())
        index = curve.x.size() - 1;
    if (index > 0 && std::abs(curve.x[index - 1] - target) <= std::abs(curve.x[index] - target))
        --index;
    const double px = curve.x[index];
    const double py = curve.y[index];
    if (!std::isfinite(px) || !std::isfinite(py))
        return;
    const QPointF point(to_pixel_x(px, area), to_pixel_y(py, area));
    painter.setPen(QPen(palette().color(QPalette::Text), 1.0));
    painter.drawLine(QPointF(point.x(), area.top()), QPointF(point.x(), area.bottom()));
    painter.drawEllipse(point, 3.0, 3.0);
    const QString text = QStringLiteral("%1  %2").arg(format_value(px), format_value(py));
    QFont small = font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.0));
    painter.setFont(small);
    const QFontMetricsF metrics(small);
    const QRectF box(point.x() + 8, point.y() - metrics.height() - 4, metrics.horizontalAdvance(text) + 10,
                     metrics.height() + 4);
    painter.fillRect(box, palette().color(QPalette::ToolTipBase));
    painter.setPen(palette().color(QPalette::ToolTipText));
    painter.drawText(box.adjusted(4, 0, 0, 0), Qt::AlignVCenter, text);
}

void AnalysisPlot::wheelEvent(QWheelEvent* event) {
    const QRectF area = plot_rect();
    const double factor = std::pow(1.0015, event->angleDelta().y());
    const double center_x = from_pixel_x(event->position().x(), area);
    const double center_y = from_pixel_y(event->position().y(), area);
    auto zoom = [&](Bounds& range, double center) {
        const bool logarithmic = &range == &x_range_ ? log_x_ : log_y_;
        if (logarithmic) {
            const double low = std::log10(clamp_positive(range.min));
            const double high = std::log10(clamp_positive(range.max));
            const double position = std::log10(clamp_positive(center));
            range.min = std::pow(10.0, position + (low - position) / factor);
            range.max = std::pow(10.0, position + (high - position) / factor);
        } else {
            range.min = center + (range.min - center) / factor;
            range.max = center + (range.max - center) / factor;
        }
    };
    zoom(x_range_, center_x);
    zoom(y_range_, center_y);
    full_view_ = false;
    update();
    event->accept();
}

void AnalysisPlot::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton)
        return;
    dragging_ = true;
    drag_start_ = event->position().toPoint();
    drag_x_ = x_range_;
    drag_y_ = y_range_;
    setCursor(Qt::ClosedHandCursor);
}

void AnalysisPlot::mouseMoveEvent(QMouseEvent* event) {
    cursor_ = event->position().toPoint();
    cursor_inside_ = plot_rect().contains(event->position());
    if (dragging_) {
        const QRectF area = plot_rect();
        const double dx = (event->position().x() - drag_start_.x()) / area.width() * (drag_x_.max - drag_x_.min);
        const double dy = (event->position().y() - drag_start_.y()) / area.height() * (drag_y_.max - drag_y_.min);
        x_range_.min = drag_x_.min - dx;
        x_range_.max = drag_x_.max - dx;
        y_range_.min = drag_y_.min + dy;
        y_range_.max = drag_y_.max + dy;
        full_view_ = false;
    }
    update();
}

void AnalysisPlot::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton)
        return;
    dragging_ = false;
    unsetCursor();
}

void AnalysisPlot::mouseDoubleClickEvent(QMouseEvent*) {
    reset_view();
}

void AnalysisPlot::leaveEvent(QEvent*) {
    cursor_inside_ = false;
    update();
}

}  // namespace gmtools::gui
