#include "DaoEngineCore.h"
#include "DaoEtherCAT.Engine.h"

#include <soem/soem.h>


DaoEngineCore::DaoEngineCore()
    : initialized_(false)
{
}

bool DaoEngineCore::Initialize()
{
    bool expected = false;

    if (!initialized_.compare_exchange_strong(expected, true))
    {
        return false;
    }

    adapters_.clear();

    if (!RefreshAdapterList())
    {
        initialized_.store(false);
        return false;
    }

    return true;
}

void DaoEngineCore::Shutdown()
{
    master_.Close();

    ClearLogicalDevices();
    adapters_.clear();

    initialized_.store(false);
}

bool DaoEngineCore::IsInitialized() const
{
    return initialized_.load();
}

int DaoEngineCore::GetAdapterCount()
{
    if (!IsInitialized())
    {
        return 0;
    }

    return static_cast<int>(adapters_.size());
}

bool DaoEngineCore::GetAdapterInfo(
    int adapterIndex,
    DaoInternalAdapterInfo& adapterInfo)
{
    if (!IsInitialized())
    {
        return false;
    }

    if (adapterIndex < 0 ||
        adapterIndex >= static_cast<int>(adapters_.size()))
    {
        return false;
    }

    adapterInfo = adapters_[adapterIndex];
    return true;
}

bool DaoEngineCore::RefreshAdapterList()
{
    ec_adaptert* adapterList = ec_find_adapters();

    if (adapterList == nullptr)
    {
        return false;
    }

    ec_adaptert* currentAdapter = adapterList;

    while (currentAdapter != nullptr)
    {
        DaoInternalAdapterInfo adapterInfo;

        if (currentAdapter->name != nullptr)
        {
            adapterInfo.name = currentAdapter->name;
        }

        if (currentAdapter->desc != nullptr)
        {
            adapterInfo.description = currentAdapter->desc;
        }

        adapters_.push_back(adapterInfo);
        currentAdapter = currentAdapter->next;
    }

    ec_free_adapters(adapterList);

    return !adapters_.empty();
}

bool DaoEngineCore::OpenAdapter(
    int adapterIndex)
{
    if (!IsInitialized())
    {
        return false;
    }

    if (adapterIndex < 0 ||
        adapterIndex >=
        static_cast<int>(adapters_.size()))
    {
        return false;
    }

    return master_.Open(
        adapters_[adapterIndex].name);
}

void DaoEngineCore::CloseAdapter()
{
    master_.Close();
    ClearLogicalDevices();
}

bool DaoEngineCore::IsAdapterOpen() const
{
    return master_.IsOpen();
}
int DaoEngineCore::ScanSlaves()
{
    if (!IsInitialized())
    {
        ClearLogicalDevices();
        return 0;
    }

    if (!master_.IsOpen())
    {
        ClearLogicalDevices();
        return 0;
    }

    const int detectedSlaveCount =
        master_.ScanSlaves();

    // 연결된 EtherCAT Slave를 검색합니다.
    ClearLogicalDevices();

    if (detectedSlaveCount <= 0)
    {
        return 0;
    }

    // 검색 결과를 바탕으로 논리 장치 목록을 다시 구성합니다.
    BuildLogicalDevices();

    return detectedSlaveCount;
}

int DaoEngineCore::GetSlaveCount() const
{
    return master_.GetSlaveCount();
}
bool DaoEngineCore::GetSlaveInfo(
    int slaveListIndex,
    DaoInternalSlaveInfo& slaveInfo) const
{
    if (!IsInitialized())
    {
        return false;
    }

    return master_.GetSlaveInfo(
        slaveListIndex,
        slaveInfo);
}

void DaoEngineCore::ClearLogicalDevices()
{
    unknownDevices_.clear();
    servoDevices_.clear();
    adcDevices_.clear();
    ioDevices_.clear();
    encoderDevices_.clear();
}

int DaoEngineCore::ClassifyDevice(
    const DaoInternalSlaveInfo& slaveInfo) const
{
    // --------------------------------------------------------
    // DAO EtherCAT ADC
    // --------------------------------------------------------
    if (slaveInfo.vendorId == 0x000011C0 &&
        slaveInfo.productCode == 0x0000DA01)
    {
        return DAO_DEVICE_ADC;
    }

    // --------------------------------------------------------
    // LS Mecapion L7NH Servo
    // --------------------------------------------------------
    if (slaveInfo.vendorId == 0x00007595 &&
        slaveInfo.productCode == 0x00010001 &&
        slaveInfo.revision == 0x00000001)
    {
        return DAO_DEVICE_SERVO;
    }

    // --------------------------------------------------------
    // FASTECH Ezi-IO IN8OUT8 / IN16OUT16
    // --------------------------------------------------------
    const bool isFastechIoProduct =
        slaveInfo.productCode == 0x00002021 ||
        slaveInfo.productCode == 0x00002023;

    if (slaveInfo.vendorId == 0x0FA00000 &&
        isFastechIoProduct &&
        slaveInfo.revision == 0x00000001)
    {
        return DAO_DEVICE_IO;
    }

    // --------------------------------------------------------
    // FASTECH Ezi-IO EtherCAT CNT02
    // --------------------------------------------------------
    if (slaveInfo.vendorId == 0x0FA00000 &&
        slaveInfo.productCode == 0x00002301)
    {
        return DAO_DEVICE_ENCODER;
    }

    // --------------------------------------------------------
    // FASTECH Ezi-IO EtherCAT CNT02
    // --------------------------------------------------------
    if (slaveInfo.vendorId == 0x0FA00000 &&
    slaveInfo.productCode == 0x00002301)
    {
        return DAO_DEVICE_ENCODER;
    }

    return DAO_DEVICE_UNKNOWN;
}

void DaoEngineCore::BuildLogicalDevices()
{
    const int slaveCount =
        master_.GetSlaveCount();

    for (int slaveListIndex = 0;
        slaveListIndex < slaveCount;
        ++slaveListIndex)
    {
        DaoInternalSlaveInfo slaveInfo{};

        if (!master_.GetSlaveInfo(
            slaveListIndex,
            slaveInfo))
        {
            continue;
        }

        const int deviceType =
            ClassifyDevice(slaveInfo);

        std::vector<DaoInternalLogicalDeviceInfo>* targetList =
            nullptr;

        switch (deviceType)
        {
        case DAO_DEVICE_SERVO:
            targetList = &servoDevices_;
            break;

        case DAO_DEVICE_ADC:
            targetList = &adcDevices_;
            break;

        case DAO_DEVICE_IO:
            targetList = &ioDevices_;
            break;
            
        case DAO_DEVICE_ENCODER:
            targetList = &encoderDevices_;
            break;

        default:
            targetList = &unknownDevices_;
            break;
        }

        DaoInternalLogicalDeviceInfo deviceInfo{};

        deviceInfo.deviceType =
            deviceType;

        // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
        deviceInfo.logicalIndex =
            static_cast<int>(targetList->size());

        deviceInfo.physicalSlaveIndex =
            slaveInfo.physicalSlaveIndex;

        deviceInfo.name =
            slaveInfo.name;

        deviceInfo.vendorId =
            slaveInfo.vendorId;

        deviceInfo.productCode =
            slaveInfo.productCode;

        deviceInfo.revision =
            slaveInfo.revision;

        targetList->push_back(deviceInfo);
    }
}

const std::vector<DaoInternalLogicalDeviceInfo>*
DaoEngineCore::GetLogicalDeviceList(
    int deviceType) const
{
    switch (deviceType)
    {
    case DAO_DEVICE_UNKNOWN:
        return &unknownDevices_;

    case DAO_DEVICE_SERVO:
        return &servoDevices_;

    case DAO_DEVICE_ADC:
        return &adcDevices_;

    case DAO_DEVICE_IO:
        return &ioDevices_;

    case DAO_DEVICE_ENCODER:
        return &encoderDevices_;

    default:
        return nullptr;
    }
}

int DaoEngineCore::GetLogicalDeviceCount(
    int deviceType) const
{
    if (!IsInitialized())
    {
        return 0;
    }

    const auto* deviceList =
        GetLogicalDeviceList(deviceType);

    if (deviceList == nullptr)
    {
        return 0;
    }

    return static_cast<int>(
        deviceList->size());
}

bool DaoEngineCore::GetLogicalDeviceInfo(
    int deviceType,
    int logicalIndex,
    DaoInternalLogicalDeviceInfo& deviceInfo) const
{
    if (!IsInitialized())
    {
        return false;
    }

    const auto* deviceList =
        GetLogicalDeviceList(deviceType);

    if (deviceList == nullptr)
    {
        return false;
    }

    if (logicalIndex < 0 ||
        logicalIndex >=
        static_cast<int>(deviceList->size()))
    {
        return false;
    }

    deviceInfo =
        (*deviceList)[logicalIndex];

    return true;
}

bool DaoEngineCore::GetPhysicalSlaveIndex(
    int deviceType,
    int logicalIndex,
    int& physicalSlaveIndex) const
{
    physicalSlaveIndex = 0;

    if (!IsInitialized())
    {
        return false;
    }

    const auto* deviceList =
        GetLogicalDeviceList(
            deviceType);

    if (deviceList == nullptr)
    {
        return false;
    }

    if (logicalIndex < 0 ||
        logicalIndex >=
        static_cast<int>(
            deviceList->size()))
    {
        return false;
    }

    const DaoInternalLogicalDeviceInfo& deviceInfo =
        (*deviceList)[
            static_cast<std::size_t>(
                logicalIndex)];

    if (deviceInfo.physicalSlaveIndex <= 0)
    {
        return false;
    }

    physicalSlaveIndex =
        deviceInfo.physicalSlaveIndex;

    return true;
}

bool DaoEngineCore::RequestAllSlavesPreOp()
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    return master_.RequestAllSlavesPreOp();
}

bool DaoEngineCore::RequestAllSlavesSafeOp()
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    return master_.RequestAllSlavesSafeOp();
}

bool DaoEngineCore::RequestAllSlavesOperational()
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    return master_.RequestAllSlavesOperational();
}

bool DaoEngineCore::RequestAllSlavesInit()
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    return master_.RequestAllSlavesInit();
}

bool DaoEngineCore::MapProcessData()
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    return master_.MapProcessData();
}

bool DaoEngineCore::GetProcessDataMapInfo(
    DaoInternalProcessDataMapInfo& mapInfo) const
{
    if (!IsInitialized())
    {
        return false;
    }

    return master_.GetProcessDataMapInfo(
        mapInfo);
}

bool DaoEngineCore::GetSlavePdoInfo(
    int slaveListIndex,
    DaoInternalSlavePdoInfo& pdoInfo) const
{
    if (!IsInitialized())
    {
        return false;
    }

    return master_.GetSlavePdoInfo(
        slaveListIndex,
        pdoInfo);
}

bool DaoEngineCore::ValidateDaoAdcPdo(
    int physicalSlaveIndex,
    DaoInternalAdcValidationInfo& validationInfo) const
{
    if (!IsInitialized())
    {
        validationInfo = {};
        validationInfo.physicalSlaveIndex =
            physicalSlaveIndex;

        return false;
    }

    if (!master_.IsOpen())
    {
        validationInfo = {};
        validationInfo.physicalSlaveIndex =
            physicalSlaveIndex;

        return false;
    }

    return master_.ValidateDaoAdcPdo(
        physicalSlaveIndex,
        validationInfo);
}

bool DaoEngineCore::RequestDaoAdcSafeOp(
    int physicalSlaveIndex)
{
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    if (!IsInitialized())
    {
        return false;
    }

    // 아래 조건을 확인한 후 현재 처리 단계를 계속합니다.
    if (!master_.IsOpen())
    {
        return false;
    }

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    return master_.RequestDaoAdcSafeOp(
        physicalSlaveIndex);
}

bool DaoEngineCore::ExchangeDaoAdcProcessDataOnce(
    int physicalSlaveIndex,
    DaoInternalProcessExchangeInfo& exchangeInfo)
{
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    if (!IsInitialized())
    {
        exchangeInfo = {};
        exchangeInfo.physicalSlaveIndex =
            physicalSlaveIndex;

        return false;
    }

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    if (!master_.IsOpen())
    {
        exchangeInfo = {};
        exchangeInfo.physicalSlaveIndex =
            physicalSlaveIndex;

        return false;
    }

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    return master_.ExchangeDaoAdcProcessDataOnce(
        physicalSlaveIndex,
        exchangeInfo);
}

bool DaoEngineCore::PrimeDaoAdcProcessData(
    int physicalSlaveIndex,
    int roundCount,
    DaoInternalPrimingInfo& primingInfo)
{
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    if (!IsInitialized())
    {
        primingInfo = {};
        primingInfo.physicalSlaveIndex =
            physicalSlaveIndex;

        primingInfo.requestedRounds =
            roundCount;

        return false;
    }

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    if (!master_.IsOpen())
    {
        primingInfo = {};
        primingInfo.physicalSlaveIndex =
            physicalSlaveIndex;

        primingInfo.requestedRounds =
            roundCount;

        return false;
    }

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    return master_.PrimeDaoAdcProcessData(
        physicalSlaveIndex,
        roundCount,
        primingInfo);
}

bool DaoEngineCore::RequestDaoAdcOperational(
    int physicalSlaveIndex,
    DaoInternalOperationalInfo& operationalInfo)
{
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    if (!IsInitialized())
    {
        operationalInfo = {};

        operationalInfo.physicalSlaveIndex =
            physicalSlaveIndex;

        return false;
    }

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    if (!master_.IsOpen())
    {
        operationalInfo = {};

        operationalInfo.physicalSlaveIndex =
            physicalSlaveIndex;

        return false;
    }

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    return master_.RequestDaoAdcOperational(
        physicalSlaveIndex,
        operationalInfo);
}

bool DaoEngineCore::ReadDaoAdcOnce(
    int physicalSlaveIndex,
    DaoInternalAdcReadInfo& readInfo)
{
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    if (!IsInitialized())
    {
        readInfo = {};

        readInfo.physicalSlaveIndex =
            physicalSlaveIndex;

        return false;
    }

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    if (!master_.IsOpen())
    {
        readInfo = {};

        readInfo.physicalSlaveIndex =
            physicalSlaveIndex;

        return false;
    }

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    return master_.ReadDaoAdcOnce(
        physicalSlaveIndex,
        readInfo);
}

bool DaoEngineCore::GetDaoAdcRuntimeInfo(
    int physicalSlaveIndex,
    DaoInternalAdcRuntimeInfo& runtimeInfo) const
{
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    if (!IsInitialized())
    {
        runtimeInfo = {};
        runtimeInfo.physicalSlaveIndex =
            physicalSlaveIndex;

        return false;
    }

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    if (!master_.IsOpen())
    {
        runtimeInfo = {};
        runtimeInfo.physicalSlaveIndex =
            physicalSlaveIndex;

        return false;
    }

    return master_.GetDaoAdcRuntimeInfo(
        physicalSlaveIndex,
        runtimeInfo);
}

bool DaoEngineCore::GetAdcRuntimeInfo(
    int logicalAdcIndex,
    DaoInternalAdcRuntimeInfo& runtimeInfo) const
{
    runtimeInfo = {};

    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_ADC,
        logicalAdcIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.GetDaoAdcRuntimeInfo(
        physicalSlaveIndex,
        runtimeInfo);
}

bool DaoEngineCore::SetAdcZero(
    int logicalAdcIndex)
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_ADC,
        logicalAdcIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.SetDaoAdcZero(
        physicalSlaveIndex);
}

bool DaoEngineCore::SetAdcCalibration(
    int logicalAdcIndex,
    double referenceValue)
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_ADC,
        logicalAdcIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.SetDaoAdcCalibration(
        physicalSlaveIndex,
        referenceValue);
}

bool DaoEngineCore::SetAdcPowerLineFilterMode(
    int logicalAdcIndex,
    int mode)
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_ADC,
        logicalAdcIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    DaoInternalAdcPowerLineFilterMode internalMode =
        DaoInternalAdcPowerLineFilterMode::OFF;

    switch (mode)
    {
    case 0:
        internalMode =
            DaoInternalAdcPowerLineFilterMode::OFF;
        break;

    case 1:
        internalMode =
            DaoInternalAdcPowerLineFilterMode::HZ_50;
        break;

    case 2:
        internalMode =
            DaoInternalAdcPowerLineFilterMode::HZ_60;
        break;

    case 3:
        internalMode =
            DaoInternalAdcPowerLineFilterMode::HZ_120;
        break;

    case 4:
        internalMode =
            DaoInternalAdcPowerLineFilterMode::HZ_50_60;
        break;

    case 5:
        internalMode =
            DaoInternalAdcPowerLineFilterMode::HZ_60_120;
        break;

    default:
        return false;
    }

    return master_.SetDaoAdcPowerLineFilterMode(
        physicalSlaveIndex,
        internalMode);
}

bool DaoEngineCore::SetAdcFilterN(
    int logicalAdcIndex,
    unsigned int filterN)
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_ADC,
        logicalAdcIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.SetDaoAdcFilterN(
        physicalSlaveIndex,
        filterN);
}


bool DaoEngineCore::StartAdcDiagnosticCapture(
    int logicalAdcIndex,
    unsigned int targetSampleCount)
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_ADC,
        logicalAdcIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.StartDaoAdcDiagnosticCapture(
        physicalSlaveIndex,
        targetSampleCount);
}


bool DaoEngineCore::GetAdcDiagnosticCaptureInfo(
    int logicalAdcIndex,
    bool& captureActive,
    unsigned int& capturedSampleCount,
    unsigned int& targetSampleCount) const
{
    captureActive = false;
    capturedSampleCount = 0;
    targetSampleCount = 0;

    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_ADC,
        logicalAdcIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.GetDaoAdcDiagnosticCaptureInfo(
        physicalSlaveIndex,
        captureActive,
        capturedSampleCount,
        targetSampleCount);
}


bool DaoEngineCore::GetAdcDiagnosticSample(
    int logicalAdcIndex,
    unsigned int sampleIndex,
    DaoInternalAdcDiagnosticSample& sample) const
{
    sample = {};

    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_ADC,
        logicalAdcIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.GetDaoAdcDiagnosticSample(
        physicalSlaveIndex,
        sampleIndex,
        sample);
}

bool DaoEngineCore::GetAdcRingBufferInfo(
    int logicalAdcIndex,
    unsigned int& sampleCount,
    unsigned long long& overflowCount) const
{
    sampleCount = 0;
    overflowCount = 0;

    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_ADC,
        logicalAdcIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.GetDaoAdcRingBufferInfo(
        physicalSlaveIndex,
        sampleCount,
        overflowCount);
}


bool DaoEngineCore::ReadAdcRingBuffer(
    int logicalAdcIndex,
    DaoInternalAdcBufferedSample* samples,
    unsigned int maxSampleCount,
    unsigned int& readSampleCount)
{
    readSampleCount = 0;

    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_ADC,
        logicalAdcIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.ReadDaoAdcRingBuffer(
        physicalSlaveIndex,
        samples,
        maxSampleCount,
        readSampleCount);
}

bool DaoEngineCore::ClearAdcRingBuffer(
    int logicalAdcIndex)
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_ADC,
        logicalAdcIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.ClearDaoAdcRingBuffer(
        physicalSlaveIndex);
}


bool DaoEngineCore::GetServoRuntimeInfo(
    int logicalServoIndex,
    DaoInternalServoRuntimeInfo& runtimeInfo) const
{
    runtimeInfo = {};

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_SERVO,
        logicalServoIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.GetServoRuntimeInfo(
        physicalSlaveIndex,
        runtimeInfo);
}


bool DaoEngineCore::GetIoRuntimeInfo(
    int logicalIoIndex,
    DaoInternalIoRuntimeInfo& runtimeInfo) const
{
    runtimeInfo = {};

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_IO,
        logicalIoIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.GetIoRuntimeInfo(
        physicalSlaveIndex,
        runtimeInfo);
}

bool DaoEngineCore::GetEncoderRuntimeInfo(
    int logicalEncoderIndex,
    DaoInternalEncoderRuntimeInfo& runtimeInfo) const
{
    runtimeInfo = {};

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_ENCODER,
        logicalEncoderIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.GetEncoderRuntimeInfo(
        physicalSlaveIndex,
        runtimeInfo);
}

bool DaoEngineCore::SetEncoderCountDirection(
    int logicalEncoderIndex,
    int channel,
    int direction)
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    if ((channel != 1 && channel != 2) ||
        (direction != 0 && direction != 1))
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_ENCODER,
        logicalEncoderIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.ConfigureFastechEncoderCountDirection(
        physicalSlaveIndex,
        channel,
        static_cast<std::uint8_t>(direction));
}

bool DaoEngineCore::ResetEncoderCounter(
    int logicalEncoderIndex,
    int channel,
    unsigned int timeoutMs)
{
    if (!IsInitialized() ||
        !master_.IsOpen() ||
        (channel != 1 && channel != 2) ||
        timeoutMs == 0)
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_ENCODER,
        logicalEncoderIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.ResetFastechEncoderCounter(
        physicalSlaveIndex,
        channel,
        timeoutMs);
}

bool DaoEngineCore::SetEncoderCalibrationScale(
    int logicalEncoderIndex,
    int channel,
    double calibrationScale)
{
    if (!IsInitialized() ||
        !master_.IsOpen() ||
        (channel != 1 && channel != 2))
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_ENCODER,
        logicalEncoderIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.SetEncoderCalibrationScale(
        physicalSlaveIndex,
        channel,
        calibrationScale);
}

bool DaoEngineCore::CalibrateEncoder(
    int logicalEncoderIndex,
    int channel,
    double referenceValue)
{
    if (!IsInitialized() ||
        !master_.IsOpen() ||
        (channel != 1 && channel != 2))
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_ENCODER,
        logicalEncoderIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.CalibrateEncoder(
        physicalSlaveIndex,
        channel,
        referenceValue);
}

bool DaoEngineCore::ServoOn(
    int logicalServoIndex)
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_SERVO,
        logicalServoIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.RequestServoOn(
        physicalSlaveIndex);
}

bool DaoEngineCore::ServoOff(
    int logicalServoIndex)
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_SERVO,
        logicalServoIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.RequestServoOff(
        physicalSlaveIndex);
}

bool DaoEngineCore::ServoHome(
    int logicalServoIndex,
    unsigned int timeoutMs)
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_SERVO,
        logicalServoIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.RequestServoHome(
        physicalSlaveIndex,
        timeoutMs);
}


bool DaoEngineCore::ServoMoveAbsolute(
    int logicalServoIndex,
    int targetPosition,
    unsigned int profileVelocity,
    unsigned int profileAcceleration,
    unsigned int profileDeceleration,
	unsigned int timeoutMs)    // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_SERVO,
        logicalServoIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.RequestServoMoveAbsolute(
        physicalSlaveIndex,
        targetPosition,
        profileVelocity,
        profileAcceleration,
        profileDeceleration,
        timeoutMs);
}

bool DaoEngineCore::ServoVelocity(
    int logicalServoIndex,
    int targetVelocity,
    unsigned int acceleration,
    unsigned int deceleration)
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_SERVO,
        logicalServoIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.RequestServoVelocity(
        physicalSlaveIndex,
        targetVelocity,
        acceleration,
        deceleration);
}

bool DaoEngineCore::ServoJogPositive(
    int logicalServoIndex,
    int speed,
    unsigned int acceleration,
    unsigned int deceleration)
{
    if (speed <= 0)
    {
        return false;
    }

    return ServoVelocity(
        logicalServoIndex,
        speed,
        acceleration,
        deceleration);
}

bool DaoEngineCore::ServoJogNegative(
    int logicalServoIndex,
    int speed,
    unsigned int acceleration,
    unsigned int deceleration)
{
    if (speed <= 0)
    {
        return false;
    }

    return ServoVelocity(
        logicalServoIndex,
        -speed,
        acceleration,
        deceleration);
}

bool DaoEngineCore::ServoStop(
    int logicalServoIndex)
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_SERVO,
        logicalServoIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.RequestServoStop(
        physicalSlaveIndex);
}

bool DaoEngineCore::SetServoOutputCommand(
    int logicalServoIndex,
    const DaoInternalLsServoOutputPdo& command)
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_SERVO,
        logicalServoIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.SetServoOutputCommand(
        physicalSlaveIndex,
        command);
}

bool DaoEngineCore::SetServoOperationMode(
    int logicalServoIndex,
    signed char mode)
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    // 통신 스레드의 실행 상태를 확인합니다.
    // 통신 스레드의 실행 상태를 확인합니다.
    if (master_.IsCommunicationRunning())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_SERVO,
        logicalServoIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.ConfigureLsL7nhOperationMode(
        physicalSlaveIndex,
        mode);
}

bool DaoEngineCore::BeginServoCommand(
    int logicalServoIndex,
    int commandType)
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_SERVO,
        logicalServoIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.BeginServoCommand(
        physicalSlaveIndex,
        commandType);
}


bool DaoEngineCore::UpdateServoCommandState(
    int logicalServoIndex,
    int commandState,
    int commandResult)
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_SERVO,
        logicalServoIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.UpdateServoCommandState(
        physicalSlaveIndex,
        commandState,
        commandResult);
}




bool DaoEngineCore::SetIoOutputCommand(
    int logicalIoIndex,
    unsigned short outputValue)
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    int physicalSlaveIndex = 0;

    if (!GetPhysicalSlaveIndex(
        DAO_DEVICE_IO,
        logicalIoIndex,
        physicalSlaveIndex))
    {
        return false;
    }

    return master_.SetIoOutputCommand(
        physicalSlaveIndex,
        outputValue);
}

bool DaoEngineCore::StartCommunication()
{
    if (!IsInitialized())
    {
        return false;
    }

    if (!master_.IsOpen())
    {
        return false;
    }

    return master_.StartCommunication();
}

void DaoEngineCore::StopCommunication()
{
    master_.StopCommunication();
}

bool DaoEngineCore::IsCommunicationRunning() const
{
    return master_.IsCommunicationRunning();
}


