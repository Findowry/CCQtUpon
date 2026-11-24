#pragma once

#include <QPair>
#include <QVector>
#include <QWidget>

#include <optional>

class QEvent;
class QMouseEvent;
class QPaintEvent;

namespace ccqt {

class FanCurveEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit FanCurveEditorWidget(QWidget* parent = nullptr);

    void setPoints(const QVector<QPair<double, quint8>>& points);
    const QVector<QPair<double, quint8>>& points() const;

    void setBounds(double tempMin,
                   double tempMax,
                   quint8 minDuty,
                   quint8 maxDuty,
                   int minPoints,
                   int maxPoints);
    void setLiveTelemetry(std::optional<double> temp, std::optional<double> duty);
    void setReadOnly(bool readOnly);
    void setSelectedIndex(int index);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void pointsChanged(const QVector<QPair<double, quint8>>& points);
    void selectedPointChanged(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    QRectF plotRect() const;
    QPointF pointToWidget(double temp, double duty, const QRectF& plot) const;
    QPair<double, quint8> widgetToPoint(const QPointF& pos, const QRectF& plot) const;
    int hitTestPoint(const QPointF& pos, const QRectF& plot) const;
    void sortPoints();

    QVector<QPair<double, quint8>> m_points;
    double m_tempMin = 0.0;
    double m_tempMax = 100.0;
    quint8 m_minDuty = 0;
    quint8 m_maxDuty = 100;
    int m_minPoints = 2;
    int m_maxPoints = 21;

    std::optional<double> m_liveTemp;
    std::optional<double> m_liveDuty;
    bool m_readOnly = false;

    int m_selectedIndex = -1;
    int m_hoverIndex = -1;
    int m_dragIndex = -1;
};

} // namespace ccqt
