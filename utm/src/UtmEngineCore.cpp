#include "UtmEngineCore.h"

#include "DaoEtherCAT.Engine.h"
#include "UtmMotionOutputArbiter.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>

UtmEngineCore::~UtmEngineCore()
{
    Shutdown();
}

bool UtmEngineCore::Initialize(
    const UtmEngineConfig& config)
{
    UtmStartupConfig startup{};
    startup.logicalServoIndex = config.logicalServoIndex;
    startup.logicalAdcIndex = config.logicalAdcIndex;
    startup.logicalIoIndex = config.logicalIoIndex;
    startup.logicalEncoderIndex = config.logicalEncoderIndex;
    startup.encoderRequired = config.useOptionalEncoder;
    startup.controlPeriodNs = config.controlPeriodNs;
    startup.staleInputTimeoutMs = config.staleInputTimeoutMs;
    startup.ioMapping = config.ioMapping;

    return InitializeConfiguration(
        config,
        startup,
        false,
        config.adapterIndex);
}

bool UtmEngineCore::InitializeV2(
    const UtmStartupConfig& config)
{
    UtmEngineConfig engineConfig{};
    engineConfig.adapterIndex = 0;
    engineConfig.logicalServoIndex =
        config.logicalServoIndex;
    engineConfig.logicalAdcIndex =
        config.logicalAdcIndex;
    engineConfig.logicalIoIndex =
        config.logicalIoIndex;
    engineConfig.logicalEncoderIndex =
        config.logicalEncoderIndex;
    engineConfig.useOptionalEncoder =
        config.encoderRequired;
    engineConfig.controlPeriodNs =
        config.controlPeriodNs;
    engineConfig.staleInputTimeoutMs =
        config.staleInputTimeoutMs;
    engineConfig.ioMapping = config.ioMapping;

    return InitializeConfiguration(
        engineConfig,
        config,
        true,
        -1);
}

bool UtmEngineCore::Start()
{
    std::lock_guard<std::mutex> lock(
        lifecycleMutex_);

    if (!initialized_.load() ||
        controlLoopRunning_.load())
    {
        return false;
    }

    if (controlThread_.joinable())
    {
        controlThread_.join();
    }

    RequestDisplayForceReset(UTM_DISPLAY_FORCE_RESET_RECONNECT);
    controlStopRequested_.store(false);
    controlLoopRunning_.store(true);

    try
    {
        controlThread_ = std::thread(
            &UtmEngineCore::ControlThreadMain,
            this);
    }
    catch (...)
    {
        controlStopRequested_.store(true);
        controlLoopRunning_.store(false);
        return false;
    }

    return true;
}

void UtmEngineCore::Stop()
{
    std::lock_guard<std::mutex> lock(
        lifecycleMutex_);

    controlStopRequested_.store(true);

    if (controlThread_.joinable())
    {
        controlThread_.join();
    }

    controlLoopRunning_.store(false);
}

void UtmEngineCore::Shutdown()
{
    Stop();

    std::lock_guard<std::mutex> lock(
        lifecycleMutex_);

    if (!initialized_.load() &&
        DaoEngine_IsInitialized() == 0)
    {
        return;
    }

    inputCollector_.reset();
    commandMailbox_.Clear();
    CleanupBasicEngine();
    initialized_.store(false);

    UtmRuntimeInfoV3 finalRuntime{};
    runtimeStore_.ReadV3(finalRuntime);
    finalRuntime.runtime.runtime.initialized = 0;
    finalRuntime.runtime.runtime.controlLoopRunning = 0;
    finalRuntime.runtime.runtime.machineState =
        UTM_MACHINE_STOPPED;
    runtimeStore_.Publish(
        finalRuntime.runtime.runtime,
        finalRuntime.runtime.jog,
        finalRuntime.startup);
}

bool UtmEngineCore::IsInitialized() const
{
    return initialized_.load();
}

bool UtmEngineCore::IsRunning() const
{
    return controlLoopRunning_.load();
}

bool UtmEngineCore::SubmitCommand(
    const UtmCommandRequest& request)
{
    if (!initialized_.load() ||
        controlStopRequested_.load())
    {
        return false;
    }

    // CALIBRATION is an Engine-private owner. Its public enum value is for
    // diagnostics only; API callers cannot impersonate the controller.
    if (request.source == UTM_COMMAND_SOURCE_CALIBRATION) return false;

    if (request.type == UTM_COMMAND_STOP)
    {
        RequestUserStop();
        return true;
    }

    if (complianceCalibration_.OwnsMotion()) return false;

    UtmCommandRequest queuedRequest = request;

    if (queuedRequest.commandId == 0)
    {
        queuedRequest.commandId =
            nextCommandId_.fetch_add(1);
    }

    queuedRequest.commandEpoch =
        commandEpoch_.load();

    if (queuedRequest.requestedTimestampNs == 0)
    {
        queuedRequest.requestedTimestampNs =
            GetSteadyTimestampNs();
    }

    return commandMailbox_.Push(
        queuedRequest);
}

void UtmEngineCore::RequestUserStop()
{
    userStopRequested_.store(true);
}

bool UtmEngineCore::AcknowledgeStop()
{
    UtmCommandRequest request{};
    request.type =
        UTM_COMMAND_ACKNOWLEDGE_STOP;
    request.source =
        UTM_COMMAND_SOURCE_UI;

    return SubmitCommand(request);
}

bool UtmEngineCore::GetRuntime(
    UtmRuntimeInfo& runtime) const
{
    return runtimeStore_.Read(runtime);
}

bool UtmEngineCore::ConfigureJog(
    const UtmJogConfig& config)
{
    if (!initialized_.load())
    {
        return false;
    }

    // 단위 변환이나 방향 설정이 Motion 중 바뀌지 않도록 Control
    // Loop 시작 전에만 Jog 설정을 허용합니다.
    if (controlLoopRunning_.load())
    {
        return false;
    }

    if (!jogController_.Configure(config))
    {
        return false;
    }

    return motionController_.Configure(
        jogController_.GetConfig());
}

bool UtmEngineCore::ConfigureJogV2(
    const UtmJogConfigV2& config)
{
    if (!initialized_.load() ||
        controlLoopRunning_.load())
    {
        return false;
    }

    return jogController_.ConfigureV2(config) &&
        motionController_.Configure(config);
}

bool UtmEngineCore::StartJog(
    int direction,
    double speedMmPerMin,
    int commandSource)
{
    if (!initialized_.load() || communicationMotionInhibited_.load() ||
        !controlLoopRunning_.load() ||
        controlStopRequested_.load() || sequencer_.IsRunning() ||
        commandSource == UTM_COMMAND_SOURCE_CALIBRATION ||
        complianceCalibration_.OwnsMotion())
    {
        return false;
    }

    UtmRuntimeInfo runtime{};
    runtimeStore_.Read(runtime);

    const bool validJogState =
        runtime.machineState == UTM_MACHINE_READY ||
        runtime.machineState == UTM_MACHINE_MANUAL;

    const bool servoCanPrepare =
        runtime.input.servoCommunicationValid != 0 &&
        runtime.input.servoFault == 0 &&
        runtime.input.servoStoActive == 0 &&
        (runtime.input.servoReady != 0 ||
            runtime.input.servoOperationState == 0x0040);

    if (!validJogState ||
        runtime.stop.latched != 0 ||
        motionController_.HasPendingOrActive())
    {
        return false;
    }

    if (runtime.machineState == UTM_MACHINE_READY &&
        (runtime.input.communicationValid == 0 ||
            runtime.input.emergency != 0 ||
            !servoCanPrepare))
    {
        return false;
    }

    return jogController_.StartRequest(
        commandSource,
        direction,
        speedMmPerMin);
}

bool UtmEngineCore::StopJog(
    int commandSource)
{
    if (!initialized_.load())
    {
        return false;
    }

    return jogController_.StopRequest(
        commandSource);
}

bool UtmEngineCore::GetRuntimeV2(
    UtmRuntimeInfoV2& runtime) const
{
    return runtimeStore_.ReadV2(runtime);
}

bool UtmEngineCore::GetRuntimeV3(
    UtmRuntimeInfoV3& runtime) const
{
    return runtimeStore_.ReadV3(runtime);
}

bool UtmEngineCore::MoveAbsolute(
    double targetTestPositionMm,
    double speedMmPerMin,
    unsigned int acceleration,
    unsigned int deceleration,
    unsigned int timeoutMs,
    int commandSource)
{
    UtmMotionCommandV2 command{};
    command.motionType = UTM_MOTION_ABSOLUTE;
    command.targetPositionMm = targetTestPositionMm;
    command.speedMmPerMin = speedMmPerMin;
    command.acceleration = acceleration;
    command.deceleration = deceleration;
    command.timeoutMs = timeoutMs;
    command.source = commandSource;
    return SubmitMotion(command);
}

bool UtmEngineCore::MoveIncremental(
    double incrementalDistanceMm,
    double speedMmPerMin,
    unsigned int acceleration,
    unsigned int deceleration,
    unsigned int timeoutMs,
    int commandSource)
{
    UtmMotionCommandV2 command{};
    command.motionType = UTM_MOTION_INCREMENTAL;
    command.incrementalDistanceMm = incrementalDistanceMm;
    command.speedMmPerMin = speedMmPerMin;
    command.acceleration = acceleration;
    command.deceleration = deceleration;
    command.timeoutMs = timeoutMs;
    command.source = commandSource;
    return SubmitMotion(command);
}

bool UtmEngineCore::MoveVelocity(
    int direction,
    double speedMmPerMin,
    unsigned int acceleration,
    unsigned int deceleration,
    int commandSource)
{
    UtmMotionCommandV2 command{};
    command.motionType = UTM_MOTION_VELOCITY;
    command.direction = direction;
    command.speedMmPerMin = speedMmPerMin;
    command.acceleration = acceleration;
    command.deceleration = deceleration;
    command.source = commandSource;
    return SubmitMotion(command);
}

bool UtmEngineCore::StopMotion(int commandSource)
{
    if (!initialized_.load() ||
        !controlLoopRunning_.load())
    {
        return false;
    }

    UtmComplianceCalibrationRuntimeV1 calibration{};
    complianceCalibration_.GetRuntime(calibration);
    if (calibration.active != 0)
        return complianceCalibration_.Abort(calibration.sessionId);
    return motionController_.RequestStop(commandSource);
}

bool UtmEngineCore::GetRuntimeV4(
    UtmRuntimeInfoV4& runtime) const
{
    return runtimeStore_.ReadV4(runtime);
}

bool UtmEngineCore::GetRuntimeV5(UtmRuntimeInfoV5& runtime) const
{
    return runtimeStore_.ReadV5(runtime);
}

bool UtmEngineCore::SetPositionZero()
{
    UtmRuntimeInfoV4 runtime{};
    runtimeStore_.ReadV4(runtime);
    const UtmRuntimeInfo& base = runtime.runtime.runtime.runtime;
    if (!initialized_.load() || !controlLoopRunning_.load() || sequencer_.IsRunning() ||
        base.machineState != UTM_MACHINE_READY ||
        base.stop.latched != 0 || base.input.servoPositionValid == 0 ||
        runtime.runtime.runtime.jog.activeJogSourceMask != 0 ||
        motionController_.HasPendingOrActive())
    {
        return false;
    }
    return motionController_.SetPositionZero(
        base.input.servoActualPosition);
}

bool UtmEngineCore::ClearPositionZero()
{
    UtmRuntimeInfoV4 runtime{};
    runtimeStore_.ReadV4(runtime);
    const UtmRuntimeInfo& base = runtime.runtime.runtime.runtime;
    if (!initialized_.load() || !controlLoopRunning_.load() || sequencer_.IsRunning() ||
        base.machineState != UTM_MACHINE_READY ||
        base.stop.latched != 0 ||
        runtime.runtime.runtime.jog.activeJogSourceMask != 0 ||
        motionController_.HasPendingOrActive())
    {
        return false;
    }
    return motionController_.ClearPositionZero();
}

bool UtmEngineCore::ZeroForce()
{
    UtmRuntimeInfo runtime{};
    runtimeStore_.Read(runtime);
    if (!initialized_.load() || !controlLoopRunning_.load() ||
        sequencer_.IsRunning() || runtime.machineState != UTM_MACHINE_READY ||
        runtime.stop.latched != 0 || motionController_.HasPendingOrActive())
        return false;
    const bool accepted=DaoEngine_SetAdcZero(config_.logicalAdcIndex)==1;
    if(accepted)displayCaptureResetType_.store(1);
    return accepted;
}

bool UtmEngineCore::ZeroEncoder(unsigned int timeoutMs)
{
    UtmRuntimeInfo runtime{};
    runtimeStore_.Read(runtime);
    if (!initialized_.load() || !controlLoopRunning_.load() || timeoutMs == 0 ||
        sequencer_.IsRunning() || config_.useOptionalEncoder == 0 ||
        runtime.machineState != UTM_MACHINE_READY || runtime.stop.latched != 0 ||
        motionController_.HasPendingOrActive())
        return false;
    return DaoEngine_ResetEncoderCounter(config_.logicalEncoderIndex,
        config_.encoderChannel, timeoutMs) == 1;
}

bool UtmEngineCore::GetAvailableAdapters(UtmAdapterInfo* adapters,
    unsigned int capacity, unsigned int& adapterCount)
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);
    const bool temporaryInitialization = DaoEngine_IsInitialized() == 0;
    if (temporaryInitialization && DaoEngine_Initialize() == 0)
        return false;
    const int count = DaoEngine_GetAdapterCount();
    adapterCount = count > 0 ? static_cast<unsigned int>(count) : 0U;
    const unsigned int copied = std::min(capacity, adapterCount);
    for (unsigned int i = 0; i < copied; ++i)
    {
        DaoAdapterInfo source{};
        if (DaoEngine_GetAdapterInfo(static_cast<int>(i), &source) == 0)
        {
            if (temporaryInitialization) DaoEngine_Shutdown();
            return false;
        }
        std::strncpy(adapters[i].name, source.name, sizeof(adapters[i].name)-1);
        std::strncpy(adapters[i].description, source.description,
            sizeof(adapters[i].description)-1);
    }
    if (temporaryInitialization) DaoEngine_Shutdown();
    return true;
}

bool UtmEngineCore::CalibrateForce(double referenceForceN)
{
    UtmRuntimeInfo runtime{}; runtimeStore_.Read(runtime);
    if (!initialized_.load() || !controlLoopRunning_.load() ||
        !std::isfinite(referenceForceN) || referenceForceN == 0.0 ||
        sequencer_.IsRunning() || runtime.machineState != UTM_MACHINE_READY ||
        runtime.stop.latched != 0 || motionController_.HasPendingOrActive())
        return false;
    const bool accepted=DaoEngine_SetAdcCalibration(config_.logicalAdcIndex,referenceForceN)==1;
    if(accepted)displayCaptureResetType_.store(2);
    return accepted;
}

bool UtmEngineCore::SetForceCalibrationScale(double scale)
{
    if (!initialized_.load() || !controlLoopRunning_.load() ||
        !std::isfinite(scale) || scale <= 0.0 || sequencer_.IsRunning() ||
        motionController_.HasPendingOrActive())
        return false;
    const bool applied=DaoEngine_SetAdcCalibrationScale(config_.logicalAdcIndex,scale)==1;
    if(applied)RequestDisplayForceReset(UTM_DISPLAY_FORCE_RESET_CALIBRATION_SCALE);
    return applied;
}

bool UtmEngineCore::SetEncoderCalibrationScale(double scale)
{
    if (!initialized_.load() || !controlLoopRunning_.load() ||
        !std::isfinite(scale) || scale <= 0.0 || config_.useOptionalEncoder == 0 ||
        sequencer_.IsRunning() || motionController_.HasPendingOrActive())
        return false;
    return DaoEngine_SetEncoderCalibrationScale(config_.logicalEncoderIndex,
        config_.encoderChannel, scale) == 1;
}

bool UtmEngineCore::ConfigureAdcFilters(const UtmAdcFilterConfig& value)
{
    if(!initialized_.load()||!controlLoopRunning_.load()||sequencer_.IsRunning()||
       motionController_.HasPendingOrActive()||
       (value.lowLevelFilterEnabled!=0&&value.lowLevelFilterEnabled!=1)||
       !std::isfinite(value.lowLevelFilterAlpha)||value.lowLevelFilterAlpha<=0.0||value.lowLevelFilterAlpha>1.0||
       value.powerLineFilterMode<0||value.powerLineFilterMode>5||
       (value.medianFilterEnabled!=0&&value.medianFilterEnabled!=1)||
       value.movingAverageSampleCount<1||value.movingAverageSampleCount>64)return false;
    const bool applied=DaoEngine_SetAdcLowLevelFilter(config_.logicalAdcIndex,value.lowLevelFilterEnabled,value.lowLevelFilterAlpha)==1&&
        DaoEngine_SetAdcPowerLineFilterMode(config_.logicalAdcIndex,value.powerLineFilterMode)==1&&
        DaoEngine_SetAdcMedianFilter(config_.logicalAdcIndex,value.medianFilterEnabled)==1&&
        DaoEngine_SetAdcFilterN(config_.logicalAdcIndex,value.movingAverageSampleCount)==1;
    if(applied)RequestDisplayForceReset(UTM_DISPLAY_FORCE_RESET_ADC_FILTER);
    return applied;
}

bool UtmEngineCore::CalibrateEncoder(double referenceDisplacementMm)
{
    UtmRuntimeInfo runtime{};runtimeStore_.Read(runtime);
    if(!initialized_.load()||!controlLoopRunning_.load()||!std::isfinite(referenceDisplacementMm)||referenceDisplacementMm==0.0||
       config_.useOptionalEncoder==0||sequencer_.IsRunning()||runtime.machineState!=UTM_MACHINE_READY||
       runtime.stop.latched!=0||motionController_.HasPendingOrActive())return false;
    return DaoEngine_CalibrateEncoder(config_.logicalEncoderIndex,config_.encoderChannel,referenceDisplacementMm)==1;
}

bool UtmEngineCore::ConfigureDisplayForceAverage(unsigned int sampleCount)
{
    if(sampleCount<20||sampleCount>50)return false;
    if(displayAverageSamples_.load()==sampleCount)return true;
    displayAverageSamples_.store(sampleCount);
    RequestDisplayForceReset(UTM_DISPLAY_FORCE_RESET_SAMPLE_COUNT);
    return true;
}

void UtmEngineCore::RequestDisplayForceReset(int reason)
{
    displayForceResetReason_.store(reason);
    displayForceValid_.store(false);displayForceValidCount_.store(0);
    displayForceState_.store(UTM_DISPLAY_FORCE_STABILIZING);
    displayForceResetRequest_.fetch_add(1);
}

void UtmEngineCore::GetDisplayForce(double& forceN, bool& valid,
    unsigned int& sampleCount) const
{
    forceN=displayForceN_.load();valid=displayForceValid_.load();sampleCount=displayAverageSamples_.load();
}

void UtmEngineCore::GetDisplayForceRuntime(UtmDisplayForceRuntimeInfo& result) const
{
    result={};result.displayForceN=displayForceN_.load();result.state=displayForceState_.load();
    result.validSampleCount=displayForceValidCount_.load();result.configuredSampleCount=displayAverageSamples_.load();
    result.generation=displayForceGeneration_.load();result.resetCount=displayForceResetCount_.load();
    result.lastResetReason=displayForceResetReason_.load();
}

bool UtmEngineCore::GetCalibrationRuntime(
    UtmCalibrationRuntimeInfo& result) const
{
    if (!initialized_.load()) return false;
    DaoAdcRuntimeInfoV3 adc{};
    if (DaoEngine_GetAdcRuntimeInfoV3(config_.logicalAdcIndex, &adc) == 0)
        return false;
    result = {};
    result.engineeringForceN = adc.runtime.engineeringValue;
    result.adcRaw0 = adc.runtime.latestData.adcRaw0;
    result.adcRaw1 = adc.runtime.latestData.adcRaw1;
    result.adcRaw2 = adc.runtime.latestData.adcRaw2;
    result.adcRaw3 = adc.runtime.latestData.adcRaw3;
    result.lowLevelFiltered = adc.runtime.lowLevelFiltered;
    result.powerLineFiltered = adc.runtime.powerLineFiltered;
    result.zeroedValue = adc.runtime.zeroedValue;
    result.calibratedValue = adc.runtime.calibratedValue;
    result.forceValid = adc.runtime.engineeringValueValid;
    result.forceZeroValid = adc.zeroValid;
    result.forceCalibrationValid = adc.calibrationValid;
    result.forceZeroOffset = adc.zeroOffset;
    result.forceCalibrationScale = adc.calibrationScale;
    result.lowLevelFilterEnabled=adc.lowLevelFilterEnabled;
    result.lowLevelFilterAlpha=adc.lowLevelFilterAlpha;
    result.powerLineFilterMode=adc.powerLineFilterMode;
    result.medianFilterEnabled=adc.medianFilterEnabled;
    result.movingAverageSampleCount=adc.movingAverageSampleCount;
    result.forceCaptureActive = adc.runtime.stableCaptureActive;
    result.forceCaptureType = adc.runtime.stableCaptureType;
    result.forceCaptureCollectedCount = adc.runtime.stableCaptureCollectedCount;
    result.forceCaptureSampleCount = adc.runtime.stableCaptureSampleCount;
    UtmRuntimeInfoV5 utm{}; runtimeStore_.ReadV5(utm);
    result.encoderPresent = utm.runtime.runtime.runtime.runtime.input.encoderPresent;
    result.encoderValid = utm.runtime.runtime.runtime.runtime.input.encoderValid;
    result.encoderEngineeringPosition =
        utm.runtime.runtime.runtime.runtime.input.encoderPosition;
    result.positionZeroValid = utm.runtime.motion.positionZeroValid;
    result.testZeroOffsetMm = utm.runtime.motion.testZeroOffsetMm;
    if (result.encoderPresent)
    {
        DaoEncoderRuntimeInfo encoder{};
        if (DaoEngine_GetEncoderRuntimeInfo(config_.logicalEncoderIndex, &encoder) == 1)
        {
            result.encoderCalibrationScale = config_.encoderChannel == 1
                ? encoder.calibrationScaleCh1 : encoder.calibrationScaleCh2;
            result.encoderRawCount = config_.encoderChannel == 1 ? encoder.presentCounterCh1 : encoder.presentCounterCh2;
            result.encoderSignedCount = config_.encoderChannel == 1 ? encoder.signedCountCh1 : encoder.signedCountCh2;
            result.encoderResetState = config_.encoderChannel == 1 ? encoder.resetStateCh1 : encoder.resetStateCh2;
            result.encoderResetCompletedStatus = config_.encoderChannel == 1 ? encoder.resetCompletedStatusCh1 : encoder.resetCompletedStatusCh2;
        }
    }
    return true;
}

bool UtmEngineCore::ConfigureForceControl(
    const UtmForceControlConfig& config)
{
    const bool speeds = std::isfinite(config.approachSpeedMmPerMin) &&
        std::isfinite(config.mediumSpeedMmPerMin) &&
        std::isfinite(config.fineSpeedMmPerMin) &&
        std::isfinite(config.reverseSpeedMmPerMin) &&
        config.approachSpeedMmPerMin > 0.0 &&
        config.mediumSpeedMmPerMin > 0.0 && config.fineSpeedMmPerMin > 0.0 &&
        config.reverseSpeedMmPerMin > 0.0 &&
        config.approachSpeedMmPerMin >= config.mediumSpeedMmPerMin &&
        config.mediumSpeedMmPerMin >= config.fineSpeedMmPerMin &&
        config.reverseSpeedMmPerMin <= config.fineSpeedMmPerMin;
    const bool bands = std::isfinite(config.mediumErrorN) &&
        std::isfinite(config.fineErrorN) && std::isfinite(config.toleranceN) &&
        config.mediumErrorN > config.fineErrorN &&
        config.fineErrorN > config.toleranceN && config.toleranceN > 0.0;
    if (!initialized_.load() || controlLoopRunning_.load() || !speeds || !bands ||
        config.acceleration == 0 || config.deceleration == 0 ||
        !std::isfinite(config.maxTravelMm) || config.maxTravelMm <= 0.0 ||
        !std::isfinite(config.timeoutSec) || config.timeoutSec <= 0.0 ||
        (config.forceDirectionSign != 1 && config.forceDirectionSign != -1))
    {
        return false;
    }
    forceConfig_ = config;
    return true;
}

bool UtmEngineCore::SubmitMotion(const UtmMotionCommandV2& publicCommand)
{
    if(communicationMotionInhibited_.load())return false;
    if (publicCommand.source == UTM_COMMAND_SOURCE_CALIBRATION ||
        complianceCalibration_.OwnsMotion()) return false;
    UtmGeneralMotionCommand command{};
    command.type = publicCommand.motionType;
    command.direction = publicCommand.direction;
    command.targetPositionMm = publicCommand.targetPositionMm;
    command.incrementalDistanceMm = publicCommand.incrementalDistanceMm;
    command.speedMmPerMin = publicCommand.speedMmPerMin;
    command.acceleration = publicCommand.acceleration;
    command.deceleration = publicCommand.deceleration;
    command.timeoutMs = publicCommand.timeoutMs;
    command.targetForceN = publicCommand.targetForceN;
    command.holdTimeSec = publicCommand.holdTimeSec;
    command.maxTravelMm = publicCommand.maxTravelMm;
    command.toleranceN = publicCommand.toleranceN;
    command.source = publicCommand.source;
    if (command.type == UTM_MOTION_HOLD_FORCE)
    {
        command.speedMmPerMin = forceConfig_.approachSpeedMmPerMin;
        command.acceleration = forceConfig_.acceleration;
        command.deceleration = forceConfig_.deceleration;
    }

    UtmRuntimeInfoV3 runtime{};
    runtimeStore_.ReadV3(runtime);
    const UtmRuntimeInfo& base = runtime.runtime.runtime;
    const bool forceMotion = command.type == UTM_MOTION_MOVE_TO_FORCE ||
        command.type == UTM_MOTION_HOLD_FORCE;
    if (!initialized_.load() || !controlLoopRunning_.load() ||
        (sequencer_.IsRunning() && publicCommand.source != UTM_COMMAND_SOURCE_SEQUENCER) ||
        controlStopRequested_.load() || runtime.startup.startupComplete == 0 ||
        base.machineState != UTM_MACHINE_READY || base.stop.latched != 0 ||
        runtime.runtime.jog.activeJogSourceMask != 0 ||
        (forceMotion && (base.input.communicationValid == 0 ||
            base.input.servoOn == 0 || base.input.servoFault != 0)))
    {
        return false;
    }
    if (forceMotion && base.input.forceValid == 0)
    {
        motionController_.RecordRejected(
            command, UTM_MOTION_FAILURE_FORCE_INVALID);
        return false;
    }
    if (forceMotion && (!std::isfinite(command.targetForceN) ||
        command.targetForceN <= 0.0 || !std::isfinite(command.maxTravelMm) ||
        command.maxTravelMm <= 0.0 || command.timeoutMs == 0 ||
        command.speedMmPerMin > forceConfig_.approachSpeedMmPerMin ||
        (command.type == UTM_MOTION_HOLD_FORCE &&
            (!std::isfinite(command.holdTimeSec) || command.holdTimeSec <= 0.0 ||
             !std::isfinite(command.toleranceN) || command.toleranceN <= 0.0))))
    {
        return false;
    }
    return motionController_.Submit(command);
}

bool UtmEngineCore::ConfigureMachineProtection(
    const UtmMachineProtectionConfig& config)
{
    return initialized_.load() && !controlLoopRunning_.load() &&
        safetyMonitor_.ConfigureMachineProtection(config);
}

bool UtmEngineCore::LoadSequence(const UtmSequenceDefinition& definition)
{
    return initialized_.load() && sequencer_.Load(definition);
}

bool UtmEngineCore::ValidateSequence()
{
    UtmRuntimeInfoV3 runtime{};
    runtimeStore_.ReadV3(runtime);
    return initialized_.load() && sequencer_.Validate(
        config_.useOptionalEncoder != 0 || runtime.startup.encoderFound != 0,
        safetyMonitor_.GetMachineProtection().maxAllowedForceN);
}

bool UtmEngineCore::CommitSequence()
{
    return initialized_.load() && sequencer_.Commit();
}

bool UtmEngineCore::StartSequence()
{
    UtmRuntimeInfo runtime{};
    runtimeStore_.Read(runtime);
    return initialized_.load() && controlLoopRunning_.load() && !communicationMotionInhibited_.load() &&
        runtime.machineState == UTM_MACHINE_READY && runtime.stop.latched == 0 &&
        !motionController_.HasPendingOrActive() &&
        !complianceCalibration_.OwnsMotion() && sequencer_.RequestStart();
}

bool UtmEngineCore::StopSequence()
{
    return sequencer_.RequestStop();
}

bool UtmEngineCore::GetRuntimeV6(UtmRuntimeInfoV6& runtime) const
{
    return runtimeStore_.ReadV6(runtime);
}

bool UtmEngineCore::GetCommunicationRuntime(UtmCommunicationRuntimeInfoV1& runtime) const
{ return runtimeStore_.ReadCommunication(runtime); }

bool UtmEngineCore::StartCompliancePrecheck(
    const UtmComplianceCalibrationConfigV1& config,
    unsigned long long& sessionId)
{
    if (!initialized_.load() || !controlLoopRunning_.load() || communicationMotionInhibited_.load() ||
        sequencer_.IsRunning() || motionController_.HasPendingOrActive()) return false;
    UtmRuntimeInfoV6 current{};
    runtimeStore_.ReadV6(current);
    const auto& base = current.runtime.runtime.runtime.runtime.runtime;
    const auto& motion = current.runtime.runtime.motion;
    if (current.runtime.runtime.runtime.runtime.jog.activeJogSourceMask != 0) return false;
    UtmComplianceCalibrationInput input{};
    input.timestampNs = base.publishedTimestampNs;
    input.forceN = base.input.forceN;
    input.machinePositionMm = motion.machinePositionMm;
    input.testPositionMm = motion.testPositionMm;
    input.forceValid = base.input.forceValid;
    input.machineReady = base.machineState == UTM_MACHINE_READY;
    input.servoReady = base.input.servoOn && !base.input.servoFault;
    input.communicationValid = base.input.communicationValid;
    input.emergency = base.input.emergency;
    input.externalStop = base.input.externalStop;
    input.upperLimit = base.input.upperLimit;
    input.lowerLimit = base.input.lowerLimit;
    input.servoFault = base.input.servoFault;
    input.stopLatched = base.stop.latched;
    input.motionOutputStopped = motion.motionActive == 0;
    const auto& protection = safetyMonitor_.GetMachineProtection();
    return complianceCalibration_.StartPrecheck(config,
        protection.maxAllowedForceN, protection.overloadEnabled,
        forceConfig_.forceDirectionSign, input, sessionId);
}

bool UtmEngineCore::ConfirmComplianceFullCalibration(unsigned long long sessionId)
{ return complianceCalibration_.ConfirmFull(sessionId); }

bool UtmEngineCore::AbortComplianceCalibration(unsigned long long sessionId)
{ return complianceCalibration_.Abort(sessionId); }

bool UtmEngineCore::GetComplianceCalibrationRuntime(
    UtmComplianceCalibrationRuntimeV1& runtime) const
{ return complianceCalibration_.GetRuntime(runtime); }

bool UtmEngineCore::GetCompliancePendingPoints(unsigned long long sessionId,
    UtmComplianceCalibrationPoint* points, unsigned int capacity,
    unsigned int& pointCount) const
{ return complianceCalibration_.GetPendingPoints(sessionId, points, capacity, pointCount); }

bool UtmEngineCore::DiscardCompliancePending(unsigned long long sessionId)
{ return complianceCalibration_.DiscardPending(sessionId); }

bool UtmEngineCore::RetryStartup()
{
    if (!initialized_.load() ||
        !controlLoopRunning_.load())
    {
        return false;
    }

    UtmRuntimeInfoV3 runtime{};
    runtimeStore_.ReadV3(runtime);

    if (runtime.startup.startupPhase !=
        UTM_STARTUP_FAILED)
    {
        return false;
    }

    startupRetryRequested_.store(true);
    return true;
}

bool UtmEngineCore::ValidateConfig(
    const UtmEngineConfig& config) const
{
    if (config.adapterIndex < 0 ||
        config.logicalServoIndex < 0 ||
        config.logicalAdcIndex < 0 ||
        config.logicalIoIndex < 0 ||
        config.controlPeriodNs == 0 ||
        config.staleInputTimeoutMs == 0 ||
        (config.encoderChannel != 1 &&
            config.encoderChannel != 2))
    {
        return false;
    }

    const unsigned char bits[] =
    {
        config.ioMapping.emergencyBit,
        config.ioMapping.upperLimitBit,
        config.ioMapping.lowerLimitBit,
        config.ioMapping.jogUpBit,
        config.ioMapping.jogDownBit,
        config.ioMapping.externalGoBit,
        config.ioMapping.externalStopBit
    };

    for (unsigned char bit : bits)
    {
        if (bit >= 16)
        {
            return false;
        }
    }

    return true;
}

bool UtmEngineCore::ValidateStartupConfig(
    const UtmStartupConfig& config) const
{
    return config.ethercatAdapterName[0] != '\0' &&
        config.logicalServoIndex >= 0 &&
        config.logicalAdcIndex >= 0 &&
        config.logicalIoIndex >= 0 &&
        config.logicalEncoderIndex >= 0 &&
        (config.encoderRequired == 0 ||
            config.encoderRequired == 1) &&
        (config.autoServoOn == 0 ||
            config.autoServoOn == 1) &&
        config.communicationStabilizeMs > 0 &&
        config.communicationTimeoutMs >=
            config.communicationStabilizeMs &&
        config.servoOnTimeoutMs > 0 &&
        config.readyStabilizeMs > 0 &&
        config.shutdownTimeoutMs > 0 &&
        config.controlPeriodNs > 0 &&
        config.staleInputTimeoutMs > 0;
}

bool UtmEngineCore::InitializeConfiguration(
    const UtmEngineConfig& engineConfig,
    const UtmStartupConfig& startupConfig,
    bool selectAdapterByName,
    int legacyAdapterIndex)
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);

    const bool startupValid =
        selectAdapterByName
            ? ValidateStartupConfig(startupConfig)
            : ValidateConfig(engineConfig);

    if (initialized_.load())
    {
        return false;
    }

    if (!startupValid)
    {
        UtmRuntimeInfo runtime{};
        runtime.machineState = UTM_MACHINE_FAULT;
        UtmStartupRuntimeInfo startup{};
        startup.startupPhase = UTM_STARTUP_FAILED;
        startup.startupFault =
            UTM_STARTUP_FAULT_CONFIG_INVALID;
        runtimeStore_.Publish(runtime, {}, startup);
        return false;
    }

    config_ = engineConfig;
    startupConfig_ = startupConfig;
    selectAdapterByName_ = selectAdapterByName;
    legacyAdapterIndex_ = legacyAdapterIndex;

    stateMachine_.Reset();
    commandMailbox_.Clear();
    jogController_.ClearCommandRequests();
    jogController_.InhibitPhysicalUntilReleased();
    userStopRequested_.store(false);
    startupRetryRequested_.store(false);
    commandEpoch_.store(1);
    nextCommandId_.store(1);
    initialized_.store(true);

    UtmRuntimeInfo runtime{};
    runtime.initialized = 1;
    runtime.machineState = UTM_MACHINE_INITIALIZING;

    UtmStartupRuntimeInfo startup{};
    startup.startupPhase = UTM_STARTUP_CONFIG_LOADED;
    runtimeStore_.Publish(runtime, {}, startup);
    return true;
}

bool UtmEngineCore::PrepareBasicEngine(
    UtmStartupRuntimeInfo& startup)
{
    auto publishPhase = [this, &startup](UtmStartupPhase phase)
    {
        startup.startupPhase = phase;
        UtmRuntimeInfo runtime{};
        runtimeStore_.Read(runtime);
        runtime.initialized = 1;
        runtime.machineState = UTM_MACHINE_INITIALIZING;
        runtimeStore_.Publish(runtime, {}, startup);
    };

    publishPhase(UTM_STARTUP_ADAPTER_SELECTING);
    std::fprintf(stderr,"[UTM] adapter discovery start requested=%s\n",startupConfig_.ethercatAdapterName);

    const int initializeResult=DaoEngine_Initialize();
    std::fprintf(stderr,"[UTM] DaoEngine_Initialize result=%d requested=%s\n",initializeResult,startupConfig_.ethercatAdapterName);
    if (initializeResult == 0)
    {
        SetStartupFault(
            startup,
            UTM_STARTUP_FAULT_ADAPTER_OPEN_FAILED);
        return false;
    }

    const int adapterCount =
        DaoEngine_GetAdapterCount();
    std::fprintf(stderr,"[UTM] adapter discovery count=%d\n",adapterCount);
    int selectedIndex = -1;
    DaoAdapterInfo selectedInfo{};

    for (int index = 0; index < adapterCount; ++index)
    {
        DaoAdapterInfo adapter{};

        if (DaoEngine_GetAdapterInfo(index, &adapter) == 0)
        {
            continue;
        }
        std::fprintf(stderr,"[UTM] adapter found index=%d name=%s description=%s\n",index,adapter.name,adapter.description);

        const bool selected = selectAdapterByName_
            ? std::strncmp(
                adapter.name,
                startupConfig_.ethercatAdapterName,
                sizeof(adapter.name)) == 0
            : index == legacyAdapterIndex_;

        if (selected)
        {
            selectedIndex = index;
            selectedInfo = adapter;
            break;
        }
    }

    if (selectedIndex < 0)
    {
        std::fprintf(stderr,"[UTM] adapter selection failed startupFault=%d adapterName=%s\n",UTM_STARTUP_FAULT_ADAPTER_NOT_FOUND,startupConfig_.ethercatAdapterName);
        SetStartupFault(
            startup,
            UTM_STARTUP_FAULT_ADAPTER_NOT_FOUND);
        return false;
    }

    startup.selectedAdapterIndex = selectedIndex;
    std::strncpy(
        startup.selectedAdapterName,
        selectedInfo.name,
        sizeof(startup.selectedAdapterName) - 1);

    publishPhase(UTM_STARTUP_ADAPTER_OPENING);

    const int openResult=DaoEngine_OpenAdapter(selectedIndex);
    std::fprintf(stderr,"[UTM] DaoEngine_OpenAdapter index=%d result=%d adapterName=%s\n",selectedIndex,openResult,selectedInfo.name);
    if (openResult == 0)
    {
        SetStartupFault(
            startup,
            UTM_STARTUP_FAULT_ADAPTER_OPEN_FAILED);
        return false;
    }

    publishPhase(UTM_STARTUP_SLAVE_SCANNING);
    startup.slaveCount = DaoEngine_ScanSlaves();

    if (startup.slaveCount <= 0)
    {
        SetStartupFault(
            startup,
            UTM_STARTUP_FAULT_SLAVE_SCAN_FAILED);
        return false;
    }

    publishPhase(UTM_STARTUP_DEVICE_CHECK);
    const int servoCount =
        DaoEngine_GetLogicalDeviceCount(DAO_DEVICE_SERVO);
    const int adcCount =
        DaoEngine_GetLogicalDeviceCount(DAO_DEVICE_ADC);
    const int ioCount =
        DaoEngine_GetLogicalDeviceCount(DAO_DEVICE_IO);
    const int encoderCount =
        DaoEngine_GetLogicalDeviceCount(DAO_DEVICE_ENCODER);

    startup.servoFound =
        config_.logicalServoIndex < servoCount ? 1 : 0;
    startup.adcFound =
        config_.logicalAdcIndex < adcCount ? 1 : 0;
    startup.ioFound =
        config_.logicalIoIndex < ioCount ? 1 : 0;
    startup.encoderFound =
        config_.logicalEncoderIndex < encoderCount ? 1 : 0;

    if (startup.servoFound == 0)
    {
        SetStartupFault(startup, UTM_STARTUP_FAULT_SERVO_NOT_FOUND);
        return false;
    }
    if (startup.adcFound == 0)
    {
        SetStartupFault(startup, UTM_STARTUP_FAULT_ADC_NOT_FOUND);
        return false;
    }
    if (startup.ioFound == 0)
    {
        SetStartupFault(startup, UTM_STARTUP_FAULT_IO_NOT_FOUND);
        return false;
    }
    if (startupConfig_.encoderRequired != 0 &&
        startup.encoderFound == 0)
    {
        SetStartupFault(
            startup,
            UTM_STARTUP_FAULT_ENCODER_REQUIRED_NOT_FOUND);
        return false;
    }

    config_.useOptionalEncoder = startup.encoderFound;

    publishPhase(UTM_STARTUP_PDO_PREPARING);

    if (DaoEngine_RequestAllSlavesPreOp() == 0 ||
        DaoEngine_MapProcessData() == 0 ||
        DaoEngine_RequestAllSlavesSafeOp() == 0 ||
        DaoEngine_RequestAllSlavesOperational() == 0)
    {
        SetStartupFault(
            startup,
            UTM_STARTUP_FAULT_PDO_PREPARE_FAILED);
        return false;
    }

    publishPhase(UTM_STARTUP_COMMUNICATION_STARTING);

    if (DaoEngine_StartCommunication() == 0)
    {
        SetStartupFault(
            startup,
            UTM_STARTUP_FAULT_COMMUNICATION_START_FAILED);
        return false;
    }

    inputCollector_ =
        std::make_unique<UtmInputCollector>(config_);
    startup.startupPhase = UTM_STARTUP_COMMUNICATION_WAIT;
    return true;
}

void UtmEngineCore::SetStartupFault(
    UtmStartupRuntimeInfo& startup,
    UtmStartupFault fault)
{
    startup.startupFault = fault;
    startup.startupComplete = 0;
    startup.startupPhase = UTM_STARTUP_FAILED;
    stateMachine_.SetStartupFault();
}

void UtmEngineCore::CleanupBasicEngine()
{
    if (DaoEngine_IsCommunicationRunning() == 1)
    {
        DaoEngine_StopCommunication();
    }

    if (DaoEngine_IsAdapterOpen() == 1)
    {
        DaoEngine_CloseAdapter();
    }

    if (DaoEngine_IsInitialized() == 1)
    {
        DaoEngine_Shutdown();
    }
}

void UtmEngineCore::ControlThreadMain()
{
    using Clock = std::chrono::steady_clock;

    const auto period =
        std::chrono::nanoseconds(
            config_.controlPeriodNs);

    auto nextWakeTime = Clock::now();
    unsigned long long cycle = 0;
    bool previousExternalGo = false;
    UtmMotionOutputArbiter motionOutput(
        config_.logicalServoIndex);
    UtmServoCommandSnapshot lastServoCommand{};
    UtmInputSnapshot lastSnapshot{};
    UtmMotionRequest generalMotionRequest{};
    bool generalMotionActive = false;
    bool generalMotionStopping = false;
    int generalStopReason = UTM_MOTION_FAILURE_NONE;
    int generalStopDisposition = 0; // 1 complete, 2 abort, 3 fail
    UtmForceMotionRuntimeInfo forceRuntime{};
    unsigned long long forceStartedNs = 0;
    unsigned long long forceHoldSinceNs = 0;
    std::array<double,50> displayForceSamples{};
    unsigned int displayForceCount=0,displayForceIndex=0,activeDisplaySampleCount=0;
    double displayForceSum=0.0;
    unsigned long long observedDisplayResetRequest=0;
    int observedCaptureResetType=0;bool captureResetObservedActive=false;
    unsigned long long observedCommunicationIncident=0;
    bool communicationIncidentMotionActive=false;
    unsigned long long alignedCommunicationIncident=0;
    UtmCommunicationWarningPolicy communicationWarningPolicy{};
    UtmCommunicationRuntimeInfoV1 communicationRuntime{};

    UtmRuntimeInfo runtime{};
    UtmJogRuntimeInfo jogRuntime{};
    runtime.initialized = 1;
    runtime.controlLoopRunning = 1;
    runtime.requiredServoPresent = 1;
    runtime.requiredAdcPresent = 1;
    runtime.requiredIoPresent = 1;
    runtime.optionalEncoderPresent =
        config_.useOptionalEncoder != 0 ? 1 : 0;

    UtmStartupRuntimeInfo startup{};
    bool startupPrepared =
        PrepareBasicEngine(startup);
    auto startupPhaseStarted = Clock::now();
    auto startupStableSince = Clock::now();
    bool startupStableTiming = false;

    while (!controlStopRequested_.load())
    {
        nextWakeTime += period;
        const auto cycleStart = Clock::now();

        ++cycle;
        const unsigned long long timestampNs =
            GetSteadyTimestampNs();

        if (!startupPrepared)
        {
            runtime.publishedTimestampNs = timestampNs;
            runtime.controlCycle = cycle;
            runtime.machineState = stateMachine_.GetState();
            runtime.stop = stopLatch_.Get();
            if (generalMotionActive || generalMotionStopping)
            {
                generalMotionActive = false;
                generalMotionStopping = false;
                generalMotionRequest = {};
                motionController_.MarkAborted(
                    UTM_MOTION_FAILURE_SAFETY_STOP);
            }

            runtimeStore_.Publish(
                runtime,
                jogRuntime,
                startup,
                motionController_.GetRuntime());

            if (startupRetryRequested_.exchange(false))
            {
                inputCollector_.reset();
                CleanupBasicEngine();
                stateMachine_.Reset();
                startup = {};
                startupPrepared = PrepareBasicEngine(startup);
                startupPhaseStarted = Clock::now();
                startupStableTiming = false;
            }

            std::this_thread::sleep_until(nextWakeTime);
            continue;
        }

        UtmInputSnapshot snapshot{};
        UtmServoCommandSnapshot servoCommand{};
        (void)inputCollector_->Capture(
            cycle,
            timestampNs,
            snapshot,
            servoCommand);
        lastServoCommand = servoCommand;
        lastSnapshot = snapshot;
        const int pendingCaptureReset=displayCaptureResetType_.load();
        if(pendingCaptureReset!=observedCaptureResetType){observedCaptureResetType=pendingCaptureReset;captureResetObservedActive=false;}
        if(pendingCaptureReset!=0&&servoCommand.adcStableCaptureActive!=0&&servoCommand.adcStableCaptureType==pendingCaptureReset)captureResetObservedActive=true;
        if(pendingCaptureReset!=0&&captureResetObservedActive&&servoCommand.adcStableCaptureActive==0)
        {
            RequestDisplayForceReset(pendingCaptureReset==1?UTM_DISPLAY_FORCE_RESET_FORCE_ZERO:UTM_DISPLAY_FORCE_RESET_FORCE_CALIBRATION);
            displayCaptureResetType_.store(0);observedCaptureResetType=0;captureResetObservedActive=false;
        }
        const unsigned int requestedDisplaySamples=displayAverageSamples_.load();
        const unsigned long long resetRequest=displayForceResetRequest_.load();
        if(resetRequest!=observedDisplayResetRequest||requestedDisplaySamples!=activeDisplaySampleCount)
        {
            const unsigned long long resetDelta=resetRequest-observedDisplayResetRequest;
            observedDisplayResetRequest=resetRequest;activeDisplaySampleCount=requestedDisplaySamples;
            displayForceCount=0;displayForceIndex=0;displayForceSum=0.0;displayForceValid_.store(false);
            displayForceValidCount_.store(0);displayForceState_.store(UTM_DISPLAY_FORCE_STABILIZING);
            displayForceGeneration_.fetch_add(std::max(1ULL,resetDelta));displayForceResetCount_.fetch_add(std::max(1ULL,resetDelta));
        }
        if(snapshot.forceValid!=0&&std::isfinite(snapshot.forceN))
        {
            if(displayForceCount<activeDisplaySampleCount){displayForceSamples[displayForceIndex]=snapshot.forceN;displayForceSum+=snapshot.forceN;++displayForceCount;}
            else{displayForceSum-=displayForceSamples[displayForceIndex];displayForceSamples[displayForceIndex]=snapshot.forceN;displayForceSum+=snapshot.forceN;}
            displayForceIndex=(displayForceIndex+1)%activeDisplaySampleCount;
            displayForceValidCount_.store(displayForceCount);
            if(displayForceCount==activeDisplaySampleCount)
            {
                displayForceN_.store(displayForceSum/static_cast<double>(displayForceCount));
                displayForceValid_.store(true);displayForceState_.store(UTM_DISPLAY_FORCE_VALID);
            }
            else{displayForceValid_.store(false);displayForceState_.store(UTM_DISPLAY_FORCE_STABILIZING);}
        }
        else{displayForceCount=0;displayForceIndex=0;displayForceSum=0.0;displayForceValidCount_.store(0);displayForceValid_.store(false);displayForceState_.store(UTM_DISPLAY_FORCE_INVALID);displayForceResetReason_.store(UTM_DISPLAY_FORCE_RESET_FORCE_INVALID);}
        motionController_.UpdatePosition(
            snapshot.servoActualPosition);

        if (startup.startupComplete == 0)
        {
            const auto now = Clock::now();
            const auto phaseElapsedMs =
                std::chrono::duration_cast<
                    std::chrono::milliseconds>(
                        now - startupPhaseStarted).count();

            if (startup.startupPhase ==
                UTM_STARTUP_COMMUNICATION_WAIT)
            {
                if (snapshot.communicationValid != 0)
                {
                    if (!startupStableTiming)
                    {
                        startupStableSince = now;
                        startupStableTiming = true;
                    }

                    const auto stableMs =
                        std::chrono::duration_cast<
                            std::chrono::milliseconds>(
                                now - startupStableSince).count();

                    if (stableMs >=
                        startupConfig_.communicationStabilizeMs)
                    {
                        startup.startupPhase =
                            UTM_STARTUP_SAFETY_CHECK;
                        startupPhaseStarted = now;
                        startupStableTiming = false;
                    }
                }
                else
                {
                    startupStableTiming = false;

                    if (phaseElapsedMs >=
                        startupConfig_.communicationTimeoutMs)
                    {
                        SetStartupFault(
                            startup,
                            snapshot.basicCommunicationRunning != 0
                                ? UTM_STARTUP_FAULT_COMMUNICATION_STALE
                                : UTM_STARTUP_FAULT_COMMUNICATION_TIMEOUT);
                        startupPrepared = false;
                    }
                }
            }
            else if (startup.startupPhase ==
                UTM_STARTUP_SAFETY_CHECK)
            {
                if (snapshot.communicationValid == 0)
                {
                    SetStartupFault(
                        startup,
                        UTM_STARTUP_FAULT_COMMUNICATION_STALE);
                    startupPrepared = false;
                }
                else if (snapshot.servoFault != 0)
                {
                    SetStartupFault(
                        startup,
                        UTM_STARTUP_FAULT_SERVO_FAULT);
                    startupPrepared = false;
                }
                else if (snapshot.emergency != 0)
                {
                    SetStartupFault(
                        startup,
                        UTM_STARTUP_FAULT_EMERGENCY_ACTIVE);
                    startupPrepared = false;
                }
                else if (snapshot.externalStop != 0)
                {
                    SetStartupFault(
                        startup,
                        UTM_STARTUP_FAULT_EXTERNAL_STOP_ACTIVE);
                    startupPrepared = false;
                }
                else
                {
                    startup.startupPhase =
                        UTM_STARTUP_SERVO_PREPARING;
                    std::fprintf(stderr,"[UTM STARTUP] phase=SERVO_PREPARING autoServoOn=%d servoOn/operationEnabled=%d servoState=0x%x\n",startupConfig_.autoServoOn,snapshot.servoOn,snapshot.servoOperationState);
                    startupPhaseStarted = now;
                }
            }
            else if (startup.startupPhase ==
                UTM_STARTUP_SERVO_PREPARING)
            {
                if (snapshot.servoOn != 0)
                {
                    startup.startupPhase =
                        UTM_STARTUP_READY_STABILIZING;
                    startupStableSince = now;
                    startupStableTiming = true;
                }
                else if (startupConfig_.autoServoOn != 0)
                {
                    const int servoOnResult=DaoEngine_ServoOn(config_.logicalServoIndex);
                    std::fprintf(stderr,"[UTM STARTUP] DaoEngine_ServoOn servo=%d result=%d autoServoOn=%d\n",config_.logicalServoIndex,servoOnResult,startupConfig_.autoServoOn);
                    if (servoOnResult == 0)
                    {
                        SetStartupFault(
                            startup,
                            UTM_STARTUP_FAULT_SERVO_ON_FAILED);
                        startupPrepared = false;
                    }
                    else
                    {
                        startup.startupPhase =
                            UTM_STARTUP_SERVO_ON_WAIT;
                        std::fprintf(stderr,"[UTM STARTUP] phase=SERVO_ON_WAIT servoOn/operationEnabled=%d servoState=0x%x\n",snapshot.servoOn,snapshot.servoOperationState);
                        startupPhaseStarted = now;
                    }
                }
                else
                {
                    startup.startupPhase =
                        UTM_STARTUP_SERVO_ON_WAIT;
                    std::fprintf(stderr,"[UTM STARTUP] autoServoOn=0; phase=SERVO_ON_WAIT without ServoOn request\n");
                    startupPhaseStarted = now;
                }
            }
            else if (startup.startupPhase ==
                UTM_STARTUP_SERVO_ON_WAIT)
            {
                if (snapshot.communicationValid == 0)
                {
                    SetStartupFault(
                        startup,
                        UTM_STARTUP_FAULT_COMMUNICATION_STALE);
                    startupPrepared = false;
                }
                else if (snapshot.servoFault != 0)
                {
                    SetStartupFault(
                        startup,
                        UTM_STARTUP_FAULT_SERVO_FAULT);
                    startupPrepared = false;
                }
                else if (snapshot.emergency != 0 ||
                    snapshot.externalStop != 0)
                {
                    SetStartupFault(
                        startup,
                        snapshot.emergency != 0
                            ? UTM_STARTUP_FAULT_EMERGENCY_ACTIVE
                            : UTM_STARTUP_FAULT_EXTERNAL_STOP_ACTIVE);
                    startupPrepared = false;
                }
                else if (snapshot.servoOn != 0)
                {
                    startup.startupPhase =
                        UTM_STARTUP_READY_STABILIZING;
                    std::fprintf(stderr,"[UTM STARTUP] phase=READY_STABILIZING servoOn/operationEnabled=%d servoState=0x%x\n",snapshot.servoOn,snapshot.servoOperationState);
                    startupStableSince = now;
                    startupStableTiming = true;
                }
                else if (phaseElapsedMs >=
                    startupConfig_.servoOnTimeoutMs)
                {
                    SetStartupFault(
                        startup,
                        UTM_STARTUP_FAULT_SERVO_ON_TIMEOUT);
                    startupPrepared = false;
                }
            }
            else if (startup.startupPhase ==
                UTM_STARTUP_READY_STABILIZING)
            {
                const bool readyConditions =
                    snapshot.communicationValid != 0 &&
                    snapshot.servoOn != 0 &&
                    snapshot.servoFault == 0 &&
                    snapshot.emergency == 0 &&
                    snapshot.externalStop == 0;

                if (!readyConditions)
                {
                    SetStartupFault(
                        startup,
                        snapshot.communicationValid == 0
                            ? UTM_STARTUP_FAULT_COMMUNICATION_STALE
                            : snapshot.servoFault != 0
                                ? UTM_STARTUP_FAULT_SERVO_FAULT
                                : snapshot.emergency != 0
                                    ? UTM_STARTUP_FAULT_EMERGENCY_ACTIVE
                                    : snapshot.externalStop != 0
                                        ? UTM_STARTUP_FAULT_EXTERNAL_STOP_ACTIVE
                                        : UTM_STARTUP_FAULT_SERVO_ON_TIMEOUT);
                    startupPrepared = false;
                }
                else
                {
                    const auto stableMs =
                        std::chrono::duration_cast<
                            std::chrono::milliseconds>(
                                now - startupStableSince).count();

                    if (stableMs >=
                        startupConfig_.readyStabilizeMs)
                    {
                        startup.startupPhase =
                            UTM_STARTUP_COMPLETE;
                        startup.startupFault =
                            UTM_STARTUP_FAULT_NONE;
                        startup.startupComplete = 1;
                        stateMachine_.CompleteStartup();
                        std::fprintf(stderr,"[UTM STARTUP] phase=COMPLETE machine=READY servoOn/operationEnabled=%d servoState=0x%x\n",snapshot.servoOn,snapshot.servoOperationState);
                    }
                }
            }

            runtime.publishedTimestampNs = timestampNs;
            runtime.controlCycle = cycle;
            runtime.machineState = stateMachine_.GetState();
            runtime.input = snapshot;
            runtime.requiredServoPresent = startup.servoFound;
            runtime.requiredAdcPresent = startup.adcFound;
            runtime.requiredIoPresent = startup.ioFound;
            runtime.optionalEncoderPresent = startup.encoderFound;
            runtimeStore_.Publish(runtime, jogRuntime, startup);

            std::this_thread::sleep_until(nextWakeTime);
            continue;
        }

        // READY 이후에도 READY의 필수 조건은 계속 유지되어야 합니다.
        // 통신 또는 Servo Operation Enabled가 사라지면 Motion 요청을
        // 받지 않고 명시적인 Lifecycle Fault로 전환합니다.
        if (snapshot.communicationValid == 0 ||
            (snapshot.servoOn == 0 &&
                snapshot.servoFault == 0))
        {
            if (complianceCalibration_.OwnsMotion())
            {
                UtmComplianceCalibrationInput failedCalibration{};
                failedCalibration.timestampNs = timestampNs;
                failedCalibration.forceN = snapshot.forceN;
                failedCalibration.forceValid = snapshot.forceValid;
                failedCalibration.communicationValid = snapshot.communicationValid;
                failedCalibration.communicationRecovering = 1;
                failedCalibration.servoFault = snapshot.servoFault;
                failedCalibration.machinePositionMm = motionController_.GetRuntime().machinePositionMm;
                complianceCalibration_.Update(failedCalibration);
                motionOutput.Update({}, true, servoCommand);
            }
            if (sequencer_.IsRunning())
            {
                sequencer_.AbortFromSafety();
                (void)DaoEngine_SetIoOutputCommand(config_.logicalIoIndex,
                    static_cast<unsigned short>(servoCommand.ioOutputCommand & 0xff00U));
            }
            SetStartupFault(
                startup,
                snapshot.communicationValid == 0
                    ? UTM_STARTUP_FAULT_COMMUNICATION_STALE
                    : UTM_STARTUP_FAULT_SERVO_ON_FAILED);
            startupPrepared = false;

            runtime.publishedTimestampNs = timestampNs;
            runtime.controlCycle = cycle;
            runtime.machineState = stateMachine_.GetState();
            runtime.input = snapshot;
            if (generalMotionActive || generalMotionStopping)
            {
                generalMotionActive = false;
                generalMotionStopping = false;
                generalMotionRequest = {};
                motionController_.MarkAborted(
                    UTM_MOTION_FAILURE_SAFETY_STOP);
            }
            runtimeStore_.Publish(
                runtime,
                jogRuntime,
                startup,
                motionController_.GetRuntime());
            std::this_thread::sleep_until(nextWakeTime);
            continue;
        }

        const UtmJogArbitration jog =
            jogController_.Resolve(
                snapshot.jogUp != 0,
                snapshot.jogDown != 0);

        UtmGeneralMotionCommand pendingMotion{};

        if (motionController_.ConsumePending(pendingMotion))
        {
            UtmMotionRequest prepared{};
            const bool isForceMotion =
                pendingMotion.type == UTM_MOTION_MOVE_TO_FORCE ||
                pendingMotion.type == UTM_MOTION_HOLD_FORCE;
            const bool canStart =
                (stateMachine_.GetState() == UTM_MACHINE_READY ||
                 (sequencer_.IsRunning() && pendingMotion.source ==
                    UTM_COMMAND_SOURCE_SEQUENCER &&
                  stateMachine_.GetState() == UTM_MACHINE_RUNNING)) &&
                stopLatch_.Get().latched == 0 &&
                snapshot.communicationValid != 0 &&
                snapshot.servoOn != 0 &&
                snapshot.servoFault == 0 &&
                snapshot.servoPositionValid != 0 &&
                jog.sourceMask == 0 &&
                motionOutput.IsStopComplete() &&
                (!isForceMotion || snapshot.forceValid != 0);

            if (!canStart)
            {
                motionController_.MarkFailed(
                    stateMachine_.GetState() == UTM_MACHINE_READY
                        ? UTM_MOTION_FAILURE_BUSY
                        : UTM_MOTION_FAILURE_NOT_READY);
            }
            else if (!motionController_.Prepare(
                pendingMotion,
                snapshot.servoActualPosition,
                prepared))
            {
                if (motionController_.GetRuntime().motionFailureReason ==
                    UTM_MOTION_FAILURE_NONE)
                {
                    motionController_.MarkFailed(
                        UTM_MOTION_FAILURE_INVALID_REQUEST);
                }
            }
            else if ((prepared.direction == UTM_DIRECTION_UP &&
                    snapshot.upperLimit != 0) ||
                (prepared.direction == UTM_DIRECTION_DOWN &&
                    snapshot.lowerLimit != 0))
            {
                motionController_.MarkFailed(
                    UTM_MOTION_FAILURE_LIMIT_BLOCKED);
            }
            else if (prepared.direction == UTM_DIRECTION_NONE)
            {
                motionController_.MarkCompleted();
            }
            else if (!stateMachine_.TryStartGeneralMotion())
            {
                motionController_.MarkFailed(
                    UTM_MOTION_FAILURE_NOT_READY);
            }
            else
            {
                generalMotionRequest = prepared;
                generalMotionRequest.targetForceN = pendingMotion.targetForceN;
                generalMotionRequest.holdTimeSec = pendingMotion.holdTimeSec;
                generalMotionRequest.maxTravelMm = pendingMotion.maxTravelMm;
                generalMotionRequest.toleranceN = pendingMotion.toleranceN;
                generalMotionActive = true;
                generalMotionStopping = false;
                generalStopReason = UTM_MOTION_FAILURE_NONE;
                generalStopDisposition = 0;
                if (isForceMotion)
                {
                    forceRuntime = {};
                    forceRuntime.forceMotionActive = 1;
                    forceRuntime.forcePhase = UTM_FORCE_PHASE_APPROACH;
                    forceRuntime.currentForceN = snapshot.forceN;
                    forceRuntime.forceValid = snapshot.forceValid;
                    forceRuntime.targetForceN = pendingMotion.targetForceN;
                    const int sign = pendingMotion.direction == UTM_DIRECTION_UP ? 1 : -1;
                    forceRuntime.signedTargetForceN =
                        pendingMotion.targetForceN * sign;
                    forceRuntime.holdTimeTargetSec = pendingMotion.holdTimeSec;
                    forceRuntime.forceToleranceN = pendingMotion.type == UTM_MOTION_HOLD_FORCE
                        ? pendingMotion.toleranceN : 0.0;
                    forceRuntime.forceMotionStartPositionMm =
                        motionController_.GetRuntime().machinePositionMm;
                    forceRuntime.maxTravelMm = pendingMotion.maxTravelMm;
                    forceRuntime.timeoutRemainingSec =
                        static_cast<double>(pendingMotion.timeoutMs) / 1000.0;
                    forceStartedNs = timestampNs;
                    forceHoldSinceNs = 0;
                }
            }
        }

        int stopMotionSource = UTM_COMMAND_SOURCE_INTERNAL;

        if (motionController_.ConsumeStopRequest(stopMotionSource) &&
            generalMotionActive)
        {
            (void)stopMotionSource;
            generalMotionActive = false;
            generalMotionStopping = true;
            generalStopReason = UTM_MOTION_FAILURE_USER_STOP;
            generalStopDisposition = 2;
            motionController_.MarkStopping();
            stateMachine_.BeginGeneralMotionStop();
        }

        // 상태 전이 명령은 Mailbox에서 Control Thread만 소비합니다.
        // UI/Remote Jog 요청도 source별 store를 통해서만 반영됩니다.
        UtmCommandRequest command{};
        bool hasCommand =
            commandMailbox_.TryPop(command);

        const bool externalGoRising =
            snapshot.externalGo != 0 &&
            !previousExternalGo;

        previousExternalGo =
            snapshot.externalGo != 0;

        if (!hasCommand &&
            externalGoRising &&
            stopLatch_.Get().latched == 0)
        {
            const UtmSequenceRuntimeInfo sequence = sequencer_.GetRuntime();
            if (sequence.sequenceCommitted != 0 && sequence.sequenceRunning == 0)
                (void)sequencer_.RequestGoStart();
            else
            {
                command.commandId = nextCommandId_.fetch_add(1);
                command.commandEpoch = commandEpoch_.load();
                command.requestedTimestampNs = timestampNs;
                command.source = UTM_COMMAND_SOURCE_EXTERNAL_GO;
                command.type = UTM_COMMAND_START;
                hasCommand = true;
            }
        }

        if (hasCommand &&
            command.commandEpoch != commandEpoch_.load())
        {
            hasCommand = false;
        }

        // READY의 Jog 요청도 Limit/통신/Servo 준비 상태를 먼저
        // 평가하고, 통과한 경우에만 MANUAL 전이와 출력을 수행합니다.
        UtmSafetyContext safetyContext{};
        const UtmComplianceCalibrationOutput calibrationOutputBefore =
            complianceCalibration_.GetOutput();
        const auto& basicRecovery=inputCollector_->GetBasicRecoveryRuntime();
        const UtmMotionOutputRuntime& ownershipOutput=motionOutput.GetRuntime();
        const UtmSequenceRuntimeInfo ownershipSequence=sequencer_.GetRuntime();
        UtmMotionOwnershipSnapshot ownership{};
        ownership.jogRequested=jog.requested;ownership.jogActive=motionOutput.IsMotionActive()&&ownershipOutput.activeType==UTM_MOTION_JOG;
        ownership.generalMotionActive=generalMotionActive;ownership.generalMotionStopping=generalMotionStopping;
        ownership.sequenceRunning=ownershipSequence.sequenceRunning;ownership.sequenceActionPending=sequencer_.HasPendingAction();
        ownership.calibrationActive=complianceCalibration_.IsActive();ownership.calibrationVelocityRequested=calibrationOutputBefore.velocityRequested;
        ownership.calibrationForceStopRequested=calibrationOutputBefore.forceStopRequested;
        ownership.calibrationReturnPending=motionController_.GetRuntime().motionSource==UTM_COMMAND_SOURCE_CALIBRATION&&motionController_.HasPendingOrActive();
        ownership.outputPositionActive=ownershipOutput.state==UtmMotionOutputState::POSITION_ACTIVE;
        ownership.outputVelocityActive=ownershipOutput.state==UtmMotionOutputState::VELOCITY_ACTIVE;
        ownership.pendingMotionCommand=motionController_.HasPendingOrActive()||commandMailbox_.HasPendingMotion()||
            (hasCommand&&(command.type==UTM_COMMAND_START||command.type==UTM_COMMAND_JOG_UP_REQUEST||command.type==UTM_COMMAND_JOG_DOWN_REQUEST));
        ownership.commandType=hasCommand?command.type:motionController_.GetRuntime().motionType;
        ownership.commandSource=hasCommand?command.source:motionController_.GetRuntime().motionSource;
        ownership.sequenceStep=ownershipSequence.currentStepType;
        if(basicRecovery.incidentGeneration!=0&&basicRecovery.incidentGeneration!=observedCommunicationIncident)
        {
            observedCommunicationIncident=basicRecovery.incidentGeneration;
            communicationIncidentMotionActive=UtmMotionOwnershipPolicy::IsActive(ownership);
            communicationMotionInhibited_.store(true);
            communicationWarningPolicy.Add(timestampNs);
            const unsigned long long newEpoch=commandEpoch_.fetch_add(1)+1;
            commandMailbox_.InvalidateMotionCommands(newEpoch);
            jogController_.ClearCommandRequests();jogController_.InhibitPhysicalUntilReleased();
            std::fprintf(stderr,"[UTM COMM] incident=%llu ownership=%s commandType=%d source=%d sequenceRunning=%d step=%d calibration=%d\n",basicRecovery.incidentGeneration,communicationIncidentMotionActive?"active":"idle",ownership.commandType,ownership.commandSource,ownership.sequenceRunning,ownership.sequenceStep,ownership.calibrationActive);
            communicationRuntime.incidentMotionActive=communicationIncidentMotionActive?1:0;
            communicationRuntime.motionInterrupted=communicationIncidentMotionActive?1:0;
            communicationRuntime.incidentCommandType=ownership.commandType;communicationRuntime.incidentCommandSource=ownership.commandSource;
            communicationRuntime.incidentSequenceRunning=ownership.sequenceRunning;communicationRuntime.incidentSequenceStep=ownership.sequenceStep;
            communicationRuntime.incidentCalibrationActive=ownership.calibrationActive;
        }
        if(basicRecovery.communicationState==DAO_COMMUNICATION_HEALTHY&&basicRecovery.recoveryActive==0&&
            snapshot.communicationValid&&snapshot.servoCommunicationValid&&!snapshot.servoFault&&
            !snapshot.servoStoActive&&snapshot.servoOperationState!=0)
            communicationMotionInhibited_.store(false);
        if(communicationIncidentMotionActive&&basicRecovery.recoverySucceeded&&
            alignedCommunicationIncident!=basicRecovery.incidentGeneration)
        {
            alignedCommunicationIncident=basicRecovery.incidentGeneration;
            motionOutput.RequestRecoveryStopAlignment();
            communicationRuntime.safeAlignmentResult=1; // requested; completion remains governed by StopLatch/arbiter
        }
        communicationRuntime.version=1;communicationRuntime.communicationState=basicRecovery.communicationState;
        communicationRuntime.incidentGeneration=basicRecovery.incidentGeneration;communicationRuntime.recoveryActive=basicRecovery.recoveryActive;
        communicationRuntime.recoveryStage=basicRecovery.recoveryStage;communicationRuntime.recoveryAttempt=basicRecovery.recoveryAttempt;
        communicationRuntime.recoverySucceeded=basicRecovery.recoverySucceeded;communicationRuntime.recoveryFailed=basicRecovery.recoveryFailed;
        communicationRuntime.recoveryElapsedMs=basicRecovery.recoveryElapsedMs;communicationRuntime.expectedWkc=basicRecovery.expectedWkc;
        communicationRuntime.currentWkc=basicRecovery.currentWkc;communicationRuntime.minimumWkc=basicRecovery.minimumWkc;
        communicationRuntime.consecutiveBadWkc=basicRecovery.consecutiveBadWkc;communicationRuntime.consecutiveGoodWkc=basicRecovery.consecutiveGoodWkc;
        communicationRuntime.maximumConsecutiveBadWkc=basicRecovery.maximumConsecutiveBadWkc;
        communicationRuntime.failedSlaveIndex=basicRecovery.failedSlaveIndex;communicationRuntime.failedSlaveState=basicRecovery.failedSlaveState;
        communicationRuntime.failedSlaveAlStatus=basicRecovery.failedSlaveAlStatus;communicationRuntime.totalIncidentCount=basicRecovery.totalIncidentCount;
        communicationRuntime.recoveredIncidentCount=basicRecovery.recoveredIncidentCount;communicationRuntime.recoveryFailureCount=basicRecovery.recoveryFailureCount;
        communicationRuntime.maximumRecoveryDurationMs=basicRecovery.maximumRecoveryDurationMs;communicationRuntime.lastIncidentTimestampNs=basicRecovery.lastIncidentTimestampNs;
        communicationRuntime.rollingHourIncidentCount=communicationWarningPolicy.Recent(timestampNs);
        communicationRuntime.communicationUnstableWarning=communicationWarningPolicy.Unstable(timestampNs)?1:0;
        communicationRuntime.postRecoveryServoState=snapshot.servoOperationState;
        safetyContext.machineState =
            stateMachine_.GetState();
        safetyContext.motionActive = UtmMotionOwnershipPolicy::IsActive(ownership)?1:0;
        safetyContext.requestedDirection =
            calibrationOutputBefore.velocityRequested != 0
                ? calibrationOutputBefore.direction
                : (generalMotionActive || generalMotionStopping)
                ? generalMotionRequest.direction
                : jog.requested != 0
                ? jog.direction
                : motionOutput.GetRuntime().activeDirection;

        const bool servoCanPrepareForMotion =
            snapshot.servoCommunicationValid != 0 &&
            snapshot.servoFault == 0 &&
            snapshot.servoStoActive == 0 &&
            (snapshot.servoReady != 0 ||
                snapshot.servoOperationState == 0x0040);

        const bool userStop =
            userStopRequested_.exchange(false);

        UtmStopEvaluation evaluation =
            safetyMonitor_.Evaluate(
                snapshot,
                safetyContext,
                userStop,
                servoCanPrepareForMotion,
                (inputCollector_->GetCommunicationState()==UtmInputCollector::RECOVERING&&communicationIncidentMotionActive)||inputCollector_->GetCommunicationState()==UtmInputCollector::FAULT);

        const bool newlyLatched =
            stopLatch_.Update(
                evaluation,
                cycle,
                timestampNs);

        if (newlyLatched)
        {
            const unsigned long long newEpoch =
                commandEpoch_.fetch_add(1) + 1;

            commandMailbox_.InvalidateMotionCommands(
                newEpoch);

            jogController_.ClearCommandRequests();
            jogController_.InhibitPhysicalUntilReleased();

            if (generalMotionActive || generalMotionStopping)
            {
                generalMotionActive = false;
                generalMotionStopping = true;
                generalStopReason = UTM_MOTION_FAILURE_SAFETY_STOP;
                generalStopDisposition = 2;
                motionController_.MarkStopping();
                stateMachine_.BeginGeneralMotionStop();
            }
            if (sequencer_.IsRunning()) sequencer_.AbortFromSafety();
        }

        bool stopAcknowledged = false;

        if (hasCommand &&
            command.type ==
                UTM_COMMAND_ACKNOWLEDGE_STOP)
        {
            stopAcknowledged =
                stopLatch_.TryAcknowledge(
                    snapshot,
                    evaluation);

            if(stopAcknowledged)
            {
                communicationRuntime.motionInterrupted=0;
                communicationIncidentMotionActive=false;
            }

            hasCommand = false;
        }

        const UtmCommandRequest* stateCommand =
            hasCommand &&
            stopLatch_.Get().latched == 0
            ? &command
            : nullptr;

        stateMachine_.Update(
            snapshot,
            stopLatch_.Get(),
            stateCommand,
            stopAcknowledged);

        UtmComplianceCalibrationInput calibrationInput{};
        calibrationInput.timestampNs = timestampNs;
        calibrationInput.forceN = snapshot.forceN;
        calibrationInput.machinePositionMm = motionController_.GetRuntime().machinePositionMm;
        calibrationInput.testPositionMm = motionController_.GetRuntime().testPositionMm;
        calibrationInput.forceValid = snapshot.forceValid;
        calibrationInput.machineReady = stateMachine_.GetState() == UTM_MACHINE_READY;
        calibrationInput.servoReady = snapshot.servoOn != 0 && snapshot.servoFault == 0;
        calibrationInput.communicationValid = snapshot.communicationValid;
        calibrationInput.communicationRecovering =
            inputCollector_->GetCommunicationState() == UtmInputCollector::RECOVERING ||
            inputCollector_->GetCommunicationState() == UtmInputCollector::FAULT;
        calibrationInput.emergency = snapshot.emergency;
        calibrationInput.externalStop = snapshot.externalStop;
        calibrationInput.upperLimit = snapshot.upperLimit;
        calibrationInput.lowerLimit = snapshot.lowerLimit;
        calibrationInput.servoFault = snapshot.servoFault;
        const auto& protection = safetyMonitor_.GetMachineProtection();
        calibrationInput.overload = protection.overloadEnabled != 0 &&
            snapshot.forceValid != 0 &&
            std::fabs(snapshot.forceN) >= protection.maxAllowedForceN;
        calibrationInput.stopLatched = stopLatch_.Get().latched;
        calibrationInput.motionOutputStopped = motionOutput.IsStopComplete() ? 1 : 0;
        calibrationInput.forceZeroCaptureActive =
            servoCommand.adcStableCaptureActive != 0 &&
            servoCommand.adcStableCaptureType == 1;
        const UtmGeneralMotionRuntimeInfo calibrationMotionRuntime =
            motionController_.GetRuntime();
        calibrationInput.returnMotionActive =
            calibrationMotionRuntime.motionSource == UTM_COMMAND_SOURCE_CALIBRATION &&
            calibrationMotionRuntime.motionActive != 0;
        calibrationInput.returnMotionComplete =
            calibrationMotionRuntime.motionSource == UTM_COMMAND_SOURCE_CALIBRATION &&
            calibrationMotionRuntime.motionComplete != 0;
        calibrationInput.returnMotionFailed =
            calibrationMotionRuntime.motionSource == UTM_COMMAND_SOURCE_CALIBRATION &&
            (calibrationMotionRuntime.motionState == UTM_MOTION_STATE_FAILED ||
             calibrationMotionRuntime.motionState == UTM_MOTION_STATE_ABORTED);
        complianceCalibration_.Update(calibrationInput);

        // Calibration-local guards are intentionally evaluated below the
        // existing machine Safety layer.  A local guard fault must still use
        // the common StopLatch/ACK contract so that motion ownership cannot be
        // silently released while the operator has not acknowledged the stop.
        UtmComplianceCalibrationRuntimeV1 calibrationRuntimeAfterUpdate{};
        complianceCalibration_.GetRuntime(calibrationRuntimeAfterUpdate);
        if (calibrationRuntimeAfterUpdate.state == UTM_COMPLIANCE_CAL_FAULTED &&
            stopLatch_.Get().latched == 0)
        {
            UtmStopEvaluation calibrationStop{};
            calibrationStop.requested = true;
            calibrationStop.reasonMask =
                1ULL << static_cast<unsigned int>(UTM_STOP_CALIBRATION_FAULT);
            calibrationStop.primaryReason = UTM_STOP_CALIBRATION_FAULT;
            calibrationStop.action = UTM_STOP_ACTION_CONTROLLED_STOP;

            if (stopLatch_.Update(calibrationStop, cycle, timestampNs))
            {
                const unsigned long long newEpoch = commandEpoch_.fetch_add(1) + 1;
                commandMailbox_.InvalidateMotionCommands(newEpoch);
                jogController_.ClearCommandRequests();
                jogController_.InhibitPhysicalUntilReleased();

                if (generalMotionActive || generalMotionStopping)
                {
                    generalMotionActive = false;
                    generalMotionStopping = true;
                    generalStopReason = UTM_MOTION_FAILURE_SAFETY_STOP;
                    generalStopDisposition = 2;
                    motionController_.MarkStopping();
                    stateMachine_.BeginGeneralMotionStop();
                }
                if (sequencer_.IsRunning()) sequencer_.AbortFromSafety();
            }
        }

        UtmComplianceCalibrationAction calibrationAction{};
        if (complianceCalibration_.TakeAction(calibrationAction))
        {
            bool accepted = false;
            if (calibrationAction.type == UtmComplianceCalibrationActionType::ZeroForce)
            {
                accepted = DaoEngine_SetAdcZero(config_.logicalAdcIndex) == 1;
                if (accepted) displayCaptureResetType_.store(1);
            }
            else if (calibrationAction.type == UtmComplianceCalibrationActionType::ReturnAbsolute)
            {
                const UtmJogConfigV2 motionConfig = motionController_.GetConfig();
                UtmGeneralMotionCommand returnCommand{};
                returnCommand.type = UTM_MOTION_ABSOLUTE;
                returnCommand.targetPositionMm = calibrationAction.targetTestPositionMm;
                returnCommand.speedMmPerMin = calibrationAction.speedMmPerMin;
                returnCommand.acceleration = motionConfig.config.acceleration;
                returnCommand.deceleration = motionConfig.config.deceleration;
                returnCommand.timeoutMs = calibrationAction.timeoutMs;
                returnCommand.source = UTM_COMMAND_SOURCE_CALIBRATION;
                accepted = motionController_.Submit(returnCommand);
            }
            complianceCalibration_.ReportActionResult(calibrationAction.type, accepted);
        }

        if (sequencer_.IsRunning())
        {
            const UtmSequenceStep currentStep = sequencer_.GetCurrentStep();
            const bool monitoredMotion = currentStep.stepType >=
                    UTM_SEQUENCE_STEP_MOVE_ABSOLUTE &&
                currentStep.stepType <= UTM_SEQUENCE_STEP_HOLD_FORCE;
            if (monitoredMotion && generalMotionActive)
            {
                const int completion = stopConditionMonitor_.Update(snapshot,
                    motionController_.GetRuntime().machinePositionMm, timestampNs);
                if (completion != UTM_COMPLETION_NONE)
                {
                    sequencer_.ReportStopCondition(completion);
                    (void)motionController_.RequestStop(UTM_COMMAND_SOURCE_SEQUENCER);
                }
            }
        }

        sequencer_.Update(snapshot, servoCommand,
            motionController_.GetRuntime(), timestampNs);
        UtmSequenceAction sequenceAction{};
        if (sequencer_.ConsumeAction(sequenceAction))
        {
            bool actionOk = true;
            if (sequenceAction.type == UTM_SEQUENCE_ACTION_SEQUENCE_BEGIN)
                actionOk = stateMachine_.BeginSequence();
            else if (sequenceAction.type == UTM_SEQUENCE_ACTION_SUBMIT_MOTION)
            {
                const UtmSequenceStep& s = sequenceAction.step;
                UtmGeneralMotionCommand c{};
                c.source = UTM_COMMAND_SOURCE_SEQUENCER;
                c.direction = s.direction;
                c.targetPositionMm = s.positionMm;
                c.incrementalDistanceMm = s.distanceMm;
                c.speedMmPerMin = s.speedMmPerMin;
                c.acceleration = s.acceleration;
                c.deceleration = s.deceleration;
                c.timeoutMs = s.timeoutMs;
                c.targetForceN = s.forceN;
                c.holdTimeSec = s.holdTimeSec;
                c.toleranceN = s.toleranceN;
                c.maxTravelMm = s.stopConditions.enableMaxTravel
                    ? s.stopConditions.maxTravelMm : forceConfig_.maxTravelMm;
                switch (s.stepType)
                {
                case UTM_SEQUENCE_STEP_MOVE_ABSOLUTE: c.type = UTM_MOTION_ABSOLUTE; break;
                case UTM_SEQUENCE_STEP_MOVE_INCREMENTAL: c.type = UTM_MOTION_INCREMENTAL; break;
                case UTM_SEQUENCE_STEP_MOVE_VELOCITY: c.type = UTM_MOTION_VELOCITY; break;
                case UTM_SEQUENCE_STEP_MOVE_TO_FORCE: c.type = UTM_MOTION_MOVE_TO_FORCE; break;
                case UTM_SEQUENCE_STEP_HOLD_FORCE:
                    c.type = UTM_MOTION_HOLD_FORCE;
                    c.speedMmPerMin = forceConfig_.approachSpeedMmPerMin;
                    c.acceleration = forceConfig_.acceleration;
                    c.deceleration = forceConfig_.deceleration;
                    break;
                default: actionOk = false; break;
                }
                if (actionOk && (c.type == UTM_MOTION_MOVE_TO_FORCE ||
                    c.type == UTM_MOTION_HOLD_FORCE) && snapshot.forceValid == 0)
                    actionOk = false;
                if (actionOk) actionOk = motionController_.Submit(c);
                if (actionOk) stopConditionMonitor_.Start(s.stopConditions,
                    motionController_.GetRuntime().machinePositionMm, timestampNs);
            }
            else if (sequenceAction.type == UTM_SEQUENCE_ACTION_STOP_MOTION)
            {
                if (motionController_.HasPendingOrActive())
                    (void)motionController_.RequestStop(UTM_COMMAND_SOURCE_SEQUENCER);
                actionOk = true;
            }
            else if (sequenceAction.type == UTM_SEQUENCE_ACTION_ZERO_FORCE)
                actionOk = DaoEngine_SetAdcZero(config_.logicalAdcIndex) == 1;
            else if (sequenceAction.type == UTM_SEQUENCE_ACTION_ZERO_POSITION)
                actionOk = motionController_.SetPositionZero(snapshot.servoActualPosition);
            else if (sequenceAction.type == UTM_SEQUENCE_ACTION_ZERO_ENCODER)
                actionOk = DaoEngine_ResetEncoderCounter(config_.logicalEncoderIndex,
                    config_.encoderChannel, sequenceAction.step.timeoutMs) == 1;
            else if (sequenceAction.type == UTM_SEQUENCE_ACTION_SET_OUTPUTS)
                actionOk = DaoEngine_SetIoOutputCommand(config_.logicalIoIndex,
                    static_cast<unsigned short>((servoCommand.ioOutputCommand & 0xff00U) |
                        (sequenceAction.outputs & 0x00ffU))) == 1;
            else if (sequenceAction.type == UTM_SEQUENCE_ACTION_SEQUENCE_COMPLETE)
            {
                (void)DaoEngine_SetIoOutputCommand(config_.logicalIoIndex,
                    static_cast<unsigned short>(servoCommand.ioOutputCommand & 0xff00U));
                stateMachine_.CompleteSequence();
                stopConditionMonitor_.Reset();
            }
            else if (sequenceAction.type == UTM_SEQUENCE_ACTION_SEQUENCE_ABORT)
            {
                (void)DaoEngine_SetIoOutputCommand(config_.logicalIoIndex,
                    static_cast<unsigned short>(servoCommand.ioOutputCommand & 0xff00U));
                stopConditionMonitor_.Reset();
            }
            sequencer_.ReportActionResult(actionOk);
        }

        if (!sequencer_.IsRunning() && !motionController_.HasPendingOrActive())
        {
            const UtmSequenceRuntimeInfo sequence = sequencer_.GetRuntime();
            if (sequence.sequenceState == UTM_SEQUENCE_STATE_ABORTED)
                stateMachine_.CompleteSequence();
            else if (sequence.sequenceState == UTM_SEQUENCE_STATE_FAILED)
                stateMachine_.FailSequence();
        }

        if (stopLatch_.Get().latched == 0 &&
            !evaluation.requested &&
            jog.conflict == 0 &&
            !generalMotionActive &&
            !generalMotionStopping &&
            servoCanPrepareForMotion)
        {
            (void)stateMachine_.TryEnterAutomaticJogMode(
                snapshot,
                jog.requested != 0,
                motionOutput.IsStopComplete());
        }

        if (stateCommand != nullptr)
        {
            runtime.lastAcceptedCommandId =
                stateCommand->commandId;
            runtime.lastAcceptedCommand =
                stateCommand->type;
        }

        const bool jogPermitted =
            stateMachine_.GetState() == UTM_MACHINE_MANUAL &&
            stopLatch_.Get().latched == 0 &&
            evaluation.requested == false &&
            jog.requested != 0 &&
            jog.conflict == 0;

        UtmMotionRequest motionRequest{};

        const UtmComplianceCalibrationOutput calibrationOutput =
            complianceCalibration_.GetOutput();

        const bool forceMotionActive = generalMotionActive &&
            (generalMotionRequest.type == UTM_MOTION_MOVE_TO_FORCE ||
             generalMotionRequest.type == UTM_MOTION_HOLD_FORCE);

        if (forceMotionActive)
        {
            const int commandedForceDirection =
                motionController_.GetRuntime().motionDirection;
            const int directionSign = commandedForceDirection == UTM_DIRECTION_UP ? 1 : -1;
            const double directionalForce = snapshot.forceN *
                static_cast<double>(forceConfig_.forceDirectionSign * directionSign);
            const double error = generalMotionRequest.targetForceN - directionalForce;
            const double elapsed = static_cast<double>(timestampNs - forceStartedNs) / 1.0e9;
            const double machinePosition = motionController_.GetRuntime().machinePositionMm;
            forceRuntime.currentForceN = snapshot.forceN;
            forceRuntime.forceValid = snapshot.forceValid;
            forceRuntime.forceErrorN = error;
            forceRuntime.forceMotionTravelMm = std::fabs(
                machinePosition - forceRuntime.forceMotionStartPositionMm);
            forceRuntime.timeoutElapsedSec = elapsed;
            forceRuntime.timeoutRemainingSec = std::max(0.0,
                static_cast<double>(generalMotionRequest.timeoutMs) / 1000.0 - elapsed);

            int failReason = UTM_MOTION_FAILURE_NONE;
            if (snapshot.forceValid == 0)
                failReason = UTM_MOTION_FAILURE_FORCE_INVALID;
            else if (forceRuntime.forceMotionTravelMm >= generalMotionRequest.maxTravelMm)
                failReason = UTM_MOTION_FAILURE_MAX_TRAVEL;
            else if (elapsed * 1000.0 >= generalMotionRequest.timeoutMs)
                failReason = UTM_MOTION_FAILURE_TIMEOUT;

            bool completed = false;
            bool wantsVelocity = true;
            int requestedDirection = commandedForceDirection;
            double requestedSpeed = generalMotionRequest.speed;

            if (failReason == UTM_MOTION_FAILURE_NONE &&
                generalMotionRequest.type == UTM_MOTION_MOVE_TO_FORCE)
            {
                if (error <= 0.0)
                {
                    forceRuntime.forceTargetReached = 1;
                    completed = true;
                    wantsVelocity = false;
                }
            }
            else if (failReason == UTM_MOTION_FAILURE_NONE)
            {
                const double absError = std::fabs(error);
                if (absError <= generalMotionRequest.toleranceN)
                {
                    wantsVelocity = false;
                    forceRuntime.forceTargetReached = 1;
                    forceRuntime.forceHoldStable = 1;
                    forceRuntime.forcePhase = UTM_FORCE_PHASE_HOLDING;
                    if (forceHoldSinceNs == 0) forceHoldSinceNs = timestampNs;
                    forceRuntime.holdTimeElapsedSec =
                        static_cast<double>(timestampNs - forceHoldSinceNs) / 1.0e9;
                    completed = forceRuntime.holdTimeElapsedSec >=
                        generalMotionRequest.holdTimeSec;
                }
                else
                {
                    forceHoldSinceNs = 0;
                    forceRuntime.holdTimeElapsedSec = 0.0;
                    forceRuntime.forceHoldStable = 0;
                    if (error < 0.0)
                    {
                        requestedDirection = commandedForceDirection == UTM_DIRECTION_UP
                            ? UTM_DIRECTION_DOWN : UTM_DIRECTION_UP;
                        requestedSpeed = forceConfig_.reverseSpeedMmPerMin;
                        forceRuntime.forcePhase = UTM_FORCE_PHASE_CORRECTING;
                    }
                    else if (absError > forceConfig_.mediumErrorN)
                    {
                        requestedSpeed = forceConfig_.approachSpeedMmPerMin;
                        forceRuntime.forcePhase = UTM_FORCE_PHASE_APPROACH;
                    }
                    else
                    {
                        requestedSpeed = absError > forceConfig_.fineErrorN
                            ? forceConfig_.mediumSpeedMmPerMin
                            : forceConfig_.fineSpeedMmPerMin;
                        forceRuntime.forcePhase = UTM_FORCE_PHASE_FINE_APPROACH;
                    }
                }
            }

            if (failReason != UTM_MOTION_FAILURE_NONE || completed)
            {
                generalMotionActive = false;
                generalMotionStopping = true;
                generalStopReason = failReason;
                generalStopDisposition = completed ? 1 : 3;
                forceRuntime.forceMotionActive = 0;
                forceRuntime.forcePhase = completed
                    ? UTM_FORCE_PHASE_COMPLETED : UTM_FORCE_PHASE_FAILED;
                motionController_.MarkStopping();
                stateMachine_.BeginGeneralMotionStop();
            }
            else if (wantsVelocity)
            {
                if (!motionController_.SetVelocityOutput(requestedSpeed,
                    requestedDirection, generalMotionRequest.acceleration,
                    generalMotionRequest.deceleration, generalMotionRequest))
                {
                    generalMotionActive = false;
                    generalMotionStopping = true;
                    generalStopReason = UTM_MOTION_FAILURE_VELOCITY_OVERFLOW;
                    generalStopDisposition = 3;
                    forceRuntime.forceMotionActive = 0;
                    forceRuntime.forcePhase = UTM_FORCE_PHASE_FAILED;
                    motionController_.MarkStopping();
                    stateMachine_.BeginGeneralMotionStop();
                }
                forceRuntime.currentCommandSpeedMmPerMin = requestedSpeed;
            }
            else
            {
                generalMotionRequest.servoTargetVelocity = 0;
                generalMotionRequest.direction = UTM_DIRECTION_NONE;
                forceRuntime.currentCommandSpeedMmPerMin = 0.0;
            }
        }

        // HOLD correction can reverse direction within this cycle. Re-evaluate
        // directional limits before allowing that newly selected output.
        if (generalMotionActive &&
            (generalMotionRequest.type == UTM_MOTION_MOVE_TO_FORCE ||
             generalMotionRequest.type == UTM_MOTION_HOLD_FORCE))
        {
            UtmSafetyContext forceDirectionContext{};
            forceDirectionContext.machineState = stateMachine_.GetState();
            forceDirectionContext.motionActive = 1;
            forceDirectionContext.requestedDirection = generalMotionRequest.direction;
            const UtmStopEvaluation directionEvaluation = safetyMonitor_.Evaluate(
                snapshot, forceDirectionContext, false, servoCanPrepareForMotion,
                inputCollector_->GetCommunicationState()==UtmInputCollector::RECOVERING||inputCollector_->GetCommunicationState()==UtmInputCollector::FAULT);
            if (stopLatch_.Update(directionEvaluation, cycle, timestampNs))
            {
                generalMotionActive = false;
                generalMotionStopping = true;
                generalStopReason = UTM_MOTION_FAILURE_SAFETY_STOP;
                generalStopDisposition = 2;
                forceRuntime.forceMotionActive = 0;
                forceRuntime.forcePhase = UTM_FORCE_PHASE_ABORTED;
                motionController_.MarkStopping();
                stateMachine_.BeginGeneralMotionStop();
            }
        }

        if (calibrationOutput.velocityRequested != 0)
        {
            const UtmJogConfigV2 motionConfig = motionController_.GetConfig();
            if (!motionController_.SetVelocityOutput(
                calibrationOutput.speedMmPerMin,
                calibrationOutput.direction,
                motionConfig.config.acceleration,
                motionConfig.config.deceleration,
                motionRequest))
            {
                complianceCalibration_.ReportMotionOutputFailure();
            }
            else
            {
                motionRequest.type = UTM_MOTION_MOVE_TO_FORCE;
                motionRequest.source = UTM_COMMAND_SOURCE_CALIBRATION;
            }
        }
        else if (generalMotionActive)
        {
            motionRequest = generalMotionRequest;
        }
        else if (jogPermitted)
        {
            const UtmJogConfigV2 jogConfig =
                jogController_.GetConfig();

            motionRequest.type = UTM_MOTION_JOG;
            motionRequest.direction = jog.direction;
            motionRequest.speed = jog.speedMmPerMin;
            motionRequest.acceleration =
                jogConfig.config.acceleration;
            motionRequest.deceleration =
                jogConfig.config.deceleration;
            motionRequest.servoTargetVelocity =
                jog.servoTargetVelocity;
        }

        const bool forceMotionStop =
            evaluation.requested ||
            stopLatch_.Get().latched != 0 ||
            calibrationOutput.forceStopRequested != 0 ||
            generalMotionStopping ||
            (stateMachine_.GetState() == UTM_MACHINE_MANUAL &&
                jog.conflict != 0);

        motionOutput.Update(
            motionRequest,
            forceMotionStop,
            servoCommand);

        if (generalMotionActive)
        {
            const bool isForce =
                generalMotionRequest.type == UTM_MOTION_MOVE_TO_FORCE ||
                generalMotionRequest.type == UTM_MOTION_HOLD_FORCE;
            const bool isPosition =
                generalMotionRequest.type == UTM_MOTION_ABSOLUTE ||
                generalMotionRequest.type == UTM_MOTION_INCREMENTAL;
            const UtmGeneralMotionRuntimeInfo beforeObservation =
                motionController_.GetRuntime();
            const UtmMotionOutputRuntime& issued =
                motionOutput.GetRuntime();

            if (!isForce && isPosition &&
                beforeObservation.motionState !=
                    UTM_MOTION_STATE_PREPARING &&
                issued.commandObserved != 0 &&
                servoCommand.commandType ==
                    DAO_SERVO_CMD_MOVE_ABSOLUTE)
            {
                if (servoCommand.commandState ==
                    DAO_SERVO_STATE_COMPLETED)
                {
                    generalMotionActive = false;
                    generalMotionRequest = {};
                    motionController_.MarkCompleted();
                    stateMachine_.CompleteGeneralMotion();
                }
                else if (servoCommand.commandState ==
                        DAO_SERVO_STATE_ERROR ||
                    servoCommand.commandState ==
                        DAO_SERVO_STATE_TIMEOUT)
                {
                    generalMotionActive = false;
                    generalMotionRequest = {};
                    motionController_.MarkFailed(
                        servoCommand.commandState ==
                                DAO_SERVO_STATE_TIMEOUT
                            ? UTM_MOTION_FAILURE_SERVO_TIMEOUT
                            : UTM_MOTION_FAILURE_SERVO_ERROR);
                    stateMachine_.CompleteGeneralMotion();
                }
                else if (servoCommand.commandState ==
                    DAO_SERVO_STATE_RUNNING)
                {
                    motionController_.MarkMoving();
                }
            }
            else if (!isForce && !isPosition &&
                beforeObservation.motionState ==
                    UTM_MOTION_STATE_COMMAND_SENT &&
                issued.commandObserved != 0 &&
                servoCommand.commandType ==
                    DAO_SERVO_CMD_VELOCITY &&
                servoCommand.commandState ==
                    DAO_SERVO_STATE_RUNNING)
            {
                motionController_.MarkMoving();
            }

            const UtmGeneralMotionRuntimeInfo generalRuntime =
                motionController_.GetRuntime();

            if (generalMotionActive &&
                issued.activeType == generalMotionRequest.type)
            {
                if (generalRuntime.motionState ==
                    UTM_MOTION_STATE_PREPARING)
                {
                    motionController_.MarkCommandSent();
                }
            }
            else if (!isForce && generalMotionActive &&
                motionOutput.IsStopComplete())
            {
                generalMotionActive = false;
                generalMotionRequest = {};
                motionController_.MarkFailed(
                    UTM_MOTION_FAILURE_COMMAND_REJECTED);
                stateMachine_.CompleteGeneralMotion();
            }
        }

        if (generalMotionStopping &&
            motionOutput.IsStopComplete())
        {
            generalMotionStopping = false;
            generalMotionRequest = {};
            if (generalStopDisposition == 1)
                motionController_.MarkCompleted();
            else if (generalStopDisposition == 3)
                motionController_.MarkFailed(generalStopReason);
            else
                motionController_.MarkAborted(generalStopReason);

            if (stopLatch_.Get().latched == 0)
            {
                stateMachine_.AbortGeneralMotionToReady();
            }
        }

        (void)stateMachine_.TryReturnReadyFromJog(
            jog.sourceMask == 0,
            motionOutput.IsStopComplete());

        const UtmMotionOutputRuntime& motionRuntime =
            motionOutput.GetRuntime();
        if(communicationRuntime.motionInterrupted&&alignedCommunicationIncident==observedCommunicationIncident&&
            motionOutput.IsStopComplete())communicationRuntime.safeAlignmentResult=2;

        jogRuntime.jogActive =
            motionOutput.IsMotionActive() &&
            motionRuntime.activeType == UTM_MOTION_JOG
                ? 1
                : 0;
        jogRuntime.jogDirection =
            jogRuntime.jogActive != 0
                ? motionRuntime.activeDirection
                : UTM_DIRECTION_NONE;
        jogRuntime.jogSpeedMmPerMin =
            jogRuntime.jogActive != 0
                ? jog.speedMmPerMin
                : 0.0;
        jogRuntime.jogConflict = jog.conflict;
        jogRuntime.activeJogSourceMask =
            jog.sourceMask;
        jogRuntime.requestedMotionDirection =
            jog.direction;
        jogRuntime.servoTargetVelocity =
            motionRuntime.servoTargetVelocity;

        const auto cycleEnd = Clock::now();
        const auto cycleDuration =
            std::chrono::duration_cast<
                std::chrono::nanoseconds>(
                    cycleEnd - cycleStart);

        runtime.publishedTimestampNs =
            timestampNs;
        runtime.controlCycle = cycle;
        runtime.machineState =
            stateMachine_.GetState();
        runtime.input = snapshot;
        runtime.stop = stopLatch_.Get();
        runtime.commandEpoch =
            commandEpoch_.load();
        runtime.lastCycleTimeNs =
            static_cast<unsigned long long>(
                cycleDuration.count());
        runtime.maximumCycleTimeNs =
            std::max(
                runtime.maximumCycleTimeNs,
                runtime.lastCycleTimeNs);

        if (cycleEnd > nextWakeTime)
        {
            ++runtime.cycleOverrunCount;
        }

        UtmSequenceRuntimeInfo sequenceRuntime = sequencer_.GetRuntime();
        sequenceRuntime.stopCondition = stopConditionMonitor_.GetRuntime();
        runtimeStore_.Publish(
            runtime,
            jogRuntime,
            startup,
            motionController_.GetRuntime(),
            forceRuntime,
            sequenceRuntime);
        runtimeStore_.PublishCommunication(communicationRuntime);

        std::this_thread::sleep_until(
            nextWakeTime);
    }

    jogController_.ClearCommandRequests();
    commandMailbox_.Clear();
    (void)DaoEngine_SetIoOutputCommand(config_.logicalIoIndex,
        static_cast<unsigned short>(lastServoCommand.ioOutputCommand & 0xff00U));
    stateMachine_.BeginShutdown();

    auto publishShutdown = [this, &runtime, &jogRuntime, &startup]()
    {
        runtime.machineState = stateMachine_.GetState();
        runtime.publishedTimestampNs = GetSteadyTimestampNs();
        runtimeStore_.Publish(runtime, jogRuntime, startup);
    };

    const auto shutdownDeadline =
        Clock::now() +
        std::chrono::milliseconds(
            startupConfig_.shutdownTimeoutMs);

    if (inputCollector_ != nullptr &&
        DaoEngine_IsCommunicationRunning() == 1)
    {
        startup.startupPhase =
            UTM_STARTUP_SHUTDOWN_MOTION_STOP;
        publishShutdown();

        UtmMotionRequest noMotion{};

        do
        {
            UtmInputSnapshot snapshot{};
            UtmServoCommandSnapshot servoCommand{};
            (void)inputCollector_->Capture(
                ++cycle,
                GetSteadyTimestampNs(),
                snapshot,
                servoCommand);
            lastSnapshot = snapshot;
            lastServoCommand = servoCommand;

            motionOutput.Update(
                noMotion,
                true,
                servoCommand);

            if (motionOutput.IsStopComplete())
            {
                break;
            }

            std::this_thread::sleep_for(period);
        }
        while (Clock::now() < shutdownDeadline);

        startup.startupPhase =
            UTM_STARTUP_SHUTDOWN_SERVO_OFF;
        publishShutdown();

        if (lastSnapshot.servoOn != 0)
        {
            (void)DaoEngine_ServoOff(
                config_.logicalServoIndex);

            while (Clock::now() < shutdownDeadline)
            {
                UtmInputSnapshot snapshot{};
                UtmServoCommandSnapshot servoCommand{};
                (void)inputCollector_->Capture(
                    ++cycle,
                    GetSteadyTimestampNs(),
                    snapshot,
                    servoCommand);
                lastSnapshot = snapshot;

                if (snapshot.servoOn == 0)
                {
                    break;
                }

                std::this_thread::sleep_for(period);
            }
        }
    }

    startup.startupPhase =
        UTM_STARTUP_SHUTDOWN_COMMUNICATION;
    publishShutdown();
    CleanupBasicEngine();
    inputCollector_.reset();

    startup.startupComplete = 0;
    startup.startupPhase =
        UTM_STARTUP_SHUTDOWN_COMPLETE;
    stateMachine_.CompleteShutdown();

    runtime.controlLoopRunning = 0;
    jogRuntime = {};
    runtime.machineState = stateMachine_.GetState();
    runtime.input = lastSnapshot;
    runtime.publishedTimestampNs =
        GetSteadyTimestampNs();
    runtimeStore_.Publish(runtime, jogRuntime, startup);

    controlLoopRunning_.store(false);
}

unsigned long long UtmEngineCore::GetSteadyTimestampNs()
{
    return static_cast<unsigned long long>(
        std::chrono::duration_cast<
            std::chrono::nanoseconds>(
                std::chrono::steady_clock::now()
                    .time_since_epoch())
            .count());
}
