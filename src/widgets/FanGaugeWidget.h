#pragma once

#include <QWidget>

#include <optional>

class QEvent;
class QPaintEvent;

namespace ccqt {

class FanGaugeWidget : public QWidget {
    Q_OBJECT

public:
    explicit FanGaugeWidget(QWidget* parent = nullptr);

    void setDutyRange(int minDuty, int maxDuty);
    void setLiveMetrics(std::optional<double> duty, std::optional<quint32> rpm);
    void setTargetDuty(int duty, bool showTarget);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    int m_minDuty = 0;
    int m_maxDuty = 100;
    std::optional<double> m_liveDuty;
    std::optional<quint32> m_liveRpm;
    int m_targetDuty = 0;
    bool m_showTarget = false;
};

} // namespace ccqt
