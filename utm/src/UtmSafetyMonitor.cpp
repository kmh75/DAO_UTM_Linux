#include "UtmSafetyMonitor.h"

#include <cmath>

namespace
{
    unsigned long long ReasonBit(
        UtmStopReason reason)
    {
        return 1ULL <<
            static_cast<unsigned int>(reason);
    }

    void AddReason(
        UtmStopEvaluation& evaluation,
        UtmStopReason reason,
        UtmStopAction action)
    {
        evaluation.requested = true;
        evaluation.reasonMask |=
            ReasonBit(reason);

        if (evaluation.primaryReason ==
            UTM_STOP_NONE)
        {
            evaluation.primaryReason = reason;
            evaluation.action = action;
        }
    }
}

bool UtmSafetyMonitor::ConfigureMachineProtection(
    const UtmMachineProtectionConfig& config)
{
    if ((config.overloadEnabled != 0 && config.overloadEnabled != 1) ||
        !std::isfinite(config.maxAllowedForceN) || config.maxAllowedForceN <= 0.0)
    {
        return false;
    }
    protection_ = config;
    return true;
}

const UtmMachineProtectionConfig&
UtmSafetyMonitor::GetMachineProtection() const
{
    return protection_;
}

UtmStopEvaluation UtmSafetyMonitor::Evaluate(
    const UtmInputSnapshot& snapshot,
    const UtmSafetyContext& context,
    bool userStopRequested,
    bool servoCanPrepareForMotion,
    bool communicationRecovering) const
{
    UtmStopEvaluation evaluation{};

    if(communicationRecovering &&
        (context.motionActive != 0 || snapshot.communicationValid == 0))
        AddReason(evaluation,UTM_STOP_COMMUNICATION_FAULT,UTM_STOP_ACTION_DISABLE_MOTION);

    // 평가 순서가 동시에 발생한 Stop의 우선순위입니다.
    if (snapshot.emergency != 0)
    {
        AddReason(
            evaluation,
            UTM_STOP_EMERGENCY,
            UTM_STOP_ACTION_DISABLE_MOTION);
    }

    if (snapshot.servoFault != 0)
    {
        AddReason(
            evaluation,
            UTM_STOP_SERVO_FAULT,
            UTM_STOP_ACTION_DISABLE_MOTION);
    }

    if (snapshot.externalStop != 0)
    {
        AddReason(
            evaluation,
            UTM_STOP_EXTERNAL_STOP,
            UTM_STOP_ACTION_CONTROLLED_STOP);
    }

    if (protection_.overloadEnabled != 0 && snapshot.forceValid != 0 &&
        std::fabs(snapshot.forceN) >= protection_.maxAllowedForceN)
    {
        AddReason(evaluation, UTM_STOP_OVERLOAD,
            UTM_STOP_ACTION_CONTROLLED_STOP);
    }

    // Limit 입력만으로는 Stop하지 않습니다. 실제 Motion이 Limit
    // 방향으로 진행할 때만 금지하며 반대 방향 탈출은 허용합니다.
    if (context.motionActive != 0)
    {
        if (snapshot.communicationValid == 0)
        {
            AddReason(
                evaluation,
                UTM_STOP_COMMUNICATION_FAULT,
                UTM_STOP_ACTION_DISABLE_MOTION);
        }

        if (snapshot.servoReady == 0 &&
            !servoCanPrepareForMotion)
        {
            AddReason(
                evaluation,
                UTM_STOP_SERVO_NOT_READY,
                UTM_STOP_ACTION_DISABLE_MOTION);
        }

        if (snapshot.upperLimit != 0 &&
            (context.requestedDirection ==
                UTM_DIRECTION_UP ||
                context.requestedDirection ==
                UTM_DIRECTION_UNKNOWN))
        {
            AddReason(
                evaluation,
                UTM_STOP_UPPER_LIMIT,
                UTM_STOP_ACTION_CONTROLLED_STOP);
        }

        if (snapshot.lowerLimit != 0 &&
            (context.requestedDirection ==
                UTM_DIRECTION_DOWN ||
                context.requestedDirection ==
                UTM_DIRECTION_UNKNOWN))
        {
            AddReason(
                evaluation,
                UTM_STOP_LOWER_LIMIT,
                UTM_STOP_ACTION_CONTROLLED_STOP);
        }
    }

    if (userStopRequested)
    {
        AddReason(
            evaluation,
            UTM_STOP_USER_STOP,
            UTM_STOP_ACTION_CONTROLLED_STOP);
    }

    return evaluation;
}

bool UtmStopLatch::Update(
    const UtmStopEvaluation& evaluation,
    unsigned long long cycle,
    unsigned long long timestampNs)
{
    if (!evaluation.requested)
    {
        return false;
    }

    const bool newlyLatched =
        request_.latched == 0;

    if (newlyLatched)
    {
        request_.requested = 1;
        request_.latched = 1;
        request_.primaryReason =
            evaluation.primaryReason;
        request_.action =
            evaluation.action;
        request_.firstTimestampNs =
            timestampNs;
        request_.firstCycle = cycle;
        ++request_.latchSequence;
    }

    request_.reasonMask |=
        evaluation.reasonMask;
    request_.lastTimestampNs =
        timestampNs;
    request_.lastCycle = cycle;

    // 뒤늦게 Emergency가 발생하면 기존 Stop보다 우선 표시합니다.
    if ((evaluation.reasonMask &
        ReasonBit(UTM_STOP_EMERGENCY)) != 0)
    {
        request_.primaryReason =
            UTM_STOP_EMERGENCY;
        request_.action =
            UTM_STOP_ACTION_DISABLE_MOTION;
    }

    return newlyLatched;
}

bool UtmStopLatch::TryAcknowledge(
    const UtmInputSnapshot& snapshot,
    const UtmStopEvaluation& evaluation)
{
    if (request_.latched == 0)
    {
        return true;
    }

    if (evaluation.requested ||
        snapshot.emergency != 0 ||
        snapshot.externalStop != 0 ||
        snapshot.servoFault != 0 ||
        snapshot.communicationValid == 0)
    {
        return false;
    }

    const unsigned long long sequence =
        request_.latchSequence;

    request_ = {};
    request_.latchSequence = sequence;

    return true;
}

const UtmStopRequest& UtmStopLatch::Get() const
{
    return request_;
}
