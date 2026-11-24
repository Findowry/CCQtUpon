#pragma once

#include <QString>
#include <QWidget>

class QEvent;
class QLabel;
class QPaintEvent;
class QPushButton;

namespace ccqt {

class StatusBanner : public QWidget {
    Q_OBJECT

public:
    enum class Severity {
        Info,
        Warning,
        Error
    };

    explicit StatusBanner(QWidget* parent = nullptr);

    void showMessage(Severity severity,
                     const QString& message,
                     bool showSettingsAction = false,
                     bool showRetryAction = false,
                     bool dismissible = false);
    void clear();

signals:
    void settingsRequested();
    void retryRequested();
    void dismissed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void updateIcon();
    void retranslateUi();

    Severity m_severity = Severity::Info;
    QLabel* m_iconLabel = nullptr;
    QLabel* m_messageLabel = nullptr;
    QPushButton* m_settingsButton = nullptr;
    QPushButton* m_retryButton = nullptr;
    QPushButton* m_dismissButton = nullptr;
};

} // namespace ccqt
