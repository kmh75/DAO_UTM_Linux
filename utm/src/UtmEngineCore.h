#pragma once

#include "DaoUtm.Types.h"
#include "UtmCommandMailbox.h"
#include "UtmComplianceCalibrationController.h"
#include "UtmInputCollector.h"
#include "UtmJogController.h"
#include "UtmMotionController.h"
#include "UtmMotionOwnershipPolicy.h"
#include "UtmRuntimeStore.h"
#include "UtmSafetyMonitor.h"
#include "UtmStateMachine.h"
#include "UtmStopConditionMonitor.h"
#include "UtmSequencer.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

class UtmEngineCore
{
public:
    UtmEngineCore() = default;
    ~UtmEngineCore();

    bool Initialize(
        const UtmEngineConfig& config);

    bool InitializeV2(
        const UtmStartupConfig& config);

    bool Start();
    void Stop();
    void Shutdown();

    bool IsInitialized() const;
    bool IsRunning() const;

    bool SubmitCommand(
        const UtmCommandRequest& request);

    void RequestUserStop();
    bool AcknowledgeStop();

    bool GetRuntime(
        UtmRuntimeInfo& runtime) const;

    bool ConfigureJog(
        const UtmJogConfig& config);

    bool ConfigureJogV2(
        const UtmJogConfigV2& config);

    bool StartJog(
        int direction,
        double speedMmPerMin,
        int commandSource);

    bool StopJog(
        int commandSource);

    bool GetRuntimeV2(
        UtmRuntimeInfoV2& runtime) const;

    bool GetRuntimeV3(
        UtmRuntimeInfoV3& runtime) const;

    bool MoveAbsolute(
        double targetTestPositionMm,
        double speedMmPerMin,
        unsigned int acceleration,
        unsigned int deceleration,
        unsigned int timeoutMs,
        int commandSource);

    bool MoveIncremental(
        double incrementalDistanceMm,
        double speedMmPerMin,
        unsigned int acceleration,
        unsigned int deceleration,
        unsigned int timeoutMs,
        int commandSource);

    bool MoveVelocity(
        int direction,
        double speedMmPerMin,
        unsigned int acceleration,
        unsigned int deceleration,
        int commandSource);

    bool StopMotion(int commandSource);
    bool GetRuntimeV4(UtmRuntimeInfoV4& runtime) const;
    bool GetRuntimeV5(UtmRuntimeInfoV5& runtime) const;
    bool SetPositionZero();
    bool ClearPositionZero();
    bool ZeroForce();
    bool ZeroEncoder(unsigned int timeoutMs);
    bool GetAvailableAdapters(UtmAdapterInfo* adapters,
        unsigned int capacity, unsigned int& adapterCount);
    bool CalibrateForce(double referenceForceN);
    bool SetForceCalibrationScale(double scale);
    bool SetEncoderCalibrationScale(double scale);
    bool GetCalibrationRuntime(UtmCalibrationRuntimeInfo& runtime) const;
    bool ConfigureAdcFilters(const UtmAdcFilterConfig& config);
    bool CalibrateEncoder(double referenceDisplacementMm);
    bool ConfigureDisplayForceAverage(unsigned int sampleCount);
    void GetDisplayForce(double& forceN, bool& valid, unsigned int& sampleCount) const;
    void GetDisplayForceRuntime(UtmDisplayForceRuntimeInfo& runtime) const;
    bool ConfigureForceControl(const UtmForceControlConfig& config);
    bool SubmitMotion(const UtmMotionCommandV2& command);
    bool ConfigureMachineProtection(const UtmMachineProtectionConfig& config);
    bool LoadSequence(const UtmSequenceDefinition& definition);
    bool ValidateSequence();
    bool CommitSequence();
    bool StartSequence();
    bool StopSequence();
    bool GetRuntimeV6(UtmRuntimeInfoV6& runtime) const;
    bool GetCommunicationRuntime(UtmCommunicationRuntimeInfoV1& runtime) const;
    bool StartCompliancePrecheck(const UtmComplianceCalibrationConfigV1& config,
        unsigned long long& sessionId);
    bool ConfirmComplianceFullCalibration(unsigned long long sessionId);
    bool AbortComplianceCalibration(unsigned long long sessionId);
    bool GetComplianceCalibrationRuntime(UtmComplianceCalibrationRuntimeV1& runtime) const;
    bool GetCompliancePendingPoints(unsigned long long sessionId,
        UtmComplianceCalibrationPoint* points, unsigned int capacity,
        unsigned int& pointCount) const;
    bool DiscardCompliancePending(unsigned long long sessionId);

    bool RetryStartup();

private:
    bool ValidateConfig(
        const UtmEngineConfig& config) const;

    bool ValidateStartupConfig(
        const UtmStartupConfig& config) const;

    bool InitializeConfiguration(
        const UtmEngineConfig& engineConfig,
        const UtmStartupConfig& startupConfig,
        bool selectAdapterByName,
        int legacyAdapterIndex);

    bool PrepareBasicEngine(
        UtmStartupRuntimeInfo& startup);

    void SetStartupFault(
        UtmStartupRuntimeInfo& startup,
        UtmStartupFault fault);

    void CleanupBasicEngine();
    void ControlThreadMain();

    static unsigned long long GetSteadyTimestampNs();

private:
    mutable std::mutex lifecycleMutex_;

    UtmEngineConfig config_{};
    UtmStartupConfig startupConfig_{};
    bool selectAdapterByName_ = false;
    int legacyAdapterIndex_ = -1;
    std::unique_ptr<UtmInputCollector> inputCollector_;

    UtmSafetyMonitor safetyMonitor_{};
    UtmStopLatch stopLatch_{};
    UtmStateMachine stateMachine_{};
    UtmCommandMailbox commandMailbox_{};
    UtmJogController jogController_{};
    UtmMotionController motionController_{};
    UtmRuntimeStore runtimeStore_{};
    UtmForceControlConfig forceConfig_{};
    UtmStopConditionMonitor stopConditionMonitor_{};
    UtmSequencer sequencer_{};
    UtmComplianceCalibrationController complianceCalibration_{};

    std::thread controlThread_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> controlLoopRunning_{false};
    std::atomic<bool> controlStopRequested_{true};
    std::atomic<bool> userStopRequested_{false};
    std::atomic<bool> startupRetryRequested_{false};
    std::atomic<bool> communicationMotionInhibited_{false};

    std::atomic<unsigned long long> nextCommandId_{1};
    std::atomic<unsigned long long> commandEpoch_{1};
    std::atomic<unsigned int> displayAverageSamples_{20};
    std::atomic<double> displayForceN_{0.0};
    std::atomic<bool> displayForceValid_{false};
    std::atomic<int> displayForceState_{UTM_DISPLAY_FORCE_INVALID};
    std::atomic<unsigned int> displayForceValidCount_{0};
    std::atomic<unsigned long long> displayForceGeneration_{0};
    std::atomic<unsigned long long> displayForceResetCount_{0};
    std::atomic<int> displayForceResetReason_{UTM_DISPLAY_FORCE_RESET_NONE};
    std::atomic<unsigned long long> displayForceResetRequest_{0};
    std::atomic<int> displayCaptureResetType_{0};

    void RequestDisplayForceReset(int reason);
};
