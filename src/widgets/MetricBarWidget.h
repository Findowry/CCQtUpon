#pragma once

#include <QWidget>

#include <deque>

class QPaintEvent;

namespace ccqt {

class MetricBarWidget : public QWidget {
    Q_OBJECT

public:
    enum class Kind {
        Duty,
        Temperature
    };

    explicit MetricBarWidget(Kind kind, QWidget* parent = nullptr);

    void setRange(double minVal, double maxVal);
    void setValue(double value);
    void clearValue();

    void setSparklineEnabled(bool enabled);
    void pushSample(double value);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QColor activeAccentColor() const;

    Kind m_kind;
    double m_min = 0.0;
    double m_max = 100.0;
    double m_value = 0.0;
    bool m_hasValue = false;
    bool m_sparklineEnabled = false;
    std::deque<double> m_samples;
};

} // namespace ccqt
