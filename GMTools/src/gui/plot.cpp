#include "plot.hpp"
#include "ui_helpers.hpp"

#include <QCoreApplication>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>

#include <algorithm>
#include <cmath>
#include <utility>

namespace gmtools::gui {
namespace {

QString number(double value) {
    return QString::number(value, 'g', 6);
}

// Avoid overflowing size_t when partitioning a large sample array.
std::size_t boundary(std::size_t part, std::size_t count, std::size_t parts) {
    return part * (count / parts) + part * (count % parts) / parts;
}

} // namespace

PlotData prepare_plot(const Trace& trace) {
    PlotData result;
    result.sample_count = trace.samples.size();
    const auto count = std::min<std::size_t>(result.sample_count, 4096);
    result.buckets.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto* begin = trace.samples.data() + boundary(i, result.sample_count, count);
        const auto* end = trace.samples.data() + boundary(i + 1, result.sample_count, count);
        const auto [low, high] = std::minmax_element(begin, end);
        result.buckets.push_back({*begin, *(end - 1), *low, *high});
        result.minimum = i == 0 ? *low : std::min(result.minimum, *low);
        result.maximum = i == 0 ? *high : std::max(result.maximum, *high);
    }
    return result;
}

TracePlot::TracePlot(const Trace& trace, PlotData data, std::size_t channel, QWidget* parent)
    : QWidget(parent), data_(std::move(data)), channel_(channel) {
    set_metadata(trace);
    setMinimumWidth(260);
    setFixedHeight(218);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setToolTip(QCoreApplication::translate("gmtools::gui::TracePlot",
        "Waveform overview · peaks are preserved in the display envelope.\nExport uses the original samples."));
}

void TracePlot::set_metadata(const Trace& trace) {
    name_ = trace.name.empty() ? QCoreApplication::translate("gmtools::gui::TracePlot", "Unnamed channel") : as_text(trace.name);
    unit_ = as_text(gmtools::name(trace.unit));
    span_ = trace.sample_span();
    setAccessibleName(QCoreApplication::translate("gmtools::gui::TracePlot", "Channel %1: %2 waveform").arg(channel_).arg(name_));
    update();
}

void TracePlot::set_selected(bool selected) {
    selected_ = selected;
    update();
}

void TracePlot::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(palette().base());
    painter.setPen(QPen(selected_ ? palette().highlight().color() : palette().mid().color(),
                        selected_ ? 1.5 : 1.0));
    painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 10, 10);

    auto title_font = font();
    title_font.setBold(true);
    painter.setFont(title_font);
    painter.setPen(palette().text().color());
    const auto title = QStringLiteral("%1   %2").arg(channel_, 2, 10, QLatin1Char('0')).arg(name_);
    painter.drawText(QRect(16, 10, width() - 32, 23), Qt::AlignVCenter,
                     painter.fontMetrics().elidedText(title, Qt::ElideRight, width() - 32));
    painter.setFont(font());
    painter.setPen(palette().placeholderText().color());
    painter.drawText(QRect(16, 34, width() - 32, 20), Qt::AlignVCenter,
        painter.fontMetrics().elidedText(QCoreApplication::translate("gmtools::gui::TracePlot", "%1 samples  ·  %2")
            .arg(data_.sample_count).arg(unit_), Qt::ElideRight, width() - 32));

    const auto low_label = number(data_.minimum);
    const auto high_label = number(data_.maximum);
    const int left = std::max(64, std::max(painter.fontMetrics().horizontalAdvance(low_label),
                                          painter.fontMetrics().horizontalAdvance(high_label)) + 22);
    const QRectF area(left, 67, width() - left - 24, height() - 113);
    if (data_.buckets.empty() || area.width() <= 1 || area.height() <= 1)
        return;

    painter.setPen(QPen(palette().mid().color(), 0.7, Qt::DotLine));
    for (int i = 0; i <= 4; ++i) {
        const auto x = area.left() + area.width() * i / 4.0;
        const auto y = area.top() + area.height() * i / 4.0;
        painter.drawLine(QPointF{x, area.top()}, QPointF{x, area.bottom()});
        painter.drawLine(QPointF{area.left(), y}, QPointF{area.right(), y});
    }
    painter.setPen(palette().placeholderText().color());
    painter.drawText(QRectF(4, area.top() - 9, left - 13, 20), Qt::AlignRight, high_label);
    if (data_.minimum != data_.maximum)
        painter.drawText(QRectF(4, area.bottom() - 9, left - 13, 20), Qt::AlignRight, low_label);

    painter.drawText(QRectF(area.left(), area.bottom() + 8, area.width(), 18), Qt::AlignLeft,
                     span_ ? QStringLiteral("0") : QStringLiteral("1"));
    painter.drawText(QRectF(area.left(), area.bottom() + 8, area.width(), 18), Qt::AlignRight,
                     span_ ? number(*span_) : QString::number(data_.sample_count));
    painter.drawText(QRectF(area.left(), area.bottom() + 25, area.width(), 18), Qt::AlignHCenter,
                     span_ ? QCoreApplication::translate("gmtools::gui::TracePlot", "Elapsed time (s)") : QCoreApplication::translate("gmtools::gui::TracePlot", "Sample index"));

    const auto scale = std::max(std::abs(data_.minimum), std::abs(data_.maximum));
    const auto y = [&](double value) {
        if (data_.maximum == data_.minimum)
            return area.center().y();
        const auto low = data_.minimum / scale;
        const auto high = data_.maximum / scale;
        const auto range = high - low;
        const auto fraction = range > 0.0 ? (value / scale - low) / range
                                          : (value - data_.minimum) / (data_.maximum - data_.minimum);
        return std::clamp(area.bottom() - area.height() * fraction, area.top(), area.bottom());
    };
    painter.setClipRect(area.adjusted(-2, -2, 2, 2));
    if (data_.minimum < 0.0 && data_.maximum > 0.0) {
        painter.setPen(QPen(palette().mid().color(), 1));
        painter.drawLine(QPointF{area.left(), y(0)}, QPointF{area.right(), y(0)});
    }
    painter.setPen(QPen(palette().highlight().color(), 1.25));
    const auto& buckets = data_.buckets;
    if (data_.sample_count == 1) {
        painter.setBrush(palette().highlight());
        painter.drawEllipse(QPointF{area.left(), y(buckets.front().first)}, 2.5, 2.5);
        return;
    }
    const auto pixels = std::max<std::size_t>(2, static_cast<std::size_t>(area.width()));
    const auto count = std::min(buckets.size(), pixels);
    QPointF previous{area.left(), y(buckets.front().first)};
    for (std::size_t i = 0; i < count; ++i) {
        const auto first = boundary(i, buckets.size(), count);
        const auto last = boundary(i + 1, buckets.size(), count);
        auto low = buckets[first].minimum;
        auto high = buckets[first].maximum;
        for (auto j = first + 1; j < last; ++j) {
            low = std::min(low, buckets[j].minimum);
            high = std::max(high, buckets[j].maximum);
        }
        const auto x = area.left() + area.width() * static_cast<double>(i) / static_cast<double>(count - 1);
        painter.drawLine(previous, QPointF{x, y(buckets[first].first)});
        painter.drawLine(QPointF{x, y(low)}, QPointF{x, y(high)});
        previous = {x, y(buckets[last - 1].last)};
    }
}

} // namespace gmtools::gui
