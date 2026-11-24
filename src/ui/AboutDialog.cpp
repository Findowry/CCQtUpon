#include "ui/AboutDialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFont>
#include <QFormLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QVBoxLayout>

namespace ccqt {

AboutDialog::AboutDialog(const HealthInfo& health, QWidget* parent)
    : QDialog(parent)
    , m_health(health)
{
    setModal(true);
    setMinimumWidth(380);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(12);

    m_titleLabel = new QLabel(this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() + 3.0);
    m_titleLabel->setFont(titleFont);
    root->addWidget(m_titleLabel);

    m_descLabel = new QLabel(this);
    m_descLabel->setWordWrap(true);
    root->addWidget(m_descLabel);

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
