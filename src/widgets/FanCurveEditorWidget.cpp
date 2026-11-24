#include "widgets/FanCurveEditorWidget.h"

#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

#include <algorithm>
#include <cmath>

namespace ccqt {
namespace {

constexpr qreal kHitRadiusPx = 12.0;

double roundStep(double val, double step)
{
    return std::round(val / step) * step;
}

} // namespace

FanCurveEditorWidget::FanCurveEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void FanCurveEditorWidget::setPoints(const QVector<QPair<double, quint8>>& points)
{
    m_points = points;
    sortPoints();
    if (m_selectedIndex >= m_points.size()) {
        m_selectedIndex = m_points.isEmpty() ? -1 : m_points.size() - 1;
    }
    update();
}

const QVector<QPair<double, quint8>>& FanCurveEditorWidget::points() const
{
    return m_points;
}

void FanCurveEditorWidget::setBounds(double tempMin,
                                     double tempMax,
                                     quint8 minDuty,
                                     quint8 maxDuty,
                                     int minPoints,
                                     int maxPoints)
{
    m_tempMin = tempMin;
    m_tempMax = std::max(tempMin + 5.0, tempMax);
    m_minDuty = std::min<quint8>(minDuty, 100);
    m_maxDuty = std::clamp<quint8>(std::max(m_minDuty, maxDuty), 0, 100);
    m_minPoints = std::max(2, minPoints);
    m_maxPoints = std::max(m_minPoints, maxPoints);
    update();
}

void FanCurveEditorWidget::setLiveTelemetry(std::optional<double> temp, std::optional<double> duty)
{
    m_liveTemp = temp;
    m_liveDuty = duty;
    update();
}

void FanCurveEditorWidget::setReadOnly(bool readOnly)
{
    m_readOnly = readOnly;
    if (m_readOnly) {
        m_dragIndex = -1;
        m_hoverIndex = -1;
        unsetCursor();
    }
    update();
}

void FanCurveEditorWidget::setSelectedIndex(int index)
{
    if (index < -1 || index >= m_points.size()) {
        index = -1;
    }
    if (m_selectedIndex != index) {
        m_selectedIndex = index;
        update();
    }
}

QSize FanCurveEditorWidget::sizeHint() const
{
    const int fh = fontMetrics().height();
    return QSize(std::max(340, fh * 24), std::max(220, fh * 15));
}

QSize FanCurveEditorWidget::minimumSizeHint() const
{
    const int fh = fontMetrics().height();
    return QSize(std::max(240, fh * 16), std::max(160, fh * 11));
}

QRectF FanCurveEditorWidget::plotRect() const
{
    const QFontMetrics fm = fontMetrics();
    const qreal leftMargin = fm.horizontalAdvance(QStringLiteral("100%")) + 12.0;
    const qreal rightMargin = fm.horizontalAdvance(QStringLiteral("100%")) + 10.0;
    const qreal topMargin = fm.height() + 10.0;
    const qreal bottomMargin = fm.height() * 2.1 + 10.0;
    return QRectF(rect()).adjusted(leftMargin, topMargin, -rightMargin, -bottomMargin);
}

QPointF FanCurveEditorWidget::pointToWidget(double temp, double duty, const QRectF& plot) const
{
    const double tNorm = std::clamp((temp - m_tempMin) / (m_tempMax - m_tempMin), 0.0, 1.0);
    const double dNorm = std::clamp(duty / 100.0, 0.0, 1.0);
    return QPointF(plot.left() + tNorm * plot.width(), plot.bottom() - dNorm * plot.height());
}

QPair<double, quint8> FanCurveEditorWidget::widgetToPoint(const QPointF& pos,
                                                          const QRectF& plot) const
{
    const double tNorm = std::clamp((pos.x() - plot.left()) / plot.width(), 0.0, 1.0);
    const double dNorm = std::clamp((plot.bottom() - pos.y()) / plot.height(), 0.0, 1.0);
    const double temp = m_tempMin + tNorm * (m_tempMax - m_tempMin);
    const quint8 duty = static_cast<quint8>(
        std::clamp<int>(static_cast<int>(std::round(dNorm * 100.0)), m_minDuty, m_maxDuty));
    return {temp, duty};
}

int FanCurveEditorWidget::hitTestPoint(const QPointF& pos, const QRectF& plot) const
{
    int bestIdx = -1;
    qreal bestDist = kHitRadiusPx * kHitRadiusPx;
    for (int i = 0; i < m_points.size(); ++i) {
        const QPointF wp = pointToWidget(m_points.at(i).first, m_points.at(i).second, plot);
        const qreal dx = pos.x() - wp.x();
        const qreal dy = pos.y() - wp.y();
        const qreal d2 = dx * dx + dy * dy;
        if (d2 <= bestDist) {
            bestDist = d2;
            bestIdx = i;
        }
    }
    return bestIdx;
}

void FanCurveEditorWidget::sortPoints()
{
    std::sort(m_points.begin(), m_points.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
}

void FanCurveEditorWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF plot = plotRect();
    if (plot.width() <= 24.0 || plot.height() <= 24.0) {
        return;
    }

    QFont axisFont = font();
    axisFont.setPointSizeF(std::max(7.5, axisFont.pointSizeF() - 1.5));
    p.setFont(axisFont);
    const QFontMetrics fm(axisFont);

    const QColor accent = palette().color(QPalette::Highlight);
    const QColor muted = palette().color(QPalette::PlaceholderText);

    // Plot background & border
    QColor bg = palette().color(QPalette::Base);
    bg.setAlpha(68);
    QColor border = palette().color(QPalette::Mid);
    border.setAlpha(110);
    p.setPen(QPen(border, 1.0));
    p.setBrush(bg);
    p.drawRoundedRect(plot, 4.0, 4.0);

    // Subtle shaded hardware duty limits if min > 0 or max < 100
    if (m_minDuty > 0) {
        const qreal yMin = pointToWidget(m_tempMin, m_minDuty, plot).y();
        QColor limitShade = palette().color(QPalette::Mid);
        limitShade.setAlpha(35);
        p.fillRect(QRectF(plot.left(), yMin, plot.width(), plot.bottom() - yMin), limitShade);
    }
    if (m_maxDuty < 100) {
        const qreal yMax = pointToWidget(m_tempMin, m_maxDuty, plot).y();
        QColor limitShade = palette().color(QPalette::Mid);
        limitShade.setAlpha(35);
        p.fillRect(QRectF(plot.left(), plot.top(), plot.width(), yMax - plot.top()), limitShade);
    }

    // Y grid (0..100%)
    QColor gridColor = palette().color(QPalette::Mid);
    gridColor.setAlpha(55);
    for (int duty = 0; duty <= 100; duty += 25) {
        const qreal y = pointToWidget(m_tempMin, duty, plot).y();
        if (duty > 0 && duty < 100) {
            p.setPen(QPen(gridColor, 1.0, Qt::DashLine));
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        p.setPen(muted);
        p.drawText(QRectF(2.0, y - fm.height() * 0.5, plot.left() - 8.0, fm.height()),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("%1%").arg(duty));
    }

    // X grid (temperature)
    const double tSpan = m_tempMax - m_tempMin;
    const double tStep = (tSpan <= 45.0) ? 5.0 : (tSpan <= 90.0 ? 10.0 : 20.0);
    const double firstTick = std::ceil(m_tempMin / tStep) * tStep;
    for (double t = firstTick; t <= m_tempMax + 0.01; t += tStep) {
        const qreal x = pointToWidget(t, 0.0, plot).x();
        if (t > m_tempMin + 0.5 && t < m_tempMax - 0.5) {
            p.setPen(QPen(gridColor, 1.0, Qt::DashLine));
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }
        p.setPen(muted);
        p.drawText(QRectF(x - 22.0, plot.bottom() + 3.0, 44.0, fm.height()),
                   Qt::AlignCenter | Qt::AlignTop,
                   QStringLiteral("%1°").arg(QString::number(t, 'f', 0)));
    }

    // Curve path + fill
    if (!m_points.isEmpty()) {
        p.save();
        QPainterPath clip;
        clip.addRoundedRect(plot.adjusted(0.5, 0.5, -0.5, -0.5), 4.0, 4.0);
        p.setClipPath(clip);

        QPainterPath linePath;
        QPainterPath areaPath;

        const QPointF firstPt = pointToWidget(m_points.first().first, m_points.first().second, plot);
        const QPointF leftStart(plot.left(), firstPt.y());
        linePath.moveTo(leftStart);
        areaPath.moveTo(plot.left(), plot.bottom());
        areaPath.lineTo(leftStart);

        for (const auto& pt : m_points) {
            const QPointF wp = pointToWidget(pt.first, pt.second, plot);
            linePath.lineTo(wp);
            areaPath.lineTo(wp);
        }

        const QPointF lastPt = pointToWidget(m_points.last().first, m_points.last().second, plot);
        const QPointF rightEnd(plot.right(), lastPt.y());
        linePath.lineTo(rightEnd);
        areaPath.lineTo(rightEnd);
        areaPath.lineTo(plot.right(), plot.bottom());
        areaPath.closeSubpath();

        QColor areaColor = accent;
        areaColor.setAlpha(38);
        p.setPen(Qt::NoPen);
        p.setBrush(areaColor);
        p.drawPath(areaPath);

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(accent, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(linePath);

        p.restore();
    }

    // Live temperature & duty dashed lines
    const QColor liveTempColor(224, 130, 40);
    if (m_liveTemp.has_value()) {
        const qreal tx = pointToWidget(*m_liveTemp, 0.0, plot).x();
        p.setPen(QPen(liveTempColor, 1.4, Qt::DashLine));
        p.drawLine(QPointF(tx, plot.top()), QPointF(tx, plot.bottom()));

        const QString tempLbl = QStringLiteral("%1°C").arg(QString::number(*m_liveTemp, 'f', 1));
        const qreal lblW = fm.horizontalAdvance(tempLbl) + 6.0;
        const qreal lblX = std::clamp(tx - lblW * 0.5, plot.left(), plot.right() - lblW);
        p.setPen(liveTempColor);
        p.drawText(QRectF(lblX, 2.0, lblW, fm.height() + 2.0), Qt::AlignCenter, tempLbl);
    }

    if (m_liveDuty.has_value()) {
        const qreal dy = pointToWidget(m_tempMin, *m_liveDuty, plot).y();
        QColor liveDutyColor = accent;
        liveDutyColor.setAlpha(190);
        p.setPen(QPen(liveDutyColor, 1.4, Qt::DashLine));
        p.drawLine(QPointF(plot.left(), dy), QPointF(plot.right(), dy));

        const QString dutyLbl = QStringLiteral("%1%").arg(QString::number(*m_liveDuty, 'f', 0));
        p.setPen(accent);
        p.drawText(QRectF(plot.right() + 5.0, dy - fm.height() * 0.5, width() - plot.right() - 7.0, fm.height()),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   dutyLbl);
    }

    if (m_liveTemp.has_value() && m_liveDuty.has_value()) {
        const QPointF cross = pointToWidget(*m_liveTemp, *m_liveDuty, plot);
        p.setPen(QPen(palette().color(QPalette::Base), 1.5));
        p.setBrush(liveTempColor);
        p.drawEllipse(cross, 4.8, 4.8);
    }

    // Control points
    for (int i = 0; i < m_points.size(); ++i) {
        const QPointF wp = pointToWidget(m_points.at(i).first, m_points.at(i).second, plot);
        const bool isActive = (i == m_selectedIndex || i == m_hoverIndex || i == m_dragIndex);
        const qreal r = isActive ? 6.8 : 5.2;

        p.setPen(QPen(isActive ? liveTempColor : accent, 2.2));
        p.setBrush(palette().color(QPalette::Base));
        p.drawEllipse(wp, r, r);

        if (i == m_hoverIndex || i == m_dragIndex) {
            const QString info = QStringLiteral("%1°C · %2%")
                                     .arg(QString::number(m_points.at(i).first, 'f', 1))
                                     .arg(m_points.at(i).second);
            const qreal bw = fm.horizontalAdvance(info) + 12.0;
            const qreal bh = fm.height() + 6.0;
            const qreal bx = std::clamp(wp.x() - bw * 0.5, plot.left() + 4.0, plot.right() - bw - 4.0);
            const qreal by = (wp.y() - bh - 10.0 >= plot.top()) ? (wp.y() - bh - 10.0) : (wp.y() + 12.0);

            const QRectF badgeRect(bx, by, bw, bh);
            QColor badgeBg = palette().color(QPalette::ToolTipBase);
            if (!badgeBg.isValid()) {
                badgeBg = palette().color(QPalette::Window);
            }
            p.setPen(QPen(border, 1.0));
            p.setBrush(badgeBg);
            p.drawRoundedRect(badgeRect, 4.0, 4.0);
            p.setPen(palette().color(QPalette::ToolTipText));
            p.drawText(badgeRect, Qt::AlignCenter, info);
        }
    }

    // Bottom interaction hint
    if (!m_readOnly) {
        p.setPen(muted);
        const QRectF hintRect(plot.left(),
                              plot.bottom() + fm.height() + 5.0,
                              plot.width(),
                              fm.height() + 2.0);
        p.drawText(hintRect,
                   Qt::AlignCenter,
                   tr("Drag points · Double-click to add · Right-click point to remove"));
    }
}

void FanCurveEditorWidget::mousePressEvent(QMouseEvent* event)
{
    if (m_readOnly) {
        QWidget::mousePressEvent(event);
        return;
    }

    const QRectF plot = plotRect();
    const int hit = hitTestPoint(event->position(), plot);

    if (event->button() == Qt::LeftButton) {
        if (hit >= 0) {
            m_dragIndex = hit;
            m_selectedIndex = hit;
            setCursor(Qt::ClosedHandCursor);
            emit selectedPointChanged(hit);
            update();
        } else {
            m_selectedIndex = -1;
            emit selectedPointChanged(-1);
            update();
        }
        return;
    }

    if (event->button() == Qt::RightButton && hit >= 0) {
        if (m_points.size() > m_minPoints) {
            m_points.removeAt(hit);
            m_hoverIndex = -1;
            m_dragIndex = -1;
            if (m_selectedIndex == hit) {
                m_selectedIndex = -1;
            } else if (m_selectedIndex > hit) {
                --m_selectedIndex;
            }
            unsetCursor();
            emit pointsChanged(m_points);
            emit selectedPointChanged(m_selectedIndex);
            update();
        }
        return;
    }

    QWidget::mousePressEvent(event);
}

void FanCurveEditorWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_readOnly) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QRectF plot = plotRect();
    if (m_dragIndex >= 0 && m_dragIndex < m_points.size()) {
        auto [rawTemp, duty] = widgetToPoint(event->position(), plot);
        const double step = (event->modifiers() & Qt::ShiftModifier) ? 0.1 : 0.5;
        double temp = roundStep(rawTemp, step);

        const double minT = (m_dragIndex > 0)
            ? (m_points.at(m_dragIndex - 1).first + 0.5)
            : m_tempMin;
        const double maxT = (m_dragIndex + 1 < m_points.size())
            ? (m_points.at(m_dragIndex + 1).first - 0.5)
            : m_tempMax;

        if (minT <= maxT) {
            temp = std::clamp(temp, minT, maxT);
        } else {
            temp = m_points.at(m_dragIndex).first;
        }
        temp = std::round(temp * 10.0) / 10.0;

        if (m_points.at(m_dragIndex).first != temp || m_points.at(m_dragIndex).second != duty) {
            m_points[m_dragIndex] = {temp, duty};
            emit pointsChanged(m_points);
            update();
        }
        return;
    }

    const int hit = hitTestPoint(event->position(), plot);
    if (hit != m_hoverIndex) {
        m_hoverIndex = hit;
        if (m_hoverIndex >= 0) {
            setCursor(Qt::OpenHandCursor);
        } else {
            unsetCursor();
        }
        update();
    }
}

void FanCurveEditorWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragIndex >= 0) {
        m_dragIndex = -1;
        const int hit = hitTestPoint(event->position(), plotRect());
        m_hoverIndex = hit;
        if (hit >= 0 && !m_readOnly) {
            setCursor(Qt::OpenHandCursor);
        } else {
            unsetCursor();
        }
        update();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void FanCurveEditorWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_readOnly || event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    const QRectF plot = plotRect();
    if (!plot.contains(event->position()) || m_points.size() >= m_maxPoints) {
        return;
    }
    if (hitTestPoint(event->position(), plot) >= 0) {
        return;
    }

    auto [rawTemp, duty] = widgetToPoint(event->position(), plot);
    double temp = std::clamp(roundStep(rawTemp, 0.5), m_tempMin, m_tempMax);
    temp = std::round(temp * 10.0) / 10.0;

    // Avoid exact temperature duplicate with an existing point
    for (int attempt = 0; attempt < 10; ++attempt) {
        const bool exists = std::any_of(m_points.cbegin(), m_points.cend(), [temp](const auto& pt) {
            return std::abs(pt.first - temp) < 0.05;
        });
        if (!exists) {
            break;
        }
        temp = std::clamp(std::round((temp + 0.5) * 10.0) / 10.0, m_tempMin, m_tempMax);
    }

    m_points.append({temp, duty});
    sortPoints();

    int newIdx = -1;
    for (int i = 0; i < m_points.size(); ++i) {
        if (std::abs(m_points.at(i).first - temp) < 0.05 && m_points.at(i).second == duty) {
            newIdx = i;
            break;
        }
    }
    m_selectedIndex = newIdx;
    emit pointsChanged(m_points);
    emit selectedPointChanged(m_selectedIndex);
    update();
}

void FanCurveEditorWidget::leaveEvent(QEvent* event)
{
    if (m_hoverIndex >= 0 && m_dragIndex < 0) {
        m_hoverIndex = -1;
        unsetCursor();
        update();
    }
    QWidget::leaveEvent(event);
}

void FanCurveEditorWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange || event->type() == QEvent::PaletteChange
        || event->type() == QEvent::StyleChange) {
        update();
    }
}

} // namespace ccqt
