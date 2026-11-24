#include "ui/AuthDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace ccqt {

AuthDialog::AuthDialog(CoolerControlClient* client,
                       AppTranslator* translator,
                       const ConnectionConfig& currentConfig,
                       bool saveTokenLocally,
                       QWidget* parent)
    : QDialog(parent)
    , m_client(client)
    , m_translator(translator)
{
    setModal(true);
    setMinimumWidth(520);

    buildUi(currentConfig, saveTokenLocally);
    retranslateUi();
    refreshMutedPalette();

    if (m_client != nullptr) {
        connect(m_client,
                &CoolerControlClient::connectionTestFinished,
                this,
                &AuthDialog::onConnectionTestFinished);
    }
}

void AuthDialog::buildUi(const ConnectionConfig& currentConfig, bool saveTokenLocally)
{
    auto* root = new QVBoxLayout(this);
    root->setSpacing(12);
    root->setContentsMargins(16, 16, 16, 16);

    m_connGroup = new QGroupBox(this);
    auto* form = new QFormLayout(m_connGroup);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setSpacing(8);

    m_urlLabel = new QLabel(m_connGroup);
    m_urlEdit = new QLineEdit(currentConfig.baseUrl, m_connGroup);
    m_urlEdit->setPlaceholderText(QStringLiteral("http://localhost:11987"));
    form->addRow(m_urlLabel, m_urlEdit);

    m_tlsHintLabel = new QLabel(m_connGroup);
    m_tlsHintLabel->setWordWrap(true);
    QFont hintFont = m_tlsHintLabel->font();
    hintFont.setPointSizeF(std::max(8.0, hintFont.pointSizeF() - 1.0));
    m_tlsHintLabel->setFont(hintFont);
    form->addRow(QString(), m_tlsHintLabel);

    m_tokenLabel = new QLabel(m_connGroup);
    auto* tokenRow = new QHBoxLayout();
    tokenRow->setSpacing(6);

    m_tokenEdit = new QLineEdit(currentConfig.accessToken, m_connGroup);
    m_tokenEdit->setEchoMode(QLineEdit::Password);
    tokenRow->addWidget(m_tokenEdit, 1);

    m_toggleTokenButton = new QToolButton(m_connGroup);
    m_toggleTokenButton->setCheckable(true);
    connect(m_toggleTokenButton, &QToolButton::toggled, this, [this](bool checked) {
        m_tokenEdit->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
        m_toggleTokenButton->setText(checked ? tr("Hide") : tr("Show"));
    });
    tokenRow->addWidget(m_toggleTokenButton);
    form->addRow(m_tokenLabel, tokenRow);

    m_saveTokenCheck = new QCheckBox(m_connGroup);
    m_saveTokenCheck->setChecked(saveTokenLocally);
    form->addRow(QString(), m_saveTokenCheck);

    m_langLabel = new QLabel(m_connGroup);
    m_langCombo = new QComboBox(m_connGroup);
    m_langCombo->addItem(QStringLiteral("English"), QStringLiteral("en"));
    m_langCombo->addItem(QStringLiteral("Türkçe"), QStringLiteral("tr"));
    if (m_translator != nullptr && m_translator->language() == Language::Turkish) {
        m_langCombo->setCurrentIndex(1);
    } else {
        m_langCombo->setCurrentIndex(0);
    }
    connect(m_langCombo, &QComboBox::activated, this, [this](int idx) {
        const Language lang = AppTranslator::fromCode(m_langCombo->itemData(idx).toString());
        if (m_translator != nullptr) {
            m_translator->setLanguage(lang);
        }
        emit languageChanged(lang);
    });
    form->addRow(m_langLabel, m_langCombo);

    auto* testRow = new QHBoxLayout();
    testRow->setSpacing(10);
    m_testButton = new QPushButton(m_connGroup);
    connect(m_testButton, &QPushButton::clicked, this, &AuthDialog::onTestClicked);
    testRow->addWidget(m_testButton);

    m_testResultLabel = new QLabel(m_connGroup);
    m_testResultLabel->setWordWrap(true);
    m_testResultLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    testRow->addWidget(m_testResultLabel, 1);
    form->addRow(QString(), testRow);

    root->addWidget(m_connGroup);

    m_guideGroup = new QGroupBox(this);
    auto* guideLayout = new QVBoxLayout(m_guideGroup);
    m_guideLabel = new QLabel(m_guideGroup);
    m_guideLabel->setWordWrap(true);
    m_guideLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    guideLayout->addWidget(m_guideLabel);
    root->addWidget(m_guideGroup);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &AuthDialog::onAcceptClicked);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(m_buttonBox);
}

ConnectionConfig AuthDialog::resultConfig() const
{
    ConnectionConfig cfg;
    QString normalized;
    if (CoolerControlClient::isValidBaseUrl(m_urlEdit->text(), &normalized)) {
        cfg.baseUrl = normalized;
    } else {
        cfg.baseUrl = m_urlEdit->text().trimmed();
    }
    cfg.accessToken = m_tokenEdit->text().trimmed();
    cfg.authMode = AuthMode::AccessToken;
    return cfg;
}

bool AuthDialog::saveToken() const
{
    return m_saveTokenCheck->isChecked();
}

void AuthDialog::onTestClicked()
{
    if (m_client == nullptr || m_testInProgress) {
        return;
    }
    m_testInProgress = true;
    m_testButton->setEnabled(false);
    m_testResultLabel->setText(tr("Testing connection..."));
    m_client->testConnection(m_urlEdit->text(), m_tokenEdit->text());
}

void AuthDialog::onConnectionTestFinished(bool ok,
                                          const QString& message,
                                          const HealthInfo& health)
{
    Q_UNUSED(ok)
    Q_UNUSED(health)
    m_testInProgress = false;
    m_testButton->setEnabled(true);
    m_testResultLabel->setText(message);
}

void AuthDialog::onAcceptClicked()
{
    QString normalized;
    if (!CoolerControlClient::isValidBaseUrl(m_urlEdit->text(), &normalized)) {
        m_testResultLabel->setText(tr("Please enter a valid URL."));
        m_urlEdit->setFocus();
        return;
    }
    if (m_tokenEdit->text().trimmed().isEmpty()) {
        m_testResultLabel->setText(tr("Please enter an Access Token."));
        m_tokenEdit->setFocus();
        return;
    }
    accept();
}

void AuthDialog::retranslateUi()
{
    setWindowTitle(tr("CoolerControl Connection Setup"));
    m_connGroup->setTitle(tr("Connection"));
    m_urlLabel->setText(tr("Daemon URL:"));
    m_tlsHintLabel->setText(
        tr("Self-signed TLS certificates are accepted automatically only for localhost / 127.0.0.1."));
    m_tokenLabel->setText(tr("Access Token:"));
    m_tokenEdit->setPlaceholderText(tr("Paste cc_... token here"));
    m_toggleTokenButton->setText(m_toggleTokenButton->isChecked() ? tr("Hide") : tr("Show"));
    m_saveTokenCheck->setText(tr("Save Access Token locally (0600 permissions)"));
    m_langLabel->setText(tr("Language:"));
    m_testButton->setText(tr("Test Connection"));
    m_guideGroup->setTitle(tr("How to Get an Access Token"));
    m_guideLabel->setText(tr(
        "1. Open the official CoolerControl desktop app (or web interface).\n"
        "2. Click \"Access\" in the bottom-left corner.\n"
        "3. Select \"Access Tokens\" from the popup menu.\n"
        "4. Enter a name in the \"Label\" field (e.g. CCQt Upon).\n"
        "5. Leave \"Expiry Date\" empty for a non-expiring token.\n"
        "6. Enable \"Write Access\" so you can switch modes and assign fan profiles.\n"
        "7. Click \"Create Token\", copy the generated cc_... token, and paste it above."));

    if (QPushButton* okBtn = m_buttonBox->button(QDialogButtonBox::Ok)) {
        okBtn->setText(tr("Save & Connect"));
    }
    if (QPushButton* cancelBtn = m_buttonBox->button(QDialogButtonBox::Cancel)) {
        cancelBtn->setText(tr("Cancel"));
    }
}

void AuthDialog::refreshMutedPalette()
{
    QPalette pal = palette();
    const QColor muted = pal.color(QPalette::PlaceholderText);
    pal.setColor(QPalette::WindowText, muted);
    m_tlsHintLabel->setPalette(pal);
}

void AuthDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    } else if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        refreshMutedPalette();
    }
}

} // namespace ccqt
