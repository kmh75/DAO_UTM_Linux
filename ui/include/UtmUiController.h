#pragma once

#include "DaoUtm.Engine.h"
#include "MachineProfile.h"
#include "ForceUnit.h"

#include <QObject>
#include <QTimer>

class UtmUiController final : public QObject
{
    Q_OBJECT
public:
    explicit UtmUiController(QObject* parent = nullptr);
    ~UtmUiController() override;

    bool startOffline();
    bool startHardware(const UtmStartupConfig& startup,
        const UtmJogConfigV2& jog,
        const UtmForceControlConfig& force,
        const UtmMachineProtectionConfig& protection);
    bool isOffline() const { return offline_; }
    void setVerbose(bool enabled) { verbose_ = enabled; }
    void setVerboseRuntime(bool enabled) { verboseRuntime_ = enabled; }
    QString profileApplyState() const { return profileApplyState_; }
    bool hasActiveProfile() const { return activeProfileValid_; }
    const UtmRuntimeInfoV6& runtime() const { return runtime_; }
    QString utmVersion() const;
    QString basicVersion() const;
    QStringList refreshAdapters();
    bool connectProfile();
    void disconnectEngine();
    bool isConnected() const { return ownsEngine_; }
    bool isVerbose() const { return verbose_; }
    unsigned int offlineJogStopCount() const { return offlineJogStopCount_; }
    void resetOfflineJogStopCount() { offlineJogStopCount_=0; }
    MachineProfile& profile() { return profile_; }
    const MachineProfile& profile() const { return profile_; }
    UtmConnectionConfig hardwareConfiguration() const;
    QStringList profileNames() const;
    bool saveProfile(const QString& name);
    bool loadProfile(const QString& name);
    const UtmCalibrationRuntimeInfo& calibration() const { return calibration_; }
    double displayForceN() const { return displayForceN_; }
    bool displayForceValid() const { return displayForceValid_; }
    unsigned int displayForceAverageSamples() const { return displayForceAverageSamples_; }
    const UtmDisplayForceRuntimeInfo& displayForceRuntime() const { return displayForceRuntime_; }

    bool startJog(int direction, double speed);
    bool stopJog();
    bool moveAbsolute(double target, double speed, unsigned int acceleration,
        unsigned int deceleration, unsigned int timeoutMs);
    bool moveIncremental(double distance, double speed, unsigned int acceleration,
        unsigned int deceleration, unsigned int timeoutMs);
    bool moveVelocity(int direction, double speed, unsigned int acceleration,
        unsigned int deceleration);
    bool moveToForce(int direction, double target, double speed, double maxTravel,
        double timeoutSec, unsigned int acceleration, unsigned int deceleration);
    bool holdForce(int direction, double target, double holdSec, double tolerance,
        double maxTravel, double timeoutSec);
    bool stopMotion();
    bool setPositionZero();
    bool clearPositionZero();
    bool zeroForce();
    bool zeroEncoder(unsigned int timeoutMs = 5000);
    bool calibrateForce(double referenceValue, ForceUnit referenceUnit);
    double lastCalibrationReferenceN() const { return lastCalibrationReferenceN_; }
    bool configureAdcFilters(const UtmAdcFilterConfig& config);
    bool applyAdcFilterConfiguration(const UtmAdcFilterConfig& config, unsigned int displayAverageSamples);
    bool calibrateEncoder(double referenceDisplacementMm);
    bool configureDisplayForceAverage(unsigned int sampleCount);
    bool requestStop();
    bool acknowledgeStop();
    bool retryStartup();
    bool configureForce(const UtmForceControlConfig& config);
    bool configureProtection(const UtmMachineProtectionConfig& config);
    bool loadSequence(const UtmSequenceDefinition& definition);
    bool validateSequence();
    bool commitSequence();
    bool startSequence();
    bool stopSequence();

signals:
    void runtimeUpdated();
    void commandFailed(const QString& operation, const QString& detail);
    void connectionChanged();
    void profileStatus(const QString& message);

private slots:
    void pollRuntime();

private:
    bool result(const QString& operation, int value);
    bool ensureActiveProfile();
    bool autosaveCalibrationScale(bool force);
    bool applyStoredConfiguration();
    void updateOffline();

    QTimer timer_;
    UtmRuntimeInfoV6 runtime_{};
    bool offline_ = false;
    bool ownsEngine_ = false;
    qint64 offlineStartedMs_ = 0;
    MachineProfile profile_{};
    bool activeProfileValid_ = false;
    UtmCalibrationRuntimeInfo calibration_{};
    double displayForceN_ = 0.0;
    bool displayForceValid_ = false;
    unsigned int displayForceAverageSamples_ = 20;
    UtmDisplayForceRuntimeInfo displayForceRuntime_{};
    unsigned int offlineDisplayCollected_ = 20;
    bool pendingScaleApply_ = false;
    bool forceCalibrationSavePending_ = false;
    bool forceCalibrationCaptureObserved_ = false;
    bool encoderCalibrationSavePending_ = false;
    bool profileApplyFailureReported_ = false;
    double lastCalibrationReferenceN_ = 0.0;
    bool verbose_ = false;
    bool verboseRuntime_ = false;
    bool profileApplying_ = false;
    QString profileApplyState_ = "IDLE";
    unsigned long long runtimePollSuccessCount_ = 0;
    unsigned long long runtimePollFailureCount_ = 0;
    qint64 lastRuntimeLogMs_ = 0;
    int lastLoggedStartupPhase_ = -1;
    int lastLoggedStartupFault_ = -1;
    int lastLoggedJogActive_ = -1;
    int lastLoggedJogDirection_ = UTM_DIRECTION_NONE;
    int lastLoggedMachineState_ = -1;
    int lastLoggedMotionState_ = -1;
    unsigned long long lastLoggedServoCommandId_ = 0;
    unsigned int offlineJogStopCount_ = 0;
};
