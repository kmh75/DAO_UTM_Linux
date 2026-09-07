#pragma once

#include "DaoUtm.Types.h"
#include "UtmCoordinateController.h"
#include "UtmMotionTypes.h"

#include <mutex>

struct UtmGeneralMotionCommand
{
    int type = UTM_MOTION_NONE;
    int direction = UTM_DIRECTION_NONE;
    double targetPositionMm = 0.0;
    double incrementalDistanceMm = 0.0;
    double speedMmPerMin = 0.0;
    unsigned int acceleration = 0;
    unsigned int deceleration = 0;
    unsigned int timeoutMs = 0;
    double targetForceN = 0.0;
    double holdTimeSec = 0.0;
    double maxTravelMm = 0.0;
    double toleranceN = 0.0;
    int source = UTM_COMMAND_SOURCE_UI;
};

class UtmMotionController
{
public:
    bool Configure(const UtmJogConfigV2& config);
    bool Submit(const UtmGeneralMotionCommand& command);
    bool RequestStop(int source);
    bool SetPositionZero(int currentServoPosition);
    bool ClearPositionZero();

    bool ConsumePending(UtmGeneralMotionCommand& command);
    bool ConsumeStopRequest(int& source);

    bool Prepare(
        const UtmGeneralMotionCommand& command,
        int currentServoPosition,
        UtmMotionRequest& request);
    bool SetVelocityOutput(
        double speedMmPerMin,
        int direction,
        unsigned int acceleration,
        unsigned int deceleration,
        UtmMotionRequest& request) const;

    void UpdatePosition(int servoPosition);
    void MarkCommandSent();
    void MarkMoving();
    void MarkStopping();
    void MarkCompleted();
    void MarkAborted(int reason);
    void MarkFailed(int reason);
    void RecordRejected(const UtmGeneralMotionCommand& command, int reason);

    bool IsActive() const;
    bool HasPendingOrActive() const;
    UtmGeneralMotionRuntimeInfo GetRuntime() const;
    UtmJogConfigV2 GetConfig() const;

private:
    static bool IsValidSource(int source);

private:
    mutable std::mutex mutex_;
    UtmCoordinateController coordinate_{};
    UtmJogConfigV2 config_{};
    bool configured_ = false;
    bool pending_ = false;
    UtmGeneralMotionCommand pendingCommand_{};
    bool stopRequested_ = false;
    int stopSource_ = UTM_COMMAND_SOURCE_INTERNAL;
    UtmGeneralMotionRuntimeInfo runtime_{};
};
