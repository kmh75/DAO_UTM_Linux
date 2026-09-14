#pragma once

#include "DaoUtm.Types.h"

#include <array>
#include <mutex>

struct UtmComplianceCalibrationInput
{
    unsigned long long timestampNs = 0;
    double forceN = 0.0;
    double machinePositionMm = 0.0;
    double testPositionMm = 0.0;
    int forceValid = 0;
    int machineReady = 0;
    int servoReady = 0;
    int communicationValid = 0;
    int communicationRecovering = 0;
    int emergency = 0;
    int externalStop = 0;
    int upperLimit = 0;
    int lowerLimit = 0;
    int servoFault = 0;
    int overload = 0;
    int stopLatched = 0;
    int motionOutputStopped = 1;
    int forceZeroCaptureActive = 0;
    int returnMotionActive = 0;
    int returnMotionComplete = 0;
    int returnMotionFailed = 0;
};

enum class UtmComplianceCalibrationActionType
{
    None, ZeroForce, ReturnAbsolute
};

struct UtmComplianceCalibrationAction
{
    UtmComplianceCalibrationActionType type = UtmComplianceCalibrationActionType::None;
    double targetTestPositionMm = 0.0;
    double speedMmPerMin = 0.0;
    unsigned int timeoutMs = 0;
};

struct UtmComplianceCalibrationOutput
{
    int velocityRequested = 0;
    int direction = UTM_DIRECTION_NONE;
    double speedMmPerMin = 0.0;
    int forceStopRequested = 0;
};

class UtmComplianceCalibrationController
{
public:
    bool StartPrecheck(const UtmComplianceCalibrationConfigV1& config,
        double overloadLimitN, int overloadEnabled, int forceDirectionSign,
        const UtmComplianceCalibrationInput& input,
        unsigned long long& sessionId);
    bool ConfirmFull(unsigned long long sessionId);
    bool Abort(unsigned long long sessionId);
    bool DiscardPending(unsigned long long sessionId);
    bool GetRuntime(UtmComplianceCalibrationRuntimeV1& runtime) const;
    bool GetPendingPoints(unsigned long long sessionId,
        UtmComplianceCalibrationPoint* points, unsigned int capacity,
        unsigned int& count) const;

    void Update(const UtmComplianceCalibrationInput& input);
    UtmComplianceCalibrationOutput GetOutput() const;
    bool TakeAction(UtmComplianceCalibrationAction& action);
    void ReportActionResult(UtmComplianceCalibrationActionType type, bool accepted);
    void ReportMotionOutputFailure();
    bool IsActive() const;
    bool OwnsMotion() const;

    static bool ValidateConfig(const UtmComplianceCalibrationConfigV1& config,
        double overloadLimitN, int overloadEnabled, double& allowedMaximumN);
    static unsigned int GenerateTargets(const UtmComplianceCalibrationConfigV1& config,
        double* targets, unsigned int capacity);

private:
    bool CheckFaults(const UtmComplianceCalibrationInput& input);
    void SetFault(int fault);
    void BeginAbort();
    void BeginApproach(unsigned int index, const UtmComplianceCalibrationInput& input);
    void BeginStable(int state, const UtmComplianceCalibrationInput& input);
    bool AccumulateStable(const UtmComplianceCalibrationInput& input,
        double directionalTargetN);
    void BeginRelease(const UtmComplianceCalibrationInput& input, bool expired);
    double DirectionalForce(double rawForceN) const;
    double SelectSpeed(double targetN, double errorN) const;
    bool ActiveState() const;

    mutable std::mutex mutex_;
    UtmComplianceCalibrationConfigV1 config_{};
    UtmComplianceCalibrationRuntimeV1 runtime_{};
    UtmComplianceCalibrationOutput output_{};
    UtmComplianceCalibrationAction action_{};
    std::array<double, UTM_COMPLIANCE_MAX_POINTS> targets_{};
    std::array<UtmComplianceCalibrationPoint, UTM_COMPLIANCE_MAX_POINTS> points_{};
    unsigned int targetCount_ = 0;
    unsigned int targetIndex_ = 0;
    unsigned int pointCount_ = 0;
    int forceDirectionSign_ = 1;
    int expectedPositionSign_ = 1;
    double startMachinePositionMm_ = 0.0;
    double startTestPositionMm_ = 0.0;
    double referenceMachinePositionMm_ = 0.0;
    double approachStartMachinePositionMm_ = 0.0;
    double approachStartDirectionalForceN_ = 0.0;
    double releaseStartMachinePositionMm_ = 0.0;
    double previousForceN_ = 0.0;
    bool previousForceValid_ = false;
    bool zeroActiveObserved_ = false;
    bool fullConfirmed_ = false;
    bool releaseAfterPrecheckExpiry_ = false;
    bool returnActionIssued_ = false;
    bool returnActionAccepted_ = false;
    unsigned long long stateStartedNs_ = 0;
    unsigned long long stableStartedNs_ = 0;
    double stableForceSum_ = 0.0;
    double stablePositionSum_ = 0.0;
    unsigned int stableSamples_ = 0;
    unsigned long long nextSessionId_ = 1;
};
