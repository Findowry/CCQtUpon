#pragma once

#include "api/CoolerControlClient.h"
#include "ui/AppTranslator.h"

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QEvent;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

namespace ccqt {

class AuthDialog : public QDialog {
    Q_OBJECT

public:
    AuthDialog(CoolerControlClient* client,
               AppTranslator* translator,
               const ConnectionConfig& currentConfig,
               bool saveTokenLocally,
               QWidget* parent = nullptr);

    ConnectionConfig resultConfig() const;
    bool saveToken() const;

signals:
    void languageChanged(ccqt::Language language);

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void onTestClicked();
    void onConnectionTestFinished(bool ok, const QString& message, const ccqt::HealthInfo& health);
    void onAcceptClicked();

private:
    void buildUi(const ConnectionConfig& currentConfig, bool saveTokenLocally);
    void retranslateUi();
    void refreshMutedPalette();

    CoolerControlClient* m_client = nullptr;
    AppTranslator* m_translator = nullptr;

    QGroupBox* m_connGroup = nullptr;
    QLabel* m_urlLabel = nullptr;
    QLineEdit* m_urlEdit = nullptr;
    QLabel* m_tlsHintLabel = nullptr;

    QLabel* m_tokenLabel = nullptr;
    QLineEdit* m_tokenEdit = nullptr;
    QToolButton* m_toggleTokenButton = nullptr;
    QCheckBox* m_saveTokenCheck = nullptr;

    QLabel* m_langLabel = nullptr;
    QComboBox* m_langCombo = nullptr;

    QPushButton* m_testButton = nullptr;
    QLabel* m_testResultLabel = nullptr;

    QGroupBox* m_guideGroup = nullptr;
    QLabel* m_guideLabel = nullptr;

    QDialogButtonBox* m_buttonBox = nullptr;
    bool m_testInProgress = false;
};

} // namespace ccqt
