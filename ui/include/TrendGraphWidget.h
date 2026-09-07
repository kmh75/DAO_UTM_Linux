#pragma once

#include <QElapsedTimer>
#include <QVector>
#include <QWidget>
#include <QString>

class TrendGraphWidget final : public QWidget
{
public:
    explicit TrendGraphWidget(QWidget* parent = nullptr);
    void append(double forceN, double testPositionMm);
    void clear();
    void setForceUnitLabel(const QString& label) { forceUnitLabel_=label; update(); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    struct Sample { double seconds; double force; double position; };
    QVector<Sample> samples_;
    QElapsedTimer clock_;
    QString forceUnitLabel_ = "N";
    static constexpr int MAX_SAMPLES = 2048;
};
