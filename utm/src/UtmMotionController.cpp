#include "UtmMotionController.h"

#include <cmath>

bool UtmMotionController::Configure(
    const UtmJogConfigV2& config)
{
    if (!coordinate_.Configure(
        config.config.servoUnitsPerMm))
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    configured_ = true;
    return true;
}

bool UtmMotionController::Submit(
    const UtmGeneralMotionCommand& command)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!IsValidSource(command.source) ||
        !configured_ || pending_ || runtime_.motionActive != 0 ||
        (command.type != UTM_MOTION_ABSOLUTE &&
            command.type != UTM_MOTION_INCREMENTAL &&
            command.type != UTM_MOTION_VELOCITY &&
            command.type != UTM_MOTION_MOVE_TO_FORCE &&
            command.type != UTM_MOTION_HOLD_FORCE) ||
        !std::isfinite(command.speedMmPerMin) ||
        command.speedMmPerMin < config_.minJogSpeedMmPerMin ||
        command.speedMmPerMin > config_.maxJogSpeedMmPerMin ||
        command.acceleration == 0 ||
        command.deceleration == 0)
    {
        return false;
    }

    if (command.type == UTM_MOTION_ABSOLUTE &&
        !std::isfinite(command.targetPositionMm))
    {
        return false;
    }

    if (command.type == UTM_MOTION_INCREMENTAL &&
        !std::isfinite(command.incrementalDistanceMm))
    {
        return false;
    }

    if ((command.type == UTM_MOTION_VELOCITY ||
            command.type == UTM_MOTION_MOVE_TO_FORCE ||
            command.type == UTM_MOTION_HOLD_FORCE) &&
        command.direction != UTM_DIRECTION_UP &&
        command.direction != UTM_DIRECTION_DOWN)
    {
        return false;
    }

    pendingCommand_ = command;
    pending_ = true;
    return true;
}

bool UtmMotionController::RequestStop(int source)
{
    if (!IsValidSource(source))
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (!pending_ && runtime_.motionActive == 0)
    {
        return false;
    }

    if (pending_)
    {
        runtime_ = {};
        runtime_.motionType = pendingCommand_.type;
        runtime_.motionSource = pendingCommand_.source;
        runtime_.motionState = UTM_MOTION_STATE_ABORTED;
        runtime_.motionAborted = 1;
        runtime_.motionFailureReason =
            UTM_MOTION_FAILURE_USER_STOP;
        pending_ = false;
        return true;
    }

    stopRequested_ = true;
    stopSource_ = source;
    return true;
}

bool UtmMotionController::SetPositionZero(int currentServoPosition)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!configured_ || pending_ || runtime_.motionActive != 0)
    {
        return false;
    }
    runtime_ = {};
    return coordinate_.SetPositionZero(currentServoPosition);
}

bool UtmMotionController::ClearPositionZero()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!configured_ || pending_ || runtime_.motionActive != 0)
    {
        return false;
    }
    runtime_ = {};
    coordinate_.ClearPositionZero();
    return true;
}

bool UtmMotionController::ConsumePending(
    UtmGeneralMotionCommand& command)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!pending_)
    {
        return false;
    }

    command = pendingCommand_;
    pending_ = false;
    return true;
}

bool UtmMotionController::ConsumeStopRequest(int& source)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!stopRequested_)
    {
        return false;
    }

    source = stopSource_;
    stopRequested_ = false;
    return true;
}

bool UtmMotionController::Prepare(
    const UtmGeneralMotionCommand& command,
    int currentServoPosition,
    UtmMotionRequest& request)
{
    std::lock_guard<std::mutex> lock(mutex_);
    request = {};

    if (!configured_)
    {
        return false;
    }

    runtime_ = {};
    runtime_.motionType = command.type;
    runtime_.motionState = UTM_MOTION_STATE_PREPARING;
    runtime_.motionSource = command.source;
    runtime_.commandSpeedMmPerMin = command.speedMmPerMin;
    runtime_.incrementalDistanceMm =
        command.incrementalDistanceMm;
    runtime_.servoActualPosition = currentServoPosition;
    runtime_.machinePositionMm =
        coordinate_.MachinePositionMm(currentServoPosition);
    runtime_.testPositionMm =
        coordinate_.TestPositionMm(currentServoPosition);
    runtime_.testZeroOffsetMm =
        coordinate_.GetTestZeroOffsetMm();
    runtime_.positionZeroValid =
        coordinate_.IsPositionZeroValid() ? 1 : 0;

    request.type = command.type;
    request.speed = command.speedMmPerMin;
    request.acceleration = command.acceleration;
    request.deceleration = command.deceleration;
    request.timeoutMs = command.timeoutMs;
    request.source = command.source;

    if (command.type == UTM_MOTION_VELOCITY ||
        command.type == UTM_MOTION_MOVE_TO_FORCE ||
        command.type == UTM_MOTION_HOLD_FORCE)
    {
        if (!coordinate_.SpeedToServoVelocity(
            command.speedMmPerMin,
            command.direction,
            config_.config.servoDirectionSign,
            request.servoTargetVelocity))
        {
            runtime_.motionFailureReason =
                UTM_MOTION_FAILURE_VELOCITY_OVERFLOW;
            return false;
        }

        request.direction = command.direction;
        runtime_.motionDirection = command.direction;
        runtime_.servoTargetVelocity =
            request.servoTargetVelocity;
    }
    else
    {
        double targetTestPosition =
            command.targetPositionMm;
        bool converted = false;

        if (command.type == UTM_MOTION_ABSOLUTE)
        {
            converted =
                coordinate_.TestTargetToServoPosition(
                    targetTestPosition,
                    request.servoTargetPosition);
        }
        else
        {
            converted =
                coordinate_.IncrementalTargetToServoPosition(
                    currentServoPosition,
                    command.incrementalDistanceMm,
                    targetTestPosition,
                    request.servoTargetPosition);
        }

        if (!converted ||
            !coordinate_.SpeedToServoMagnitude(
                command.speedMmPerMin,
                request.servoProfileVelocity))
        {
            runtime_.motionFailureReason =
                UTM_MOTION_FAILURE_POSITION_OVERFLOW;
            return false;
        }

        request.direction =
            request.servoTargetPosition > currentServoPosition
                ? UTM_DIRECTION_UP
                : request.servoTargetPosition < currentServoPosition
                    ? UTM_DIRECTION_DOWN
                    : UTM_DIRECTION_NONE;
        runtime_.motionDirection = request.direction;
        runtime_.targetPositionMm = targetTestPosition;
        runtime_.servoTargetPosition =
            request.servoTargetPosition;
    }

    runtime_.motionActive = 1;
    return true;
}

bool UtmMotionController::SetVelocityOutput(
    double speedMmPerMin,
    int direction,
    unsigned int acceleration,
    unsigned int deceleration,
    UtmMotionRequest& request) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    int servoVelocity = 0;
    if (!configured_ || !coordinate_.SpeedToServoVelocity(
            speedMmPerMin, direction,
            config_.config.servoDirectionSign, servoVelocity))
    {
        return false;
    }
    request.direction = direction;
    request.speed = speedMmPerMin;
    request.acceleration = acceleration;
    request.deceleration = deceleration;
    request.servoTargetVelocity = servoVelocity;
    return true;
}

void UtmMotionController::UpdatePosition(int servoPosition)
{
    std::lock_guard<std::mutex> lock(mutex_);
    runtime_.servoActualPosition = servoPosition;
    runtime_.machinePositionMm =
        coordinate_.MachinePositionMm(servoPosition);
    runtime_.testPositionMm =
        coordinate_.TestPositionMm(servoPosition);
    runtime_.testZeroOffsetMm =
        coordinate_.GetTestZeroOffsetMm();
    runtime_.positionZeroValid =
        coordinate_.IsPositionZeroValid() ? 1 : 0;
}

void UtmMotionController::MarkCommandSent()
{
    std::lock_guard<std::mutex> lock(mutex_);
    runtime_.motionState = UTM_MOTION_STATE_COMMAND_SENT;
}

void UtmMotionController::MarkMoving()
{
    std::lock_guard<std::mutex> lock(mutex_);
    runtime_.motionState = UTM_MOTION_STATE_MOVING;
}

void UtmMotionController::MarkStopping()
{
    std::lock_guard<std::mutex> lock(mutex_);
    runtime_.motionState = UTM_MOTION_STATE_STOPPING;
}

void UtmMotionController::MarkCompleted()
{
    std::lock_guard<std::mutex> lock(mutex_);
    runtime_.motionActive = 0;
    runtime_.motionState = UTM_MOTION_STATE_COMPLETED;
    runtime_.motionComplete = 1;
}

void UtmMotionController::MarkAborted(int reason)
{
    std::lock_guard<std::mutex> lock(mutex_);
    runtime_.motionActive = 0;
    runtime_.motionState = UTM_MOTION_STATE_ABORTED;
    runtime_.motionAborted = 1;
    runtime_.motionFailureReason = reason;
}

void UtmMotionController::MarkFailed(int reason)
{
    std::lock_guard<std::mutex> lock(mutex_);
    runtime_.motionActive = 0;
    runtime_.motionState = UTM_MOTION_STATE_FAILED;
    runtime_.motionFailureReason = reason;
}

void UtmMotionController::RecordRejected(
    const UtmGeneralMotionCommand& command, int reason)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_ || runtime_.motionActive != 0)
    {
        return;
    }
    runtime_ = {};
    runtime_.motionType = command.type;
    runtime_.motionDirection = command.direction;
    runtime_.motionSource = command.source;
    runtime_.motionState = UTM_MOTION_STATE_FAILED;
    runtime_.motionFailureReason = reason;
}

bool UtmMotionController::IsActive() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return runtime_.motionActive != 0;
}

bool UtmMotionController::HasPendingOrActive() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_ || runtime_.motionActive != 0;
}

UtmGeneralMotionRuntimeInfo
UtmMotionController::GetRuntime() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return runtime_;
}

UtmJogConfigV2 UtmMotionController::GetConfig() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

bool UtmMotionController::IsValidSource(int source)
{
    return source == UTM_COMMAND_SOURCE_UI ||
        source == UTM_COMMAND_SOURCE_REMOTE ||
        source == UTM_COMMAND_SOURCE_SEQUENCER ||
        source == UTM_COMMAND_SOURCE_INTERNAL;
}
