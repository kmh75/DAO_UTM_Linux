#include "TrendGraphWidget.h"
#include "ForceUnit.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

TrendGraphWidget::TrendGraphWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(210);
    clock_.start();
}

void TrendGraphWidget::append(double forceN, double testPositionMm)
{
    if (!std::isfinite(forceN) || !std::isfinite(testPositionMm)) return;
    const double now = clock_.elapsed() / 1000.0;
    samples_.append({now, forceN, testPositionMm});
    while (!samples_.isEmpty() && now - samples_.front().seconds > 30.0)
        samples_.removeFirst();
    while (samples_.size() > MAX_SAMPLES) samples_.removeFirst();
    update();
}

void TrendGraphWidget::clear()
{
    samples_.clear(); clock_.restart(); update();
}

void TrendGraphWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#111820"));
    const QRectF plot = rect().adjusted(52, 18, -52, -32);
    if (plot.width() <= 1.0 || plot.height() <= 1.0) return;
    p.setPen(QColor("#33404c"));
    for (int i = 0; i <= 5; ++i)
    {
        const qreal y = plot.top() + plot.height() * i / 5.0;
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    p.setPen(QColor("#8997a5"));
    p.drawText(8, 22, "Force " + forceUnitLabel_);
    p.drawText(width() - 82, 22, "Position mm");
    p.drawText(plot.left(), height() - 8, "30 s rolling monitor • UI samples only");
    if (samples_.size() < 2) return;

    double maxForce = 1.0, maxPosition = 1.0;
    for (const auto& s : samples_)
    {
        const double displayedForce = ForceUnits::fromNewtons(s.force, ForceUnits::fromText(forceUnitLabel_));
        maxForce = std::max(maxForce, std::abs(displayedForce));
        maxPosition = std::max(maxPosition, std::abs(s.position));
    }
    const double end = samples_.back().seconds;
    const double start = std::max(0.0, end - 30.0);
    auto pathFor = [&](bool force) {
        QPainterPath path;
        for (int i = 0; i < samples_.size(); ++i)
        {
            const auto& s = samples_[i];
            const double displayedForce = ForceUnits::fromNewtons(s.force, ForceUnits::fromText(forceUnitLabel_));
            const double value = force ? displayedForce / maxForce : s.position / maxPosition;
            const QPointF point(plot.left() + (s.seconds - start) / 30.0 * plot.width(),
                plot.center().y() - value * plot.height() * 0.45);
            if (i == 0) path.moveTo(point); else path.lineTo(point);
        }
        return path;
    };
    p.setPen(QPen(QColor("#33d6b3"), 2)); p.drawPath(pathFor(true));
    p.setPen(QPen(QColor("#4ba3ff"), 2)); p.drawPath(pathFor(false));
}
