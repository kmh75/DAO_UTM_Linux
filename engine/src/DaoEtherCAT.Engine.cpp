#include "DaoEtherCAT.Engine.h"
#include "DaoEngineCore.h"
#include <cstring>
#include <vector>

namespace
{
    DaoEngineCore g_engine;

    void CopyString(char* destination, std::size_t destinationSize, const char* source)
    {
        if (destination == nullptr || destinationSize == 0)
            return;

        if (source == nullptr)
        {
            destination[0] = '\0';
            return;
        }

        std::strncpy(destination, source, destinationSize - 1);
        destination[destinationSize - 1] = '\0';
    }
}

const char* DaoEngine_GetVersion()
{
    return "DAO EtherCAT Engine 0.1.0";
}

int DaoEngine_Initialize()
{
    return g_engine.Initialize() ? 1 : 0;
}

void DaoEngine_Shutdown()
{
    g_engine.Shutdown();
}

int DaoEngine_IsInitialized()
{
    return g_engine.IsInitialized() ? 1 : 0;
}

int DaoEngine_GetAdapterCount()
{
    return g_engine.GetAdapterCount();
}

int DaoEngine_GetAdapterInfo(
    int adapterIndex,
    DaoAdapterInfo* adapterInfo)
{
    if (adapterInfo == nullptr)
    {
        return 0;
    }

    DaoInternalAdapterInfo internalInfo;

    if (!g_engine.GetAdapterInfo(adapterIndex, internalInfo))
    {
        return 0;
    }

    std::memset(adapterInfo, 0, sizeof(DaoAdapterInfo));

    CopyString(
        adapterInfo->name,
        sizeof(adapterInfo->name),
        internalInfo.name.c_str());

    CopyString(
        adapterInfo->description,
        sizeof(adapterInfo->description),
        internalInfo.description.c_str());

    return 1;
}

int DaoEngine_OpenAdapter(
    int adapterIndex)
{
    return g_engine.OpenAdapter(adapterIndex)
        ? 1
        : 0;
}

void DaoEngine_CloseAdapter()
{
    g_engine.CloseAdapter();
}

int DaoEngine_IsAdapterOpen()
{
    return g_engine.IsAdapterOpen()
        ? 1
        : 0;
}

int DaoEngine_ScanSlaves()
{
    return g_engine.ScanSlaves();
}

int DaoEngine_GetSlaveCount()
{
    return g_engine.GetSlaveCount();
}



int DaoEngine_GetSlaveInfo(
    int slaveListIndex,
    DaoSlaveInfo* slaveInfo)
{
    if (slaveInfo == nullptr)
    {
        return 0;
    }

    DaoInternalSlaveInfo internalInfo{};

    if (!g_engine.GetSlaveInfo(
        slaveListIndex,
        internalInfo))
    {
        return 0;
    }

    std::memset(
        slaveInfo,
        0,
        sizeof(DaoSlaveInfo));

    slaveInfo->listIndex =
        internalInfo.listIndex;

    slaveInfo->physicalSlaveIndex =
        internalInfo.physicalSlaveIndex;

    slaveInfo->vendorId =
        internalInfo.vendorId;

    slaveInfo->productCode =
        internalInfo.productCode;

    slaveInfo->revision =
        internalInfo.revision;

    slaveInfo->state =
        internalInfo.state;

    slaveInfo->alStatusCode =
        internalInfo.alStatusCode;

    CopyString(
    slaveInfo->name,
    sizeof(slaveInfo->name),
    internalInfo.name.c_str());

    return 1;
}

int DaoEngine_GetLogicalDeviceCount(
    int deviceType)
{
    return g_engine.GetLogicalDeviceCount(
        deviceType);
}

int DaoEngine_GetLogicalDeviceInfo(
    int deviceType,
    int logicalIndex,
    DaoLogicalDeviceInfo* deviceInfo)
{
    if (deviceInfo == nullptr)
    {
        return 0;
    }

    DaoInternalLogicalDeviceInfo internalInfo{};

    if (!g_engine.GetLogicalDeviceInfo(
        deviceType,
        logicalIndex,
        internalInfo))
    {
        return 0;
    }

    std::memset(
        deviceInfo,
        0,
        sizeof(DaoLogicalDeviceInfo));

    deviceInfo->deviceType =
        internalInfo.deviceType;

    deviceInfo->logicalIndex =
        internalInfo.logicalIndex;

    deviceInfo->physicalSlaveIndex =
        internalInfo.physicalSlaveIndex;

    deviceInfo->vendorId =
        internalInfo.vendorId;

    deviceInfo->productCode =
        internalInfo.productCode;

    deviceInfo->revision =
        internalInfo.revision;

    CopyString(
    deviceInfo->name,
    sizeof(deviceInfo->name),
    internalInfo.name.c_str());

    return 1;
}

int DaoEngine_RequestAllSlavesPreOp()
{
    return g_engine.RequestAllSlavesPreOp()
        ? 1
        : 0;
}

int DaoEngine_RequestAllSlavesSafeOp()
{
    return g_engine.RequestAllSlavesSafeOp()
        ? 1
        : 0;
}

int DaoEngine_RequestAllSlavesOperational()
{
    return g_engine.RequestAllSlavesOperational()
        ? 1
        : 0;
}

int DaoEngine_RequestAllSlavesInit()
{
    return g_engine.RequestAllSlavesInit()
        ? 1
        : 0;
}

int DaoEngine_MapProcessData()
{
    return g_engine.MapProcessData()
        ? 1
        : 0;
}

int DaoEngine_GetProcessDataMapInfo(
    DaoProcessDataMapInfo* mapInfo)
{
    if (mapInfo == nullptr)
    {
        return 0;
    }

    DaoInternalProcessDataMapInfo
        internalInfo{};

    if (!g_engine.GetProcessDataMapInfo(
        internalInfo))
    {
        return 0;
    }

    std::memset(
        mapInfo,
        0,
        sizeof(DaoProcessDataMapInfo));

    mapInfo->mappedBytes =
        internalInfo.mappedBytes;

    mapInfo->outputWkc =
        internalInfo.outputWkc;

    mapInfo->inputWkc =
        internalInfo.inputWkc;

    mapInfo->expectedWkc =
        internalInfo.expectedWkc;

    return 1;
}

int DaoEngine_GetSlavePdoInfo(
    int slaveListIndex,
    DaoSlavePdoInfo* pdoInfo)
{
    if (pdoInfo == nullptr)
    {
        return 0;
    }

    DaoInternalSlavePdoInfo
        internalInfo{};

    if (!g_engine.GetSlavePdoInfo(
        slaveListIndex,
        internalInfo))
    {
        return 0;
    }

    std::memset(
        pdoInfo,
        0,
        sizeof(DaoSlavePdoInfo));

    pdoInfo->listIndex =
        internalInfo.listIndex;

    pdoInfo->physicalSlaveIndex =
        internalInfo.physicalSlaveIndex;

    pdoInfo->outputBytes =
        internalInfo.outputBytes;

    pdoInfo->inputBytes =
        internalInfo.inputBytes;

    return 1;
}

int DaoEngine_ValidateDaoAdcPdo(
    int physicalSlaveIndex,
    DaoAdcValidationInfo* validationInfo)
{
    if (validationInfo == nullptr)
    {
        return 0;
    }

    DaoInternalAdcValidationInfo
        internalInfo{};

    const bool validationResult =
        g_engine.ValidateDaoAdcPdo(
            physicalSlaveIndex,
            internalInfo);

    std::memset(
        validationInfo,
        0,
        sizeof(DaoAdcValidationInfo));

    validationInfo->physicalSlaveIndex =
        internalInfo.physicalSlaveIndex;

    validationInfo->processDataMapped =
        internalInfo.processDataMapped ? 1 : 0;

    validationInfo->slaveIndexValid =
        internalInfo.slaveIndexValid ? 1 : 0;

    validationInfo->identityValid =
        internalInfo.identityValid ? 1 : 0;

    validationInfo->outputSizeValid =
        internalInfo.outputSizeValid ? 1 : 0;

    validationInfo->inputSizeValid =
        internalInfo.inputSizeValid ? 1 : 0;

    validationInfo->outputPointerValid =
        internalInfo.outputPointerValid ? 1 : 0;

    validationInfo->inputPointerValid =
        internalInfo.inputPointerValid ? 1 : 0;

    validationInfo->actualOutputBytes =
        internalInfo.actualOutputBytes;

    validationInfo->actualInputBytes =
        internalInfo.actualInputBytes;

    return validationResult ? 1 : 0;
}

int DaoEngine_RequestDaoAdcSafeOp(
    int physicalSlaveIndex)
{
    return g_engine.RequestDaoAdcSafeOp(
        physicalSlaveIndex)
        ? 1
        : 0;
}

int DaoEngine_ExchangeDaoAdcProcessDataOnce(
    int physicalSlaveIndex,
    DaoProcessExchangeInfo* exchangeInfo)
{
    if (exchangeInfo == nullptr)
    {
        return 0;
    }

    DaoInternalProcessExchangeInfo
        internalInfo{};

    const bool exchangeResult =
        g_engine.ExchangeDaoAdcProcessDataOnce(
            physicalSlaveIndex,
            internalInfo);

    std::memset(
        exchangeInfo,
        0,
        sizeof(DaoProcessExchangeInfo));

    exchangeInfo->physicalSlaveIndex =
        internalInfo.physicalSlaveIndex;

    exchangeInfo->actualWkc =
        internalInfo.actualWkc;

    exchangeInfo->expectedWkc =
        internalInfo.expectedWkc;

    exchangeInfo->safeOpStateValid =
        internalInfo.safeOpStateValid ? 1 : 0;

    exchangeInfo->adcValidationPassed =
        internalInfo.adcValidationPassed ? 1 : 0;

    exchangeInfo->outputCleared =
        internalInfo.outputCleared ? 1 : 0;

    exchangeInfo->wkcValid =
        internalInfo.wkcValid ? 1 : 0;

    return exchangeResult ? 1 : 0;
}

int DaoEngine_PrimeDaoAdcProcessData(
    int physicalSlaveIndex,
    int roundCount,
    DaoPrimingInfo* primingInfo)
{
    if (primingInfo == nullptr)
    {
        return 0;
    }

    DaoInternalPrimingInfo internalInfo{};

    const bool primingResult =
        g_engine.PrimeDaoAdcProcessData(
            physicalSlaveIndex,
            roundCount,
            internalInfo);

    std::memset(
        primingInfo,
        0,
        sizeof(DaoPrimingInfo));

    primingInfo->physicalSlaveIndex =
        internalInfo.physicalSlaveIndex;

    primingInfo->requestedRounds =
        internalInfo.requestedRounds;

    primingInfo->completedRounds =
        internalInfo.completedRounds;

    primingInfo->expectedWkc =
        internalInfo.expectedWkc;

    primingInfo->minimumWkc =
        internalInfo.minimumWkc;

    primingInfo->maximumWkc =
        internalInfo.maximumWkc;

    primingInfo->lastWkc =
        internalInfo.lastWkc;

    primingInfo->goodWkcCount =
        internalInfo.goodWkcCount;

    primingInfo->badWkcCount =
        internalInfo.badWkcCount;

    primingInfo->safeOpStateValid =
        internalInfo.safeOpStateValid ? 1 : 0;

    primingInfo->adcValidationPassed =
        internalInfo.adcValidationPassed ? 1 : 0;

    primingInfo->allRoundsValid =
        internalInfo.allRoundsValid ? 1 : 0;

    return primingResult ? 1 : 0;
}

int DaoEngine_RequestDaoAdcOperational(
    int physicalSlaveIndex,
    DaoOperationalInfo* operationalInfo)
{
    if (operationalInfo == nullptr)
    {
        return 0;
    }

    DaoInternalOperationalInfo
        internalInfo{};

    const bool operationalResult =
        g_engine.RequestDaoAdcOperational(
            physicalSlaveIndex,
            internalInfo);

    std::memset(
        operationalInfo,
        0,
        sizeof(DaoOperationalInfo));

    operationalInfo->physicalSlaveIndex =
        internalInfo.physicalSlaveIndex;

    operationalInfo->expectedWkc =
        internalInfo.expectedWkc;

    operationalInfo->lastWkc =
        internalInfo.lastWkc;

    operationalInfo->exchangeCount =
        internalInfo.exchangeCount;

    operationalInfo->goodWkcCount =
        internalInfo.goodWkcCount;

    operationalInfo->badWkcCount =
        internalInfo.badWkcCount;

    operationalInfo->adcValidationPassed =
        internalInfo.adcValidationPassed ? 1 : 0;

    operationalInfo->safeOpStateValid =
        internalInfo.safeOpStateValid ? 1 : 0;

    operationalInfo->primingPassed =
        internalInfo.primingPassed ? 1 : 0;

    operationalInfo->stateWriteSucceeded =
        internalInfo.stateWriteSucceeded ? 1 : 0;

    operationalInfo->operationalReached =
        internalInfo.operationalReached ? 1 : 0;

    operationalInfo->finalState =
        internalInfo.finalState;

    operationalInfo->alStatusCode =
        internalInfo.alStatusCode;

    return operationalResult ? 1 : 0;
}

int DaoEngine_ReadDaoAdcOnce(
    int physicalSlaveIndex,
    DaoAdcReadInfo* readInfo)
{
    if (readInfo == nullptr)
    {
        return 0;
    }

    DaoInternalAdcReadInfo internalInfo{};

    const bool readResult =
        g_engine.ReadDaoAdcOnce(
            physicalSlaveIndex,
            internalInfo);

    std::memset(
        readInfo,
        0,
        sizeof(DaoAdcReadInfo));

    readInfo->physicalSlaveIndex =
        internalInfo.physicalSlaveIndex;

    readInfo->actualWkc =
        internalInfo.actualWkc;

    readInfo->expectedWkc =
        internalInfo.expectedWkc;

    readInfo->operationalStateValid =
        internalInfo.operationalStateValid ? 1 : 0;

    readInfo->adcValidationPassed =
        internalInfo.adcValidationPassed ? 1 : 0;

    readInfo->outputCleared =
        internalInfo.outputCleared ? 1 : 0;

    readInfo->wkcValid =
        internalInfo.wkcValid ? 1 : 0;

    readInfo->inputCopied =
        internalInfo.inputCopied ? 1 : 0;

    readInfo->data.testCounter =
        internalInfo.data.testCounter;

    readInfo->data.adcRaw0 =
        internalInfo.data.adcRaw0;

    readInfo->data.adcRaw1 =
        internalInfo.data.adcRaw1;

    readInfo->data.adcRaw2 =
        internalInfo.data.adcRaw2;

    readInfo->data.adcRaw3 =
        internalInfo.data.adcRaw3;

    readInfo->data.status =
        internalInfo.data.status;

    return readResult ? 1 : 0;
}

int DaoEngine_GetDaoAdcRuntimeInfo(
    int physicalSlaveIndex,
    DaoAdcRuntimeInfo* runtimeInfo)
{
    if (runtimeInfo == nullptr)
    {
        return 0;
    }

    DaoInternalAdcRuntimeInfo internalInfo{};

    const bool result =
        g_engine.GetDaoAdcRuntimeInfo(
            physicalSlaveIndex,
            internalInfo);

    std::memset(
        runtimeInfo,
        0,
        sizeof(DaoAdcRuntimeInfo));

    runtimeInfo->physicalSlaveIndex =
        internalInfo.physicalSlaveIndex;

    runtimeInfo->communicationRunning =
        internalInfo.communicationRunning ? 1 : 0;

    runtimeInfo->hasValidData =
        internalInfo.hasValidData ? 1 : 0;

    runtimeInfo->lastWkc =
        internalInfo.lastWkc;

    runtimeInfo->expectedWkc =
        internalInfo.expectedWkc;

    runtimeInfo->totalFrameCount =
        internalInfo.totalFrameCount;

    runtimeInfo->goodWkcFrameCount =
        internalInfo.goodWkcFrameCount;

    runtimeInfo->badWkcFrameCount =
        internalInfo.badWkcFrameCount;

    runtimeInfo->dataUpdateCount =
        internalInfo.dataUpdateCount;

    runtimeInfo->latestData.testCounter =
        internalInfo.latestData.testCounter;

    runtimeInfo->latestData.adcRaw0 =
        internalInfo.latestData.adcRaw0;

    runtimeInfo->latestData.adcRaw1 =
        internalInfo.latestData.adcRaw1;

    runtimeInfo->latestData.adcRaw2 =
        internalInfo.latestData.adcRaw2;

    runtimeInfo->latestData.adcRaw3 =
        internalInfo.latestData.adcRaw3;

    runtimeInfo->latestData.status =
        internalInfo.latestData.status;

    runtimeInfo->lowLevelFiltered =
        internalInfo.processing.lowLevelFiltered;

    runtimeInfo->powerLineFiltered =
        internalInfo.processing.powerLineFiltered;

    runtimeInfo->zeroedValue =
        internalInfo.processing.zeroedValue;

    runtimeInfo->calibratedValue =
        internalInfo.processing.calibratedValue;

    runtimeInfo->filteredValue =
        internalInfo.processing.filteredValue;

    runtimeInfo->stableCaptureActive =
        internalInfo.processing.stableCaptureActive ? 1 : 0;

    runtimeInfo->stableCaptureType =
        static_cast<int>(
            internalInfo.processing.stableCaptureType);

    runtimeInfo->stableCaptureCollectedCount =
        internalInfo.processing.stableCaptureCollectedCount;

    runtimeInfo->stableCaptureSampleCount =
        internalInfo.processing.stableCaptureSampleCount;

    return result ? 1 : 0;
}

int DaoEngine_GetAdcRuntimeInfo(
    int logicalAdcIndex,
    DaoAdcRuntimeInfo* runtimeInfo)
{
    if (runtimeInfo == nullptr)
    {
        return 0;
    }

    DaoInternalAdcRuntimeInfo internalInfo{};

    const bool result =
        g_engine.GetAdcRuntimeInfo(
            logicalAdcIndex,
            internalInfo);

    std::memset(
        runtimeInfo,
        0,
        sizeof(DaoAdcRuntimeInfo));

    runtimeInfo->physicalSlaveIndex =
        internalInfo.physicalSlaveIndex;

    runtimeInfo->communicationRunning =
        internalInfo.communicationRunning ? 1 : 0;

    runtimeInfo->hasValidData =
        internalInfo.hasValidData ? 1 : 0;

    runtimeInfo->lastWkc =
        internalInfo.lastWkc;

    runtimeInfo->expectedWkc =
        internalInfo.expectedWkc;

    runtimeInfo->totalFrameCount =
        internalInfo.totalFrameCount;

    runtimeInfo->goodWkcFrameCount =
        internalInfo.goodWkcFrameCount;

    runtimeInfo->badWkcFrameCount =
        internalInfo.badWkcFrameCount;

    runtimeInfo->dataUpdateCount =
        internalInfo.dataUpdateCount;

    runtimeInfo->latestData.testCounter =
        internalInfo.latestData.testCounter;

    runtimeInfo->latestData.adcRaw0 =
        internalInfo.latestData.adcRaw0;

    runtimeInfo->latestData.adcRaw1 =
        internalInfo.latestData.adcRaw1;

    runtimeInfo->latestData.adcRaw2 =
        internalInfo.latestData.adcRaw2;

    runtimeInfo->latestData.adcRaw3 =
        internalInfo.latestData.adcRaw3;

    runtimeInfo->latestData.status =
        internalInfo.latestData.status;

    runtimeInfo->lowLevelFiltered =
        internalInfo.processing.lowLevelFiltered;

    runtimeInfo->powerLineFiltered =
        internalInfo.processing.powerLineFiltered;

    runtimeInfo->zeroedValue =
        internalInfo.processing.zeroedValue;

    runtimeInfo->calibratedValue =
        internalInfo.processing.calibratedValue;

    runtimeInfo->filteredValue =
        internalInfo.processing.filteredValue;

    runtimeInfo->stableCaptureActive =
        internalInfo.processing.stableCaptureActive ? 1 : 0;

    runtimeInfo->stableCaptureType =
        static_cast<int>(
            internalInfo.processing.stableCaptureType);

    runtimeInfo->stableCaptureCollectedCount =
        internalInfo.processing.stableCaptureCollectedCount;

    runtimeInfo->stableCaptureSampleCount =
        internalInfo.processing.stableCaptureSampleCount;

    return result ? 1 : 0;
}

int DaoEngine_SetAdcZero(
	int logicalAdcIndex) // 논리 ADC 장치의 현재 입력값을 0으로 설정합니다.
{
    return g_engine.SetAdcZero(
        logicalAdcIndex)
        ? 1
        : 0;
}

int DaoEngine_SetAdcCalibration(
    int logicalAdcIndex,
	double referenceValue) // 논리 ADC 장치의 현재 입력값을 기준값으로 설정합니다. 이후 입력값은 기준값을 기준으로 보정됩니다.
{
    return g_engine.SetAdcCalibration(
        logicalAdcIndex,
        referenceValue)
        ? 1
        : 0;
}

int DaoEngine_SetAdcPowerLineFilterMode(
    int logicalAdcIndex,
    int mode) // 전원필터 관련함수
{
    return g_engine.SetAdcPowerLineFilterMode(
        logicalAdcIndex,
        mode)
        ? 1
        : 0;
}


int DaoEngine_SetAdcFilterN(
    int logicalAdcIndex,
    unsigned int filterN)  //필터 적용하여 UI에서 값을 넘겨줌 맞나?
{
    return g_engine.SetAdcFilterN(
        logicalAdcIndex,
        filterN)
        ? 1
        : 0;
}

int DaoEngine_StartAdcDiagnosticCapture(
    int logicalAdcIndex,
    unsigned int targetSampleCount)
{
    return g_engine.StartAdcDiagnosticCapture(
        logicalAdcIndex,
        targetSampleCount)
        ? 1
        : 0;
}


int DaoEngine_GetAdcDiagnosticCaptureInfo(
    int logicalAdcIndex,
    int* captureActive,
    unsigned int* capturedSampleCount,
    unsigned int* targetSampleCount)
{
    if (captureActive == nullptr ||
        capturedSampleCount == nullptr ||
        targetSampleCount == nullptr)
    {
        return 0;
    }

    bool internalCaptureActive = false;
    unsigned int internalCapturedSampleCount = 0;
    unsigned int internalTargetSampleCount = 0;

    const bool result =
        g_engine.GetAdcDiagnosticCaptureInfo(
            logicalAdcIndex,
            internalCaptureActive,
            internalCapturedSampleCount,
            internalTargetSampleCount);

    *captureActive =
        internalCaptureActive ? 1 : 0;

    *capturedSampleCount =
        internalCapturedSampleCount;

    *targetSampleCount =
        internalTargetSampleCount;

    return result ? 1 : 0;
}


int DaoEngine_GetAdcDiagnosticSample(
    int logicalAdcIndex,
    unsigned int sampleIndex,
    DaoAdcDiagnosticSample* sample)
{
    if (sample == nullptr)
    {
        return 0;
    }

    DaoInternalAdcDiagnosticSample
        internalSample{};

    if (!g_engine.GetAdcDiagnosticSample(
        logicalAdcIndex,
        sampleIndex,
        internalSample))
    {
        return 0;
    }

    sample->sampleIndex =
        internalSample.sampleIndex;

    sample->rawValue =
        internalSample.rawValue;

    sample->lowLevelFiltered =
        internalSample.lowLevelFiltered;

    sample->powerLineFiltered =
        internalSample.powerLineFiltered;

    sample->zeroedValue =
        internalSample.zeroedValue;

    sample->calibratedValue =
        internalSample.calibratedValue;

    sample->medianFilteredValue =
        internalSample.medianFilteredValue;

    sample->filteredValue =
        internalSample.filteredValue;

    return 1;
}


int DaoEngine_GetAdcRingBufferInfo(
    int logicalAdcIndex,
    unsigned int* sampleCount,
    unsigned long long* overflowCount)
{
    if (sampleCount == nullptr ||
        overflowCount == nullptr)
    {
        return 0;
    }

    unsigned int internalSampleCount = 0;
    unsigned long long internalOverflowCount = 0;

    const bool result =
        g_engine.GetAdcRingBufferInfo(
            logicalAdcIndex,
            internalSampleCount,
            internalOverflowCount);

    *sampleCount =
        internalSampleCount;

    *overflowCount =
        internalOverflowCount;

    return result ? 1 : 0;
}


int DaoEngine_ReadAdcRingBuffer(
    int logicalAdcIndex,
    DaoAdcBufferedSample* samples,
    unsigned int maxSampleCount,
    unsigned int* readSampleCount)
{
    if (samples == nullptr ||
        readSampleCount == nullptr)
    {
        return 0;
    }

    *readSampleCount = 0;

    if (maxSampleCount == 0)
    {
        return 0;
    }

    std::vector<DaoInternalAdcBufferedSample>
        internalSamples(
            static_cast<std::size_t>(
                maxSampleCount));

    unsigned int internalReadSampleCount = 0;

    const bool result =
        g_engine.ReadAdcRingBuffer(
            logicalAdcIndex,
            internalSamples.data(),
            maxSampleCount,
            internalReadSampleCount);

    if (!result)
    {
        return 0;
    }

    for (unsigned int i = 0;
        i < internalReadSampleCount;
        ++i)
    {
        samples[i].sampleIndex =
            internalSamples[i].sampleIndex;

        samples[i].filteredValue =
            internalSamples[i].filteredValue;
    }

    *readSampleCount =
        internalReadSampleCount;

    return 1;
}

int DaoEngine_ClearAdcRingBuffer(
    int logicalAdcIndex)
{
    return g_engine.ClearAdcRingBuffer(
        logicalAdcIndex)
        ? 1
        : 0;
}



int DaoEngine_GetServoRuntimeInfo(
    int logicalServoIndex,
    DaoServoRuntimeInfo* runtimeInfo)
{
    if (runtimeInfo == nullptr)
    {
        return 0;
    }

    DaoInternalServoRuntimeInfo
        internalInfo{};

    const bool result =
        g_engine.GetServoRuntimeInfo(
            logicalServoIndex,
            internalInfo);

    std::memset(
        runtimeInfo,
        0,
        sizeof(DaoServoRuntimeInfo));

    runtimeInfo->physicalSlaveIndex =
        internalInfo.physicalSlaveIndex;

    runtimeInfo->configured =
        internalInfo.configured ? 1 : 0;

    runtimeInfo->communicationRunning =
        internalInfo.communicationRunning ? 1 : 0;

    runtimeInfo->hasValidInputData =
        internalInfo.hasValidInputData ? 1 : 0;


    runtimeInfo->cia402State =
        internalInfo.cia402State;

    runtimeInfo->fault =
        internalInfo.fault ? 1 : 0;

    runtimeInfo->operationEnabled =
        internalInfo.operationEnabled ? 1 : 0;

    runtimeInfo->targetReached =
        internalInfo.targetReached ? 1 : 0;

    runtimeInfo->negativeLimit =
        internalInfo.negativeLimit ? 1 : 0;

    runtimeInfo->positiveLimit =
        internalInfo.positiveLimit ? 1 : 0;

    runtimeInfo->homeSensor =
        internalInfo.homeSensor ? 1 : 0;

    runtimeInfo->stopInput =
        internalInfo.stopInput ? 1 : 0;

    runtimeInfo->stoActive =
        internalInfo.stoActive ? 1 : 0;


    runtimeInfo->commandId =
        internalInfo.commandId;

    runtimeInfo->commandType =
        internalInfo.commandType;

    runtimeInfo->commandState =
        internalInfo.commandState;

    runtimeInfo->commandStep =
        internalInfo.commandStep;

    runtimeInfo->commandResult =
        internalInfo.commandResult;

    runtimeInfo->mailboxRequestType =
        internalInfo.mailboxRequestType;

    runtimeInfo->mailboxRequestState =
        internalInfo.mailboxRequestState;

    runtimeInfo->requestedOperationMode =
        internalInfo.requestedOperationMode;

    runtimeInfo->mailboxResult =
        internalInfo.mailboxResult;

    runtimeInfo->homed =
        internalInfo.homed ? 1 : 0;

    runtimeInfo->lastWkc =
        internalInfo.lastWkc;

    runtimeInfo->expectedWkc =
        internalInfo.expectedWkc;

    runtimeInfo->totalFrameCount =
        internalInfo.totalFrameCount;

    runtimeInfo->goodWkcFrameCount =
        internalInfo.goodWkcFrameCount;

    runtimeInfo->badWkcFrameCount =
        internalInfo.badWkcFrameCount;

    runtimeInfo->inputUpdateCount =
        internalInfo.inputUpdateCount;

    runtimeInfo->outputCommand.controlWord =
        internalInfo.outputCommand.controlWord;
    
    runtimeInfo->outputCommand.operationMode =
        internalInfo.outputCommand.operationMode;

    runtimeInfo->outputCommand.targetPosition =
        internalInfo.outputCommand.targetPosition;

    runtimeInfo->outputCommand.profileVelocity =
        internalInfo.outputCommand.profileVelocity;

    runtimeInfo->outputCommand.profileAcceleration =
        internalInfo.outputCommand.profileAcceleration;

    runtimeInfo->outputCommand.profileDeceleration =
        internalInfo.outputCommand.profileDeceleration;

    runtimeInfo->outputCommand.targetVelocity =
        internalInfo.outputCommand.targetVelocity;

    runtimeInfo->outputCommand.touchProbeFunction =
        internalInfo.outputCommand.touchProbeFunction;

    runtimeInfo->outputCommand.digitalOutputs =
        internalInfo.outputCommand.digitalOutputs;

    runtimeInfo->latestInput.statusWord =
        internalInfo.latestInput.statusWord;

    runtimeInfo->latestInput.operationModeDisplay =
        internalInfo.latestInput.operationModeDisplay;

    runtimeInfo->latestInput.actualPosition =
        internalInfo.latestInput.actualPosition;

    runtimeInfo->latestInput.positionError =
        internalInfo.latestInput.positionError;

    runtimeInfo->latestInput.touchProbeStatus =
        internalInfo.latestInput.touchProbeStatus;

    runtimeInfo->latestInput.touchProbePosition =
        internalInfo.latestInput.touchProbePosition;

    runtimeInfo->latestInput.digitalInputs =
        internalInfo.latestInput.digitalInputs;

    return result ? 1 : 0;
}


int DaoEngine_GetIoRuntimeInfo(
    int logicalIoIndex,
    DaoIoRuntimeInfo* runtimeInfo)
{
    if (runtimeInfo == nullptr)
    {
        return 0;
    }

    DaoInternalIoRuntimeInfo
        internalInfo{};

    const bool result =
        g_engine.GetIoRuntimeInfo(
            logicalIoIndex,
            internalInfo);

    std::memset(
        runtimeInfo,
        0,
        sizeof(DaoIoRuntimeInfo));

    runtimeInfo->physicalSlaveIndex =
        internalInfo.physicalSlaveIndex;

    runtimeInfo->configured =
        internalInfo.configured ? 1 : 0;

    runtimeInfo->inputBytes =
        internalInfo.inputBytes;

    runtimeInfo->outputBytes =
        internalInfo.outputBytes;

    runtimeInfo->communicationRunning =
        internalInfo.communicationRunning ? 1 : 0;

    runtimeInfo->hasValidInputData =
        internalInfo.hasValidInputData ? 1 : 0;

    runtimeInfo->lastWkc =
        internalInfo.lastWkc;

    runtimeInfo->expectedWkc =
        internalInfo.expectedWkc;

    runtimeInfo->totalFrameCount =
        internalInfo.totalFrameCount;

    runtimeInfo->goodWkcFrameCount =
        internalInfo.goodWkcFrameCount;

    runtimeInfo->badWkcFrameCount =
        internalInfo.badWkcFrameCount;

    runtimeInfo->inputUpdateCount =
        internalInfo.inputUpdateCount;

    runtimeInfo->outputCommand =
        internalInfo.outputCommand;

    runtimeInfo->latestInput =
        internalInfo.latestInput;

    return result ? 1 : 0;
}


int DaoEngine_GetEncoderRuntimeInfo(
    int logicalEncoderIndex,
    DaoEncoderRuntimeInfo* runtimeInfo)
{
    if (runtimeInfo == nullptr)
    {
        return 0;
    }

    DaoInternalEncoderRuntimeInfo
        internalInfo{};

    const bool result =
        g_engine.GetEncoderRuntimeInfo(
            logicalEncoderIndex,
            internalInfo);

    std::memset(
        runtimeInfo,
        0,
        sizeof(DaoEncoderRuntimeInfo));

    runtimeInfo->physicalSlaveIndex =
        internalInfo.physicalSlaveIndex;

    runtimeInfo->configured =
        internalInfo.configured ? 1 : 0;

    runtimeInfo->communicationRunning =
        internalInfo.communicationRunning ? 1 : 0;

    runtimeInfo->hasValidInputData =
        internalInfo.hasValidInputData ? 1 : 0;

    runtimeInfo->lastWkc =
        internalInfo.lastWkc;

    runtimeInfo->expectedWkc =
        internalInfo.expectedWkc;

    runtimeInfo->totalFrameCount =
        internalInfo.totalFrameCount;

    runtimeInfo->goodWkcFrameCount =
        internalInfo.goodWkcFrameCount;

    runtimeInfo->badWkcFrameCount =
        internalInfo.badWkcFrameCount;

    runtimeInfo->inputUpdateCount =
        internalInfo.inputUpdateCount;

    runtimeInfo->presentCounterCh1 =
        internalInfo.latestInput.presentCounterCh1;

    runtimeInfo->presentCounterCh2 =
        internalInfo.latestInput.presentCounterCh2;

    runtimeInfo->pulseRateCh1 =
        internalInfo.latestInput.pulseRateCh1;

    runtimeInfo->pulseRateCh2 =
        internalInfo.latestInput.pulseRateCh2;

    runtimeInfo->counterCommand =
        internalInfo.outputCommand.counterCommand;

    return result ? 1 : 0;
}


int DaoEngine_ServoOn(
    int logicalServoIndex)
{
    return g_engine.ServoOn(
        logicalServoIndex)
        ? 1
        : 0;
}

int DaoEngine_ServoOff(
    int logicalServoIndex)
{
    return g_engine.ServoOff(
        logicalServoIndex)
        ? 1
        : 0;
}

int DaoEngine_ServoHome(
    int logicalServoIndex,
    unsigned int timeoutMs)
{
    return g_engine.ServoHome(
        logicalServoIndex,
        timeoutMs)
        ? 1
        : 0;
}

int DaoEngine_ServoMoveAbsolute(
    int logicalServoIndex,
    int targetPosition,
    unsigned int profileVelocity,
    unsigned int profileAcceleration,
    unsigned int profileDeceleration,
    unsigned int timeoutMs)
{
    return g_engine.ServoMoveAbsolute(
        logicalServoIndex,
        targetPosition,
        profileVelocity,
        profileAcceleration,
        profileDeceleration,
        timeoutMs)
        ? 1
        : 0;
}

int DaoEngine_ServoVelocity(
    int logicalServoIndex,
    int targetVelocity,
    unsigned int acceleration,
    unsigned int deceleration)
{
    return g_engine.ServoVelocity(
        logicalServoIndex,
        targetVelocity,
        acceleration,
        deceleration)
        ? 1
        : 0;
}


int DaoEngine_ServoJogPositive(
    int logicalServoIndex,
    int speed,
    unsigned int acceleration,
    unsigned int deceleration)
{
    return g_engine.ServoJogPositive(
        logicalServoIndex,
        speed,
        acceleration,
        deceleration)
        ? 1
        : 0;
}

int DaoEngine_ServoJogNegative(
    int logicalServoIndex,
    int speed,
    unsigned int acceleration,
    unsigned int deceleration)
{
    return g_engine.ServoJogNegative(
        logicalServoIndex,
        speed,
        acceleration,
        deceleration)
        ? 1
        : 0;
}

int DaoEngine_ServoStop(
    int logicalServoIndex)
{
    return g_engine.ServoStop(
        logicalServoIndex)
        ? 1
        : 0;
}

int DaoEngine_SetServoOutputCommand(
    int logicalServoIndex,
    const DaoServoOutputPdo* command)
{
    if (command == nullptr)
    {
        return 0;
    }

    DaoInternalLsServoOutputPdo
        internalCommand{};

    internalCommand.controlWord =
        command->controlWord;

    internalCommand.operationMode =
        command->operationMode;

    internalCommand.targetPosition =
        command->targetPosition;

    internalCommand.profileVelocity =
        command->profileVelocity;

    internalCommand.profileAcceleration =
        command->profileAcceleration;

    internalCommand.profileDeceleration =
        command->profileDeceleration;

    internalCommand.targetVelocity =
        command->targetVelocity;

    internalCommand.touchProbeFunction =
        command->touchProbeFunction;

    internalCommand.digitalOutputs =
        command->digitalOutputs;

    return g_engine.SetServoOutputCommand(
        logicalServoIndex,
        internalCommand)
        ? 1
        : 0;
}

int DaoEngine_SetServoOperationMode(
    int logicalServoIndex,
    signed char mode)
{
    return g_engine.SetServoOperationMode(
        logicalServoIndex,
        mode)
        ? 1
        : 0;
}


int DaoEngine_SetIoOutputCommand(
    int logicalIoIndex,
    unsigned short outputValue)
{
    return g_engine.SetIoOutputCommand(
        logicalIoIndex,
        outputValue)
        ? 1
        : 0;
}

int DaoEngine_StartCommunication()
{
    return g_engine.StartCommunication()
        ? 1
        : 0;
}

void DaoEngine_StopCommunication()
{
    g_engine.StopCommunication();
}

int DaoEngine_IsCommunicationRunning()
{
    return g_engine.IsCommunicationRunning()
        ? 1
        : 0;
}
