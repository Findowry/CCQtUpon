#include "ui/AboutDialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace ccqt {
namespace {

class EmblemWidget : public QWidget {
public:
    explicit EmblemWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(48, 48);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event)
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const qreal c = width() * 0.5;
        const qreal r = width() * 0.43;

        QColor accent = palette().color(QPalette::Highlight);
        if (!accent.isValid()) {
            accent = QColor(46, 158, 214);
        }

        QColor bg = palette().color(QPalette::Base);
        bg.setAlpha(210);
        p.setPen(QPen(accent, 2.2));
        p.setBrush(bg);
        p.drawEllipse(QPointF(c, c), r, r);

        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        for (int i = 0; i < 3; ++i) {
            const qreal baseAngle = i * (2.0 * M_PI / 3.0) - M_PI / 2.0;
            const qreal tipAngle = baseAngle + 0.55;
            const qreal midAngle = baseAngle + 0.95;

            QPainterPath blade;
            blade.moveTo(c, c);
            blade.quadTo(c + std::cos(tipAngle) * r * 0.85,
                         c + std::sin(tipAngle) * r * 0.85,
                         c + std::cos(midAngle) * r * 0.68,
                         c + std::sin(midAngle) * r * 0.68);
            blade.quadTo(c + std::cos(baseAngle + 0.22) * r * 0.35,
                         c + std::sin(baseAngle + 0.22) * r * 0.35,
                         c, c);
            p.drawPath(blade);
        }

        p.setBrush(palette().color(QPalette::WindowText));
        p.drawEllipse(QPointF(c, c), width() * 0.08, width() * 0.08);
    }
};

} // namespace

AboutDialog::AboutDialog(const HealthInfo& health, QWidget* parent)
    : QDialog(parent)
    , m_health(health)
{
    setModal(true);
    setMinimumWidth(400);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(14);

    auto* headerRow = new QHBoxLayout();
    headerRow->setSpacing(14);

    auto* emblem = new EmblemWidget(this);
    headerRow->addWidget(emblem, 0, Qt::AlignTop);

    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(4);

    m_titleLabel = new QLabel(this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() + 3.0);
    m_titleLabel->setFont(titleFont);
    titleCol->addWidget(m_titleLabel);

    m_descLabel = new QLabel(this);
    m_descLabel->setWordWrap(true);
    titleCol->addWidget(m_descLabel);

    headerRow->addLayout(titleCol, 1);
    root->addLayout(headerRow);

    auto* form = new QFormLayout();
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(6);

    m_versionCaption = new QLabel(this);
    m_versionValue = new QLabel(QApplication::applicationVersion(), this);
    form->addRow(m_versionCaption, m_versionValue);

    m_licenseCaption = new QLabel(this);
    m_licenseValue = new QLabel(this);
    form->addRow(m_licenseCaption, m_licenseValue);

    m_qtCaption = new QLabel(this);
    m_qtValue = new QLabel(QString::fromLatin1(qVersion()), this);
    form->addRow(m_qtCaption, m_qtValue);

    m_daemonCaption = new QLabel(this);
    m_daemonValue = new QLabel(this);
    form->addRow(m_daemonCaption, m_daemonValue);

    root->addLayout(form);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    root->addWidget(m_buttonBox);

    retranslateUi();
    refreshMutedPalette();
}

void AboutDialog::retranslateUi()
{
    setWindowTitle(tr("About CCQt Upon"));
    m_titleLabel->setText(QStringLiteral("CCQt Upon v%1").arg(QApplication::applicationVersion()));
    m_descLabel->setText(
        tr("Ultra-lightweight native Qt6 desktop client for CoolerControl v5.1+."));

    m_versionCaption->setText(tr("Version:"));
    m_licenseCaption->setText(tr("License:"));
    m_licenseValue->setText(tr("MIT License"));
    m_qtCaption->setText(tr("Qt Version:"));
    m_daemonCaption->setText(tr("Daemon:"));
    if (m_health.valid && !m_health.version.isEmpty()) {
        m_daemonValue->setText(QStringLiteral("coolercontrold v%1").arg(m_health.version));
    } else {
        m_daemonValue->setText(tr("Not connected"));
    }

    if (QPushButton* closeBtn = m_buttonBox->button(QDialogButtonBox::Close)) {
        closeBtn->setText(tr("Close"));
    }
}

void AboutDialog::refreshMutedPalette()
{
    QPalette pal = palette();
    const QColor muted = pal.color(QPalette::PlaceholderText);
    pal.setColor(QPalette::WindowText, muted);
    m_descLabel->setPalette(pal);
}

void AboutDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    } else if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        refreshMutedPalette();
    }
}

} // namespace ccqt
