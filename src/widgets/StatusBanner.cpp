#include "widgets/StatusBanner.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QStyle>

namespace ccqt {

StatusBanner::StatusBanner(QWidget* parent)
    : QWidget(parent)
{
    setVisible(false);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 8, 12, 8);
    layout->setSpacing(10);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(m_iconLabel, 0, Qt::AlignVCenter);

    m_messageLabel = new QLabel(this);
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_messageLabel, 1, Qt::AlignVCenter);

    m_settingsButton = new QPushButton(this);
    m_settingsButton->setVisible(false);
    connect(m_settingsButton, &QPushButton::clicked, this, &StatusBanner::settingsRequested);
    layout->addWidget(m_settingsButton, 0, Qt::AlignVCenter);

    m_retryButton = new QPushButton(this);
    m_retryButton->setVisible(false);
    connect(m_retryButton, &QPushButton::clicked, this, &StatusBanner::retryRequested);
    layout->addWidget(m_retryButton, 0, Qt::AlignVCenter);

    m_dismissButton = new QPushButton(this);
    m_dismissButton->setVisible(false);
    connect(m_dismissButton, &QPushButton::clicked, this, [this]() {
        clear();
        emit dismissed();
    });
    layout->addWidget(m_dismissButton, 0, Qt::AlignVCenter);

    retranslateUi();
    updateIcon();
}

void StatusBanner::showMessage(Severity severity,
                               const QString& message,
                               bool showSettingsAction,
                               bool showRetryAction,
                               bool dismissible)
{
    m_severity = severity;
    m_messageLabel->setText(message);
    m_settingsButton->setVisible(showSettingsAction);
    m_retryButton->setVisible(showRetryAction);
    m_dismissButton->setVisible(dismissible);
    updateIcon();
    setVisible(true);
    update();
}

void StatusBanner::clear()
{
    if (isVisible()) {
        setVisible(false);
        m_messageLabel->clear();
    }
}

void StatusBanner::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor accent;
    QColor tint;
    switch (m_severity) {
    case Severity::Error:
        accent = QColor(212, 66, 66);
        tint = QColor(212, 66, 66, 32);
        break;
    case Severity::Warning:
        accent = QColor(218, 148, 32);
        tint = QColor(218, 148, 32, 32);
        break;
    case Severity::Info:
        accent = palette().color(QPalette::Highlight);
        tint = accent;
        tint.setAlpha(28);
        break;
    }

    const QRectF r = rect();
    painter.fillRect(r, tint);

    const QRectF accentRect(0.0, 0.0, 3.5, r.height());
    painter.fillRect(accentRect, accent);

    QColor border = palette().color(QPalette::Mid);
    border.setAlpha(95);
    painter.setPen(QPen(border, 1.0));
    painter.drawLine(QPointF(0.0, r.height() - 0.5), QPointF(r.width(), r.height() - 0.5));
}

void StatusBanner::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    } else if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
        updateIcon();
        update();
    }
}

void StatusBanner::updateIcon()
{
    QStyle::StandardPixmap sp = QStyle::SP_MessageBoxInformation;
    if (m_severity == Severity::Warning) {
        sp = QStyle::SP_MessageBoxWarning;
    } else if (m_severity == Severity::Error) {
        sp = QStyle::SP_MessageBoxCritical;
    }
    const int iconExtent = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    const QIcon icon = style()->standardIcon(sp, nullptr, this);
    m_iconLabel->setPixmap(icon.pixmap(iconExtent, iconExtent));
}

void StatusBanner::retranslateUi()
{
    m_settingsButton->setText(tr("Connection Settings"));
    m_retryButton->setText(tr("Retry Now"));
    m_dismissButton->setText(tr("Dismiss"));
}

} // namespace ccqt
