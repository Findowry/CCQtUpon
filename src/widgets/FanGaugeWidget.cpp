#include "widgets/FanGaugeWidget.h"

#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>

#include <algorithm>
#include <cmath>

namespace ccqt {
namespace {

constexpr double kStartAngleDeg = 210.0;
constexpr double kSweepDeg = 240.0;
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

} // namespace

FanGaugeWidget::FanGaugeWidget(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void FanGaugeWidget::setDutyRange(int minDuty, int maxDuty)
{
    m_minDuty = std::clamp(minDuty, 0, 100);
    m_maxDuty = std::clamp(std::max(minDuty, maxDuty), 0, 100);
    update();
}

void FanGaugeWidget::setLiveMetrics(std::optional<double> duty, std::optional<quint32> rpm)
{
    m_liveDuty = duty;
    m_liveRpm = rpm;
    update();
}

void FanGaugeWidget::setTargetDuty(int duty, bool showTarget)
{
    m_targetDuty = std::clamp(duty, 0, 100);
    m_showTarget = showTarget;
    update();
}

QSize FanGaugeWidget::sizeHint() const
{
    const int fh = fontMetrics().height();
    return QSize(std::max(200, fh * 13), std::max(175, fh * 11));
}

QSize FanGaugeWidget::minimumSizeHint() const
{
    const int fh = fontMetrics().height();
    return QSize(std::max(150, fh * 9), std::max(140, fh * 8));
}

void FanGaugeWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QFontMetrics fm = fontMetrics();
    const qreal outerMargin = fm.height() * 1.4;
    const qreal availW = width() - outerMargin * 2.0;
    const qreal availH = height() - outerMargin * 2.0;
    const qreal diameter = std::max(60.0, std::min(availW, availH));
    const qreal radius = diameter * 0.5;
    const QPointF center(width() * 0.5, height() * 0.52);

    const QRectF arcRect(center.x() - radius, center.y() - radius, diameter, diameter);
    const qreal trackWidth = std::clamp(diameter * 0.085, 8.0, 18.0);

    // Background track
    QColor trackColor = palette().color(QPalette::Mid);
    trackColor.setAlpha(72);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(trackColor, trackWidth, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(arcRect, static_cast<int>(kStartAngleDeg * 16.0), static_cast<int>(-kSweepDeg * 16.0));

    // Scale ticks and labels (0, 25, 50, 75, 100%)
    QFont tickFont = font();
    tickFont.setPointSizeF(std::max(7.5, tickFont.pointSizeF() - 2.0));
    p.setFont(tickFont);
    const QFontMetrics tickFm(tickFont);

    QColor tickColor = palette().color(QPalette::PlaceholderText);
    for (int pct = 0; pct <= 100; pct += 25) {
        const double angleDeg = kStartAngleDeg - kSweepDeg * (pct / 100.0);
        const double rad = angleDeg * kDegToRad;
        const qreal cosA = std::cos(rad);
        const qreal sinA = std::sin(rad);

        const qreal rInner = radius - trackWidth * 0.68;
        const qreal rOuter = radius + trackWidth * 0.62;
        p.setPen(QPen(tickColor, 1.2));
        p.drawLine(QPointF(center.x() + cosA * rInner, center.y() - sinA * rInner),
                   QPointF(center.x() + cosA * rOuter, center.y() - sinA * rOuter));

        const qreal rLabel = radius + trackWidth * 0.75 + tickFm.height() * 0.65;
        const QPointF lblCenter(center.x() + cosA * rLabel, center.y() - sinA * rLabel);
        const QString lbl = QString::number(pct);
        const QRectF lblRect(lblCenter.x() - 18.0, lblCenter.y() - tickFm.height() * 0.5, 36.0, tickFm.height());
        p.setPen(tickColor);
        p.drawText(lblRect, Qt::AlignCenter, lbl);
    }

    // Live duty arc
    const QColor highlight = palette().color(QPalette::Highlight);
    if (m_liveDuty.has_value()) {
        const double clamped = std::clamp(*m_liveDuty, 0.0, 100.0);
        if (clamped > 0.2) {
            const double liveSpan = -kSweepDeg * (clamped / 100.0);
            p.setPen(QPen(highlight, trackWidth, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(arcRect,
                      static_cast<int>(kStartAngleDeg * 16.0),
                      static_cast<int>(std::round(liveSpan * 16.0)));
        }
    }

    // Target manual needle & arc marker
    const QColor targetColor(224, 150, 35);
    if (m_showTarget) {
        const double angleDeg = kStartAngleDeg - kSweepDeg * (m_targetDuty / 100.0);
        const double rad = angleDeg * kDegToRad;
        const qreal cosA = std::cos(rad);
        const qreal sinA = std::sin(rad);

        const qreal rStart = radius * 0.42;
        const qreal rEnd = radius - trackWidth * 0.05;
        p.setPen(QPen(targetColor, 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(center.x() + cosA * rStart, center.y() - sinA * rStart),
                   QPointF(center.x() + cosA * rEnd, center.y() - sinA * rEnd));

        const QPointF tip(center.x() + cosA * radius, center.y() - sinA * radius);
        p.setPen(QPen(palette().color(QPalette::Base), 1.6));
        p.setBrush(targetColor);
        p.drawEllipse(tip, 4.5, 4.5);
    }

    // Center live Duty %
    QFont bigFont = font();
    bigFont.setBold(true);
    bigFont.setPointSizeF(std::max(14.0, bigFont.pointSizeF() + 8.0));
    p.setFont(bigFont);
    const QFontMetrics bigFm(bigFont);

    const QString dutyText = m_liveDuty.has_value()
        ? QStringLiteral("%1%").arg(QString::number(*m_liveDuty, 'f', 0))
        : QStringLiteral("—");
    const QRectF dutyRect(center.x() - radius * 0.72,
                          center.y() - bigFm.height() * 0.65,
                          radius * 1.44,
                          bigFm.height());
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(dutyRect, Qt::AlignCenter, dutyText);

    // Center live RPM below Duty %
    QFont rpmFont = font();
    rpmFont.setPointSizeF(std::max(8.5, rpmFont.pointSizeF()));
    p.setFont(rpmFont);
    const QFontMetrics rpmFm(rpmFont);

    const QString rpmText = m_liveRpm.has_value()
        ? tr("%1 RPM").arg(*m_liveRpm)
        : QStringLiteral("—");
    const QRectF rpmRect(center.x() - radius * 0.72,
                         dutyRect.bottom() + 2.0,
                         radius * 1.44,
                         rpmFm.height());
    p.setPen(palette().color(QPalette::PlaceholderText));
    p.drawText(rpmRect, Qt::AlignCenter, rpmText);

    // Bottom target readout
    if (m_showTarget) {
        QFont targetFont = font();
        targetFont.setBold(true);
        p.setFont(targetFont);
        const QFontMetrics tFm(targetFont);
        const QRectF tRect(0.0,
                           center.y() + radius * 0.56,
                           width(),
                           tFm.height() + 4.0);
        p.setPen(targetColor);
        p.drawText(tRect, Qt::AlignCenter, tr("Target: %1%").arg(m_targetDuty));
    }
}

void FanGaugeWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange || event->type() == QEvent::PaletteChange
        || event->type() == QEvent::StyleChange) {
        update();
    }
}

} // namespace ccqt
