#pragma once

#include "DaoUtm.Types.h"

class UtmStopConditionMonitor
{
public:
    void Start(const UtmStopConditionConfig& config,
        double startMachinePositionMm, unsigned long long timestampNs);
    void Reset();
    int Update(const UtmInputSnapshot& snapshot,
        double machinePositionMm, unsigned long long timestampNs);
    const UtmStopConditionRuntime& GetRuntime() const;

private:
    UtmStopConditionConfig config_{};
    UtmStopConditionRuntime runtime_{};
    double startMachinePositionMm_ = 0.0;
    unsigned long long startedNs_ = 0;
    unsigned long long breakCandidateSinceNs_ = 0;
};
