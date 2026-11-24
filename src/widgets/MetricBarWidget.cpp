#include "widgets/MetricBarWidget.h"

#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

#include <algorithm>
#include <cmath>

namespace ccqt {
namespace {

constexpr std::size_t kMaxSparklineSamples = 60;

} // namespace

MetricBarWidget::MetricBarWidget(Kind kind, QWidget* parent)
    : QWidget(parent)
    , m_kind(kind)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void MetricBarWidget::setRange(double minVal, double maxVal)
{
    if (maxVal <= minVal) {
        maxVal = minVal + 1.0;
    }
    m_min = minVal;
    m_max = maxVal;
    update();
}

void MetricBarWidget::setValue(double value)
{
    m_value = value;
    m_hasValue = true;
    update();
}

void MetricBarWidget::clearValue()
{
    m_hasValue = false;
    update();
}

void MetricBarWidget::setSparklineEnabled(bool enabled)
{
    if (m_sparklineEnabled == enabled) {
        return;
    }
    m_sparklineEnabled = enabled;
    updateGeometry();
    update();
}

void MetricBarWidget::pushSample(double value)
{
    if (!std::isfinite(value)) {
        return;
    }
    m_samples.push_back(value);
    while (m_samples.size() > kMaxSparklineSamples) {
        m_samples.pop_front();
    }
    if (m_sparklineEnabled) {
        update();
    }
}

QSize MetricBarWidget::sizeHint() const
{
    const int fh = fontMetrics().height();
    const int h = std::max(14, fh);
    const int w = m_sparklineEnabled ? std::max(150, fh * 10) : std::max(90, fh * 6);
    return QSize(w, h);
}

QSize MetricBarWidget::minimumSizeHint() const
{
    const int fh = fontMetrics().height();
    const int h = std::max(12, fh - 2);
    const int w = m_sparklineEnabled ? std::max(100, fh * 6) : std::max(60, fh * 4);
    return QSize(w, h);
}

QColor MetricBarWidget::activeAccentColor() const
{
    if (m_kind == Kind::Temperature && m_hasValue) {
        if (m_value >= 85.0) {
            return QColor(214, 69, 65);
        }
        if (m_value >= 72.0) {
            return QColor(224, 150, 35);
        }
    }
    return palette().color(QPalette::Highlight);
}

void MetricBarWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF full = QRectF(rect()).adjusted(0.5, 1.5, -0.5, -1.5);
    if (full.width() <= 4.0 || full.height() <= 4.0) {
        return;
    }

    const bool drawSparkline = m_sparklineEnabled && full.width() >= 100.0;
    const qreal gap = drawSparkline ? 6.0 : 0.0;
    const qreal sparkWidth = drawSparkline ? std::clamp(full.width() * 0.42, 42.0, 96.0) : 0.0;
    const qreal barTotalWidth = full.width() - gap - sparkWidth;

    const qreal barHeight = std::min(full.height(), 8.0);
    const qreal barTop = full.center().y() - barHeight * 0.5;
    const QRectF barRect(full.left(), barTop, barTotalWidth, barHeight);
    const qreal radius = std::min(4.0, barHeight * 0.5);

    QColor trackFill = palette().color(QPalette::Mid);
    trackFill.setAlpha(60);
    QColor trackBorder = palette().color(QPalette::Mid);
    trackBorder.setAlpha(105);

    painter.setPen(QPen(trackBorder, 1.0));
    painter.setBrush(trackFill);
    painter.drawRoundedRect(barRect, radius, radius);

    const QColor accent = activeAccentColor();
    if (m_hasValue) {
        const double norm = std::clamp((m_value - m_min) / (m_max - m_min), 0.0, 1.0);
        const qreal fillWidth = barRect.width() * norm;
        if (fillWidth > 1.0) {
            painter.save();
            QPainterPath clipPath;
            clipPath.addRoundedRect(barRect, radius, radius);
            painter.setClipPath(clipPath);
            painter.setPen(Qt::NoPen);
            painter.setBrush(accent);
            painter.drawRect(QRectF(barRect.left(), barRect.top(), fillWidth, barRect.height()));
            painter.restore();
        }
    }

    if (!drawSparkline) {
        return;
    }

    const QRectF sparkRect(barRect.right() + gap, full.top(), sparkWidth, full.height());
    QColor sparkBg = palette().color(QPalette::Mid);
    sparkBg.setAlpha(28);
    QColor sparkBorder = palette().color(QPalette::Mid);
    sparkBorder.setAlpha(70);

    painter.setPen(QPen(sparkBorder, 1.0));
    painter.setBrush(sparkBg);
    painter.drawRoundedRect(sparkRect, 3.0, 3.0);

    const QRectF plotRect = sparkRect.adjusted(2.0, 2.0, -2.0, -2.0);
    if (m_samples.size() < 2) {
        QColor baseLine = palette().color(QPalette::Mid);
        baseLine.setAlpha(120);
        painter.setPen(QPen(baseLine, 1.0));
        const qreal midY = plotRect.center().y();
        painter.drawLine(QPointF(plotRect.left(), midY), QPointF(plotRect.right(), midY));
        return;
    }

    auto [minIt, maxIt] = std::minmax_element(m_samples.cbegin(), m_samples.cend());
    double sMin = *minIt;
    double sMax = *maxIt;
    constexpr double kMinSpan = 10.0;
    if ((sMax - sMin) < kMinSpan) {
        const double center = 0.5 * (sMax + sMin);
        sMin = center - 0.5 * kMinSpan;
        sMax = center + 0.5 * kMinSpan;
    }

    const std::size_t n = m_samples.size();
    const qreal stepX = plotRect.width() / static_cast<qreal>(kMaxSparklineSamples - 1);
    const qreal startX = plotRect.right() - stepX * static_cast<qreal>(n - 1);

    QPainterPath linePath;
    QPainterPath fillPath;
    for (std::size_t i = 0; i < n; ++i) {
        const double normY = std::clamp((m_samples[i] - sMin) / (sMax - sMin), 0.0, 1.0);
        const qreal x = startX + stepX * static_cast<qreal>(i);
        const qreal y = plotRect.bottom() - normY * plotRect.height();
        if (i == 0) {
            linePath.moveTo(x, y);
            fillPath.moveTo(x, plotRect.bottom());
            fillPath.lineTo(x, y);
        } else {
            linePath.lineTo(x, y);
            fillPath.lineTo(x, y);
        }
    }
    fillPath.lineTo(plotRect.right(), plotRect.bottom());
    fillPath.closeSubpath();

    QColor areaColor = accent;
    areaColor.setAlpha(45);
    painter.save();
    QPainterPath sparkClip;
    sparkClip.addRoundedRect(sparkRect, 3.0, 3.0);
    painter.setClipPath(sparkClip);

    painter.setPen(Qt::NoPen);
    painter.setBrush(areaColor);
    painter.drawPath(fillPath);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(accent, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(linePath);
    painter.restore();
}

} // namespace ccqt
