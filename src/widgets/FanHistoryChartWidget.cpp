#include "widgets/FanHistoryChartWidget.h"

#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

#include <algorithm>
#include <cmath>

namespace ccqt {
namespace {

constexpr qint64 kWindowSpanMs = 120000;

} // namespace

FanHistoryChartWidget::FanHistoryChartWidget(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void FanHistoryChartWidget::setHistory(const std::deque<FanHistoryPoint>& points)
{
    m_points = points;
    update();
}

void FanHistoryChartWidget::clear()
{
    m_points.clear();
    update();
}

QSize FanHistoryChartWidget::sizeHint() const
{
    const int fh = fontMetrics().height();
    return QSize(std::max(320, fh * 22), std::max(140, fh * 9));
}

QSize FanHistoryChartWidget::minimumSizeHint() const
{
    const int fh = fontMetrics().height();
    return QSize(std::max(220, fh * 14), std::max(105, fh * 6));
}

void FanHistoryChartWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QFont smallFont = font();
    smallFont.setPointSizeF(std::max(7.5, smallFont.pointSizeF() - 1.5));
    p.setFont(smallFont);
    const QFontMetrics fm(smallFont);

    const qreal leftMargin = fm.horizontalAdvance(QStringLiteral("100%")) + 10.0;
    const qreal rightMargin = fm.horizontalAdvance(QStringLiteral("10000")) + 12.0;
    const qreal topMargin = fm.height() + 10.0;
    const qreal bottomMargin = fm.height() + 8.0;

    const QRectF plotRect = QRectF(rect()).adjusted(leftMargin, topMargin, -rightMargin, -bottomMargin);
    if (plotRect.width() <= 20.0 || plotRect.height() <= 20.0) {
        return;
    }

    const QColor dutyColor = palette().color(QPalette::Highlight);
    const QColor rpmColor(224, 150, 35);
    const QColor mutedColor = palette().color(QPalette::PlaceholderText);

    // Top legend
    const qreal legendY = 4.0 + fm.height() * 0.5;
    p.setPen(Qt::NoPen);
    p.setBrush(dutyColor);
    p.drawEllipse(QPointF(plotRect.left() + 5.0, legendY), 4.0, 4.0);
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(QRectF(plotRect.left() + 14.0, 2.0, 140.0, fm.height() + 2.0),
               Qt::AlignVCenter | Qt::AlignLeft,
               tr("Duty (%)"));

    const QString rpmTitle = tr("RPM");
    const qreal rpmLabelW = fm.horizontalAdvance(rpmTitle) + 4.0;
    p.setPen(Qt::NoPen);
    p.setBrush(rpmColor);
    p.drawEllipse(QPointF(plotRect.right() - rpmLabelW - 8.0, legendY), 4.0, 4.0);
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(QRectF(plotRect.right() - rpmLabelW, 2.0, rpmLabelW, fm.height() + 2.0),
               Qt::AlignVCenter | Qt::AlignRight,
               rpmTitle);

    // Plot box
    QColor bg = palette().color(QPalette::Base);
    bg.setAlpha(60);
    QColor border = palette().color(QPalette::Mid);
    border.setAlpha(105);
    p.setPen(QPen(border, 1.0));
    p.setBrush(bg);
    p.drawRoundedRect(plotRect, 4.0, 4.0);

    // Compute RPM max scale cleanly
    quint32 peakRpm = 0;
    for (const FanHistoryPoint& pt : m_points) {
        if (pt.rpm.has_value()) {
            peakRpm = std::max(peakRpm, *pt.rpm);
        }
    }
    const quint32 rpmStep = std::max<quint32>(
        250u,
        static_cast<quint32>(std::ceil(static_cast<double>(std::max<quint32>(peakRpm, 1000u))
                                       / 4.0 / 250.0))
            * 250u);
    const quint32 rpmMax = rpmStep * 4u;

    // Horizontal grid lines + left/right axis ticks
    QColor gridColor = palette().color(QPalette::Mid);
    gridColor.setAlpha(55);
    for (int i = 0; i <= 4; ++i) {
        const qreal frac = i / 4.0;
        const qreal y = plotRect.bottom() - frac * plotRect.height();
        if (i > 0 && i < 4) {
            p.setPen(QPen(gridColor, 1.0, Qt::DashLine));
            p.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
        }

        const int pctLabel = i * 25;
        p.setPen(mutedColor);
        p.drawText(QRectF(2.0, y - fm.height() * 0.5, leftMargin - 6.0, fm.height()),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("%1%").arg(pctLabel));

        if (peakRpm > 0) {
            const quint32 rpmVal = rpmStep * static_cast<quint32>(i);
            p.drawText(QRectF(plotRect.right() + 5.0,
                              y - fm.height() * 0.5,
                              rightMargin - 6.0,
                              fm.height()),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QString::number(rpmVal));
        }
    }

    // Bottom X axis labels (-120s, -60s, Now)
    p.setPen(mutedColor);
    const qreal xAxisY = plotRect.bottom() + 2.0;
    p.drawText(QRectF(plotRect.left(), xAxisY, 55.0, fm.height()),
               Qt::AlignLeft | Qt::AlignTop,
               tr("-120s"));
    p.drawText(QRectF(plotRect.center().x() - 28.0, xAxisY, 56.0, fm.height()),
               Qt::AlignCenter | Qt::AlignTop,
               tr("-60s"));
    p.drawText(QRectF(plotRect.right() - 60.0, xAxisY, 60.0, fm.height()),
               Qt::AlignRight | Qt::AlignTop,
               tr("Now"));

    if (m_points.size() < 2) {
        p.setPen(mutedColor);
        p.drawText(plotRect, Qt::AlignCenter, tr("Collecting fan history..."));
        return;
    }

    const qint64 latestMs = m_points.back().timestampMs;

    auto xForTime = [&](qint64 tsMs) -> qreal {
        const qint64 age = std::clamp<qint64>(latestMs - tsMs, 0, kWindowSpanMs);
        const double norm = 1.0 - static_cast<double>(age) / static_cast<double>(kWindowSpanMs);
        return plotRect.left() + norm * plotRect.width();
    };

    QPainterPath dutyLine;
    QPainterPath dutyArea;
    bool hasDutyStart = false;
    qreal lastDutyX = plotRect.right();

    QPainterPath rpmLine;
    bool hasRpmStart = false;

    for (const FanHistoryPoint& pt : m_points) {
        const qreal x = xForTime(pt.timestampMs);
        if (pt.duty.has_value()) {
            const double normY = std::clamp(*pt.duty, 0.0, 100.0) / 100.0;
            const qreal y = plotRect.bottom() - normY * plotRect.height();
            if (!hasDutyStart) {
                dutyLine.moveTo(x, y);
                dutyArea.moveTo(x, plotRect.bottom());
                dutyArea.lineTo(x, y);
                hasDutyStart = true;
            } else {
                dutyLine.lineTo(x, y);
                dutyArea.lineTo(x, y);
            }
            lastDutyX = x;
        }
        if (pt.rpm.has_value() && rpmMax > 0) {
            const double normY = std::clamp(static_cast<double>(*pt.rpm) / static_cast<double>(rpmMax),
                                            0.0,
                                            1.0);
            const qreal y = plotRect.bottom() - normY * plotRect.height();
            if (!hasRpmStart) {
                rpmLine.moveTo(x, y);
                hasRpmStart = true;
            } else {
                rpmLine.lineTo(x, y);
            }
        }
    }

    p.save();
    p.setClipRect(plotRect.adjusted(0.5, 0.5, -0.5, -0.5));

    if (hasDutyStart) {
        dutyArea.lineTo(lastDutyX, plotRect.bottom());
        dutyArea.closeSubpath();
        QColor fill = dutyColor;
        fill.setAlpha(36);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPath(dutyArea);

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(dutyColor, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(dutyLine);
    }

    if (hasRpmStart) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(rpmColor, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(rpmLine);
    }

    p.restore();
}

void FanHistoryChartWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange || event->type() == QEvent::PaletteChange
        || event->type() == QEvent::StyleChange) {
        update();
    }
}

} // namespace ccqt
