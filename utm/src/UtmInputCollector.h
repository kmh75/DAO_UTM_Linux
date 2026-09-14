#pragma once

#include "DaoUtm.Types.h"
#include "UtmMotionTypes.h"
#include "UtmCommunicationPolicy.h"
#include "DaoEtherCAT.Engine.h"

class UtmInputCollector
{
public:
    using CommunicationState=UtmCommunicationPolicy::State;
    static constexpr auto NORMAL=UtmCommunicationPolicy::NORMAL;static constexpr auto TRANSIENT=UtmCommunicationPolicy::TRANSIENT;static constexpr auto DEGRADED=UtmCommunicationPolicy::DEGRADED;static constexpr auto RECOVERING=UtmCommunicationPolicy::RECOVERING;static constexpr auto FAULT=UtmCommunicationPolicy::FAULT;
    explicit UtmInputCollector(
        const UtmEngineConfig& config);

    bool Capture(
        unsigned long long cycle,
        unsigned long long timestampNs,
        UtmInputSnapshot& snapshot,
        UtmServoCommandSnapshot& servoCommand);
    CommunicationState GetCommunicationState() const { return communicationState_; }
    const DaoCommunicationRecoveryRuntimeV1& GetBasicRecoveryRuntime() const { return basicRecovery_; }

private:
    struct SourceState
    {
        unsigned long long lastObservedUpdateCount = 0;
        unsigned long long lastFreshTimestampNs = 0;
        bool observed = false;
        unsigned int consecutiveInvalid = 0;
    };

    void UpdateFreshness(
        SourceState& state,
        bool runtimeRead,
        bool validInput,
        unsigned long long updateCount,
        unsigned long long timestampNs,
        UtmSourceFreshness& freshness);

    bool ReadLogicalInput(
        unsigned short rawInputs,
        unsigned char bit) const;

private:
    UtmEngineConfig config_{};

    SourceState servoState_{};
    SourceState adcState_{};
    SourceState ioState_{};
    SourceState encoderState_{};
    UtmCommunicationPolicy communicationPolicy_{};
    CommunicationState communicationState_=NORMAL;
    DaoCommunicationRecoveryRuntimeV1 basicRecovery_{};
};
