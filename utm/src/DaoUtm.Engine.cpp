#include "DaoUtm.Engine.h"

#include "UtmEngineCore.h"
#include "DaoEtherCAT.Engine.h"

#include <cstring>
#include <cstdio>

namespace
{
    UtmEngineCore g_utmEngine;
}

const char* DaoUtm_GetVersion()
{
    return "1.7.0-local-hmi";
}

const char* DaoUtm_GetBasicEngineVersion()
{
    return DaoEngine_GetVersion();
}

int DaoUtm_GetAvailableAdapters(UtmAdapterInfo* adapters,
    unsigned int capacity, unsigned int* adapterCount)
{
    if (adapterCount == nullptr || (capacity > 0 && adapters == nullptr)) return 0;
    unsigned int count = 0;
    const bool result = g_utmEngine.GetAvailableAdapters(adapters, capacity, count);
    *adapterCount = count;
    return result ? 1 : 0;
}

int DaoUtm_Connect(const UtmConnectionConfig* config)
{
    if (config == nullptr)
    {
        std::fprintf(stderr,"[UTM] DaoUtm_Connect failed function=argument-check return=0 startupFault=0 adapterName=<null>\n");
        return 0;
    }
    const char* adapter=config->startup.ethercatAdapterName;
    std::fprintf(stderr,"[UTM] DaoUtm_Connect begin adapterName=%s autoServoOn=%d\n",adapter,config->startup.autoServoOn);
    const UtmJogConfig& jog=config->jog.config;
    std::fprintf(stderr,"[UTM] ConfigureJogV2 values servoUnitsPerMm=%.15g servoDirectionSign=%d physicalJogSpeedMmPerMin=%.15g minJogSpeedMmPerMin=%.15g maxJogSpeedMmPerMin=%.15g acceleration=%u deceleration=%u\n",
        jog.servoUnitsPerMm,jog.servoDirectionSign,jog.physicalJogSpeedMmPerMin,
        config->jog.minJogSpeedMmPerMin,config->jog.maxJogSpeedMmPerMin,
        jog.acceleration,jog.deceleration);
    if (DaoUtm_IsInitialized())
    {
        std::fprintf(stderr,"[UTM] DaoUtm_Connect failed function=DaoUtm_IsInitialized return=1 startupFault=0 adapterName=%s\n",adapter);
        return 0;
    }
    const bool initialized=g_utmEngine.InitializeV2(config->startup);
    std::fprintf(stderr,"[UTM] InitializeV2 result=%d adapterName=%s autoServoOn=%d\n",initialized?1:0,adapter,config->startup.autoServoOn);
    if (!initialized)
    {
        UtmRuntimeInfoV3 runtime{};const bool runtimeResult=g_utmEngine.GetRuntimeV3(runtime);
        std::fprintf(stderr,"[UTM] DaoUtm_Connect failed function=InitializeV2 return=0 runtime=%d startupPhase=%d startupFault=%d adapterName=%s\n",
            runtimeResult?1:0,runtime.startup.startupPhase,runtime.startup.startupFault,adapter);
        return 0;
    }
    const bool jogConfigured=g_utmEngine.ConfigureJogV2(config->jog);
    const bool forceConfigured=jogConfigured&&g_utmEngine.ConfigureForceControl(config->force);
    const bool protectionConfigured=forceConfigured&&g_utmEngine.ConfigureMachineProtection(config->protection);
    const bool started=protectionConfigured&&g_utmEngine.Start();
    std::fprintf(stderr,"[UTM] connect stages jog=%d force=%d protection=%d start=%d adapterName=%s\n",
        jogConfigured?1:0,forceConfigured?1:0,protectionConfigured?1:0,started?1:0,adapter);
    if(!started)
    {
        UtmRuntimeInfoV3 runtime{};const bool runtimeResult=g_utmEngine.GetRuntimeV3(runtime);
        std::fprintf(stderr,"[UTM] DaoUtm_Connect failed function=%s return=0 runtime=%d startupPhase=%d startupFault=%d adapterName=%s\n",
            !jogConfigured?"ConfigureJogV2":(!forceConfigured?"ConfigureForceControl":(!protectionConfigured?"ConfigureMachineProtection":"Start")),
            runtimeResult?1:0,runtime.startup.startupPhase,runtime.startup.startupFault,adapter);
        g_utmEngine.Shutdown();return 0;
    }
    std::fprintf(stderr,"[UTM] DaoUtm_Connect result=1 startup lifecycle running adapterName=%s\n",adapter);
    return 1;
}

void DaoUtm_Disconnect()
{
    g_utmEngine.Shutdown();
}

int DaoUtm_Initialize(
    const UtmEngineConfig* config)
{
    if (config == nullptr)
    {
        return 0;
    }

    return g_utmEngine.Initialize(*config)
        ? 1
        : 0;
}

int DaoUtm_InitializeV2(
    const UtmStartupConfig* config)
{
    if (config == nullptr)
    {
        return 0;
    }

    return g_utmEngine.InitializeV2(*config)
        ? 1
        : 0;
}

int DaoUtm_Start()
{
    return g_utmEngine.Start()
        ? 1
        : 0;
}

void DaoUtm_Stop()
{
    g_utmEngine.Stop();
}

void DaoUtm_Shutdown()
{
    g_utmEngine.Shutdown();
}

int DaoUtm_IsInitialized()
{
    return g_utmEngine.IsInitialized()
        ? 1
        : 0;
}

int DaoUtm_IsRunning()
{
    return g_utmEngine.IsRunning()
        ? 1
        : 0;
}

int DaoUtm_SubmitCommand(
    const UtmCommandRequest* request)
{
    if (request == nullptr)
    {
        return 0;
    }

    return g_utmEngine.SubmitCommand(*request)
        ? 1
        : 0;
}

void DaoUtm_RequestUserStop()
{
    g_utmEngine.RequestUserStop();
}

int DaoUtm_AcknowledgeStop()
{
    return g_utmEngine.AcknowledgeStop()
        ? 1
        : 0;
}

int DaoUtm_GetRuntime(
    UtmRuntimeInfo* runtime)
{
    if (runtime == nullptr)
    {
        return 0;
    }

    UtmRuntimeInfo localRuntime{};

    const bool result =
        g_utmEngine.GetRuntime(
            localRuntime);

    std::memcpy(
        runtime,
        &localRuntime,
        sizeof(UtmRuntimeInfo));

    return result ? 1 : 0;
}

int DaoUtm_ConfigureJog(
    const UtmJogConfig* config)
{
    if (config == nullptr)
    {
        return 0;
    }

    return g_utmEngine.ConfigureJog(*config)
        ? 1
        : 0;
}

int DaoUtm_ConfigureJogV2(
    const UtmJogConfigV2* config)
{
    if (config == nullptr)
    {
        return 0;
    }

    return g_utmEngine.ConfigureJogV2(*config)
        ? 1
        : 0;
}

int DaoUtm_StartJog(
    int direction,
    double speedMmPerMin,
    int commandSource)
{
    return g_utmEngine.StartJog(
        direction,
        speedMmPerMin,
        commandSource)
        ? 1
        : 0;
}

int DaoUtm_StopJog(
    int commandSource)
{
    return g_utmEngine.StopJog(commandSource)
        ? 1
        : 0;
}

int DaoUtm_GetRuntimeV2(
    UtmRuntimeInfoV2* runtime)
{
    if (runtime == nullptr)
    {
        return 0;
    }

    UtmRuntimeInfoV2 localRuntime{};
    const bool result =
        g_utmEngine.GetRuntimeV2(localRuntime);

    std::memcpy(
        runtime,
        &localRuntime,
        sizeof(UtmRuntimeInfoV2));

    return result ? 1 : 0;
}

int DaoUtm_GetRuntimeV3(
    UtmRuntimeInfoV3* runtime)
{
    if (runtime == nullptr)
    {
        return 0;
    }

    UtmRuntimeInfoV3 localRuntime{};
    const bool result =
        g_utmEngine.GetRuntimeV3(localRuntime);

    std::memcpy(
        runtime,
        &localRuntime,
        sizeof(UtmRuntimeInfoV3));

    return result ? 1 : 0;
}

int DaoUtm_MoveAbsolute(
    double targetTestPositionMm,
    double speedMmPerMin,
    unsigned int acceleration,
    unsigned int deceleration,
    unsigned int timeoutMs,
    int commandSource)
{
    return g_utmEngine.MoveAbsolute(
        targetTestPositionMm, speedMmPerMin,
        acceleration, deceleration, timeoutMs,
        commandSource) ? 1 : 0;
}

int DaoUtm_GetCommunicationRuntimeV1(UtmCommunicationRuntimeInfoV1* runtime)
{
    if(runtime==nullptr)return 0;UtmCommunicationRuntimeInfoV1 local{};
    const bool result=g_utmEngine.GetCommunicationRuntime(local);*runtime=local;return result?1:0;
}

int DaoUtm_MoveIncremental(
    double incrementalDistanceMm,
    double speedMmPerMin,
    unsigned int acceleration,
    unsigned int deceleration,
    unsigned int timeoutMs,
    int commandSource)
{
    return g_utmEngine.MoveIncremental(
        incrementalDistanceMm, speedMmPerMin,
        acceleration, deceleration, timeoutMs,
        commandSource) ? 1 : 0;
}

int DaoUtm_MoveVelocity(
    int direction,
    double speedMmPerMin,
    unsigned int acceleration,
    unsigned int deceleration,
    int commandSource)
{
    return g_utmEngine.MoveVelocity(
        direction, speedMmPerMin,
        acceleration, deceleration,
        commandSource) ? 1 : 0;
}

int DaoUtm_StopMotion(int commandSource)
{
    return g_utmEngine.StopMotion(commandSource) ? 1 : 0;
}

int DaoUtm_GetRuntimeV4(UtmRuntimeInfoV4* runtime)
{
    if (runtime == nullptr)
    {
        return 0;
    }

    UtmRuntimeInfoV4 localRuntime{};
    const bool result = g_utmEngine.GetRuntimeV4(localRuntime);
    std::memcpy(runtime, &localRuntime, sizeof(UtmRuntimeInfoV4));
    return result ? 1 : 0;
}

int DaoUtm_SetPositionZero()
{
    return g_utmEngine.SetPositionZero() ? 1 : 0;
}

int DaoUtm_ClearPositionZero()
{
    return g_utmEngine.ClearPositionZero() ? 1 : 0;
}

int DaoUtm_ZeroForce()
{
    return g_utmEngine.ZeroForce() ? 1 : 0;
}

int DaoUtm_ZeroEncoder(unsigned int timeoutMs)
{
    return g_utmEngine.ZeroEncoder(timeoutMs) ? 1 : 0;
}

int DaoUtm_CalibrateForce(double referenceForceN)
{
    return g_utmEngine.CalibrateForce(referenceForceN) ? 1 : 0;
}

int DaoUtm_SetForceCalibrationScale(double scale)
{
    return g_utmEngine.SetForceCalibrationScale(scale) ? 1 : 0;
}

int DaoUtm_SetEncoderCalibrationScale(double scale)
{
    return g_utmEngine.SetEncoderCalibrationScale(scale) ? 1 : 0;
}

int DaoUtm_GetCalibrationRuntime(UtmCalibrationRuntimeInfo* runtime)
{
    if (runtime == nullptr) return 0;
    UtmCalibrationRuntimeInfo local{};
    const bool result = g_utmEngine.GetCalibrationRuntime(local);
    std::memcpy(runtime, &local, sizeof(local));
    return result ? 1 : 0;
}

int DaoUtm_ConfigureAdcFilters(const UtmAdcFilterConfig* config)
{ return config != nullptr && g_utmEngine.ConfigureAdcFilters(*config) ? 1 : 0; }

int DaoUtm_CalibrateEncoder(double referenceDisplacementMm)
{ return g_utmEngine.CalibrateEncoder(referenceDisplacementMm) ? 1 : 0; }

int DaoUtm_ConfigureDisplayForceAverage(unsigned int sampleCount)
{ return g_utmEngine.ConfigureDisplayForceAverage(sampleCount) ? 1 : 0; }

int DaoUtm_GetDisplayForce(double* forceN,int* valid,unsigned int* sampleCount)
{
    if(!forceN||!valid||!sampleCount)return 0;bool localValid=false;
    g_utmEngine.GetDisplayForce(*forceN,localValid,*sampleCount);*valid=localValid?1:0;return 1;
}

int DaoUtm_GetDisplayForceRuntime(UtmDisplayForceRuntimeInfo* runtime)
{
    if(!runtime)return 0;UtmDisplayForceRuntimeInfo local{};
    g_utmEngine.GetDisplayForceRuntime(local);std::memcpy(runtime,&local,sizeof(local));return 1;
}

int DaoUtm_ConfigureForceControl(const UtmForceControlConfig* config)
{
    return config != nullptr && g_utmEngine.ConfigureForceControl(*config) ? 1 : 0;
}

int DaoUtm_SubmitMotion(const UtmMotionCommandV2* command)
{
    return command != nullptr && g_utmEngine.SubmitMotion(*command) ? 1 : 0;
}

int DaoUtm_MoveToForce(int direction, double targetForceN,
    double speedMmPerMin, unsigned int acceleration,
    unsigned int deceleration, double maxTravelMm,
    unsigned int timeoutMs, int commandSource)
{
    UtmMotionCommandV2 command{};
    command.motionType = UTM_MOTION_MOVE_TO_FORCE;
    command.direction = direction;
    command.targetForceN = targetForceN;
    command.speedMmPerMin = speedMmPerMin;
    command.acceleration = acceleration;
    command.deceleration = deceleration;
    command.maxTravelMm = maxTravelMm;
    command.timeoutMs = timeoutMs;
    command.source = commandSource;
    return g_utmEngine.SubmitMotion(command) ? 1 : 0;
}

int DaoUtm_HoldForce(int direction, double targetForceN,
    double holdTimeSec, double toleranceN, double maxTravelMm,
    unsigned int timeoutMs, int commandSource)
{
    UtmForceControlConfig config{};
    UtmMotionCommandV2 command{};
    command.motionType = UTM_MOTION_HOLD_FORCE;
    command.direction = direction;
    command.targetForceN = targetForceN;
    command.holdTimeSec = holdTimeSec;
    command.toleranceN = toleranceN;
    command.maxTravelMm = maxTravelMm;
    command.timeoutMs = timeoutMs;
    command.speedMmPerMin = config.approachSpeedMmPerMin;
    command.acceleration = config.acceleration;
    command.deceleration = config.deceleration;
    command.source = commandSource;
    return g_utmEngine.SubmitMotion(command) ? 1 : 0;
}

int DaoUtm_GetRuntimeV5(UtmRuntimeInfoV5* runtime)
{
    if (runtime == nullptr) return 0;
    UtmRuntimeInfoV5 local{};
    const bool result = g_utmEngine.GetRuntimeV5(local);
    std::memcpy(runtime, &local, sizeof(local));
    return result ? 1 : 0;
}

int DaoUtm_ConfigureMachineProtection(
    const UtmMachineProtectionConfig* config)
{
    return config != nullptr && g_utmEngine.ConfigureMachineProtection(*config) ? 1 : 0;
}

int DaoUtm_LoadSequence(const UtmSequenceDefinition* definition)
{
    return definition != nullptr && g_utmEngine.LoadSequence(*definition) ? 1 : 0;
}

int DaoUtm_ValidateSequence()
{
    return g_utmEngine.ValidateSequence() ? 1 : 0;
}

int DaoUtm_CommitSequence()
{
    return g_utmEngine.CommitSequence() ? 1 : 0;
}

int DaoUtm_StartSequence()
{
    return g_utmEngine.StartSequence() ? 1 : 0;
}

int DaoUtm_StopSequence()
{
    return g_utmEngine.StopSequence() ? 1 : 0;
}

int DaoUtm_GetRuntimeV6(UtmRuntimeInfoV6* runtime)
{
    if (runtime == nullptr) return 0;
    UtmRuntimeInfoV6 local{};
    const bool result = g_utmEngine.GetRuntimeV6(local);
    std::memcpy(runtime, &local, sizeof(local));
    return result ? 1 : 0;
}

int DaoUtm_StartCompliancePrecheck(
    const UtmComplianceCalibrationConfigV1* config,
    unsigned long long* sessionId)
{
    return config != nullptr && sessionId != nullptr &&
        g_utmEngine.StartCompliancePrecheck(*config, *sessionId) ? 1 : 0;
}

int DaoUtm_ConfirmComplianceFullCalibration(unsigned long long sessionId)
{ return g_utmEngine.ConfirmComplianceFullCalibration(sessionId) ? 1 : 0; }

int DaoUtm_AbortComplianceCalibration(unsigned long long sessionId)
{ return g_utmEngine.AbortComplianceCalibration(sessionId) ? 1 : 0; }

int DaoUtm_GetComplianceCalibrationRuntimeV1(
    UtmComplianceCalibrationRuntimeV1* runtime)
{
    if (runtime == nullptr) return 0;
    UtmComplianceCalibrationRuntimeV1 local{};
    const bool result = g_utmEngine.GetComplianceCalibrationRuntime(local);
    std::memcpy(runtime, &local, sizeof(local));
    return result ? 1 : 0;
}

int DaoUtm_GetCompliancePendingPoints(unsigned long long sessionId,
    UtmComplianceCalibrationPoint* points, unsigned int capacity,
    unsigned int* pointCount)
{
    if (pointCount == nullptr || (capacity > 0 && points == nullptr)) return 0;
    unsigned int count = 0;
    const bool result = g_utmEngine.GetCompliancePendingPoints(
        sessionId, points, capacity, count);
    *pointCount = count;
    return result ? 1 : 0;
}

int DaoUtm_DiscardCompliancePending(unsigned long long sessionId)
{ return g_utmEngine.DiscardCompliancePending(sessionId) ? 1 : 0; }

int DaoUtm_RetryStartup()
{
    return g_utmEngine.RetryStartup()
        ? 1
        : 0;
}
