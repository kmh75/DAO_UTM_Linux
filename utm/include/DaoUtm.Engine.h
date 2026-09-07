#pragma once

#include "DaoUtm.Types.h"

#if defined(_WIN32)
    #if defined(DAO_UTM_ENGINE_EXPORTS)
        #define DAO_UTM_API __declspec(dllexport)
    #else
        #define DAO_UTM_API __declspec(dllimport)
    #endif
#else
    #if defined(DAO_UTM_ENGINE_EXPORTS)
        #define DAO_UTM_API \
            __attribute__((visibility("default")))
    #else
        #define DAO_UTM_API
    #endif
#endif

extern "C"
{
    DAO_UTM_API const char* DaoUtm_GetVersion();
    DAO_UTM_API const char* DaoUtm_GetBasicEngineVersion();
    DAO_UTM_API int DaoUtm_GetAvailableAdapters(
        UtmAdapterInfo* adapters,
        unsigned int capacity,
        unsigned int* adapterCount);
    DAO_UTM_API int DaoUtm_Connect(const UtmConnectionConfig* config);
    DAO_UTM_API void DaoUtm_Disconnect();

    DAO_UTM_API int DaoUtm_Initialize(
        const UtmEngineConfig* config);

    DAO_UTM_API int DaoUtm_InitializeV2(
        const UtmStartupConfig* config);

    DAO_UTM_API int DaoUtm_Start();
    DAO_UTM_API void DaoUtm_Stop();
    DAO_UTM_API void DaoUtm_Shutdown();

    DAO_UTM_API int DaoUtm_IsInitialized();
    DAO_UTM_API int DaoUtm_IsRunning();

    DAO_UTM_API int DaoUtm_SubmitCommand(
        const UtmCommandRequest* request);

    DAO_UTM_API void DaoUtm_RequestUserStop();
    DAO_UTM_API int DaoUtm_AcknowledgeStop();

    DAO_UTM_API int DaoUtm_GetRuntime(
        UtmRuntimeInfo* runtime);

    DAO_UTM_API int DaoUtm_ConfigureJog(
        const UtmJogConfig* config);

    DAO_UTM_API int DaoUtm_ConfigureJogV2(
        const UtmJogConfigV2* config);

    DAO_UTM_API int DaoUtm_StartJog(
        int direction,
        double speedMmPerMin,
        int commandSource);

    DAO_UTM_API int DaoUtm_StopJog(
        int commandSource);

    DAO_UTM_API int DaoUtm_GetRuntimeV2(
        UtmRuntimeInfoV2* runtime);

    DAO_UTM_API int DaoUtm_GetRuntimeV3(
        UtmRuntimeInfoV3* runtime);

    DAO_UTM_API int DaoUtm_MoveAbsolute(
        double targetTestPositionMm,
        double speedMmPerMin,
        unsigned int acceleration,
        unsigned int deceleration,
        unsigned int timeoutMs,
        int commandSource);

    DAO_UTM_API int DaoUtm_MoveIncremental(
        double incrementalDistanceMm,
        double speedMmPerMin,
        unsigned int acceleration,
        unsigned int deceleration,
        unsigned int timeoutMs,
        int commandSource);

    DAO_UTM_API int DaoUtm_MoveVelocity(
        int direction,
        double speedMmPerMin,
        unsigned int acceleration,
        unsigned int deceleration,
        int commandSource);

    DAO_UTM_API int DaoUtm_StopMotion(
        int commandSource);

    DAO_UTM_API int DaoUtm_GetRuntimeV4(
        UtmRuntimeInfoV4* runtime);

    DAO_UTM_API int DaoUtm_SetPositionZero();
    DAO_UTM_API int DaoUtm_ClearPositionZero();
    DAO_UTM_API int DaoUtm_ZeroForce();
    DAO_UTM_API int DaoUtm_ZeroEncoder(unsigned int timeoutMs);
    DAO_UTM_API int DaoUtm_CalibrateForce(double referenceForceN);
    DAO_UTM_API int DaoUtm_SetForceCalibrationScale(double scale);
    DAO_UTM_API int DaoUtm_SetEncoderCalibrationScale(double scale);
    DAO_UTM_API int DaoUtm_GetCalibrationRuntime(
        UtmCalibrationRuntimeInfo* runtime);
    DAO_UTM_API int DaoUtm_ConfigureAdcFilters(const UtmAdcFilterConfig* config);
    DAO_UTM_API int DaoUtm_CalibrateEncoder(double referenceDisplacementMm);
    DAO_UTM_API int DaoUtm_ConfigureDisplayForceAverage(unsigned int sampleCount);
    DAO_UTM_API int DaoUtm_GetDisplayForce(double* forceN, int* valid,
        unsigned int* sampleCount);
    DAO_UTM_API int DaoUtm_GetDisplayForceRuntime(
        UtmDisplayForceRuntimeInfo* runtime);

    DAO_UTM_API int DaoUtm_ConfigureForceControl(
        const UtmForceControlConfig* config);

    DAO_UTM_API int DaoUtm_SubmitMotion(
        const UtmMotionCommandV2* command);

    DAO_UTM_API int DaoUtm_MoveToForce(
        int direction,
        double targetForceN,
        double speedMmPerMin,
        unsigned int acceleration,
        unsigned int deceleration,
        double maxTravelMm,
        unsigned int timeoutMs,
        int commandSource);

    DAO_UTM_API int DaoUtm_HoldForce(
        int direction,
        double targetForceN,
        double holdTimeSec,
        double toleranceN,
        double maxTravelMm,
        unsigned int timeoutMs,
        int commandSource);

    DAO_UTM_API int DaoUtm_GetRuntimeV5(
        UtmRuntimeInfoV5* runtime);

    DAO_UTM_API int DaoUtm_ConfigureMachineProtection(
        const UtmMachineProtectionConfig* config);
    DAO_UTM_API int DaoUtm_LoadSequence(
        const UtmSequenceDefinition* definition);
    DAO_UTM_API int DaoUtm_ValidateSequence();
    DAO_UTM_API int DaoUtm_CommitSequence();
    DAO_UTM_API int DaoUtm_StartSequence();
    DAO_UTM_API int DaoUtm_StopSequence();
    DAO_UTM_API int DaoUtm_GetRuntimeV6(
        UtmRuntimeInfoV6* runtime);

    DAO_UTM_API int DaoUtm_RetryStartup();
}
