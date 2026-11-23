#include "plot.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QPalette>

#include <algorithm>
#include <cmath>

namespace {

QString as_text(std::string_view value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString number(double value) {
    return QString::number(value, 'g', 7);
}

} // namespace

TracePlot::TracePlot(const gmtools::Trace& trace, std::size_t channel, QWidget* parent)
    : QWidget(parent), trace_(trace), channel_(channel), minimum_(0), maximum_(0) {
    const auto [minimum, maximum] = std::ranges::minmax_element(trace.samples);
    if (minimum != trace.samples.end()) {
        minimum_ = *minimum;
        maximum_ = *maximum;
    }
    setMinimumSize(250, 160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void TracePlot::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().base());
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(palette().text().color());

    const auto title = QStringLiteral("%1  %2 · %3 samples · %4")
        .arg(channel_)
        .arg(trace_.name.empty() ? QStringLiteral("Unnamed") : as_text(trace_.name))
        .arg(trace_.samples.size())
        .arg(as_text(gmtools::name(trace_.unit)));
    painter.drawText(QRect(12, 4, width() - 24, 22), Qt::AlignLeft | Qt::AlignVCenter,
                     painter.fontMetrics().elidedText(title, Qt::ElideRight, width() - 24));

    constexpr int left = 78;
    const QRectF area(left, 31, width() - left - 20, height() - 65);
    if (trace_.samples.empty() || area.width() <= 1 || area.height() <= 1)
        return;

    const auto top_value = number(maximum_);
    const auto lower_value = number(minimum_);
    painter.setPen(palette().mid().color());
    painter.drawRect(area);
    painter.setPen(palette().text().color());
    painter.drawText(QRectF(3, area.top() - 8, left - 8, 16), Qt::AlignRight, top_value);
    if (minimum_ != maximum_)
        painter.drawText(QRectF(3, area.bottom() - 8, left - 8, 16), Qt::AlignRight, lower_value);

    const auto span = trace_.sample_span();
    const auto left_label = span ? QStringLiteral("0") : QStringLiteral("1");
    const auto right_label = span ? number(*span) : QString::number(trace_.samples.size());
    painter.drawText(QRectF(area.left(), area.bottom() + 4, area.width(), 16), Qt::AlignLeft, left_label);
    painter.drawText(QRectF(area.left(), area.bottom() + 4, area.width(), 16), Qt::AlignRight, right_label);
    painter.drawText(QRectF(area.left(), area.bottom() + 16, area.width(), 16), Qt::AlignHCenter,
                     span ? QStringLiteral("Elapsed time (s)") : QStringLiteral("Sample index"));

    painter.setClipRect(area.adjusted(-1, -1, 1, 1));
    painter.setPen(QPen(palette().highlight().color(), 1.35));
    const auto scale = std::max(std::abs(minimum_), std::abs(maximum_));
    const auto y = [&](double value) {
        if (maximum_ == minimum_)
            return area.center().y();
        const auto low = minimum_ / scale;
        const auto high = maximum_ / scale;
        const auto range = high - low;
        const auto fraction = range > 0.0 ? (value / scale - low) / range
                                           : (value - minimum_) / (maximum_ - minimum_);
        return std::clamp(area.bottom() - area.height() * fraction, area.top(), area.bottom());
    };

    const auto& samples = trace_.samples;
    if (samples.size() == 1) {
        painter.drawEllipse(QPointF{area.left(), y(samples.front())}, 2.0, 2.0);
        return;
    }
    const auto pixels = static_cast<std::size_t>(area.width());
    if (samples.size() <= pixels) {
        QPolygonF path;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const auto fraction = static_cast<double>(i) / static_cast<double>(samples.size() - 1);
            path.append({area.left() + area.width() * fraction, y(samples[i])});
        }
        painter.drawPolyline(path);
        return;
    }

    // Each pixel covers a range of samples. Preserve both extrema so an
    // isolated peak remains visible when the trace has more samples than pixels.
    QPointF previous{area.left(), y(samples.front())};
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        const auto bound = [&](std::size_t p) {
            return p * (samples.size() / pixels) + p * (samples.size() % pixels) / pixels;
        };
        const auto first = bound(pixel);
        const auto last = bound(pixel + 1);
        const auto* begin = samples.data() + first;
        const auto* end = samples.data() + last;
        const auto [lo, hi] = std::minmax_element(begin, end);
        const auto x = area.left() + static_cast<double>(pixel);
        painter.drawLine(previous, QPointF{x, y(*begin)});
        painter.drawLine(QPointF{x, y(*lo)}, QPointF{x, y(*hi)});
        previous = {x, y(*(end - 1))};
    }
}
