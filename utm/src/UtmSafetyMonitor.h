#pragma once

#include "DaoUtm.Types.h"

struct UtmStopEvaluation
{
    bool requested = false;
    unsigned long long reasonMask = 0;
    UtmStopReason primaryReason = UTM_STOP_NONE;
    UtmStopAction action = UTM_STOP_ACTION_NONE;
};

class UtmSafetyMonitor
{
public:
    bool ConfigureMachineProtection(const UtmMachineProtectionConfig& config);
    UtmStopEvaluation Evaluate(
        const UtmInputSnapshot& snapshot,
        const UtmSafetyContext& context,
        bool userStopRequested,
        bool servoCanPrepareForMotion,
        bool communicationRecovering) const;
    const UtmMachineProtectionConfig& GetMachineProtection() const;
private:
    UtmMachineProtectionConfig protection_{};
};

class UtmStopLatch
{
public:
    bool Update(
        const UtmStopEvaluation& evaluation,
        unsigned long long cycle,
        unsigned long long timestampNs);

    bool TryAcknowledge(
        const UtmInputSnapshot& snapshot,
        const UtmStopEvaluation& evaluation);

    const UtmStopRequest& Get() const;

private:
    UtmStopRequest request_{};
};
