#pragma once

#include "models/CoolerControlModels.h"

#include <QDialog>

class QDialogButtonBox;
class QEvent;
class QLabel;

namespace ccqt {

class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(const HealthInfo& health, QWidget* parent = nullptr);

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslateUi();
    void refreshMutedPalette();

    HealthInfo m_health;

    QLabel* m_titleLabel = nullptr;
    QLabel* m_descLabel = nullptr;
    QLabel* m_versionCaption = nullptr;
    QLabel* m_versionValue = nullptr;
    QLabel* m_licenseCaption = nullptr;
    QLabel* m_licenseValue = nullptr;
    QLabel* m_qtCaption = nullptr;
    QLabel* m_qtValue = nullptr;
    QLabel* m_daemonCaption = nullptr;
    QLabel* m_daemonValue = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
};

} // namespace ccqt
