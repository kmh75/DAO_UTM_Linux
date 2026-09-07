#pragma once

#include "DaoUtm.Types.h"
#include "UtmMotionTypes.h"

#include <mutex>

enum UtmSequenceActionType
{
    UTM_SEQUENCE_ACTION_NONE = 0,
    UTM_SEQUENCE_ACTION_SUBMIT_MOTION,
    UTM_SEQUENCE_ACTION_STOP_MOTION,
    UTM_SEQUENCE_ACTION_ZERO_FORCE,
    UTM_SEQUENCE_ACTION_ZERO_POSITION,
    UTM_SEQUENCE_ACTION_ZERO_ENCODER,
    UTM_SEQUENCE_ACTION_SET_OUTPUTS,
    UTM_SEQUENCE_ACTION_SEQUENCE_BEGIN,
    UTM_SEQUENCE_ACTION_SEQUENCE_COMPLETE,
    UTM_SEQUENCE_ACTION_SEQUENCE_ABORT
};

struct UtmSequenceAction
{
    int type = UTM_SEQUENCE_ACTION_NONE;
    UtmSequenceStep step{};
    unsigned short outputs = 0;
};

class UtmSequencer
{
public:
    bool Load(const UtmSequenceDefinition& definition);
    bool Validate(bool encoderPresent, double machineOverloadN);
    bool Commit();
    bool RequestStart();
    bool RequestStop();
    bool RequestGoStart();
    void Update(const UtmInputSnapshot& input,
        const UtmServoCommandSnapshot& auxiliaryInput,
        const UtmGeneralMotionRuntimeInfo& motion,
        unsigned long long timestampNs);
    bool ConsumeAction(UtmSequenceAction& action);
    void ReportActionResult(bool success);
    void ReportStopCondition(int completionReason);
    void AbortFromSafety();
    UtmSequenceRuntimeInfo GetRuntime() const;
    bool IsRunning() const;
    UtmSequenceStep GetCurrentStep() const;

private:
    bool ValidateStep(const UtmSequenceStep& step,
        unsigned int index, bool encoderPresent, double overloadN);
    void BeginStep(unsigned long long timestampNs);
    void CompleteStep(int reason, unsigned long long timestampNs);
    void FailStep();
    void AbortUnlocked();
    void QueueAction(int type, const UtmSequenceStep* step = nullptr);
    static int MotionTypeForStep(int stepType);

private:
    mutable std::mutex mutex_;
    UtmSequenceDefinition pending_{};
    UtmSequenceDefinition active_{};
    UtmSequenceRuntimeInfo runtime_{};
    bool startRequested_ = false;
    bool stopRequested_ = false;
    bool actionPending_ = false;
    bool waitingActionResult_ = false;
    bool motionObserved_ = false;
    bool zeroObservedActive_ = false;
    int pendingStopCompletionReason_ = UTM_COMPLETION_NONE;
    unsigned long long sequenceStartedNs_ = 0;
    unsigned long long stepStartedNs_ = 0;
    unsigned short outputs_ = 0;
    struct LoopFrame { unsigned int startIndex; unsigned int count; unsigned int iteration; };
    LoopFrame loops_[UTM_SEQUENCE_MAX_LOOP_DEPTH]{};
    unsigned int loopDepth_ = 0;
    UtmSequenceAction action_{};
};
