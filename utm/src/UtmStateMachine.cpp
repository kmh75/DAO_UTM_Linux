#include "UtmStateMachine.h"

void UtmStateMachine::Reset()
{
    state_ = UTM_MACHINE_INITIALIZING;
    sequenceOwned_ = false;
}

void UtmStateMachine::Update(
    const UtmInputSnapshot& snapshot,
    const UtmStopRequest& stop,
    const UtmCommandRequest* command,
    bool stopAcknowledged)
{
    if (stop.latched != 0)
    {
        if (stop.primaryReason ==
            UTM_STOP_EMERGENCY)
        {
            state_ = UTM_MACHINE_EMERGENCY;
        }
        else if (stop.primaryReason==UTM_STOP_SERVO_FAULT||
            (stop.primaryReason==UTM_STOP_COMMUNICATION_FAULT&&snapshot.communicationValid==0))
        {
            state_ = UTM_MACHINE_FAULT;
        }
        else
        {
            // 실제 Motion이 없는 Skeleton에서는 정지 출력 대기 없이
            // STOPPED 상태로 완료합니다.
            state_ = UTM_MACHINE_STOPPED;
        }

        return;
    }

    if (stopAcknowledged &&
        (state_ == UTM_MACHINE_EMERGENCY ||
            state_ == UTM_MACHINE_FAULT ||
            state_ == UTM_MACHINE_STOPPED))
    {
        state_ = snapshot.communicationValid != 0
            ? UTM_MACHINE_READY
            : UTM_MACHINE_STOPPED;
    }

    if (state_ == UTM_MACHINE_INITIALIZING)
    {
        state_ = snapshot.communicationValid != 0
            ? UTM_MACHINE_READY
            : UTM_MACHINE_INITIALIZING;
    }

    if (command == nullptr)
    {
        return;
    }

    switch (command->type)
    {
    case UTM_COMMAND_ENTER_READY:
        if (state_ == UTM_MACHINE_STOPPED &&
            snapshot.communicationValid != 0)
        {
            state_ = UTM_MACHINE_READY;
        }
        break;

    case UTM_COMMAND_ENTER_MANUAL:
        if (state_ == UTM_MACHINE_READY)
        {
            state_ = UTM_MACHINE_MANUAL;
        }
        break;

    case UTM_COMMAND_EXIT_MANUAL:
        if (state_ == UTM_MACHINE_MANUAL)
        {
            state_ = UTM_MACHINE_READY;
        }
        break;

    case UTM_COMMAND_START:
        if (state_ == UTM_MACHINE_READY &&
            snapshot.communicationValid != 0 &&
            snapshot.servoReady != 0)
        {
            state_ = UTM_MACHINE_RUNNING;
        }
        break;

    default:
        break;
    }
}

UtmMachineState UtmStateMachine::GetState() const
{
    return state_;
}

bool UtmStateMachine::TryEnterAutomaticJogMode(
    const UtmInputSnapshot& snapshot,
    bool jogRequested,
    bool motionStopped)
{
    if (state_ != UTM_MACHINE_READY ||
        !jogRequested ||
        !motionStopped ||
        snapshot.communicationValid == 0 ||
        snapshot.emergency != 0 ||
        snapshot.servoFault != 0)
    {
        return false;
    }

    state_ = UTM_MACHINE_MANUAL;
    return true;
}

bool UtmStateMachine::TryReturnReadyFromJog(
    bool allJogRequestsReleased,
    bool motionStopCompleted)
{
    if (state_ != UTM_MACHINE_MANUAL ||
        !allJogRequestsReleased ||
        !motionStopCompleted)
    {
        return false;
    }

    state_ = UTM_MACHINE_READY;
    return true;
}

void UtmStateMachine::CompleteStartup()
{
    state_ = UTM_MACHINE_READY;
}

void UtmStateMachine::SetStartupFault()
{
    state_ = UTM_MACHINE_FAULT;
}

void UtmStateMachine::BeginShutdown()
{
    state_ = UTM_MACHINE_STOPPING;
}

void UtmStateMachine::CompleteShutdown()
{
    state_ = UTM_MACHINE_STOPPED;
}

bool UtmStateMachine::TryStartGeneralMotion()
{
    if (state_ != UTM_MACHINE_READY &&
        !(sequenceOwned_ && state_ == UTM_MACHINE_RUNNING))
    {
        return false;
    }

    state_ = UTM_MACHINE_RUNNING;
    return true;
}

void UtmStateMachine::CompleteGeneralMotion()
{
    if (state_ == UTM_MACHINE_RUNNING && !sequenceOwned_)
    {
        state_ = UTM_MACHINE_READY;
    }
}

bool UtmStateMachine::BeginSequence()
{
    if (state_ != UTM_MACHINE_READY) return false;
    sequenceOwned_ = true;
    state_ = UTM_MACHINE_RUNNING;
    return true;
}

void UtmStateMachine::CompleteSequence()
{
    sequenceOwned_ = false;
    if (state_ == UTM_MACHINE_RUNNING || state_ == UTM_MACHINE_STOPPING)
        state_ = UTM_MACHINE_READY;
}

void UtmStateMachine::FailSequence()
{
    sequenceOwned_ = false;
    state_ = UTM_MACHINE_STOPPED;
}

void UtmStateMachine::BeginGeneralMotionStop()
{
    if (state_ == UTM_MACHINE_RUNNING)
    {
        state_ = UTM_MACHINE_STOPPING;
    }
}

void UtmStateMachine::AbortGeneralMotionToReady()
{
    if (state_ == UTM_MACHINE_STOPPING)
    {
        state_ = sequenceOwned_
            ? UTM_MACHINE_RUNNING
            : UTM_MACHINE_READY;
    }
}
