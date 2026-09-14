#include "UtmInputCollector.h"

#include "DaoEtherCAT.Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

UtmInputCollector::UtmInputCollector(
    const UtmEngineConfig& config)
    : config_(config)
{
}

bool UtmInputCollector::Capture(
    unsigned long long cycle,
    unsigned long long timestampNs,
    UtmInputSnapshot& snapshot,
    UtmServoCommandSnapshot& servoCommand)
{
    snapshot = {};
    servoCommand = {};
    snapshot.cycle = cycle;
    snapshot.timestampNs = timestampNs;

    snapshot.basicCommunicationRunning =
        DaoEngine_IsCommunicationRunning() == 1 ? 1 : 0;
    const bool recoveryRead=DaoEngine_GetCommunicationRecoveryRuntimeV1(&basicRecovery_)==1;

    DaoServoRuntimeInfo servo{};
    DaoAdcRuntimeInfoV2 adc{};
    DaoIoRuntimeInfo io{};
    DaoEncoderRuntimeInfo encoder{};

    const bool servoRead =
        DaoEngine_GetServoRuntimeInfo(
            config_.logicalServoIndex,
            &servo) == 1;

    const bool adcRead =
        DaoEngine_GetAdcRuntimeInfoV2(
            config_.logicalAdcIndex,
            &adc) == 1;

    const bool ioRead =
        DaoEngine_GetIoRuntimeInfo(
            config_.logicalIoIndex,
            &io) == 1;

    const bool encoderRead =
        config_.useOptionalEncoder != 0 &&
        DaoEngine_GetEncoderRuntimeInfo(
            config_.logicalEncoderIndex,
            &encoder) == 1;

    const bool servoInputValid =
        servoRead &&
        servo.configured != 0 &&
        servo.communicationRunning != 0 &&
        servo.hasValidInputData != 0 &&
        servo.lastWkc >= servo.expectedWkc;

    const bool adcInputValid =
        adcRead &&
        adc.communicationRunning != 0 &&
        adc.hasValidData != 0 &&
        adc.lastWkc >= adc.expectedWkc;

    const bool ioInputValid =
        ioRead &&
        io.configured != 0 &&
        io.communicationRunning != 0 &&
        io.hasValidInputData != 0 &&
        io.lastWkc >= io.expectedWkc;

    const bool encoderInputValid =
        encoderRead &&
        encoder.configured != 0 &&
        encoder.communicationRunning != 0 &&
        encoder.hasValidInputData != 0 &&
        encoder.lastWkc >= encoder.expectedWkc;

    UpdateFreshness(
        servoState_,
        servoRead,
        servoInputValid,
        servo.inputUpdateCount,
        timestampNs,
        snapshot.servoSource);

    UpdateFreshness(
        adcState_,
        adcRead,
        adcInputValid,
        adc.dataUpdateCount,
        timestampNs,
        snapshot.adcSource);

    UpdateFreshness(
        ioState_,
        ioRead,
        ioInputValid,
        io.inputUpdateCount,
        timestampNs,
        snapshot.ioSource);

    UpdateFreshness(
        encoderState_,
        encoderRead,
        encoderInputValid,
        encoder.inputUpdateCount,
        timestampNs,
        snapshot.encoderSource);

    if (servoRead)
    {
        servoCommand.runtimeRead = 1;
        servoCommand.commandId = servo.commandId;
        servoCommand.commandType = servo.commandType;
        servoCommand.commandState = servo.commandState;
        servoCommand.commandStep = servo.commandStep;
        servoCommand.commandResult = servo.commandResult;
        servoCommand.outputTargetVelocity =
            servo.outputCommand.targetVelocity;
        servoCommand.outputTargetPosition =
            servo.outputCommand.targetPosition;

        snapshot.servoActualPosition =
            servo.latestInput.actualPosition;

        snapshot.servoPositionValid =
            snapshot.servoSource.communicationValid;

        snapshot.servoCommunicationValid =
            snapshot.servoSource.communicationValid;

        snapshot.servoStatusWord =
            servo.latestInput.statusWord;

        snapshot.servoOperationState =
            servo.cia402State;

        snapshot.servoOperationMode =
            servo.latestInput.operationModeDisplay;

        snapshot.servoFault =
            servo.fault != 0 ||
            servo.cia402State == 0x0008;

        snapshot.servoOn =
            servo.operationEnabled != 0;

        snapshot.servoReady =
            snapshot.servoCommunicationValid != 0 &&
            snapshot.servoFault == 0 &&
            (servo.cia402State == 0x0021 ||
                servo.cia402State == 0x0023 ||
                servo.cia402State == 0x0027);

        snapshot.servoNegativeLimitInput =
            servo.negativeLimit;
        snapshot.servoPositiveLimitInput =
            servo.positiveLimit;
        snapshot.servoHomeInput =
            servo.homeSensor;
        snapshot.servoStopInput =
            servo.stopInput;
        snapshot.servoStoActive =
            servo.stoActive;
    }

    if (adcRead)
    {
        // UTM 내부 Force 표준 단위는 항상 N입니다.
        snapshot.forceN = adc.engineeringValue;
        snapshot.forceValid =
            adc.engineeringValueValid != 0 ? 1 : 0;
        servoCommand.adcStableCaptureActive = adc.stableCaptureActive;
        servoCommand.adcStableCaptureType = adc.stableCaptureType;
        servoCommand.adcStableCaptureCollectedCount =
            adc.stableCaptureCollectedCount;
    }

    if (ioRead)
    {
        snapshot.rawDigitalInputs = io.latestInput;
        servoCommand.ioOutputCommand = io.outputCommand;
        servoCommand.logicalDigitalInputs = static_cast<unsigned short>(
            io.latestInput ^ config_.ioMapping.activeLowMask);

        snapshot.emergency =
            ReadLogicalInput(
                io.latestInput,
                config_.ioMapping.emergencyBit) ? 1 : 0;

        snapshot.upperLimit =
            ReadLogicalInput(
                io.latestInput,
                config_.ioMapping.upperLimitBit) ? 1 : 0;

        snapshot.lowerLimit =
            ReadLogicalInput(
                io.latestInput,
                config_.ioMapping.lowerLimitBit) ? 1 : 0;

        snapshot.jogUp =
            ReadLogicalInput(
                io.latestInput,
                config_.ioMapping.jogUpBit) ? 1 : 0;

        snapshot.jogDown =
            ReadLogicalInput(
                io.latestInput,
                config_.ioMapping.jogDownBit) ? 1 : 0;

        snapshot.externalGo =
            ReadLogicalInput(
                io.latestInput,
                config_.ioMapping.externalGoBit) ? 1 : 0;

        snapshot.externalStop =
            ReadLogicalInput(
                io.latestInput,
                config_.ioMapping.externalStopBit) ? 1 : 0;
    }

    snapshot.encoderPresent =
        config_.useOptionalEncoder != 0 ? 1 : 0;

    if (encoderRead)
    {
        snapshot.encoderValid =
            snapshot.encoderSource.communicationValid;

        if (config_.encoderChannel == 1)
        {
            snapshot.encoderSignedCount =
                encoder.signedCountCh1;
            snapshot.encoderPosition =
                encoder.engineeringValueCh1;
            servoCommand.encoderResetState = encoder.resetStateCh1;
            servoCommand.encoderResetCompleted = encoder.resetCompletedStatusCh1;
        }
        else
        {
            snapshot.encoderSignedCount =
                encoder.signedCountCh2;
            snapshot.encoderPosition =
                encoder.engineeringValueCh2;
            servoCommand.encoderResetState = encoder.resetStateCh2;
            servoCommand.encoderResetCompleted = encoder.resetCompletedStatusCh2;
        }
    }

    snapshot.requiredDevicesValid =
        servoRead && adcRead && ioRead ? 1 : 0;

    snapshot.communicationValid =
        snapshot.basicCommunicationRunning != 0&&servoState_.observed&&adcState_.observed&&ioState_.observed ? 1 : 0;

    const auto previousState=communicationState_;
    if(!recoveryRead||snapshot.basicCommunicationRunning==0)communicationState_=FAULT;
    else switch(basicRecovery_.communicationState){case DAO_COMMUNICATION_TRANSIENT:communicationState_=TRANSIENT;break;case DAO_COMMUNICATION_DEGRADED:communicationState_=DEGRADED;break;case DAO_COMMUNICATION_RECOVERING:communicationState_=RECOVERING;break;case DAO_COMMUNICATION_FAILED:communicationState_=FAULT;break;default:communicationState_=NORMAL;break;}
    if(communicationState_==FAULT)snapshot.communicationValid=0;
    if(previousState!=communicationState_)std::fprintf(stderr,"[ECAT COMM] UTM observes Basic state %d -> %d generation=%llu\n",previousState,communicationState_,basicRecovery_.incidentGeneration);

    return snapshot.requiredDevicesValid != 0;
}

void UtmInputCollector::UpdateFreshness(
    SourceState& state,
    bool runtimeRead,
    bool validInput,
    unsigned long long updateCount,
    unsigned long long timestampNs,
    UtmSourceFreshness& freshness)
{
    freshness.runtimeRead = runtimeRead ? 1 : 0;
    freshness.validInput = validInput ? 1 : 0;
    state.consecutiveInvalid=validInput?0:state.consecutiveInvalid+1;
    freshness.updateCount = updateCount;

    // EtherCAT Thread와 UTM Thread는 동기 위상이 아니므로 같은 Count를
    // 다시 읽거나 여러 Count를 건너뛰는 경우를 모두 정상으로 처리합니다.
    if (runtimeRead && validInput &&
        (!state.observed ||
            updateCount != state.lastObservedUpdateCount))
    {
        state.observed = true;
        state.lastObservedUpdateCount = updateCount;
        state.lastFreshTimestampNs = timestampNs;
        freshness.freshInput = 1;
    }

    freshness.lastFreshTimestampNs =
        state.lastFreshTimestampNs;

    if (state.lastFreshTimestampNs > 0 &&
        timestampNs >= state.lastFreshTimestampNs)
    {
        freshness.staleDurationNs =
            timestampNs - state.lastFreshTimestampNs;
    }

    const unsigned long long staleTimeoutNs =
        static_cast<unsigned long long>(
            config_.staleInputTimeoutMs) *
        1000000ULL;

    freshness.communicationValid =
        runtimeRead &&
        state.lastFreshTimestampNs > 0 &&
        freshness.staleDurationNs <= staleTimeoutNs
        ? 1
        : 0;
}

bool UtmInputCollector::ReadLogicalInput(
    unsigned short rawInputs,
    unsigned char bit) const
{
    if (bit >= 16)
    {
        return false;
    }

    const unsigned short mask =
        static_cast<unsigned short>(
            1U << bit);

    const bool rawState =
        (rawInputs & mask) != 0;

    const bool activeLow =
        (config_.ioMapping.activeLowMask & mask) != 0;

    return activeLow ? !rawState : rawState;
}
