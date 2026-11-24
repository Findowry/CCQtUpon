#pragma once

#include "models/CoolerControlModels.h"

#include <QWidget>

#include <deque>

class QEvent;
class QPaintEvent;

namespace ccqt {

class FanHistoryChartWidget : public QWidget {
    Q_OBJECT

public:
    explicit FanHistoryChartWidget(QWidget* parent = nullptr);

    void setHistory(const std::deque<FanHistoryPoint>& points);
    void clear();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    std::deque<FanHistoryPoint> m_points;
};

} // namespace ccqt
