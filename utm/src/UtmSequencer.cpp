#include "UtmSequencer.h"

#include <cmath>

bool UtmSequencer::Load(const UtmSequenceDefinition& definition)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (runtime_.sequenceRunning != 0 || definition.stepCount > UTM_SEQUENCE_MAX_STEPS)
        return false;
    pending_ = definition;
    runtime_ = {};
    runtime_.sequenceLoaded = 1;
    runtime_.sequenceState = UTM_SEQUENCE_STATE_EDITING;
    runtime_.stepCount = definition.stepCount;
    return true;
}

bool UtmSequencer::Validate(bool encoderPresent, double overloadN)
{
    std::lock_guard<std::mutex> lock(mutex_);
    runtime_.sequenceValidated = 0;
    runtime_.validationError = UTM_SEQUENCE_VALIDATION_NONE;
    runtime_.validationErrorStepIndex = -1;
    if (pending_.abiVersion != 1 || pending_.stepCount == 0 ||
        pending_.stepCount > UTM_SEQUENCE_MAX_STEPS)
    {
        runtime_.validationError = UTM_SEQUENCE_VALIDATION_STEP_COUNT;
        return false;
    }
    unsigned int depth = 0;
    bool hasEnd = false;
    for (unsigned int i = 0; i < pending_.stepCount; ++i)
    {
        const UtmSequenceStep& step = pending_.steps[i];
        if (!ValidateStep(step, i, encoderPresent, overloadN)) return false;
        if (step.stepType == UTM_SEQUENCE_STEP_LOOP_START)
        {
            if (++depth > UTM_SEQUENCE_MAX_LOOP_DEPTH)
            {
                runtime_.validationError = UTM_SEQUENCE_VALIDATION_LOOP_DEPTH;
                runtime_.validationErrorStepIndex = static_cast<int>(i);
                return false;
            }
        }
        else if (step.stepType == UTM_SEQUENCE_STEP_LOOP_END)
        {
            if (depth == 0)
            {
                runtime_.validationError = UTM_SEQUENCE_VALIDATION_LOOP_PAIRING;
                runtime_.validationErrorStepIndex = static_cast<int>(i);
                return false;
            }
            --depth;
        }
        if (step.stepType == UTM_SEQUENCE_STEP_END)
        {
            hasEnd = true;
            if (i + 1 != pending_.stepCount)
            {
                runtime_.validationError = UTM_SEQUENCE_VALIDATION_END_REQUIRED;
                runtime_.validationErrorStepIndex = static_cast<int>(i);
                return false;
            }
        }
    }
    if (depth != 0 || !hasEnd)
    {
        runtime_.validationError = depth != 0
            ? UTM_SEQUENCE_VALIDATION_LOOP_PAIRING : UTM_SEQUENCE_VALIDATION_END_REQUIRED;
        runtime_.validationErrorStepIndex = static_cast<int>(pending_.stepCount - 1);
        return false;
    }
    runtime_.sequenceValidated = 1;
    runtime_.sequenceState = UTM_SEQUENCE_STATE_VALIDATED;
    return true;
}

bool UtmSequencer::ValidateStep(const UtmSequenceStep& s, unsigned int index,
    bool encoderPresent, double overloadN)
{
    auto fail = [&](int error) { runtime_.validationError = error;
        runtime_.validationErrorStepIndex = static_cast<int>(index); return false; };
    if (s.stepIndex != index || s.stepType < UTM_SEQUENCE_STEP_ZERO_FORCE ||
        s.stepType > UTM_SEQUENCE_STEP_END)
        return fail(UTM_SEQUENCE_VALIDATION_UNSUPPORTED_STEP);
    const bool motion = s.stepType >= UTM_SEQUENCE_STEP_MOVE_ABSOLUTE &&
        s.stepType <= UTM_SEQUENCE_STEP_HOLD_FORCE;
    if (motion && (!std::isfinite(s.speedMmPerMin) || s.speedMmPerMin <= 0.0 ||
        s.acceleration == 0 || s.deceleration == 0))
        return fail(UTM_SEQUENCE_VALIDATION_INVALID_PARAMETER);
    if ((s.stepType == UTM_SEQUENCE_STEP_MOVE_VELOCITY ||
         s.stepType == UTM_SEQUENCE_STEP_MOVE_TO_FORCE ||
         s.stepType == UTM_SEQUENCE_STEP_HOLD_FORCE) &&
        s.direction != UTM_DIRECTION_UP && s.direction != UTM_DIRECTION_DOWN)
        return fail(UTM_SEQUENCE_VALIDATION_INVALID_DIRECTION);
    if ((s.stepType == UTM_SEQUENCE_STEP_MOVE_TO_FORCE ||
         s.stepType == UTM_SEQUENCE_STEP_HOLD_FORCE) &&
        (!std::isfinite(s.forceN) || s.forceN <= 0.0 || s.forceN > overloadN))
        return fail(s.forceN > overloadN
            ? UTM_SEQUENCE_VALIDATION_FORCE_LIMIT_OVERLOAD
            : UTM_SEQUENCE_VALIDATION_INVALID_PARAMETER);
    if ((s.stepType == UTM_SEQUENCE_STEP_MOVE_ABSOLUTE &&
            !std::isfinite(s.positionMm)) ||
        (s.stepType == UTM_SEQUENCE_STEP_MOVE_INCREMENTAL &&
            !std::isfinite(s.distanceMm)))
        return fail(UTM_SEQUENCE_VALIDATION_INVALID_PARAMETER);
    if ((s.stepType == UTM_SEQUENCE_STEP_MOVE_TO_FORCE ||
         s.stepType == UTM_SEQUENCE_STEP_HOLD_FORCE ||
         s.stepType == UTM_SEQUENCE_STEP_ZERO_FORCE ||
         s.stepType == UTM_SEQUENCE_STEP_ZERO_POSITION ||
         s.stepType == UTM_SEQUENCE_STEP_ZERO_ENCODER) && s.timeoutMs == 0)
        return fail(UTM_SEQUENCE_VALIDATION_INVALID_PARAMETER);
    if (s.stepType == UTM_SEQUENCE_STEP_HOLD_FORCE &&
        (!std::isfinite(s.holdTimeSec) || s.holdTimeSec <= 0.0 ||
         !std::isfinite(s.toleranceN) || s.toleranceN <= 0.0))
        return fail(UTM_SEQUENCE_VALIDATION_INVALID_PARAMETER);
    if ((s.stepType == UTM_SEQUENCE_STEP_WAIT_TIME ||
         s.stepType == UTM_SEQUENCE_STEP_PULSE_OUTPUT) &&
        (!std::isfinite(s.durationSec) || s.durationSec <= 0.0))
        return fail(UTM_SEQUENCE_VALIDATION_INVALID_PARAMETER);
    if ((s.stepType == UTM_SEQUENCE_STEP_WAIT_INPUT ||
         s.stepType == UTM_SEQUENCE_STEP_SET_OUTPUT ||
         s.stepType == UTM_SEQUENCE_STEP_PULSE_OUTPUT) &&
        (s.ioBit > 7 || (s.ioState != 0 && s.ioState != 1)))
        return fail(UTM_SEQUENCE_VALIDATION_INVALID_IO_BIT);
    if (s.stepType == UTM_SEQUENCE_STEP_WAIT_INPUT && s.timeoutMs == 0)
        return fail(UTM_SEQUENCE_VALIDATION_INVALID_PARAMETER);
    if (s.stepType == UTM_SEQUENCE_STEP_LOOP_START && s.loopCount == 0)
        return fail(UTM_SEQUENCE_VALIDATION_LOOP_COUNT);
    if (s.stepType == UTM_SEQUENCE_STEP_ZERO_ENCODER && !encoderPresent)
        return fail(UTM_SEQUENCE_VALIDATION_ENCODER_UNAVAILABLE);
    const UtmStopConditionConfig& c = s.stopConditions;
    const bool anyStop = c.enableMaxForce || c.enableMaxTravel ||
        c.enableTimeout || c.enableBreakDetection;
    if (s.stepType == UTM_SEQUENCE_STEP_MOVE_VELOCITY && !anyStop)
        return fail(UTM_SEQUENCE_VALIDATION_VELOCITY_STOP_REQUIRED);
    if (c.enableMaxForce && (!std::isfinite(c.maxForceN) || c.maxForceN <= 0.0 ||
        c.maxForceN > overloadN))
        return fail(UTM_SEQUENCE_VALIDATION_FORCE_LIMIT_OVERLOAD);
    if (c.enableMaxTravel && (!std::isfinite(c.maxTravelMm) || c.maxTravelMm <= 0.0))
        return fail(UTM_SEQUENCE_VALIDATION_INVALID_PARAMETER);
    if (c.enableTimeout && (!std::isfinite(c.timeoutSec) || c.timeoutSec <= 0.0))
        return fail(UTM_SEQUENCE_VALIDATION_INVALID_PARAMETER);
    if (c.enableBreakDetection && (!std::isfinite(c.minimumBreakPeakN) ||
        c.minimumBreakPeakN <= 0.0 || !std::isfinite(c.breakDropPercent) ||
        c.breakDropPercent <= 0.0 || c.breakDropPercent >= 100.0 ||
        c.breakConfirmMs == 0 || c.minimumBreakPeakN > overloadN))
        return fail(UTM_SEQUENCE_VALIDATION_INVALID_PARAMETER);
    return true;
}

bool UtmSequencer::Commit()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (runtime_.sequenceValidated == 0 || runtime_.sequenceRunning != 0) return false;
    active_ = pending_;
    runtime_.sequenceCommitted = 1;
    runtime_.sequenceState = UTM_SEQUENCE_STATE_COMMITTED;
    return true;
}

bool UtmSequencer::RequestStart()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (runtime_.sequenceCommitted == 0 || runtime_.sequenceRunning != 0) return false;
    startRequested_ = true;
    return true;
}
bool UtmSequencer::RequestGoStart() { return RequestStart(); }
bool UtmSequencer::RequestStop()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (runtime_.sequenceRunning == 0) return false;
    stopRequested_ = true;
    return true;
}

void UtmSequencer::Update(const UtmInputSnapshot& input,
    const UtmServoCommandSnapshot& auxiliaryInput,
    const UtmGeneralMotionRuntimeInfo& motion, unsigned long long now)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (startRequested_)
    {
        startRequested_ = false; stopRequested_ = false; loopDepth_ = 0; outputs_ = 0;
        runtime_.sequenceRunning = 1; runtime_.sequenceState = UTM_SEQUENCE_STATE_RUNNING;
        runtime_.sequenceComplete = runtime_.sequenceAborted = runtime_.sequenceFailed = 0;
        runtime_.currentStepIndex = 0; runtime_.recordingActive = 1;
        runtime_.recordingLastEvent = 1; ++runtime_.recordingSessionId;
        sequenceStartedNs_ = now; QueueAction(UTM_SEQUENCE_ACTION_SEQUENCE_BEGIN);
        BeginStep(now); return;
    }
    if (runtime_.sequenceRunning == 0) return;
    runtime_.sequenceElapsedSec = static_cast<double>(now - sequenceStartedNs_) / 1.0e9;
    runtime_.stepElapsedSec = static_cast<double>(now - stepStartedNs_) / 1.0e9;
    if (stopRequested_)
    {
        stopRequested_ = false; runtime_.sequenceState = UTM_SEQUENCE_STATE_STOPPING;
        runtime_.currentStepState = UTM_SEQUENCE_STEP_STATE_ABORTED;
        QueueAction(UTM_SEQUENCE_ACTION_STOP_MOTION); return;
    }
    if (actionPending_ || waitingActionResult_) return;
    const UtmSequenceStep& s = active_.steps[runtime_.currentStepIndex];
    if (runtime_.currentStepState == UTM_SEQUENCE_STEP_STATE_STARTING)
    {
        if (MotionTypeForStep(s.stepType) != UTM_MOTION_NONE)
            QueueAction(UTM_SEQUENCE_ACTION_SUBMIT_MOTION, &s);
        else if (s.stepType == UTM_SEQUENCE_STEP_ZERO_FORCE)
            QueueAction(UTM_SEQUENCE_ACTION_ZERO_FORCE, &s);
        else if (s.stepType == UTM_SEQUENCE_STEP_ZERO_POSITION)
            QueueAction(UTM_SEQUENCE_ACTION_ZERO_POSITION, &s);
        else if (s.stepType == UTM_SEQUENCE_STEP_ZERO_ENCODER)
            QueueAction(UTM_SEQUENCE_ACTION_ZERO_ENCODER, &s);
        else if (s.stepType == UTM_SEQUENCE_STEP_SET_OUTPUT ||
                 s.stepType == UTM_SEQUENCE_STEP_PULSE_OUTPUT)
        {
            if (s.ioState) outputs_ |= static_cast<unsigned short>(1U << s.ioBit);
            else outputs_ &= static_cast<unsigned short>(~(1U << s.ioBit));
            QueueAction(UTM_SEQUENCE_ACTION_SET_OUTPUTS, &s);
        }
        else runtime_.currentStepState = UTM_SEQUENCE_STEP_STATE_RUNNING;
        return;
    }
    if (MotionTypeForStep(s.stepType) != UTM_MOTION_NONE)
    {
        if (motion.motionActive != 0 && motion.motionSource == UTM_COMMAND_SOURCE_SEQUENCER)
            motionObserved_ = true;
        if (motionObserved_ && motion.motionState == UTM_MOTION_STATE_COMPLETED)
            CompleteStep(s.stepType == UTM_SEQUENCE_STEP_MOVE_TO_FORCE ||
                s.stepType == UTM_SEQUENCE_STEP_HOLD_FORCE
                    ? UTM_COMPLETION_TARGET_FORCE : UTM_COMPLETION_TARGET_POSITION, now);
        else if (motionObserved_ && motion.motionState == UTM_MOTION_STATE_ABORTED)
        {
            if (pendingStopCompletionReason_ != UTM_COMPLETION_NONE)
                CompleteStep(pendingStopCompletionReason_, now);
            else AbortUnlocked();
        }
        else if (motionObserved_ && motion.motionState == UTM_MOTION_STATE_FAILED)
            FailStep();
    }
    else if (s.stepType == UTM_SEQUENCE_STEP_ZERO_FORCE)
    {
        if (auxiliaryInput.adcStableCaptureActive != 0) zeroObservedActive_ = true;
        if (zeroObservedActive_ && auxiliaryInput.adcStableCaptureActive == 0)
            CompleteStep(UTM_COMPLETION_ZERO_COMPLETED, now);
        else if (runtime_.stepElapsedSec * 1000.0 >= s.timeoutMs) FailStep();
    }
    else if (s.stepType == UTM_SEQUENCE_STEP_ZERO_ENCODER)
    {
        if (auxiliaryInput.encoderResetState == 1) zeroObservedActive_ = true;
        if (zeroObservedActive_ && auxiliaryInput.encoderResetState == 2 &&
            auxiliaryInput.encoderResetCompleted)
            CompleteStep(UTM_COMPLETION_ZERO_COMPLETED, now);
        else if (auxiliaryInput.encoderResetState == 3 ||
            runtime_.stepElapsedSec * 1000.0 >= s.timeoutMs)
            FailStep();
    }
    else if (s.stepType == UTM_SEQUENCE_STEP_ZERO_POSITION)
    {
        if (motion.positionZeroValid && std::fabs(motion.testPositionMm) < 0.001)
            CompleteStep(UTM_COMPLETION_ZERO_COMPLETED, now);
        else if (runtime_.stepElapsedSec * 1000.0 >= s.timeoutMs) FailStep();
    }
    else if (s.stepType == UTM_SEQUENCE_STEP_WAIT_TIME)
    {
        if (runtime_.stepElapsedSec >= s.durationSec)
            CompleteStep(UTM_COMPLETION_TIME_COMPLETED, now);
    }
    else if (s.stepType == UTM_SEQUENCE_STEP_WAIT_INPUT)
    {
        const bool value = (auxiliaryInput.logicalDigitalInputs & (1U << s.ioBit)) != 0;
        if (value == (s.ioState != 0)) CompleteStep(UTM_COMPLETION_INPUT_MATCHED, now);
        else if (runtime_.stepElapsedSec * 1000.0 >= s.timeoutMs) FailStep();
    }
    else if (s.stepType == UTM_SEQUENCE_STEP_SET_OUTPUT)
        CompleteStep(UTM_COMPLETION_OUTPUT_COMPLETED, now);
    else if (s.stepType == UTM_SEQUENCE_STEP_PULSE_OUTPUT)
    {
        if (runtime_.stepElapsedSec >= s.durationSec)
        {
            outputs_ &= static_cast<unsigned short>(~(1U << s.ioBit));
            QueueAction(UTM_SEQUENCE_ACTION_SET_OUTPUTS, &s);
        }
    }
    else if (s.stepType == UTM_SEQUENCE_STEP_LOOP_START)
    {
        loops_[loopDepth_] = {runtime_.currentStepIndex + 1, s.loopCount, 1};
        ++loopDepth_; CompleteStep(UTM_COMPLETION_NONE, now);
    }
    else if (s.stepType == UTM_SEQUENCE_STEP_LOOP_END)
    {
        LoopFrame& frame = loops_[loopDepth_ - 1];
        if (frame.iteration < frame.count)
        {
            ++frame.iteration; runtime_.currentStepIndex = frame.startIndex;
            BeginStep(now);
        }
        else { --loopDepth_; CompleteStep(UTM_COMPLETION_NONE, now); }
    }
    else if (s.stepType == UTM_SEQUENCE_STEP_END)
    {
        outputs_ = 0; runtime_.sequenceRunning = 0;
        runtime_.sequenceState = UTM_SEQUENCE_STATE_COMPLETED;
        runtime_.sequenceComplete = 1; runtime_.currentStepState = UTM_SEQUENCE_STEP_STATE_COMPLETED;
        runtime_.lastStepCompletionReason = UTM_COMPLETION_END;
        runtime_.recordingActive = 0; runtime_.recordingLastEvent = 2;
        QueueAction(UTM_SEQUENCE_ACTION_SEQUENCE_COMPLETE);
    }
    runtime_.loopDepth = loopDepth_;
    if (loopDepth_ > 0) { runtime_.currentLoopIteration = loops_[loopDepth_-1].iteration;
        runtime_.currentLoopCount = loops_[loopDepth_-1].count; }
}

void UtmSequencer::BeginStep(unsigned long long now)
{
    const UtmSequenceStep& s = active_.steps[runtime_.currentStepIndex];
    runtime_.currentStepType = s.stepType;
    runtime_.currentStepState = UTM_SEQUENCE_STEP_STATE_STARTING;
    runtime_.stepElapsedSec = 0.0; stepStartedNs_ = now;
    motionObserved_ = false; zeroObservedActive_ = false;
    pendingStopCompletionReason_ = UTM_COMPLETION_NONE;
}

void UtmSequencer::CompleteStep(int reason, unsigned long long now)
{
    runtime_.lastStepResult = UTM_SEQUENCE_STEP_STATE_COMPLETED;
    runtime_.lastStepCompletionReason = reason;
    runtime_.currentStepState = UTM_SEQUENCE_STEP_STATE_COMPLETED;
    ++runtime_.currentStepIndex;
    if (runtime_.currentStepIndex < active_.stepCount) BeginStep(now);
}

void UtmSequencer::FailStep()
{
    runtime_.sequenceRunning = 0; runtime_.sequenceState = UTM_SEQUENCE_STATE_FAILED;
    runtime_.sequenceFailed = 1; runtime_.currentStepState = UTM_SEQUENCE_STEP_STATE_FAILED;
    runtime_.lastStepResult = UTM_SEQUENCE_STEP_STATE_FAILED;
    runtime_.recordingActive = 0; runtime_.recordingLastEvent = 3; outputs_ = 0;
    QueueAction(UTM_SEQUENCE_ACTION_SEQUENCE_ABORT);
}

void UtmSequencer::AbortFromSafety()
{
    std::lock_guard<std::mutex> lock(mutex_);
    AbortUnlocked();
}

void UtmSequencer::AbortUnlocked()
{
    runtime_.sequenceRunning = 0; runtime_.sequenceState = UTM_SEQUENCE_STATE_ABORTED;
    runtime_.sequenceAborted = 1; runtime_.currentStepState = UTM_SEQUENCE_STEP_STATE_ABORTED;
    runtime_.lastStepResult = UTM_SEQUENCE_STEP_STATE_ABORTED;
    runtime_.recordingActive = 0; runtime_.recordingLastEvent = 3; outputs_ = 0;
    QueueAction(UTM_SEQUENCE_ACTION_SEQUENCE_ABORT);
}

void UtmSequencer::QueueAction(int type, const UtmSequenceStep* step)
{
    action_ = {}; action_.type = type; action_.outputs = outputs_;
    runtime_.sequenceOwnedOutputs = outputs_;
    if (step) action_.step = *step;
    actionPending_ = true;
}

bool UtmSequencer::ConsumeAction(UtmSequenceAction& action)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!actionPending_) return false;
    action = action_; actionPending_ = false; waitingActionResult_ = true; return true;
}

void UtmSequencer::ReportActionResult(bool success)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!waitingActionResult_) return;
    const int actionType = action_.type; waitingActionResult_ = false;
    if (!success) { FailStep(); return; }
    if (actionType == UTM_SEQUENCE_ACTION_SEQUENCE_BEGIN) return;
    if (actionType == UTM_SEQUENCE_ACTION_SEQUENCE_COMPLETE ||
        actionType == UTM_SEQUENCE_ACTION_SEQUENCE_ABORT) return;
    if (actionType == UTM_SEQUENCE_ACTION_STOP_MOTION)
    {
        AbortUnlocked(); return;
    }
    runtime_.currentStepState = UTM_SEQUENCE_STEP_STATE_RUNNING;
    if (actionType == UTM_SEQUENCE_ACTION_SET_OUTPUTS &&
        action_.step.stepType == UTM_SEQUENCE_STEP_PULSE_OUTPUT &&
        runtime_.stepElapsedSec >= action_.step.durationSec)
        CompleteStep(UTM_COMPLETION_OUTPUT_COMPLETED, stepStartedNs_ +
            static_cast<unsigned long long>(runtime_.stepElapsedSec * 1.0e9));
}

void UtmSequencer::ReportStopCondition(int reason)
{
    std::lock_guard<std::mutex> lock(mutex_);
    pendingStopCompletionReason_ = reason;
    runtime_.lastStepCompletionReason = reason;
}

UtmSequenceRuntimeInfo UtmSequencer::GetRuntime() const
{ std::lock_guard<std::mutex> lock(mutex_); return runtime_; }
bool UtmSequencer::IsRunning() const
{ std::lock_guard<std::mutex> lock(mutex_); return runtime_.sequenceRunning != 0; }
UtmSequenceStep UtmSequencer::GetCurrentStep() const
{ std::lock_guard<std::mutex> lock(mutex_); return active_.steps[runtime_.currentStepIndex]; }

int UtmSequencer::MotionTypeForStep(int type)
{
    switch (type)
    {
    case UTM_SEQUENCE_STEP_MOVE_ABSOLUTE: return UTM_MOTION_ABSOLUTE;
    case UTM_SEQUENCE_STEP_MOVE_INCREMENTAL: return UTM_MOTION_INCREMENTAL;
    case UTM_SEQUENCE_STEP_MOVE_VELOCITY: return UTM_MOTION_VELOCITY;
    case UTM_SEQUENCE_STEP_MOVE_TO_FORCE: return UTM_MOTION_MOVE_TO_FORCE;
    case UTM_SEQUENCE_STEP_HOLD_FORCE: return UTM_MOTION_HOLD_FORCE;
    default: return UTM_MOTION_NONE;
    }
}
