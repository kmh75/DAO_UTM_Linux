#pragma once

#include "DaoUtm.Types.h"

class UtmStateMachine
{
public:
    void Reset();

    void Update(
        const UtmInputSnapshot& snapshot,
        const UtmStopRequest& stop,
        const UtmCommandRequest* command,
        bool stopAcknowledged);

    UtmMachineState GetState() const;

    bool TryEnterAutomaticJogMode(
        const UtmInputSnapshot& snapshot,
        bool jogRequested,
        bool motionStopped);

    bool TryReturnReadyFromJog(
        bool allJogRequestsReleased,
        bool motionStopCompleted);

    void CompleteStartup();
    void SetStartupFault();
    void BeginShutdown();
    void CompleteShutdown();

    bool TryStartGeneralMotion();
    void CompleteGeneralMotion();
    void BeginGeneralMotionStop();
    void AbortGeneralMotionToReady();
    bool BeginSequence();
    void CompleteSequence();
    void FailSequence();

private:
    UtmMachineState state_ =
        UTM_MACHINE_INITIALIZING;
    bool sequenceOwned_ = false;
};
