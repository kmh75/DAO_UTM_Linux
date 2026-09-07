#pragma once

#include "DaoUtm.Types.h"

enum class UtmMotionOutputState
{
    STOPPED = 0,
    VELOCITY_ACTIVE,
    POSITION_ACTIVE,
    STOPPING_FOR_REVERSAL,
    STOPPING
};

struct UtmMotionRequest
{
    int type = UTM_MOTION_NONE;
    int direction = UTM_DIRECTION_NONE;

    // Position Motion에서는 +/- Test Position을 허용합니다.
    // MachinePosition = Servo Driver absolute position
    // TestPosition = MachinePosition - TestZeroOffset
    // Absolute/Incremental Move는 향후 Machine absolute target으로
    // 변환한 뒤 Servo Absolute Position Command로 출력합니다.
    double targetPosition = 0.0;

    // Speed는 항상 양의 magnitude이며 direction이 부호를 결정합니다.
    double speed = 0.0;
    unsigned int acceleration = 0;
    unsigned int deceleration = 0;
    int source = UTM_COMMAND_SOURCE_INTERNAL;

    // 현재 JOG Velocity 출력에서만 사용합니다.
    int servoTargetVelocity = 0;
    int servoTargetPosition = 0;
    unsigned int servoProfileVelocity = 0;
    unsigned int timeoutMs = 0;
    double targetForceN = 0.0;
    double holdTimeSec = 0.0;
    double maxTravelMm = 0.0;
    double toleranceN = 0.0;
};

struct UtmServoCommandSnapshot
{
    int runtimeRead = 0;
    unsigned long long commandId = 0;
    int commandType = 0;
    int commandState = 0;
    int commandStep = 0;
    int commandResult = 0;
    int outputTargetVelocity = 0;
    int outputTargetPosition = 0;
    int adcStableCaptureActive = 0;
    int adcStableCaptureType = 0;
    unsigned int adcStableCaptureCollectedCount = 0;
    int encoderResetState = 0;
    int encoderResetCompleted = 0;
    unsigned short logicalDigitalInputs = 0;
    unsigned short ioOutputCommand = 0;
};

struct UtmMotionOutputRuntime
{
    UtmMotionOutputState state =
        UtmMotionOutputState::STOPPED;
    int activeType = UTM_MOTION_NONE;
    int activeDirection = UTM_DIRECTION_NONE;
    int servoTargetVelocity = 0;
    int servoTargetPosition = 0;
    unsigned long long commandIdBeforeIssue = 0;
    int commandObserved = 0;
};
