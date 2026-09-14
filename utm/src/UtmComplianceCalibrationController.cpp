#include "UtmComplianceCalibrationController.h"

#include <algorithm>
#include <cmath>

namespace
{
bool Positive(double value) { return std::isfinite(value) && value > 0.0; }
int Opposite(int direction)
{ return direction == UTM_DIRECTION_UP ? UTM_DIRECTION_DOWN : UTM_DIRECTION_UP; }
}

bool UtmComplianceCalibrationController::ValidateConfig(
    const UtmComplianceCalibrationConfigV1& c, double overloadLimitN,
    int overloadEnabled, double& allowed)
{
    allowed = 0.0;
    if (c.abiVersion != 1 ||
        (c.mode != UTM_COMPLIANCE_MODE_COMPRESSION && c.mode != UTM_COMPLIANCE_MODE_TENSION) ||
        (c.motionDirection != UTM_DIRECTION_UP && c.motionDirection != UTM_DIRECTION_DOWN) ||
        !Positive(c.loadcellCapacityN) || !Positive(c.maximumForceN) ||
        !Positive(c.forceStepN) || !Positive(c.approachSpeedMmPerMin) ||
        !Positive(c.calibrationSpeedMmPerMin) || !Positive(c.fineSpeedMmPerMin) ||
        !Positive(c.returnSpeedMmPerMin) ||
        c.approachSpeedMmPerMin < c.calibrationSpeedMmPerMin ||
        c.calibrationSpeedMmPerMin < c.fineSpeedMmPerMin ||
        !Positive(c.maximumTravelMm) || !Positive(c.precheckForceN) ||
        !Positive(c.precheckMaximumTravelMm) ||
        c.precheckMaximumTravelMm > c.maximumTravelMm ||
        !Positive(c.forceToleranceN) || !Positive(c.releaseForceThresholdN) ||
        !Positive(c.releaseMaximumTravelMm) || !Positive(c.minimumForceRiseN) ||
        !Positive(c.forceRiseTravelThresholdMm) || !Positive(c.maximumForceJumpN) ||
        !Positive(c.overshootGuardN) || !Positive(c.oppositeForceGuardN) ||
        c.stabilizationTimeMs == 0 || c.minimumStableSampleCount == 0 ||
        c.pointTimeoutMs == 0 || c.releaseTimeoutMs == 0 ||
        c.precheckConfirmationTimeoutMs == 0 || c.forceStepN > c.maximumForceN ||
        c.precheckForceN >= c.maximumForceN)
        return false;
    allowed = c.loadcellCapacityN * 0.90;
    if (overloadEnabled && Positive(overloadLimitN)) allowed = std::min(allowed, overloadLimitN);
    if (Positive(c.manufacturerLimitN)) allowed = std::min(allowed, c.manufacturerLimitN);
    std::array<double, UTM_COMPLIANCE_MAX_POINTS> validationTargets{};
    const unsigned int count = GenerateTargets(c, validationTargets.data(),
        static_cast<unsigned int>(validationTargets.size()));
    if (c.maximumForceN > allowed || count < 2) return false;
    for (unsigned int index = 1; index < count; ++index)
        if (validationTargets[index] - validationTargets[index - 1] <=
            2.0 * c.forceToleranceN) return false;
    return true;
}

unsigned int UtmComplianceCalibrationController::GenerateTargets(
    const UtmComplianceCalibrationConfigV1& c, double* output, unsigned int capacity)
{
    if (!Positive(c.maximumForceN) || !Positive(c.forceStepN) || capacity < 2) return 0;
    unsigned int count = 1;
    if (output) output[0] = 0.0;
    for (double target = c.forceStepN; target < c.maximumForceN; target += c.forceStepN)
    {
        if (!std::isfinite(target) || count >= capacity) return 0;
        if (output) output[count] = target;
        ++count;
    }
    if (count >= capacity) return 0;
    if (!output || output[count - 1] != c.maximumForceN)
    {
        if (output) output[count] = c.maximumForceN;
        ++count;
    }
    return count;
}

bool UtmComplianceCalibrationController::StartPrecheck(
    const UtmComplianceCalibrationConfigV1& config, double overloadLimitN,
    int overloadEnabled, int forceDirectionSign,
    const UtmComplianceCalibrationInput& input, unsigned long long& sessionId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    double allowed = 0.0;
    if (ActiveState() || runtime_.pendingResult ||
        (forceDirectionSign != 1 && forceDirectionSign != -1) ||
        !ValidateConfig(config, overloadLimitN, overloadEnabled, allowed) ||
        !input.machineReady || !input.servoReady || !input.communicationValid ||
        input.communicationRecovering || input.stopLatched || input.emergency ||
        input.externalStop || input.servoFault || input.upperLimit || input.lowerLimit ||
        !input.forceValid || !input.motionOutputStopped)
        return false;
    config_ = config;
    targetCount_ = GenerateTargets(config_, targets_.data(), targets_.size());
    if (targetCount_ < 2) return false;
    runtime_ = {};
    runtime_.abiVersion = 1;
    runtime_.sessionId = nextSessionId_++;
    runtime_.state = UTM_COMPLIANCE_CAL_ZEROING_FORCE;
    runtime_.mode = config_.mode;
    runtime_.active = 1;
    runtime_.allowedMaximumForceN = allowed;
    runtime_.targetCount = targetCount_;
    startMachinePositionMm_ = input.machinePositionMm;
    startTestPositionMm_ = input.testPositionMm;
    forceDirectionSign_ = forceDirectionSign;
    expectedPositionSign_ = config_.motionDirection == UTM_DIRECTION_UP ? 1 : -1;
    pointCount_ = targetIndex_ = stableSamples_ = 0;
    previousForceN_ = input.forceN;
    previousForceValid_ = input.forceValid != 0;
    zeroActiveObserved_ = fullConfirmed_ = releaseAfterPrecheckExpiry_ = false;
    returnActionIssued_ = returnActionAccepted_ = false;
    stateStartedNs_ = input.timestampNs;
    output_ = {};
    action_ = {UtmComplianceCalibrationActionType::ZeroForce, 0.0, 0.0,
        config_.pointTimeoutMs};
    sessionId = runtime_.sessionId;
    return true;
}

bool UtmComplianceCalibrationController::ConfirmFull(unsigned long long sessionId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (sessionId == 0 || sessionId != runtime_.sessionId ||
        runtime_.state != UTM_COMPLIANCE_CAL_WAITING_FULL_START) return false;
    fullConfirmed_ = true;
    return true;
}

bool UtmComplianceCalibrationController::Abort(unsigned long long sessionId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (sessionId == 0 || sessionId != runtime_.sessionId || !ActiveState()) return false;
    BeginAbort();
    return true;
}

bool UtmComplianceCalibrationController::DiscardPending(unsigned long long sessionId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (sessionId == 0 || sessionId != runtime_.sessionId ||
        runtime_.state != UTM_COMPLIANCE_CAL_COMPLETE_PENDING_SAVE) return false;
    runtime_ = {};
    runtime_.abiVersion = 1;
    pointCount_ = targetCount_ = targetIndex_ = 0;
    return true;
}

bool UtmComplianceCalibrationController::GetRuntime(
    UtmComplianceCalibrationRuntimeV1& runtime) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    runtime = runtime_;
    return true;
}

bool UtmComplianceCalibrationController::GetPendingPoints(unsigned long long sessionId,
    UtmComplianceCalibrationPoint* output, unsigned int capacity,
    unsigned int& count) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    count = pointCount_;
    if (sessionId == 0 || sessionId != runtime_.sessionId || !runtime_.pendingResult ||
        (capacity > 0 && output == nullptr)) return false;
    if (capacity == 0 && output == nullptr) return true;
    const unsigned int copied = std::min(capacity, pointCount_);
    for (unsigned int i = 0; i < copied; ++i) output[i] = points_[i];
    return capacity >= pointCount_;
}

bool UtmComplianceCalibrationController::ActiveState() const
{
    return runtime_.active != 0;
}

bool UtmComplianceCalibrationController::IsActive() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return ActiveState();
}

bool UtmComplianceCalibrationController::OwnsMotion() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return ActiveState() || runtime_.state == UTM_COMPLIANCE_CAL_ABORTING;
}

double UtmComplianceCalibrationController::DirectionalForce(double raw) const
{
    const int direction = config_.motionDirection == UTM_DIRECTION_UP ? 1 : -1;
    return raw * static_cast<double>(forceDirectionSign_ * direction);
}

double UtmComplianceCalibrationController::SelectSpeed(double target, double error) const
{
    if (error <= std::max(config_.forceToleranceN * 4.0, config_.overshootGuardN))
        return config_.fineSpeedMmPerMin;
    const double ratio = target / config_.maximumForceN;
    if (ratio >= 0.90) return config_.fineSpeedMmPerMin;
    if (ratio >= 0.70) return config_.calibrationSpeedMmPerMin;
    return config_.approachSpeedMmPerMin;
}

void UtmComplianceCalibrationController::SetFault(int fault)
{
    runtime_.fault = fault;
    runtime_.state = UTM_COMPLIANCE_CAL_FAULTED;
    runtime_.active = 0;
    runtime_.pendingResult = 0;
    pointCount_ = 0;
    output_ = {};
    output_.forceStopRequested = 1;
    action_ = {};
}

void UtmComplianceCalibrationController::BeginAbort()
{
    runtime_.fault = UTM_COMPLIANCE_FAULT_USER_ABORT;
    runtime_.state = UTM_COMPLIANCE_CAL_ABORTING;
    runtime_.pendingResult = 0;
    pointCount_ = 0;
    output_ = {};
    output_.forceStopRequested = 1;
    action_ = {};
}

bool UtmComplianceCalibrationController::CheckFaults(
    const UtmComplianceCalibrationInput& i)
{
    if (i.emergency) { SetFault(UTM_COMPLIANCE_FAULT_EMERGENCY); return false; }
    if (i.externalStop) { SetFault(UTM_COMPLIANCE_FAULT_EXTERNAL_STOP); return false; }
    if (i.servoFault) { SetFault(UTM_COMPLIANCE_FAULT_SERVO); return false; }
    if (i.upperLimit) { SetFault(UTM_COMPLIANCE_FAULT_UPPER_LIMIT); return false; }
    if (i.lowerLimit) { SetFault(UTM_COMPLIANCE_FAULT_LOWER_LIMIT); return false; }
    if (i.communicationRecovering)
    { SetFault(UTM_COMPLIANCE_FAULT_COMMUNICATION_INTERRUPTED); return false; }
    if (!i.communicationValid)
    { SetFault(UTM_COMPLIANCE_FAULT_COMMUNICATION); return false; }
    if (i.overload) { SetFault(UTM_COMPLIANCE_FAULT_OVERLOAD); return false; }
    if (i.stopLatched) { SetFault(UTM_COMPLIANCE_FAULT_MOTION); return false; }
    if (!i.forceValid || !std::isfinite(i.forceN))
    { SetFault(UTM_COMPLIANCE_FAULT_FORCE_INVALID); return false; }
    if (std::fabs(i.forceN) > runtime_.allowedMaximumForceN)
    { SetFault(UTM_COMPLIANCE_FAULT_HARD_FORCE); return false; }
    if (previousForceValid_ && std::fabs(i.forceN - previousForceN_) > config_.maximumForceJumpN)
    { SetFault(UTM_COMPLIANCE_FAULT_FORCE_JUMP); return false; }
    previousForceN_ = i.forceN;
    previousForceValid_ = true;
    if (std::fabs(i.machinePositionMm - startMachinePositionMm_) > config_.maximumTravelMm)
    { SetFault(UTM_COMPLIANCE_FAULT_MAX_TRAVEL); return false; }
    return true;
}

void UtmComplianceCalibrationController::BeginStable(int state,
    const UtmComplianceCalibrationInput& input)
{
    runtime_.state = state;
    stateStartedNs_ = input.timestampNs;
    stableStartedNs_ = input.timestampNs;
    stableForceSum_ = stablePositionSum_ = 0.0;
    stableSamples_ = 0;
    output_ = {};
    output_.forceStopRequested = 1;
}

bool UtmComplianceCalibrationController::AccumulateStable(
    const UtmComplianceCalibrationInput& input, double target)
{
    const double directional = DirectionalForce(input.forceN);
    if (!input.motionOutputStopped || std::fabs(directional - target) > config_.forceToleranceN)
    {
        stableStartedNs_ = input.timestampNs;
        stableForceSum_ = stablePositionSum_ = 0.0;
        stableSamples_ = 0;
        runtime_.stableSampleCount = 0;
        return false;
    }
    if (stableSamples_ == 0) stableStartedNs_ = input.timestampNs;
    stableForceSum_ += input.forceN;
    stablePositionSum_ += input.machinePositionMm;
    ++stableSamples_;
    runtime_.stableSampleCount = stableSamples_;
    return stableSamples_ >= config_.minimumStableSampleCount &&
        input.timestampNs - stableStartedNs_ >=
            static_cast<unsigned long long>(config_.stabilizationTimeMs) * 1000000ULL;
}

void UtmComplianceCalibrationController::BeginApproach(unsigned int index,
    const UtmComplianceCalibrationInput& input)
{
    targetIndex_ = index;
    runtime_.currentTargetIndex = index;
    runtime_.targetForceN = targets_[index];
    runtime_.state = targets_[index] / config_.maximumForceN >= 0.90
        ? UTM_COMPLIANCE_CAL_FINE_APPROACH : UTM_COMPLIANCE_CAL_APPROACHING_TARGET;
    stateStartedNs_ = input.timestampNs;
    approachStartMachinePositionMm_ = input.machinePositionMm;
    approachStartDirectionalForceN_ = DirectionalForce(input.forceN);
}

void UtmComplianceCalibrationController::BeginRelease(
    const UtmComplianceCalibrationInput& input, bool expired)
{
    runtime_.state = UTM_COMPLIANCE_CAL_RELEASING_FORCE;
    stateStartedNs_ = input.timestampNs;
    releaseStartMachinePositionMm_ = input.machinePositionMm;
    stableStartedNs_ = input.timestampNs;
    stableSamples_ = 0;
    releaseAfterPrecheckExpiry_ = expired;
}

void UtmComplianceCalibrationController::Update(
    const UtmComplianceCalibrationInput& input)
{
    std::lock_guard<std::mutex> lock(mutex_);
    runtime_.currentForceN = input.forceN;
    runtime_.directionalForceN = DirectionalForce(input.forceN);
    runtime_.currentMachinePositionMm = input.machinePositionMm;
    runtime_.currentDeformationMm = input.machinePositionMm - referenceMachinePositionMm_;
    runtime_.travelUsedMm = std::fabs(input.machinePositionMm - startMachinePositionMm_);
    output_ = {};

    if (runtime_.state == UTM_COMPLIANCE_CAL_IDLE ||
        runtime_.state == UTM_COMPLIANCE_CAL_COMPLETE_PENDING_SAVE ||
        runtime_.state == UTM_COMPLIANCE_CAL_ABORTED ||
        runtime_.state == UTM_COMPLIANCE_CAL_FAULTED ||
        runtime_.state == UTM_COMPLIANCE_CAL_PRECHECK_EXPIRED) return;

    if (runtime_.state == UTM_COMPLIANCE_CAL_ABORTING)
    {
        output_.forceStopRequested = 1;
        if (input.emergency || input.externalStop || input.servoFault ||
            !input.communicationValid || input.communicationRecovering || input.stopLatched)
        { CheckFaults(input); return; }
        if (input.motionOutputStopped)
        { runtime_.state = UTM_COMPLIANCE_CAL_ABORTED; runtime_.active = 0; }
        return;
    }

    if (!CheckFaults(input)) return;
    const double directional = DirectionalForce(input.forceN);
    const double elapsedMs = static_cast<double>(input.timestampNs - stateStartedNs_) / 1.0e6;

    switch (runtime_.state)
    {
    case UTM_COMPLIANCE_CAL_ZEROING_FORCE:
        if (input.forceZeroCaptureActive) zeroActiveObserved_ = true;
        if (zeroActiveObserved_ && !input.forceZeroCaptureActive)
        {
            runtime_.state = UTM_COMPLIANCE_CAL_CAPTURING_REFERENCE;
            BeginStable(UTM_COMPLIANCE_CAL_CAPTURING_REFERENCE, input);
        }
        else if (elapsedMs >= config_.pointTimeoutMs) SetFault(UTM_COMPLIANCE_FAULT_FORCE_ZERO);
        break;
    case UTM_COMPLIANCE_CAL_CAPTURING_REFERENCE:
        if (AccumulateStable(input, 0.0))
        {
            referenceMachinePositionMm_ = stablePositionSum_ / stableSamples_;
            runtime_.referenceMachinePositionMm = referenceMachinePositionMm_;
            points_[0] = {stableForceSum_ / stableSamples_, 0.0};
            pointCount_ = 1;
            runtime_.capturedPointCount = 1;
            runtime_.state = UTM_COMPLIANCE_CAL_PRECHECK_APPROACH;
            runtime_.targetForceN = config_.precheckForceN;
            stateStartedNs_ = input.timestampNs;
            approachStartMachinePositionMm_ = input.machinePositionMm;
            approachStartDirectionalForceN_ = directional;
        }
        else if (elapsedMs >= config_.pointTimeoutMs) SetFault(UTM_COMPLIANCE_FAULT_TIMEOUT);
        break;
    case UTM_COMPLIANCE_CAL_PRECHECK_APPROACH:
    case UTM_COMPLIANCE_CAL_APPROACHING_TARGET:
    case UTM_COMPLIANCE_CAL_FINE_APPROACH:
    {
        const bool precheck = runtime_.state == UTM_COMPLIANCE_CAL_PRECHECK_APPROACH;
        const double target = precheck ? config_.precheckForceN : targets_[targetIndex_];
        const double travel = std::fabs(input.machinePositionMm - approachStartMachinePositionMm_);
        if (directional < -config_.forceToleranceN)
        { SetFault(UTM_COMPLIANCE_FAULT_WRONG_FORCE_POLARITY); break; }
        if ((input.machinePositionMm - approachStartMachinePositionMm_) * expectedPositionSign_ < -1.0e-9)
        { SetFault(UTM_COMPLIANCE_FAULT_WRONG_POSITION_DIRECTION); break; }
        if (precheck && travel >= config_.forceRiseTravelThresholdMm &&
            directional - approachStartDirectionalForceN_ < config_.minimumForceRiseN)
        { SetFault(UTM_COMPLIANCE_FAULT_INSUFFICIENT_FORCE_RISE); break; }
        if (precheck && travel >= config_.precheckMaximumTravelMm)
        { SetFault(UTM_COMPLIANCE_FAULT_PRECHECK_TRAVEL); break; }
        if (directional > target + config_.overshootGuardN)
        { SetFault(UTM_COMPLIANCE_FAULT_OVERSHOOT); break; }
        if (elapsedMs >= config_.pointTimeoutMs)
        { SetFault(UTM_COMPLIANCE_FAULT_TIMEOUT); break; }
        const double error = target - directional;
        if (std::fabs(error) <= config_.forceToleranceN)
            BeginStable(precheck ? UTM_COMPLIANCE_CAL_PRECHECK_STABILIZING :
                UTM_COMPLIANCE_CAL_STABILIZING, input);
        else
        {
            output_.velocityRequested = 1;
            output_.direction = config_.motionDirection;
            output_.speedMmPerMin = precheck ? config_.fineSpeedMmPerMin : SelectSpeed(target, error);
            runtime_.currentCommandSpeedMmPerMin = output_.speedMmPerMin;
        }
        break;
    }
    case UTM_COMPLIANCE_CAL_PRECHECK_STABILIZING:
        output_.forceStopRequested = 1;
        if (AccumulateStable(input, config_.precheckForceN))
        { runtime_.state = UTM_COMPLIANCE_CAL_STOPPING_PRECHECK; }
        else if (elapsedMs >= config_.pointTimeoutMs) SetFault(UTM_COMPLIANCE_FAULT_TIMEOUT);
        break;
    case UTM_COMPLIANCE_CAL_STOPPING_PRECHECK:
        output_.forceStopRequested = 1;
        if (input.motionOutputStopped)
        {
            runtime_.state = UTM_COMPLIANCE_CAL_WAITING_FULL_START;
            runtime_.precheckPassed = 1;
            stateStartedNs_ = input.timestampNs;
        }
        break;
    case UTM_COMPLIANCE_CAL_WAITING_FULL_START:
        output_.forceStopRequested = 1;
        if (fullConfirmed_)
        { fullConfirmed_ = false; BeginApproach(1, input); }
        else if (elapsedMs >= config_.precheckConfirmationTimeoutMs)
            BeginRelease(input, true);
        break;
    case UTM_COMPLIANCE_CAL_STABILIZING:
        output_.forceStopRequested = 1;
        if (AccumulateStable(input, targets_[targetIndex_]))
            runtime_.state = UTM_COMPLIANCE_CAL_CAPTURING_POINT;
        else if (elapsedMs >= config_.pointTimeoutMs) SetFault(UTM_COMPLIANCE_FAULT_TIMEOUT);
        break;
    case UTM_COMPLIANCE_CAL_CAPTURING_POINT:
        if (stableSamples_ == 0 || pointCount_ >= UTM_COMPLIANCE_MAX_POINTS)
        { SetFault(UTM_COMPLIANCE_FAULT_MOTION); break; }
        points_[pointCount_++] = {stableForceSum_ / stableSamples_,
            stablePositionSum_ / stableSamples_ - referenceMachinePositionMm_};
        runtime_.capturedPointCount = pointCount_;
        runtime_.state = UTM_COMPLIANCE_CAL_NEXT_POINT;
        break;
    case UTM_COMPLIANCE_CAL_NEXT_POINT:
        if (++targetIndex_ < targetCount_) BeginApproach(targetIndex_, input);
        else BeginRelease(input, false);
        break;
    case UTM_COMPLIANCE_CAL_RELEASING_FORCE:
    {
        const double releaseTravel = std::fabs(input.machinePositionMm - releaseStartMachinePositionMm_);
        if (releaseTravel >= config_.releaseMaximumTravelMm)
        { SetFault(UTM_COMPLIANCE_FAULT_RELEASE_TRAVEL); break; }
        if (elapsedMs >= config_.releaseTimeoutMs)
        { SetFault(UTM_COMPLIANCE_FAULT_RELEASE_TIMEOUT); break; }
        if (directional < -config_.oppositeForceGuardN)
        { SetFault(UTM_COMPLIANCE_FAULT_OPPOSITE_FORCE); break; }
        if (std::fabs(input.forceN) <= config_.releaseForceThresholdN)
        {
            if (stableSamples_ == 0) stableStartedNs_ = input.timestampNs;
            ++stableSamples_;
            if (stableSamples_ >= config_.minimumStableSampleCount &&
                input.timestampNs - stableStartedNs_ >=
                    static_cast<unsigned long long>(config_.stabilizationTimeMs) * 1000000ULL)
                runtime_.state = UTM_COMPLIANCE_CAL_STOPPING_RELEASE;
        }
        else stableSamples_ = 0;
        if (runtime_.state == UTM_COMPLIANCE_CAL_RELEASING_FORCE)
        {
            output_.velocityRequested = 1;
            output_.direction = Opposite(config_.motionDirection);
            output_.speedMmPerMin = config_.fineSpeedMmPerMin;
            runtime_.currentCommandSpeedMmPerMin = output_.speedMmPerMin;
        }
        break;
    }
    case UTM_COMPLIANCE_CAL_STOPPING_RELEASE:
        output_.forceStopRequested = 1;
        if (input.motionOutputStopped)
        {
            if (releaseAfterPrecheckExpiry_)
            { runtime_.state = UTM_COMPLIANCE_CAL_PRECHECK_EXPIRED; runtime_.active = 0; pointCount_ = 0; }
            else
            {
                runtime_.state = UTM_COMPLIANCE_CAL_RETURNING;
                returnActionIssued_ = true;
                action_ = {UtmComplianceCalibrationActionType::ReturnAbsolute,
                    startTestPositionMm_, config_.returnSpeedMmPerMin, config_.pointTimeoutMs};
            }
        }
        break;
    case UTM_COMPLIANCE_CAL_RETURNING:
        if (input.returnMotionFailed) SetFault(UTM_COMPLIANCE_FAULT_MOTION);
        else if (returnActionAccepted_ && input.returnMotionComplete)
        {
            std::sort(points_.begin(), points_.begin() + pointCount_,
                [](const UtmComplianceCalibrationPoint& a,
                   const UtmComplianceCalibrationPoint& b)
                { return a.forceN < b.forceN; });
            runtime_.state = UTM_COMPLIANCE_CAL_COMPLETE_PENDING_SAVE;
            runtime_.active = 0;
            runtime_.pendingResult = 1;
        }
        break;
    default:
        break;
    }
}

UtmComplianceCalibrationOutput UtmComplianceCalibrationController::GetOutput() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return output_;
}

bool UtmComplianceCalibrationController::TakeAction(UtmComplianceCalibrationAction& result)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (action_.type == UtmComplianceCalibrationActionType::None) return false;
    result = action_;
    action_ = {};
    return true;
}

void UtmComplianceCalibrationController::ReportActionResult(
    UtmComplianceCalibrationActionType type, bool accepted)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!accepted) { SetFault(type == UtmComplianceCalibrationActionType::ZeroForce
        ? UTM_COMPLIANCE_FAULT_FORCE_ZERO : UTM_COMPLIANCE_FAULT_MOTION); return; }
    if (type == UtmComplianceCalibrationActionType::ReturnAbsolute)
        returnActionAccepted_ = true;
}

void UtmComplianceCalibrationController::ReportMotionOutputFailure()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (ActiveState()) SetFault(UTM_COMPLIANCE_FAULT_MOTION);
}
