#include "UtmStopConditionMonitor.h"

#include <algorithm>
#include <cmath>

void UtmStopConditionMonitor::Start(
    const UtmStopConditionConfig& config,
    double startMachinePositionMm,
    unsigned long long timestampNs)
{
    config_ = config;
    runtime_ = {};
    runtime_.active = 1;
    startMachinePositionMm_ = startMachinePositionMm;
    startedNs_ = timestampNs;
    breakCandidateSinceNs_ = 0;
}

void UtmStopConditionMonitor::Reset()
{
    config_ = {};
    runtime_ = {};
    startMachinePositionMm_ = 0.0;
    startedNs_ = 0;
    breakCandidateSinceNs_ = 0;
}

int UtmStopConditionMonitor::Update(
    const UtmInputSnapshot& snapshot,
    double machinePositionMm,
    unsigned long long timestampNs)
{
    if (runtime_.active == 0) return UTM_COMPLETION_NONE;

    runtime_.elapsedSec = static_cast<double>(timestampNs - startedNs_) / 1.0e9;
    runtime_.travelMm = std::fabs(machinePositionMm - startMachinePositionMm_);

    if (snapshot.forceValid != 0)
    {
        runtime_.currentForceMagnitudeN = std::fabs(snapshot.forceN);
        runtime_.peakForceMagnitudeN = std::max(runtime_.peakForceMagnitudeN,
            runtime_.currentForceMagnitudeN);

        if (config_.enableMaxForce != 0 &&
            runtime_.currentForceMagnitudeN >= config_.maxForceN)
        {
            runtime_.completionReason = UTM_COMPLETION_MAX_FORCE_REACHED;
        }

        const bool breakCandidate = config_.enableBreakDetection != 0 &&
            runtime_.peakForceMagnitudeN >= config_.minimumBreakPeakN &&
            runtime_.currentForceMagnitudeN <= runtime_.peakForceMagnitudeN *
                (100.0 - config_.breakDropPercent) / 100.0;
        if (breakCandidate)
        {
            if (breakCandidateSinceNs_ == 0) breakCandidateSinceNs_ = timestampNs;
            runtime_.breakConfirmElapsedMs = static_cast<unsigned int>(
                (timestampNs - breakCandidateSinceNs_) / 1000000ULL);
            if (runtime_.completionReason == UTM_COMPLETION_NONE &&
                runtime_.breakConfirmElapsedMs >= config_.breakConfirmMs)
                runtime_.completionReason = UTM_COMPLETION_BREAK_DETECTED;
        }
        else
        {
            breakCandidateSinceNs_ = 0;
            runtime_.breakConfirmElapsedMs = 0;
        }
    }

    if (runtime_.completionReason == UTM_COMPLETION_NONE &&
        config_.enableMaxTravel != 0 && runtime_.travelMm >= config_.maxTravelMm)
        runtime_.completionReason = UTM_COMPLETION_MAX_TRAVEL_REACHED;
    if (runtime_.completionReason == UTM_COMPLETION_NONE &&
        config_.enableTimeout != 0 && runtime_.elapsedSec >= config_.timeoutSec)
        runtime_.completionReason = UTM_COMPLETION_TIME_COMPLETED;

    if (runtime_.completionReason != UTM_COMPLETION_NONE) runtime_.active = 0;
    return runtime_.completionReason;
}

const UtmStopConditionRuntime& UtmStopConditionMonitor::GetRuntime() const
{
    return runtime_;
}
