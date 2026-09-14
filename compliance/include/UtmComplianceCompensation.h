#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dao::utm
{
constexpr std::size_t kComplianceMaxPoints = 64;

struct CompliancePoint { double forceN = 0.0; double deformationMm = 0.0; };
struct ComplianceEvaluation { double compensationMm = 0.0; bool inCalibrationRange = false; };
struct ComplianceRuntime
{
    bool enabled = false;
    bool configured = false;
    std::uint32_t pointCount = 0;
    double forceN = 0.0;
    double compensationMm = 0.0;
    double rawDisplacementMm = 0.0;
    double correctedDisplacementMm = 0.0;
    bool inCalibrationRange = false;
    std::uint32_t curveVersion = 0;
};

enum class ComplianceError { None, PointCount, NonFinite, DuplicateForce };

class UtmComplianceCompensation
{
public:
    ComplianceError ConfigureCurve(const CompliancePoint* points, std::size_t count,
                                   std::uint32_t version = 1) noexcept;
    void ClearCurve() noexcept;
    void Enable() noexcept { enabled_ = true; }
    void Disable() noexcept { enabled_ = false; }
    ComplianceEvaluation Evaluate(double forceN) const noexcept;
    ComplianceRuntime GetRuntime(double forceN, double rawDisplacementMm) const noexcept;
    const CompliancePoint* Points() const noexcept { return points_.data(); }
    std::size_t PointCount() const noexcept { return count_; }
    std::uint32_t CurveVersion() const noexcept { return version_; }
    bool Enabled() const noexcept { return enabled_; }

private:
    std::array<CompliancePoint, kComplianceMaxPoints> points_{};
    std::size_t count_ = 0;
    std::uint32_t version_ = 0;
    bool enabled_ = false;
};

enum TestSampleFlag : std::uint32_t
{
    ComplianceEnabled = 1U << 0,
    ComplianceOutOfRange = 1U << 1,
    ForceValid = 1U << 2,
    DisplacementValid = 1U << 3,
    ExtensometerValid = 1U << 4
};

// Additive internal recording/protocol sample. Raw data is always retained.
struct UtmAnalysisSample
{
    std::uint64_t timestampUs = 0;
    double forceN = 0.0;
    double rawDisplacementMm = 0.0;
    double complianceCompensationMm = 0.0;
    double correctedDisplacementMm = 0.0;
    double extensometerMm = 0.0;
    std::uint32_t sequenceStep = 0;
    std::uint32_t sampleFlags = 0;
};

// Additive recording metadata. mode: 0=none, 1=compression, 2=tension.
// It deliberately does not alter the Protocol V1 LIVE_DATA binary payload.
struct UtmComplianceTraceMetadata
{
    bool enabled = false;
    std::uint32_t mode = 0;
    std::uint32_t activeCurveVersion = 0;
    std::uint32_t compressionCurveVersion = 0;
    std::uint32_t tensionCurveVersion = 0;
};
}
