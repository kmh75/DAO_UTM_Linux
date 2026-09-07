#pragma once

#include "UtmMotionTypes.h"

class UtmMotionOutputArbiter
{
public:
    explicit UtmMotionOutputArbiter(
        int logicalServoIndex);

    void Reset();

    void Update(
        const UtmMotionRequest& request,
        bool forceStop,
        const UtmServoCommandSnapshot& servoCommand);

    bool IsMotionActive() const;
    bool IsStopComplete() const;
    const UtmMotionOutputRuntime& GetRuntime() const;

private:
    bool IssueVelocity(
        const UtmMotionRequest& request,
        const UtmServoCommandSnapshot& servoCommand);
    bool IssuePosition(
        const UtmMotionRequest& request,
        const UtmServoCommandSnapshot& servoCommand);
    void IssueStop(
        UtmMotionOutputState stoppingState,
        const char* reason);
    static bool IsServoStopComplete(
        const UtmServoCommandSnapshot& servoCommand);

private:
    int logicalServoIndex_ = 0;
    UtmMotionOutputRuntime runtime_{};
    int lastAttemptedVelocity_ = 0;
    int lastAttemptedPosition_ = 0;
    bool forceStopActive_ = false;
    bool waitOneCycleAfterStop_ = false;
};
