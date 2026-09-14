#include "UtmComplianceCompensation.h"

#include <algorithm>
#include <cmath>

namespace dao::utm
{
ComplianceError UtmComplianceCompensation::ConfigureCurve(
    const CompliancePoint* input, std::size_t count, std::uint32_t version) noexcept
{
    if (!input || count < 2 || count > kComplianceMaxPoints) return ComplianceError::PointCount;
    std::array<CompliancePoint, kComplianceMaxPoints> candidate{};
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!std::isfinite(input[i].forceN) || !std::isfinite(input[i].deformationMm))
            return ComplianceError::NonFinite;
        candidate[i] = input[i];
    }
    std::sort(candidate.begin(), candidate.begin() + count,
              [](const auto& a, const auto& b) { return a.forceN < b.forceN; });
    for (std::size_t i = 1; i < count; ++i)
        if (candidate[i - 1].forceN == candidate[i].forceN) return ComplianceError::DuplicateForce;
    points_ = candidate;
    count_ = count;
    version_ = version == 0 ? 1 : version;
    return ComplianceError::None;
}

void UtmComplianceCompensation::ClearCurve() noexcept
{
    count_ = 0; version_ = 0; enabled_ = false;
}

ComplianceEvaluation UtmComplianceCompensation::Evaluate(double forceN) const noexcept
{
    if (!enabled_ || count_ < 2 || !std::isfinite(forceN)) return {};
    if (forceN <= points_[0].forceN)
        return {points_[0].deformationMm, forceN == points_[0].forceN};
    if (forceN >= points_[count_ - 1].forceN)
        return {points_[count_ - 1].deformationMm, forceN == points_[count_ - 1].forceN};
    const auto begin = points_.begin(), end = begin + count_;
    const auto hi = std::upper_bound(begin, end, forceN,
        [](double value, const CompliancePoint& point) { return value < point.forceN; });
    const auto& a = *(hi - 1); const auto& b = *hi;
    const double ratio = (forceN - a.forceN) / (b.forceN - a.forceN);
    return {a.deformationMm + ratio * (b.deformationMm - a.deformationMm), true};
}

ComplianceRuntime UtmComplianceCompensation::GetRuntime(double forceN, double raw) const noexcept
{
    const auto value = Evaluate(forceN);
    ComplianceRuntime r;
    r.enabled = enabled_; r.configured = count_ >= 2; r.pointCount = static_cast<std::uint32_t>(count_);
    r.forceN = forceN; r.compensationMm = value.compensationMm; r.rawDisplacementMm = raw;
    r.correctedDisplacementMm = raw - value.compensationMm;
    r.inCalibrationRange = value.inCalibrationRange; r.curveVersion = version_;
    return r;
}
}
