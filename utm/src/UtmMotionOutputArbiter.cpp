#include "UtmMotionOutputArbiter.h"

#include "DaoEtherCAT.Engine.h"

#include <cstdio>

UtmMotionOutputArbiter::UtmMotionOutputArbiter(
    int logicalServoIndex)
    : logicalServoIndex_(logicalServoIndex)
{
}

void UtmMotionOutputArbiter::Reset()
{
    runtime_ = {};
    lastAttemptedVelocity_ = 0;
    lastAttemptedPosition_ = 0;
    forceStopActive_ = false;
    waitOneCycleAfterStop_ = false;
}

void UtmMotionOutputArbiter::Update(
    const UtmMotionRequest& request,
    bool forceStop,
    const UtmServoCommandSnapshot& servoCommand)
{
    const bool wantsVelocity =
        (request.type == UTM_MOTION_JOG ||
            request.type == UTM_MOTION_VELOCITY ||
            request.type == UTM_MOTION_MOVE_TO_FORCE ||
            request.type == UTM_MOTION_HOLD_FORCE) &&
        request.servoTargetVelocity != 0;
    const bool wantsPosition =
        request.type == UTM_MOTION_ABSOLUTE ||
        request.type == UTM_MOTION_INCREMENTAL;

    if (forceStop)
    {
        if (!forceStopActive_)
        {
            IssueStop(UtmMotionOutputState::STOPPING, "force-stop");
            forceStopActive_ = true;
            return;
        }

        if (IsServoStopComplete(servoCommand))
        {
            runtime_ = {};
        }

        return;
    }

    forceStopActive_ = false;

    if (runtime_.state ==
            UtmMotionOutputState::VELOCITY_ACTIVE &&
        servoCommand.runtimeRead != 0 &&
        servoCommand.commandId !=
            runtime_.commandIdBeforeIssue &&
        servoCommand.commandType ==
            DAO_SERVO_CMD_VELOCITY)
    {
        runtime_.commandObserved = 1;
        std::fprintf(stderr,"[UTM MOTION] Servo command observed commandId=%llu commandState=%d targetVelocity=%d\n",servoCommand.commandId,servoCommand.commandState,servoCommand.outputTargetVelocity);
    }

    if (runtime_.state ==
            UtmMotionOutputState::STOPPING_FOR_REVERSAL ||
        runtime_.state == UtmMotionOutputState::STOPPING)
    {
        if (!IsServoStopComplete(servoCommand))
        {
            return;
        }

        runtime_ = {};
        lastAttemptedVelocity_ = 0;
        lastAttemptedPosition_ = 0;

        // Stop 완료를 관측한 Cycle과 Reverse 출력 Cycle을 분리합니다.
        waitOneCycleAfterStop_ = true;
        return;
    }

    if (runtime_.state ==
        UtmMotionOutputState::POSITION_ACTIVE)
    {
        if (servoCommand.runtimeRead != 0 &&
            servoCommand.commandId !=
                runtime_.commandIdBeforeIssue &&
            servoCommand.commandType ==
                DAO_SERVO_CMD_MOVE_ABSOLUTE)
        {
            runtime_.commandObserved = 1;
            std::fprintf(stderr,"[UTM MOTION] Servo command observed commandId=%llu commandState=%d targetPosition=%d\n",servoCommand.commandId,servoCommand.commandState,servoCommand.outputTargetPosition);
        }

        if (runtime_.commandObserved != 0 &&
            (servoCommand.commandState ==
                    DAO_SERVO_STATE_COMPLETED ||
                servoCommand.commandState ==
                    DAO_SERVO_STATE_ERROR ||
                servoCommand.commandState ==
                    DAO_SERVO_STATE_TIMEOUT))
        {
            runtime_.state = UtmMotionOutputState::STOPPED;
            return;
        }

        if (wantsPosition &&
            runtime_.servoTargetPosition ==
                request.servoTargetPosition)
        {
            return;
        }
    }

    if (!wantsVelocity && !wantsPosition)
    {
        if (runtime_.state ==
                UtmMotionOutputState::VELOCITY_ACTIVE ||
            runtime_.state ==
                UtmMotionOutputState::POSITION_ACTIVE)
        {
            IssueStop(UtmMotionOutputState::STOPPING, "request-cleared");
        }

        lastAttemptedVelocity_ = 0;
        return;
    }

    if (waitOneCycleAfterStop_)
    {
        waitOneCycleAfterStop_ = false;
        return;
    }

    if (wantsVelocity && runtime_.state ==
        UtmMotionOutputState::VELOCITY_ACTIVE)
    {
        const bool directionChanged =
            (runtime_.servoTargetVelocity > 0) !=
            (request.servoTargetVelocity > 0);

        if (directionChanged)
        {
            IssueStop(
                UtmMotionOutputState::STOPPING_FOR_REVERSAL,
                "direction-reversal");
            lastAttemptedVelocity_ = 0;
            return;
        }

        if (runtime_.servoTargetVelocity ==
            request.servoTargetVelocity)
        {
            return;
        }
    }

    if (wantsPosition)
    {
        if (lastAttemptedPosition_ ==
            request.servoTargetPosition)
        {
            return;
        }

        lastAttemptedPosition_ =
            request.servoTargetPosition;
        (void)IssuePosition(request, servoCommand);
        return;
    }

    if (lastAttemptedVelocity_ ==
        request.servoTargetVelocity)
    {
        return;
    }

    lastAttemptedVelocity_ =
        request.servoTargetVelocity;
    (void)IssueVelocity(request, servoCommand);
}

bool UtmMotionOutputArbiter::IsMotionActive() const
{
    return runtime_.state ==
            UtmMotionOutputState::VELOCITY_ACTIVE ||
        runtime_.state ==
            UtmMotionOutputState::POSITION_ACTIVE;
}

bool UtmMotionOutputArbiter::IsStopComplete() const
{
    return runtime_.state ==
        UtmMotionOutputState::STOPPED;
}

const UtmMotionOutputRuntime&
UtmMotionOutputArbiter::GetRuntime() const
{
    return runtime_;
}

void UtmMotionOutputArbiter::RequestRecoveryStopAlignment()
{
    // A STOP registered while EtherCAT was unavailable is not a physical-stop
    // confirmation. Re-arm one fresh STOP after communication stabilization.
    forceStopActive_=false;
}

bool UtmMotionOutputArbiter::IssueVelocity(
    const UtmMotionRequest& request,
    const UtmServoCommandSnapshot& servoCommand)
{
    const int result = DaoEngine_ServoVelocity(
        logicalServoIndex_,
        request.servoTargetVelocity,
        request.acceleration,
        request.deceleration);
    std::fprintf(stderr,"[UTM MOTION] ServoVelocity issued target=%d result=%d commandIdBefore=%llu type=%d direction=%d\n",request.servoTargetVelocity,result,servoCommand.commandId,request.type,request.direction);
    if (result == 0)
    {
        return false;
    }

    runtime_.state =
        UtmMotionOutputState::VELOCITY_ACTIVE;
    runtime_.activeType = request.type;
    runtime_.activeDirection = request.direction;
    runtime_.servoTargetVelocity =
        request.servoTargetVelocity;
    runtime_.servoTargetPosition = 0;
    runtime_.commandIdBeforeIssue = servoCommand.commandId;
    runtime_.commandObserved = 0;
    return true;
}

bool UtmMotionOutputArbiter::IssuePosition(
    const UtmMotionRequest& request,
    const UtmServoCommandSnapshot& servoCommand)
{
    if (DaoEngine_ServoMoveAbsolute(
        logicalServoIndex_,
        request.servoTargetPosition,
        request.servoProfileVelocity,
        request.acceleration,
        request.deceleration,
        request.timeoutMs) == 0)
    {
        return false;
    }

    runtime_.state =
        UtmMotionOutputState::POSITION_ACTIVE;
    runtime_.activeType = request.type;
    runtime_.activeDirection = request.direction;
    runtime_.servoTargetPosition =
        request.servoTargetPosition;
    runtime_.servoTargetVelocity = 0;
    runtime_.commandIdBeforeIssue = servoCommand.commandId;
    runtime_.commandObserved = 0;
    return true;
}

void UtmMotionOutputArbiter::IssueStop(
    UtmMotionOutputState stoppingState,
    const char* reason)
{
    const int previousVelocity=runtime_.servoTargetVelocity;
    const int result=DaoEngine_ServoStop(logicalServoIndex_);
    std::fprintf(stderr,"[UTM MOTION] ServoStop issued reason=%s result=%d previousTargetVelocity=%d\n",reason?reason:"unknown",result,previousVelocity);
    runtime_.state = stoppingState;
    runtime_.activeType = UTM_MOTION_NONE;
    runtime_.activeDirection = UTM_DIRECTION_NONE;
    runtime_.servoTargetVelocity = 0;
    runtime_.servoTargetPosition = 0;
}

bool UtmMotionOutputArbiter::IsServoStopComplete(
    const UtmServoCommandSnapshot& servoCommand)
{
    return servoCommand.runtimeRead != 0 &&
        servoCommand.commandType == DAO_SERVO_CMD_STOP &&
        servoCommand.commandState == DAO_SERVO_STATE_STOPPED;
}
