#include "DaoEtherCATMaster.h"
#include <vector>
#include <cstddef>
#include <chrono>
#include <thread>
#include <cstdint>
#include <climits>
#include <cmath>

#include <cstring>

DaoEtherCATMaster::DaoEtherCATMaster()
    : context_{},
    isOpen_(false),
    slaveCount_(0),
    ioMap_{},
    mappedBytes_(0),
    outputWkc_(0),
    inputWkc_(0),
    expectedWkc_(0),
    processDataMapped_(false),
    adcRuntimeInfoBySlave_{}
{
    std::memset(
        &context_,
        0,
        sizeof(context_));

    context_.manualstatechange = 1;
}

DaoEtherCATMaster::~DaoEtherCATMaster()
{
	StopCommunication(); // 주기 통신 스레드의 중지를 요청합니다.
    Close();
}

bool DaoEtherCATMaster::Open(
    const std::string& adapterName)
{
    if (isOpen_)
    {
        return false;
    }

    if (adapterName.empty())
    {
        return false;
    }

    // 출력 PDO 영역을 안전한 초기값인 0으로 설정합니다.
    std::memset(
        &context_,
        0,
        sizeof(context_));

    context_.manualstatechange = 1;
	slaveCount_ = 0; // PDO 구조체는 EtherCAT 매핑 크기와 일치하도록 바이트 단위로 정렬합니다.
    ResetAdcRuntimeInfo();
    ResetServoRuntimeInfo();
    ResetIoRuntimeInfo();
    ResetEncoderRuntimeInfo();
  

    if (ecx_init(
        &context_,
        adapterName.c_str()) <= 0)
    {
        return false;
    }

    isOpen_ = true;
    return true;
}

void DaoEtherCATMaster::Close()
{
    // --------------------------------------------------------
    // 주기 통신 스레드의 중지를 요청합니다.
    // --------------------------------------------------------
    StopCommunication();

    if (!isOpen_)
    {
        slaveCount_ = 0;
        ResetProcessDataMap();
        ResetAdcRuntimeInfo();
        ResetServoRuntimeInfo();
        ResetIoRuntimeInfo();
        ResetEncoderRuntimeInfo();

        return;
    }

    // --------------------------------------------------------
    // 통신을 중지하고 열린 네트워크 어댑터를 닫습니다.
    //
    // 통신을 중지하고 열린 네트워크 어댑터를 닫습니다.
    //
    // 검색된 모든 Slave를 SAFE-OP 상태로 전환합니다.
    // 검색된 모든 Slave를 SAFE-OP 상태로 전환합니다.
    // --------------------------------------------------------
    if (slaveCount_ > 0)
    {
        (void)RequestAllSlavesSafeOp();
        (void)RequestAllSlavesInit();
    }

    // --------------------------------------------------------
    // 통신을 중지하고 열린 네트워크 어댑터를 닫습니다.
    // --------------------------------------------------------
    ecx_close(&context_);

    std::memset(
        &context_,
        0,
        sizeof(context_));

    context_.manualstatechange = 1;

    slaveCount_ = 0;

    ResetProcessDataMap();
    ResetAdcRuntimeInfo();
    ResetServoRuntimeInfo();
    ResetIoRuntimeInfo();
    ResetEncoderRuntimeInfo();


    isOpen_ = false;
}

bool DaoEtherCATMaster::IsOpen() const
{
    return isOpen_;
}

bool DaoEtherCATMaster::IsCommunicationRunning() const
{
    return communicationRunning_.load();
}

bool DaoEtherCATMaster::StartCommunication()
{
    // 통신 스레드의 실행 상태를 확인합니다.
    if (communicationRunning_.load())
    {
        return false;
    }

    // 아래 조건을 확인한 후 현재 처리 단계를 계속합니다.
    // 아래 조건을 확인한 후 현재 처리 단계를 계속합니다.
    if (communicationThread_.joinable())
    {
        communicationThread_.join();
    }

    // Process Data 매핑이 완료된 경우에만 다음 처리를 수행합니다.
    // Process Data 매핑이 완료된 경우에만 다음 처리를 수행합니다.
    if (!isOpen_ ||
        slaveCount_ <= 0 ||
        !processDataMapped_)
    {
        return false;
    }

    communicationStopRequested_.store(false);
    communicationRunning_.store(true);

    try
    {
        communicationThread_ =
            std::thread(
                &DaoEtherCATMaster::CommunicationThreadMain,
                this);
    }
    catch (...)
    {
        communicationRunning_.store(false);
        communicationStopRequested_.store(true);
        return false;
    }

    return true;
}

void DaoEtherCATMaster::StopCommunication()
{
    // 주기 통신 스레드의 중지를 요청합니다.
    communicationStopRequested_.store(true);

    // 아래 조건을 확인한 후 현재 처리 단계를 계속합니다.
    // 아래 조건을 확인한 후 현재 처리 단계를 계속합니다.
    if (communicationThread_.joinable())
    {
        communicationThread_.join();
    }

    communicationRunning_.store(false);
}

int DaoEtherCATMaster::ScanSlaves()
{
    if (!isOpen_)
    {
        slaveCount_ = 0;
        ResetProcessDataMap();
		ResetAdcRuntimeInfo();// PDO 매핑 결과와 입출력 크기를 초기화합니다.
        return 0;
    }

    ResetProcessDataMap();
	ResetAdcRuntimeInfo();// 연결된 EtherCAT Slave를 검색합니다.

    slaveCount_ =
        ecx_config_init(&context_);

    if (slaveCount_ <= 0)
    {
        slaveCount_ = 0;
        ResetAdcRuntimeInfo();
        return 0;
    }

    slaveCount_ =
        context_.slavecount;

    // ADC 런타임 상태를 초기값으로 되돌립니다.
    // ADC 런타임 상태를 초기값으로 되돌립니다.
    ResetAdcRuntimeInfo();
    ResetServoRuntimeInfo();
    ResetIoRuntimeInfo();
    ResetEncoderRuntimeInfo();

    return slaveCount_;
}

int DaoEtherCATMaster::GetSlaveCount() const
{
    return slaveCount_;
}
bool DaoEtherCATMaster::RequestAllSlavesPreOp()
{
    // 검색된 모든 Slave를 PRE-OP 상태로 전환합니다.
    if (!isOpen_)
    {
        return false;
    }

    // 이 값은 처리된 프레임 또는 샘플의 누적 개수를 나타냅니다.
    if (slaveCount_ <= 0)
    {
        return false;
    }

    // 검색된 모든 Slave를 PRE-OP 상태로 전환합니다.
    context_.slavelist[0].state =
        EC_STATE_PRE_OP;

    // 검색된 모든 Slave를 PRE-OP 상태로 전환합니다.
    const int writeResult =
        ecx_writestate(
            &context_,
            0);

    if (writeResult <= 0)
    {
        return false;
    }

    // 검색된 모든 Slave를 PRE-OP 상태로 전환합니다.
    const uint16 reachedState =
        ecx_statecheck(
            &context_,
            0,
            EC_STATE_PRE_OP,
            EC_TIMEOUTSTATE);

    // 검색된 모든 Slave를 PRE-OP 상태로 전환합니다.
    ecx_readstate(&context_);

    return reachedState == EC_STATE_PRE_OP;
}

bool DaoEtherCATMaster::GetSlaveInfo(
    int slaveListIndex,
    DaoInternalSlaveInfo& slaveInfo) const
{
    if (!isOpen_)
    {
        return false;
    }

    if (slaveListIndex < 0 ||
        slaveListIndex >= slaveCount_)
    {
        return false;
    }

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    const int physicalSlaveIndex =
        slaveListIndex + 1;

    const ec_slavet& soemSlave =
        context_.slavelist[physicalSlaveIndex];

    slaveInfo.listIndex =
        slaveListIndex;

    slaveInfo.physicalSlaveIndex =
        physicalSlaveIndex;

    slaveInfo.name =
        soemSlave.name;

    slaveInfo.vendorId =
        static_cast<unsigned int>(
            soemSlave.eep_man);

    slaveInfo.productCode =
        static_cast<unsigned int>(
            soemSlave.eep_id);

    slaveInfo.revision =
        static_cast<unsigned int>(
            soemSlave.eep_rev);

    slaveInfo.state =
        static_cast<unsigned short>(
            soemSlave.state);

    slaveInfo.alStatusCode =
        static_cast<unsigned short>(
            soemSlave.ALstatuscode);

    return true;
}

bool DaoEtherCATMaster::RequestAllSlavesSafeOp()
{
    // --------------------------------------------------------
    // 검색된 모든 Slave를 SAFE-OP 상태로 전환합니다.
    // --------------------------------------------------------
    if (!isOpen_)
    {
        return false;
    }

    if (slaveCount_ <= 0)
    {
        return false;
    }

    // 통신 스레드의 실행 상태를 확인합니다.
    if (communicationRunning_.load())
    {
        return false;
    }

    // --------------------------------------------------------
    // Slave의 현재 상태와 AL 상태 코드를 갱신합니다.
    // --------------------------------------------------------
    ecx_readstate(&context_);

    const std::uint16_t currentBaseState =
        static_cast<std::uint16_t>(
            context_.slavelist[0].state & 0x000F);

    // 검색된 모든 Slave를 SAFE-OP 상태로 전환합니다.
    if (currentBaseState == EC_STATE_SAFE_OP)
    {
        return true;
    }

    // --------------------------------------------------------
    // 검색된 모든 Slave를 SAFE-OP 상태로 전환합니다.
    //
    // 검색된 모든 Slave를 SAFE-OP 상태로 전환합니다.
    // --------------------------------------------------------
    context_.slavelist[0].state =
        EC_STATE_SAFE_OP;

    const int writeResult =
        ecx_writestate(
            &context_,
            0);

    if (writeResult <= 0)
    {
        return false;
    }

    // --------------------------------------------------------
    // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.
    // --------------------------------------------------------
    constexpr int SAFE_OP_TIMEOUT_US =
        5 * 1000 * 1000;

    const std::uint16_t reachedState =
        ecx_statecheck(
            &context_,
            0,
            EC_STATE_SAFE_OP,
            SAFE_OP_TIMEOUT_US);

    ecx_readstate(&context_);

    const std::uint16_t finalBaseState =
        static_cast<std::uint16_t>(
            context_.slavelist[0].state & 0x000F);

    return
        reachedState == EC_STATE_SAFE_OP &&
        finalBaseState == EC_STATE_SAFE_OP;
}

bool DaoEtherCATMaster::RequestAllSlavesOperational()
{
    // --------------------------------------------------------
    // 검색된 모든 Slave를 OP 상태로 전환합니다.
    // --------------------------------------------------------
    if (!isOpen_)
    {
        return false;
    }

    if (slaveCount_ <= 0)
    {
        return false;
    }

    if (!processDataMapped_)
    {
        return false;
    }

    if (communicationRunning_.load())
    {
        return false;
    }

    // --------------------------------------------------------
    // Slave의 현재 상태와 AL 상태 코드를 갱신합니다.
    // --------------------------------------------------------
    ecx_readstate(&context_);

    const std::uint16_t currentBaseState =
        static_cast<std::uint16_t>(
            context_.slavelist[0].state & 0x000F);

    if (currentBaseState == EC_STATE_OPERATIONAL)
    {
        return true;
    }

    if (currentBaseState != EC_STATE_SAFE_OP)
    {
        return false;
    }

    // --------------------------------------------------------
    // OP 전환 전에 여러 차례 Process Data를 교환해 통신을 안정화합니다.
    // --------------------------------------------------------
    constexpr int PRIMING_ROUNDS = 10;

    for (int round = 0;
        round < PRIMING_ROUNDS;
        ++round)
    {
        // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
        for (int physicalSlaveIndex = 1;
            physicalSlaveIndex <= slaveCount_;
            ++physicalSlaveIndex)
        {
            ec_slavet& slave =
                context_.slavelist[
                    physicalSlaveIndex];

            if (!IsDaoAdcSlave(slave))
            {
                continue;
            }

            if (slave.outputs == nullptr ||
                slave.Obytes != 4)
            {
                return false;
            }

            std::memset(
                slave.outputs,
                0,
                static_cast<std::size_t>(
                    slave.Obytes));
        }

        // Process Data를 송신하고 수신 WKC를 확인합니다.
        PrepareServoAndIoOutputs();

        ecx_send_processdata(
            &context_);

        const int actualWkc =
            ecx_receive_processdata(
                &context_,
                EC_TIMEOUTRET);

        if (actualWkc < expectedWkc_)
        {
            return false;
        }
    }

    // --------------------------------------------------------
    // 검색된 모든 Slave를 OP 상태로 전환합니다.
    // --------------------------------------------------------
    context_.slavelist[0].state =
        EC_STATE_OPERATIONAL;

    const int writeResult =
        ecx_writestate(
            &context_,
            0);

    if (writeResult <= 0)
    {
        return false;
    }

    // --------------------------------------------------------
    // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.
    // --------------------------------------------------------
    constexpr int OP_TIMEOUT_MS = 12000;

    const auto startTime =
        std::chrono::steady_clock::now();

    while (true)
    {
        const auto now =
            std::chrono::steady_clock::now();

        const auto elapsedMs =
            std::chrono::duration_cast<
            std::chrono::milliseconds>(
                now - startTime)
            .count();

        if (elapsedMs >= OP_TIMEOUT_MS)
        {
            break;
        }

        // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
        for (int physicalSlaveIndex = 1;
            physicalSlaveIndex <= slaveCount_;
            ++physicalSlaveIndex)
        {
            ec_slavet& slave =
                context_.slavelist[
                    physicalSlaveIndex];

            if (!IsDaoAdcSlave(slave))
            {
                continue;
            }

            if (slave.outputs == nullptr ||
                slave.Obytes != 4)
            {
                return false;
            }

            std::memset(
                slave.outputs,
                0,
                static_cast<std::size_t>(
                    slave.Obytes));
        }

        PrepareServoAndIoOutputs();

        ecx_send_processdata(
            &context_);

        const int actualWkc =
            ecx_receive_processdata(
                &context_,
                EC_TIMEOUTRET);

        if (actualWkc < expectedWkc_)
        {
            return false;
        }

        (void)ecx_statecheck(
            &context_,
            0,
            EC_STATE_OPERATIONAL,
            100000);

        ecx_readstate(&context_);

        const std::uint16_t finalBaseState =
            static_cast<std::uint16_t>(
                context_.slavelist[0].state & 0x000F);

        if (finalBaseState ==
            EC_STATE_OPERATIONAL)
        {
            return true;
        }

        if ((context_.slavelist[0].state &
            EC_STATE_ERROR) != 0)
        {
            return false;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(10));
    }

    return false;
}

bool DaoEtherCATMaster::RequestAllSlavesInit()
{
    // --------------------------------------------------------
    // 검색된 모든 Slave를 INIT 상태로 전환합니다.
    // --------------------------------------------------------
    if (!isOpen_)
    {
        return false;
    }

    if (slaveCount_ <= 0)
    {
        return false;
    }

    if (communicationRunning_.load())
    {
        return false;
    }

    // --------------------------------------------------------
    // Slave의 현재 상태와 AL 상태 코드를 갱신합니다.
    // --------------------------------------------------------
    ecx_readstate(&context_);

    const std::uint16_t currentBaseState =
        static_cast<std::uint16_t>(
            context_.slavelist[0].state & 0x000F);

    // 검색된 모든 Slave를 INIT 상태로 전환합니다.
    if (currentBaseState == EC_STATE_INIT)
    {
        return true;
    }

    // --------------------------------------------------------
    // 검색된 모든 Slave를 INIT 상태로 전환합니다.
    // --------------------------------------------------------
    context_.slavelist[0].state =
        EC_STATE_INIT;

    const int writeResult =
        ecx_writestate(
            &context_,
            0);

    if (writeResult <= 0)
    {
        return false;
    }

    // --------------------------------------------------------
    // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.
    // --------------------------------------------------------
    constexpr int INIT_TIMEOUT_US =
        5 * 1000 * 1000;

    const std::uint16_t reachedState =
        ecx_statecheck(
            &context_,
            0,
            EC_STATE_INIT,
            INIT_TIMEOUT_US);

    ecx_readstate(&context_);

    const std::uint16_t finalBaseState =
        static_cast<std::uint16_t>(
            context_.slavelist[0].state & 0x000F);

    return
        reachedState == EC_STATE_INIT &&
        finalBaseState == EC_STATE_INIT;
}

void DaoEtherCATMaster::ResetProcessDataMap()
{
    ioMap_.fill(0);

    mappedBytes_ = 0;
    outputWkc_ = 0;
    inputWkc_ = 0;
    expectedWkc_ = 0;

    processDataMapped_ = false;
}

bool DaoEtherCATMaster::IsDaoAdcSlave(
    const ec_slavet& slave) const
{
    constexpr std::uint32_t DAO_VENDOR_ID =
        0x000011C0;

    constexpr std::uint32_t DAO_ADC_PRODUCT_CODE =
        0x0000DA01;

    return
        slave.eep_man == DAO_VENDOR_ID &&
        slave.eep_id == DAO_ADC_PRODUCT_CODE;
}

bool DaoEtherCATMaster::IsLsL7nhServo(
    int physicalSlaveIndex) const
{
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    if (physicalSlaveIndex <= 0 ||
        physicalSlaveIndex > slaveCount_)
    {
        return false;
    }

    const ec_slavet& slave =
        context_.slavelist[physicalSlaveIndex];

    constexpr uint32_t LS_MECAPION_VENDOR_ID =
        0x00007595;

    constexpr uint32_t LS_L7NH_PRODUCT_CODE =
        0x00010001;

    constexpr uint32_t LS_L7NH_REVISION =
        0x00000001;

    return
        slave.eep_man == LS_MECAPION_VENDOR_ID &&
        slave.eep_id == LS_L7NH_PRODUCT_CODE &&
        slave.eep_rev == LS_L7NH_REVISION;
}

bool DaoEtherCATMaster::ConfigureLsL7nhBasicPdo(
    int physicalSlaveIndex)
{
    // --------------------------------------------------------
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // --------------------------------------------------------
    if (!IsLsL7nhServo(
        physicalSlaveIndex))
    {
        return false;
    }

    // Slave의 현재 상태와 AL 상태 코드를 갱신합니다.
    ecx_readstate(&context_);

    const ec_slavet& slave =
        context_.slavelist[
            physicalSlaveIndex];

    const std::uint16_t baseState =
        static_cast<std::uint16_t>(
            slave.state & 0x000F);

    if (baseState != EC_STATE_PRE_OP)
    {
        return false;
    }

    constexpr int SDO_TIMEOUT_US =
        2 * 1000 * 1000;

    const std::uint16_t slaveIndex =
        static_cast<std::uint16_t>(
            physicalSlaveIndex);

    int writeWkc = 0;


    // ========================================================
    // RxPDO 0x1601
    //
    // Master -> Servo
    //
    // 1 : 6040:00 16bit Controlword
    // 2 : 6060:00  8bit Modes of Operation
    // 3 : 607A:00 32bit Target Position
    // 4 : 6081:00 32bit Profile Velocity
    // 5 : 6083:00 32bit Profile Acceleration
    // 6 : 6084:00 32bit Profile Deceleration
    // 7 : 60FF:00 32bit Target Velocity
    // 8 : 60B8:00 16bit Touch Probe Function
    // 9 : 60FE:01 32bit Digital Outputs
    //
    // Total = 29 bytes
    // ========================================================

    // LS L7NH Servo의 RxPDO와 TxPDO 매핑을 SDO로 설정합니다.
    std::uint8_t rxAssignmentCount = 0;

    writeWkc =
        ecx_SDOwrite(
            &context_,
            slaveIndex,
            0x1C12,
            0x00,
            false,
            sizeof(rxAssignmentCount),
            &rxAssignmentCount,
            SDO_TIMEOUT_US);

    if (writeWkc <= 0)
    {
        return false;
    }


    // 이 값은 처리된 프레임 또는 샘플의 누적 개수를 나타냅니다.
    std::uint8_t rxMappingCount = 0;

    writeWkc =
        ecx_SDOwrite(
            &context_,
            slaveIndex,
            0x1601,
            0x00,
            false,
            sizeof(rxMappingCount),
            &rxMappingCount,
            SDO_TIMEOUT_US);

    if (writeWkc <= 0)
    {
        return false;
    }


    const std::uint32_t rxMappingEntries[] =
    {
        0x60400010, // 1. Controlword
        0x60600008, // 2. Modes of Operation
        0x607A0020, // 3. Target Position
        0x60810020, // 4. Profile Velocity
        0x60830020, // 5. Profile Acceleration
        0x60840020, // 6. Profile Deceleration
        0x60FF0020, // 7. Target Velocity
        0x60B80010, // 8. Touch Probe Function
        0x60FE0120  // 9. Digital Outputs
    };

    constexpr std::uint8_t RX_MAPPING_COUNT = 9;

    for (std::uint8_t subIndex = 1;
        subIndex <= RX_MAPPING_COUNT;
        ++subIndex)
    {
        const std::uint32_t mappingValue =
            rxMappingEntries[
                static_cast<std::size_t>(
                    subIndex - 1)];

        writeWkc =
            ecx_SDOwrite(
                &context_,
                slaveIndex,
                0x1601,
                subIndex,
                false,
                sizeof(mappingValue),
                &mappingValue,
                SDO_TIMEOUT_US);

        if (writeWkc <= 0)
        {
            return false;
        }
    }


    // LS L7NH Servo의 RxPDO와 TxPDO 매핑을 SDO로 설정합니다.
    rxMappingCount =
        RX_MAPPING_COUNT;

    writeWkc =
        ecx_SDOwrite(
            &context_,
            slaveIndex,
            0x1601,
            0x00,
            false,
            sizeof(rxMappingCount),
            &rxMappingCount,
            SDO_TIMEOUT_US);

    if (writeWkc <= 0)
    {
        return false;
    }


    // 이 구조체는 장치의 PDO 및 런타임 정보를 저장합니다.
    std::uint16_t rxPdoIndex =
        0x1601;

    writeWkc =
        ecx_SDOwrite(
            &context_,
            slaveIndex,
            0x1C12,
            0x01,
            false,
            sizeof(rxPdoIndex),
            &rxPdoIndex,
            SDO_TIMEOUT_US);

    if (writeWkc <= 0)
    {
        return false;
    }

    rxAssignmentCount = 1;

    writeWkc =
        ecx_SDOwrite(
            &context_,
            slaveIndex,
            0x1C12,
            0x00,
            false,
            sizeof(rxAssignmentCount),
            &rxAssignmentCount,
            SDO_TIMEOUT_US);

    if (writeWkc <= 0)
    {
        return false;
    }


    // ========================================================
    // TxPDO 0x1A01
    //
    // Servo -> Master
    //
    // 1 : 6041:00 16bit Statusword
    // 2 : 6061:00  8bit Modes of Operation Display
    // 3 : 6064:00 32bit Actual Position
    // 4 : 60F4:00 32bit Position Error
    // 5 : 60B9:00 16bit Touch Probe Status
    // 6 : 60BA:00 32bit Touch Probe Position
    // 7 : 60FD:00 32bit Digital Inputs
    //
    // Total = 21 bytes
    // ========================================================

    // LS L7NH Servo의 RxPDO와 TxPDO 매핑을 SDO로 설정합니다.
    std::uint8_t txAssignmentCount = 0;

    writeWkc =
        ecx_SDOwrite(
            &context_,
            slaveIndex,
            0x1C13,
            0x00,
            false,
            sizeof(txAssignmentCount),
            &txAssignmentCount,
            SDO_TIMEOUT_US);

    if (writeWkc <= 0)
    {
        return false;
    }


    // 이 값은 처리된 프레임 또는 샘플의 누적 개수를 나타냅니다.
    std::uint8_t txMappingCount = 0;

    writeWkc =
        ecx_SDOwrite(
            &context_,
            slaveIndex,
            0x1A01,
            0x00,
            false,
            sizeof(txMappingCount),
            &txMappingCount,
            SDO_TIMEOUT_US);

    if (writeWkc <= 0)
    {
        return false;
    }


    const std::uint32_t txMappingEntries[] =
    {
        0x60410010, // 1. Statusword
        0x60610008, // 2. Modes of Operation Display
        0x60640020, // 3. Position Actual Value
        0x60F40020, // 4. Following Error Actual Value
        0x60B90010, // 5. Touch Probe Status
        0x60BA0020, // 6. Touch Probe Position
        0x60FD0020  // 7. Digital Inputs
    };

    constexpr std::uint8_t TX_MAPPING_COUNT = 7;

    for (std::uint8_t subIndex = 1;
        subIndex <= TX_MAPPING_COUNT;
        ++subIndex)
    {
        const std::uint32_t mappingValue =
            txMappingEntries[
                static_cast<std::size_t>(
                    subIndex - 1)];

        writeWkc =
            ecx_SDOwrite(
                &context_,
                slaveIndex,
                0x1A01,
                subIndex,
                false,
                sizeof(mappingValue),
                &mappingValue,
                SDO_TIMEOUT_US);

        if (writeWkc <= 0)
        {
            return false;
        }
    }


    // LS L7NH Servo의 RxPDO와 TxPDO 매핑을 SDO로 설정합니다.
    txMappingCount =
        TX_MAPPING_COUNT;

    writeWkc =
        ecx_SDOwrite(
            &context_,
            slaveIndex,
            0x1A01,
            0x00,
            false,
            sizeof(txMappingCount),
            &txMappingCount,
            SDO_TIMEOUT_US);

    if (writeWkc <= 0)
    {
        return false;
    }


    // 이 구조체는 장치의 PDO 및 런타임 정보를 저장합니다.
    std::uint16_t txPdoIndex =
        0x1A01;

    writeWkc =
        ecx_SDOwrite(
            &context_,
            slaveIndex,
            0x1C13,
            0x01,
            false,
            sizeof(txPdoIndex),
            &txPdoIndex,
            SDO_TIMEOUT_US);

    if (writeWkc <= 0)
    {
        return false;
    }

    txAssignmentCount = 1;

    writeWkc =
        ecx_SDOwrite(
            &context_,
            slaveIndex,
            0x1C13,
            0x00,
            false,
            sizeof(txAssignmentCount),
            &txAssignmentCount,
            SDO_TIMEOUT_US);

    if (writeWkc <= 0)
    {
        return false;
    }


    return true;
}


bool DaoEtherCATMaster::ConfigureLsL7nhProfilePositionMode(
    int physicalSlaveIndex,
    unsigned int profileVelocity,
    unsigned int profileAcceleration,
    unsigned int profileDeceleration)
{
    if (!isOpen_)
    {
        return false;
    }

    if (physicalSlaveIndex <= 0 ||
        physicalSlaveIndex > context_.slavecount)
    {
        return false;
    }

    // --------------------------------------------------------
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    //
    // 0x6060:00 = 1    Profile Position Mode
    // 0x6081:00        Profile Velocity
    // 0x6083:00        Profile Acceleration
    // 0x6084:00        Profile Deceleration
    // --------------------------------------------------------
    const std::int8_t profilePositionMode =
        1;

    int writeWkc =
        ecx_SDOwrite(
            &context_,
            physicalSlaveIndex,
            0x6060,
            0x00,
            FALSE,
            sizeof(profilePositionMode),
            &profilePositionMode,
            2000000);

    if (writeWkc <= 0)
    {
        return false;
    }

    writeWkc =
        ecx_SDOwrite(
            &context_,
            physicalSlaveIndex,
            0x6081,
            0x00,
            FALSE,
            sizeof(profileVelocity),
            &profileVelocity,
            2000000);

    if (writeWkc <= 0)
    {
        return false;
    }

    writeWkc =
        ecx_SDOwrite(
            &context_,
            physicalSlaveIndex,
            0x6083,
            0x00,
            FALSE,
            sizeof(profileAcceleration),
            &profileAcceleration,
            2000000);

    if (writeWkc <= 0)
    {
        return false;
    }

    writeWkc =
        ecx_SDOwrite(
            &context_,
            physicalSlaveIndex,
            0x6084,
            0x00,
            FALSE,
            sizeof(profileDeceleration),
            &profileDeceleration,
            2000000);

    if (writeWkc <= 0)
    {
        return false;
    }

    return true;
}


bool DaoEtherCATMaster::ConfigureLsL7nhOperationMode(
    int physicalSlaveIndex,
    signed char mode)
{
    if (!isOpen_)
    {
        return false;
    }

    if (!IsLsL7nhServo(
        physicalSlaveIndex))
    {
        return false;
    }

    // 아래 조건을 확인한 후 현재 처리 단계를 계속합니다.
    if (mode != 1 &&
        mode != 3 &&
        mode != 6)
    {
        return false;
    }

    constexpr int SDO_TIMEOUT_US =
        2 * 1000 * 1000;

    const std::int8_t operationMode =
        static_cast<std::int8_t>(
            mode);

    const int writeWkc =
        ecx_SDOwrite(
            &context_,
            static_cast<std::uint16_t>(
                physicalSlaveIndex),
            0x6060,
            0x00,
            false,
            sizeof(operationMode),
            &operationMode,
            SDO_TIMEOUT_US);

    return writeWkc > 0;
}


bool DaoEtherCATMaster::IsFastechIo(
    int physicalSlaveIndex) const
{
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    if (physicalSlaveIndex <= 0 ||
        physicalSlaveIndex > slaveCount_)
    {
        return false;
    }

    const ec_slavet& slave =
        context_.slavelist[physicalSlaveIndex];

    constexpr uint32_t FASTECH_VENDOR_ID =
        0x0FA00000;

    constexpr uint32_t FASTECH_IN8OUT8_PRODUCT_CODE =
        0x00002021;

    constexpr uint32_t FASTECH_IN16OUT16_PRODUCT_CODE =
        0x00002023;

    constexpr uint32_t FASTECH_IO_REVISION =
        0x00000001;

    const bool supportedProduct =
        slave.eep_id == FASTECH_IN8OUT8_PRODUCT_CODE ||
        slave.eep_id == FASTECH_IN16OUT16_PRODUCT_CODE;

    return
        slave.eep_man == FASTECH_VENDOR_ID &&
        supportedProduct &&
        slave.eep_rev == FASTECH_IO_REVISION;
}

bool DaoEtherCATMaster::IsFastechEncoder(
    int physicalSlaveIndex) const
{
    if (physicalSlaveIndex <= 0 ||
        physicalSlaveIndex > slaveCount_)
    {
        return false;
    }

    const ec_slavet& slave =
        context_.slavelist[physicalSlaveIndex];

    constexpr uint32_t FASTECH_VENDOR_ID =
        0x0FA00000;

    constexpr uint32_t FASTECH_CNT02_PRODUCT_CODE =
        0x00002301;

    return
        slave.eep_man == FASTECH_VENDOR_ID &&
        slave.eep_id == FASTECH_CNT02_PRODUCT_CODE;
}

bool DaoEtherCATMaster::MapProcessData()
{
    // 전체 Slave의 Process Data 영역을 IO Map에 매핑합니다.
    if (!isOpen_)
    {
        ResetProcessDataMap();
        return false;
    }

    // PDO 매핑 결과와 입출력 크기를 초기화합니다.
    if (slaveCount_ <= 0)
    {
        ResetProcessDataMap();
        return false;
    }

    // PDO 매핑 결과와 입출력 크기를 초기화합니다.
    ResetProcessDataMap();


    // --------------------------------------------------------
    // LS L7NH Servo의 RxPDO와 TxPDO 매핑을 SDO로 설정합니다.
    //
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // --------------------------------------------------------
    for (int physicalSlaveIndex = 1;
        physicalSlaveIndex <= slaveCount_;
        ++physicalSlaveIndex)
    {
        if (!IsLsL7nhServo(
            physicalSlaveIndex))
        {
            continue;
        }

        if (!ConfigureLsL7nhBasicPdo(
            physicalSlaveIndex))
        {
            ResetProcessDataMap();
            return false;
        }
        


    }

    // 요청된 정보를 출력 구조체에 복사합니다.
    std::vector<SavedCoeInfo> savedCoeInfoList;

    // 요청된 정보를 출력 구조체에 복사합니다.
    savedCoeInfoList.reserve(
        static_cast<std::size_t>(slaveCount_));

    // --------------------------------------------------------
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    //
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // --------------------------------------------------------
    for (int physicalSlaveIndex = 1;
        physicalSlaveIndex <= slaveCount_;
        ++physicalSlaveIndex)
    {
        ec_slavet& slave =
            context_.slavelist[physicalSlaveIndex];

        // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
        if (!IsDaoAdcSlave(slave))
        {
            continue;
        }

        SavedCoeInfo savedInfo{};

        savedInfo.physicalSlaveIndex =
            physicalSlaveIndex;

        savedInfo.mailboxProtocol =
            slave.mbx_proto;

        savedInfo.coeDetails =
            slave.CoEdetails;

        savedCoeInfoList.push_back(
            savedInfo);

        // 요청된 정보를 출력 구조체에 복사합니다.
        const std::uint16_t coeMask =
            static_cast<std::uint16_t>(
                ECT_MBXPROT_COE);

        slave.mbx_proto =
            static_cast<std::uint16_t>(
                slave.mbx_proto &
                static_cast<std::uint16_t>(
                    ~coeMask));

        slave.CoEdetails = 0;
    }


    // --------------------------------------------------------
    // 전체 Slave의 Process Data 영역을 IO Map에 매핑합니다.
    // --------------------------------------------------------

    mappedBytes_ =
        ecx_config_map_group(
            &context_,
            ioMap_.data(),
            0);

    // --------------------------------------------------------
    // 요청된 정보를 출력 구조체에 복사합니다.
    // 요청된 정보를 출력 구조체에 복사합니다.
    // --------------------------------------------------------
    for (const SavedCoeInfo& savedInfo :
        savedCoeInfoList)
    {
        ec_slavet& slave =
            context_.slavelist[
                savedInfo.physicalSlaveIndex];

        slave.mbx_proto =
            savedInfo.mailboxProtocol;

        slave.CoEdetails =
            savedInfo.coeDetails;
    }

    // PDO 매핑 결과와 입출력 크기를 초기화합니다.
    if (mappedBytes_ <= 0)
    {
        ResetProcessDataMap();
        return false;
    }

    // PDO 메모리 포인터가 유효한지 확인합니다.
    const ec_groupt& group =
        context_.grouplist[0];

    outputWkc_ =
        group.outputsWKC;

    inputWkc_ =
        group.inputsWKC;

    expectedWkc_ =
        (outputWkc_ * 2) +
        inputWkc_;

    processDataMapped_ = true;

    // Process Data 매핑이 완료된 경우에만 다음 처리를 수행합니다.
    // Process Data 매핑이 완료된 경우에만 다음 처리를 수행합니다.
    ResetAdcRuntimeInfo();
    ResetServoRuntimeInfo();
    ResetIoRuntimeInfo();
    ResetEncoderRuntimeInfo();

    ConfigureServoAndIoRuntimeInfo();
    ConfigureEncoderRuntimeInfo();

    return true;
}

bool DaoEtherCATMaster::GetProcessDataMapInfo(
    DaoInternalProcessDataMapInfo& mapInfo) const
{
    if (!processDataMapped_)
    {
        return false;
    }

    mapInfo.mappedBytes =
        mappedBytes_;

    mapInfo.outputWkc =
        outputWkc_;

    mapInfo.inputWkc =
        inputWkc_;

    mapInfo.expectedWkc =
        expectedWkc_;

    return true;
}

bool DaoEtherCATMaster::GetSlavePdoInfo(
    int slaveListIndex,
    DaoInternalSlavePdoInfo& pdoInfo) const
{
    if (!processDataMapped_)
    {
        return false;
    }

    if (slaveListIndex < 0 ||
        slaveListIndex >= slaveCount_)
    {
        return false;
    }

    const int physicalSlaveIndex =
        slaveListIndex + 1;

    const ec_slavet& slave =
        context_.slavelist[
            physicalSlaveIndex];

    pdoInfo.listIndex =
        slaveListIndex;

    pdoInfo.physicalSlaveIndex =
        physicalSlaveIndex;

    pdoInfo.outputBytes =
        static_cast<unsigned int>(
            slave.Obytes);

    pdoInfo.inputBytes =
        static_cast<unsigned int>(
            slave.Ibytes);

    return true;
}
bool DaoEtherCATMaster::ValidateDaoAdcPdo(
    int physicalSlaveIndex,
    DaoInternalAdcValidationInfo& validationInfo) const
{
    // --------------------------------------------------------
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // --------------------------------------------------------
    validationInfo = {};

    validationInfo.physicalSlaveIndex =
        physicalSlaveIndex;

    // --------------------------------------------------------
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // --------------------------------------------------------
    validationInfo.processDataMapped =
        processDataMapped_;

    if (!processDataMapped_)
    {
        return false;
    }

    // --------------------------------------------------------
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    //
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // --------------------------------------------------------
    validationInfo.slaveIndexValid =
        physicalSlaveIndex >= 1 &&
        physicalSlaveIndex <= slaveCount_;

    if (!validationInfo.slaveIndexValid)
    {
        return false;
    }

    const ec_slavet& slave =
        context_.slavelist[
            physicalSlaveIndex];

    // --------------------------------------------------------
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // --------------------------------------------------------
    validationInfo.identityValid =
        IsDaoAdcSlave(slave);

    if (!validationInfo.identityValid)
    {
        return false;
    }

    // --------------------------------------------------------
    // Slave의 출력 PDO 크기와 입력 PDO 크기를 확인합니다.
    // --------------------------------------------------------
    validationInfo.actualOutputBytes =
        static_cast<unsigned int>(
            slave.Obytes);

    validationInfo.actualInputBytes =
        static_cast<unsigned int>(
            slave.Ibytes);

    // Slave의 출력 PDO 크기와 입력 PDO 크기를 확인합니다.
    constexpr unsigned int
        DAO_ADC_OUTPUT_BYTES = 4;

    constexpr unsigned int
        DAO_ADC_INPUT_BYTES = 24;

    validationInfo.outputSizeValid =
        validationInfo.actualOutputBytes ==
        DAO_ADC_OUTPUT_BYTES;

    validationInfo.inputSizeValid =
        validationInfo.actualInputBytes ==
        DAO_ADC_INPUT_BYTES;

    if (!validationInfo.outputSizeValid ||
        !validationInfo.inputSizeValid)
    {
        return false;
    }

    // --------------------------------------------------------
    // PDO 메모리 포인터가 유효한지 확인합니다.
    //
    // PDO 메모리 포인터가 유효한지 확인합니다.
    // PDO 메모리 포인터가 유효한지 확인합니다.
    // --------------------------------------------------------
    validationInfo.outputPointerValid =
        slave.outputs != nullptr;

    validationInfo.inputPointerValid =
        slave.inputs != nullptr;

    if (!validationInfo.outputPointerValid ||
        !validationInfo.inputPointerValid)
    {
        return false;
    }

    // --------------------------------------------------------
    // ADC Slave를 SAFE-OP 상태로 전환합니다.
    // --------------------------------------------------------
    return true;
}

bool DaoEtherCATMaster::RequestDaoAdcSafeOp(
    int physicalSlaveIndex)
{
    // 통신 스레드의 실행 상태를 확인합니다.
    if (communicationRunning_.load())
    {
        return false;
    }

    // --------------------------------------------------------
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // --------------------------------------------------------
    DaoInternalAdcValidationInfo validationInfo{};

    if (!ValidateDaoAdcPdo(
        physicalSlaveIndex,
        validationInfo))
    {
        return false;
    }

    // --------------------------------------------------------
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    //
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // --------------------------------------------------------
    ec_slavet& slave =
        context_.slavelist[
            physicalSlaveIndex];

    // Slave의 현재 상태와 AL 상태 코드를 갱신합니다.
    ecx_readstate(&context_);

    const std::uint16_t currentBaseState =
        static_cast<std::uint16_t>(
            slave.state & 0x000F);

    // 검색된 모든 Slave를 SAFE-OP 상태로 전환합니다.
    if (currentBaseState ==
        EC_STATE_SAFE_OP)
    {
        return true;
    }

    // 검색된 모든 Slave를 PRE-OP 상태로 전환합니다.
    if (currentBaseState !=
        EC_STATE_PRE_OP)
    {
        return false;
    }

    // --------------------------------------------------------
    // 검색된 모든 Slave를 SAFE-OP 상태로 전환합니다.
    //
    // 검색된 모든 Slave를 SAFE-OP 상태로 전환합니다.
    // --------------------------------------------------------
    slave.state =
        EC_STATE_SAFE_OP;

    const int writeWkc =
        ecx_writestate(
            &context_,
            static_cast<std::uint16_t>(
                physicalSlaveIndex));

    if (writeWkc <= 0)
    {
        return false;
    }

    // --------------------------------------------------------
    // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.
    //
    // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.
    // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.
    // --------------------------------------------------------
    constexpr int SAFE_OP_TIMEOUT_US =
        12 * 1000 * 1000;

    const std::uint16_t reachedState =
        ecx_statecheck(
            &context_,
            static_cast<std::uint16_t>(
                physicalSlaveIndex),
            EC_STATE_SAFE_OP,
            SAFE_OP_TIMEOUT_US);

    // 검색된 모든 Slave를 SAFE-OP 상태로 전환합니다.
    ecx_readstate(&context_);

    const std::uint16_t finalBaseState =
        static_cast<std::uint16_t>(
            slave.state & 0x000F);

    // 검색된 모든 Slave를 SAFE-OP 상태로 전환합니다.
    return
        reachedState == EC_STATE_SAFE_OP &&
        finalBaseState == EC_STATE_SAFE_OP &&
        slave.ALstatuscode == 0;
}

bool DaoEtherCATMaster::ExchangeDaoAdcProcessDataOnce(
    int physicalSlaveIndex,
    DaoInternalProcessExchangeInfo& exchangeInfo)
{
    // --------------------------------------------------------
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // --------------------------------------------------------
    exchangeInfo = {};

    exchangeInfo.physicalSlaveIndex =
        physicalSlaveIndex;

    exchangeInfo.expectedWkc =
        expectedWkc_;

    // 통신 스레드의 실행 상태를 확인합니다.
    // 통신 스레드의 실행 상태를 확인합니다.
    if (communicationRunning_.load())
    {
        return false;
    }

    // --------------------------------------------------------
    // Process Data 매핑이 완료된 경우에만 다음 처리를 수행합니다.
    // --------------------------------------------------------
    if (!processDataMapped_)
    {
        return false;
    }

    // --------------------------------------------------------
    // DAO ADC의 PDO 구성과 메모리 매핑 상태를 검증합니다.
    // --------------------------------------------------------
    DaoInternalAdcValidationInfo validationInfo{};

    exchangeInfo.adcValidationPassed =
        ValidateDaoAdcPdo(
            physicalSlaveIndex,
            validationInfo);

    if (!exchangeInfo.adcValidationPassed)
    {
        return false;
    }

    // --------------------------------------------------------
    // Slave의 현재 상태와 AL 상태 코드를 갱신합니다.
    // --------------------------------------------------------
    ecx_readstate(&context_);

    ec_slavet& slave =
        context_.slavelist[
            physicalSlaveIndex];

    const std::uint16_t baseState =
        static_cast<std::uint16_t>(
            slave.state & 0x000F);

    exchangeInfo.safeOpStateValid =
        baseState == EC_STATE_SAFE_OP;

    if (!exchangeInfo.safeOpStateValid)
    {
        return false;
    }

    // --------------------------------------------------------
    // DAO ADC 장치의 Vendor ID와 Product Code를 확인합니다.
    //
    // Slave의 출력 PDO 크기와 입력 PDO 크기를 확인합니다.
    // - DAO ADC Identity
    // - Output Bytes = 4
    // Slave의 출력 PDO 크기와 입력 PDO 크기를 확인합니다.
    //
    // Slave의 출력 PDO 크기와 입력 PDO 크기를 확인합니다.
    // --------------------------------------------------------
    std::memset(
        slave.outputs,
        0,
        static_cast<std::size_t>(
            slave.Obytes));

    exchangeInfo.outputCleared = true;

    // --------------------------------------------------------
    // Process Data를 송신하고 수신 WKC를 확인합니다.
    // --------------------------------------------------------
    ecx_send_processdata(&context_);

    exchangeInfo.actualWkc =
        ecx_receive_processdata(
            &context_,
            EC_TIMEOUTRET);

    // --------------------------------------------------------
    // 정상 통신 여부를 판단하기 위한 예상 WKC 값입니다.
    //
    // Process Data를 송신하고 수신 WKC를 확인합니다.
    // Process Data를 송신하고 수신 WKC를 확인합니다.
    // --------------------------------------------------------
    exchangeInfo.wkcValid =
        exchangeInfo.actualWkc >=
        exchangeInfo.expectedWkc;

    return exchangeInfo.wkcValid;
}

bool DaoEtherCATMaster::PrimeDaoAdcProcessData(
    int physicalSlaveIndex,
    int roundCount,
    DaoInternalPrimingInfo& primingInfo)
{
    // --------------------------------------------------------
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // --------------------------------------------------------
    primingInfo = {};

    primingInfo.physicalSlaveIndex =
        physicalSlaveIndex;

    primingInfo.requestedRounds =
        roundCount;

    primingInfo.expectedWkc =
        expectedWkc_;

    // 통신 스레드의 실행 상태를 확인합니다.
    if (communicationRunning_.load())
    {
        return false;
    }

    // --------------------------------------------------------
    // OP 전환 전에 여러 차례 Process Data를 교환해 통신을 안정화합니다.
    //
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    // 이 값은 처리된 프레임 또는 샘플의 누적 개수를 나타냅니다.
    // --------------------------------------------------------
    constexpr int MIN_PRIMING_ROUNDS = 1;
    constexpr int MAX_PRIMING_ROUNDS = 100;

    if (roundCount < MIN_PRIMING_ROUNDS ||
        roundCount > MAX_PRIMING_ROUNDS)
    {
        return false;
    }

    // --------------------------------------------------------
    // DAO ADC의 PDO 구성과 메모리 매핑 상태를 검증합니다.
    // --------------------------------------------------------
    DaoInternalAdcValidationInfo validationInfo{};

    primingInfo.adcValidationPassed =
        ValidateDaoAdcPdo(
            physicalSlaveIndex,
            validationInfo);

    if (!primingInfo.adcValidationPassed)
    {
        return false;
    }

    // --------------------------------------------------------
    // Slave의 현재 상태와 AL 상태 코드를 갱신합니다.
    // --------------------------------------------------------
    ecx_readstate(&context_);

    const ec_slavet& slave =
        context_.slavelist[
            physicalSlaveIndex];

    const std::uint16_t baseState =
        static_cast<std::uint16_t>(
            slave.state & 0x000F);

    primingInfo.safeOpStateValid =
        baseState == EC_STATE_SAFE_OP;

    if (!primingInfo.safeOpStateValid)
    {
        return false;
    }

    // --------------------------------------------------------
    // 이 값은 처리된 프레임 또는 샘플의 누적 개수를 나타냅니다.
    // --------------------------------------------------------
    for (int round = 0;
        round < roundCount;
        ++round)
    {
        DaoInternalProcessExchangeInfo
            exchangeInfo{};

        const bool exchangeResult =
            ExchangeDaoAdcProcessDataOnce(
                physicalSlaveIndex,
                exchangeInfo);

        primingInfo.lastWkc =
            exchangeInfo.actualWkc;

        // Process Data를 송신하고 수신 WKC를 확인합니다.
        if (primingInfo.completedRounds == 0)
        {
            primingInfo.minimumWkc =
                exchangeInfo.actualWkc;

            primingInfo.maximumWkc =
                exchangeInfo.actualWkc;
        }
        else
        {
            if (exchangeInfo.actualWkc <
                primingInfo.minimumWkc)
            {
                primingInfo.minimumWkc =
                    exchangeInfo.actualWkc;
            }

            if (exchangeInfo.actualWkc >
                primingInfo.maximumWkc)
            {
                primingInfo.maximumWkc =
                    exchangeInfo.actualWkc;
            }
        }

        ++primingInfo.completedRounds;

        if (exchangeResult &&
            exchangeInfo.wkcValid)
        {
            ++primingInfo.goodWkcCount;
        }
        else
        {
            ++primingInfo.badWkcCount;

            // OP 전환 전에 여러 차례 Process Data를 교환해 통신을 안정화합니다.
            break;
        }
    }

    // --------------------------------------------------------
    // OP 전환 전에 여러 차례 Process Data를 교환해 통신을 안정화합니다.
    // --------------------------------------------------------
    primingInfo.allRoundsValid =
        primingInfo.completedRounds ==
        primingInfo.requestedRounds &&
        primingInfo.badWkcCount == 0 &&
        primingInfo.goodWkcCount ==
        primingInfo.requestedRounds;

    return primingInfo.allRoundsValid;
}

bool DaoEtherCATMaster::RequestDaoAdcOperational(
    int physicalSlaveIndex,
    DaoInternalOperationalInfo& operationalInfo)
{
    // --------------------------------------------------------
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // --------------------------------------------------------
    operationalInfo = {};

    operationalInfo.physicalSlaveIndex =
        physicalSlaveIndex;

    operationalInfo.expectedWkc =
        expectedWkc_;

    // 통신 스레드의 실행 상태를 확인합니다.
    if (communicationRunning_.load())
    {
        return false;
    }

    // --------------------------------------------------------
    // DAO ADC의 PDO 구성과 메모리 매핑 상태를 검증합니다.
    // --------------------------------------------------------
    DaoInternalAdcValidationInfo validationInfo{};

    operationalInfo.adcValidationPassed =
        ValidateDaoAdcPdo(
            physicalSlaveIndex,
            validationInfo);

    if (!operationalInfo.adcValidationPassed)
    {
        return false;
    }

    // --------------------------------------------------------
    // Slave의 현재 상태와 AL 상태 코드를 갱신합니다.
    // --------------------------------------------------------
    ecx_readstate(&context_);

    ec_slavet& slave =
        context_.slavelist[
            physicalSlaveIndex];

    std::uint16_t baseState =
        static_cast<std::uint16_t>(
            slave.state & 0x000F);

    // 검색된 모든 Slave를 OP 상태로 전환합니다.
    if (baseState == EC_STATE_OPERATIONAL)
    {
        operationalInfo.safeOpStateValid = true;
        operationalInfo.primingPassed = true;
        operationalInfo.stateWriteSucceeded = true;
        operationalInfo.operationalReached = true;

        operationalInfo.finalState =
            static_cast<unsigned short>(
                slave.state);

        operationalInfo.alStatusCode =
            static_cast<unsigned short>(
                slave.ALstatuscode);

        return slave.ALstatuscode == 0;
    }

    // 검색된 모든 Slave를 SAFE-OP 상태로 전환합니다.
    operationalInfo.safeOpStateValid =
        baseState == EC_STATE_SAFE_OP;

    if (!operationalInfo.safeOpStateValid)
    {
        operationalInfo.finalState =
            static_cast<unsigned short>(
                slave.state);

        operationalInfo.alStatusCode =
            static_cast<unsigned short>(
                slave.ALstatuscode);

        return false;
    }

    // --------------------------------------------------------
    // OP 전환 전에 여러 차례 Process Data를 교환해 통신을 안정화합니다.
    //
    // OP 전환 전에 여러 차례 Process Data를 교환해 통신을 안정화합니다.
    // OP 전환 전에 여러 차례 Process Data를 교환해 통신을 안정화합니다.
    // --------------------------------------------------------
    constexpr int PRIMING_ROUNDS = 10;

    DaoInternalPrimingInfo primingInfo{};

    operationalInfo.primingPassed =
        PrimeDaoAdcProcessData(
            physicalSlaveIndex,
            PRIMING_ROUNDS,
            primingInfo);

    if (!operationalInfo.primingPassed)
    {
        operationalInfo.lastWkc =
            primingInfo.lastWkc;

        operationalInfo.exchangeCount =
            primingInfo.completedRounds;

        operationalInfo.goodWkcCount =
            primingInfo.goodWkcCount;

        operationalInfo.badWkcCount =
            primingInfo.badWkcCount;

        operationalInfo.finalState =
            static_cast<unsigned short>(
                slave.state);

        operationalInfo.alStatusCode =
            static_cast<unsigned short>(
                slave.ALstatuscode);

        return false;
    }

    // --------------------------------------------------------
    // 검색된 모든 Slave를 OP 상태로 전환합니다.
    // --------------------------------------------------------
    slave.state =
        EC_STATE_OPERATIONAL;

    const int stateWriteWkc =
        ecx_writestate(
            &context_,
            static_cast<std::uint16_t>(
                physicalSlaveIndex));

    operationalInfo.stateWriteSucceeded =
        stateWriteWkc > 0;

    if (!operationalInfo.stateWriteSucceeded)
    {
        ecx_readstate(&context_);

        operationalInfo.finalState =
            static_cast<unsigned short>(
                slave.state);

        operationalInfo.alStatusCode =
            static_cast<unsigned short>(
                slave.ALstatuscode);

        return false;
    }

    // --------------------------------------------------------
    // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.
    //
    // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.
    // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.
    // --------------------------------------------------------
    constexpr int OP_TIMEOUT_MS = 12000;

    const auto startTime =
        std::chrono::steady_clock::now();

    while (true)
    {
        const auto now =
            std::chrono::steady_clock::now();

        const auto elapsedMs =
            std::chrono::duration_cast<
            std::chrono::milliseconds>(
                now - startTime)
            .count();

        if (elapsedMs >= OP_TIMEOUT_MS)
        {
            break;
        }

        // ----------------------------------------------------
        // DAO ADC 장치의 Vendor ID와 Product Code를 확인합니다.
        //
        // 출력 PDO 영역을 안전한 초기값인 0으로 설정합니다.
        // - DAO ADC Identity
        // - Output Bytes = 4
        // PDO 메모리 포인터가 유효한지 확인합니다.
        // ----------------------------------------------------
        std::memset(
            slave.outputs,
            0,
            static_cast<std::size_t>(
                slave.Obytes));

        // Process Data를 송신하고 수신 WKC를 확인합니다.
        ecx_send_processdata(&context_);

        const int processWkc =
            ecx_receive_processdata(
                &context_,
                EC_TIMEOUTRET);

        operationalInfo.lastWkc =
            processWkc;

        ++operationalInfo.exchangeCount;

        if (processWkc >= expectedWkc_)
        {
            ++operationalInfo.goodWkcCount;
        }
        else
        {
            ++operationalInfo.badWkcCount;
        }

        // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.
        // 검색된 모든 Slave를 OP 상태로 전환합니다.
        (void)ecx_statecheck(
            &context_,
            static_cast<std::uint16_t>(
                physicalSlaveIndex),
            EC_STATE_OPERATIONAL,
            100000);

        ecx_readstate(&context_);

        baseState =
            static_cast<std::uint16_t>(
                slave.state & 0x000F);

        operationalInfo.finalState =
            static_cast<unsigned short>(
                slave.state);

        operationalInfo.alStatusCode =
            static_cast<unsigned short>(
                slave.ALstatuscode);

        // 검색된 모든 Slave를 OP 상태로 전환합니다.
        if (baseState ==
            EC_STATE_OPERATIONAL)
        {
            operationalInfo.operationalReached =
                slave.ALstatuscode == 0;

            return
                operationalInfo.operationalReached;
        }

        // 요청된 정보를 출력 구조체에 복사합니다.
        if ((slave.state &
            EC_STATE_ERROR) != 0)
        {
            return false;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(10));
    }

    // --------------------------------------------------------
    // Slave의 현재 상태와 AL 상태 코드를 갱신합니다.
    // --------------------------------------------------------
    ecx_readstate(&context_);

    operationalInfo.finalState =
        static_cast<unsigned short>(
            slave.state);

    operationalInfo.alStatusCode =
        static_cast<unsigned short>(
            slave.ALstatuscode);

    operationalInfo.operationalReached = false;

    return false;
}

bool DaoEtherCATMaster::ReadDaoAdcOnce(
    int physicalSlaveIndex,
    DaoInternalAdcReadInfo& readInfo)
{
    // --------------------------------------------------------
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // --------------------------------------------------------
    readInfo = {};

    readInfo.physicalSlaveIndex =
        physicalSlaveIndex;

    readInfo.expectedWkc =
        expectedWkc_;

    // 통신 스레드의 실행 상태를 확인합니다.
    // 통신 스레드의 실행 상태를 확인합니다.
    // 통신 스레드의 실행 상태를 확인합니다.
    if (communicationRunning_.load())
    {
        return false;
    }

    // --------------------------------------------------------
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    //
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    // DAO ADC의 PDO 구성과 메모리 매핑 상태를 검증합니다.
    // DAO ADC의 PDO 구성과 메모리 매핑 상태를 검증합니다.
    // DAO ADC의 PDO 구성과 메모리 매핑 상태를 검증합니다.
    // DAO ADC의 PDO 구성과 메모리 매핑 상태를 검증합니다.
    // --------------------------------------------------------
    DaoInternalAdcValidationInfo validationInfo{};

    readInfo.adcValidationPassed =
        ValidateDaoAdcPdo(
            physicalSlaveIndex,
            validationInfo);

    if (!readInfo.adcValidationPassed)
    {
        return false;
    }

    // --------------------------------------------------------
    // Slave의 현재 상태와 AL 상태 코드를 갱신합니다.
    // --------------------------------------------------------
    ecx_readstate(&context_);

    ec_slavet& slave =
        context_.slavelist[
            physicalSlaveIndex];

    const std::uint16_t baseState =
        static_cast<std::uint16_t>(
            slave.state & 0x000F);

    readInfo.operationalStateValid =
        baseState == EC_STATE_OPERATIONAL;

    if (!readInfo.operationalStateValid)
    {
        return false;
    }

    // --------------------------------------------------------
    // Slave의 출력 PDO 크기와 입력 PDO 크기를 확인합니다.
    //
    // Slave의 출력 PDO 크기와 입력 PDO 크기를 확인합니다.
    // --------------------------------------------------------
    std::memset(
        slave.outputs,
        0,
        static_cast<std::size_t>(
            slave.Obytes));

    readInfo.outputCleared = true;

    // --------------------------------------------------------
    // Process Data를 송신하고 수신 WKC를 확인합니다.
    // --------------------------------------------------------
    ecx_send_processdata(&context_);

    readInfo.actualWkc =
        ecx_receive_processdata(
            &context_,
            EC_TIMEOUTRET);

    readInfo.wkcValid =
        readInfo.actualWkc >=
        readInfo.expectedWkc;

    if (!readInfo.wkcValid)
    {
        return false;
    }

    // --------------------------------------------------------
    // Slave의 출력 PDO 크기와 입력 PDO 크기를 확인합니다.
    //
    // Slave의 출력 PDO 크기와 입력 PDO 크기를 확인합니다.
    // - slave.inputs != nullptr
    // - slave.Ibytes == 24
    //
    // Slave의 출력 PDO 크기와 입력 PDO 크기를 확인합니다.
    // Slave의 출력 PDO 크기와 입력 PDO 크기를 확인합니다.
    // --------------------------------------------------------
    std::memcpy(
        &readInfo.data,
        slave.inputs,
        sizeof(DaoInternalAdcInputPdo));

    readInfo.inputCopied = true;

    // --------------------------------------------------------
    // 요청된 정보를 출력 구조체에 복사합니다.
    //
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // --------------------------------------------------------
    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    {
        std::lock_guard<std::mutex> lock(
            adcRuntimeMutex_);

        if (runtimeIndex >=
            adcRuntimeInfoBySlave_.size())
        {
            return false;
        }

        DaoInternalAdcRuntimeInfo& runtimeInfo =
            adcRuntimeInfoBySlave_[
                runtimeIndex];

        runtimeInfo.physicalSlaveIndex =
            physicalSlaveIndex;

        // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
        runtimeInfo.communicationRunning =
            false;

        runtimeInfo.hasValidData =
            true;

        runtimeInfo.lastWkc =
            readInfo.actualWkc;

        runtimeInfo.expectedWkc =
            readInfo.expectedWkc;

        ++runtimeInfo.totalFrameCount;
        ++runtimeInfo.goodWkcFrameCount;
        ++runtimeInfo.dataUpdateCount;

        runtimeInfo.latestData =
            readInfo.data;
    }

    return true;
}


bool DaoEtherCATMaster::GetDaoAdcRuntimeInfo(
    int physicalSlaveIndex,
    DaoInternalAdcRuntimeInfo& outInfo) const
{
    std::lock_guard<std::mutex> lock(
        adcRuntimeMutex_);

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        adcRuntimeInfoBySlave_.size())
    {
        return false;
    }

    outInfo =
        adcRuntimeInfoBySlave_[
            runtimeIndex];

    return true;
}

bool DaoEtherCATMaster::SetDaoAdcZero(
    int physicalSlaveIndex)
{
    std::lock_guard<std::mutex> lock(
        adcRuntimeMutex_);

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        adcRuntimeInfoBySlave_.size())
    {
        return false;
    }

    DaoInternalAdcRuntimeInfo& runtimeInfo =
        adcRuntimeInfoBySlave_[
            runtimeIndex];

    if (!runtimeInfo.hasValidData)
    {
        return false;
    }

    if (!runtimeInfo.processing.lowLevelFilterInitialized)
    {
        return false;
    }

    // ----------------------------------------------------
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    //
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    // 안정된 ADC 샘플을 모아 영점 오프셋을 계산합니다.
    //
    // 안정된 ADC 샘플을 모아 영점 오프셋을 계산합니다.
    // 안정된 ADC 샘플을 모아 영점 오프셋을 계산합니다.
    // ----------------------------------------------------
    constexpr unsigned int ZERO_CAPTURE_SAMPLES = 600;

    runtimeInfo.processing.stableCaptureActive =
        true;

    runtimeInfo.processing.stableCaptureType =
        DaoInternalAdcStableCaptureType::ZERO;

    runtimeInfo.processing.stableCaptureReferenceValue =
        0.0;

    // 요청된 정보를 출력 구조체에 복사합니다.
    // 안정된 ADC 샘플을 모아 영점 오프셋을 계산합니다.
    runtimeInfo.processing.stableCaptureWaitSamples =
        0;

    runtimeInfo.processing.stableCaptureSampleCount =
        ZERO_CAPTURE_SAMPLES;

    runtimeInfo.processing.stableCaptureCollectedCount =
        0;

    runtimeInfo.processing.stableCaptureSum =
        0.0;

    return true;
}

bool DaoEtherCATMaster::SetDaoAdcCalibration(
    int physicalSlaveIndex,
    double referenceValue)
{
    std::lock_guard<std::mutex> lock(
        adcRuntimeMutex_);

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        adcRuntimeInfoBySlave_.size())
    {
        return false;
    }

    DaoInternalAdcRuntimeInfo& runtimeInfo =
        adcRuntimeInfoBySlave_[runtimeIndex];

    if (!runtimeInfo.hasValidData)
    {
        return false;
    }

    if (!runtimeInfo.processing.lowLevelFilterInitialized)
    {
        return false;
    }

    if (!runtimeInfo.processing.zeroInitialized)
    {
        return false;
    }

    if (referenceValue == 0.0)
    {
        return false;
    }

    // ----------------------------------------------------
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    //
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    //
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    // 기준값과 안정된 ADC 샘플을 이용해 보정 계수를 계산합니다.
    // 기준값과 안정된 ADC 샘플을 이용해 보정 계수를 계산합니다.
    //
    // 기준값과 안정된 ADC 샘플을 이용해 보정 계수를 계산합니다.
    // ----------------------------------------------------
    constexpr unsigned int CALIBRATION_WAIT_SAMPLES =
        2000;

    constexpr unsigned int CALIBRATION_CAPTURE_SAMPLES =
        4000;

    runtimeInfo.processing.stableCaptureActive =
        true;

    runtimeInfo.processing.stableCaptureType =
        DaoInternalAdcStableCaptureType::CALIBRATION;

    runtimeInfo.processing.stableCaptureReferenceValue =
        referenceValue;

    runtimeInfo.processing.stableCaptureWaitSamples =
        CALIBRATION_WAIT_SAMPLES;

    runtimeInfo.processing.stableCaptureSampleCount =
        CALIBRATION_CAPTURE_SAMPLES;

    runtimeInfo.processing.stableCaptureCollectedCount =
        0;

    runtimeInfo.processing.stableCaptureSum =
        0.0;

    return true;
}


bool DaoEtherCATMaster::SetDaoAdcPowerLineFilterMode(
    int physicalSlaveIndex,
    DaoInternalAdcPowerLineFilterMode mode)
{
    std::lock_guard<std::mutex> lock(
        adcRuntimeMutex_);

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        adcRuntimeInfoBySlave_.size())
    {
        return false;
    }

    DaoInternalAdcRuntimeInfo& runtimeInfo =
        adcRuntimeInfoBySlave_[
            runtimeIndex];

    if (!runtimeInfo.hasValidData)
    {
        return false;
    }

    // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
    if (mode != DaoInternalAdcPowerLineFilterMode::OFF &&
        mode != DaoInternalAdcPowerLineFilterMode::HZ_50 &&
        mode != DaoInternalAdcPowerLineFilterMode::HZ_60 &&
        mode != DaoInternalAdcPowerLineFilterMode::HZ_120 &&
        mode != DaoInternalAdcPowerLineFilterMode::HZ_50_60 &&
        mode != DaoInternalAdcPowerLineFilterMode::HZ_60_120)
    {
        return false;
    }
    runtimeInfo.processing.powerLineFilterMode =
        mode;

    // ----------------------------------------------------
    // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
    //
    // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
    // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
    // ----------------------------------------------------
    runtimeInfo.processing.notch50X1 = 0.0;
    runtimeInfo.processing.notch50X2 = 0.0;
    runtimeInfo.processing.notch50Y1 = 0.0;
    runtimeInfo.processing.notch50Y2 = 0.0;

    runtimeInfo.processing.notch60X1 = 0.0;
    runtimeInfo.processing.notch60X2 = 0.0;
    runtimeInfo.processing.notch60Y1 = 0.0;
    runtimeInfo.processing.notch60Y2 = 0.0;

    runtimeInfo.processing.notch120X1 = 0.0;
    runtimeInfo.processing.notch120X2 = 0.0;
    runtimeInfo.processing.notch120Y1 = 0.0;
    runtimeInfo.processing.notch120Y2 = 0.0;

    // ADC 원시 샘플에 저역 통과 필터를 적용합니다.
    runtimeInfo.processing.powerLineFiltered =
        runtimeInfo.processing.lowLevelFiltered;

    return true;
}

bool DaoEtherCATMaster::SetDaoAdcFilterN(
    int physicalSlaveIndex,
    unsigned int filterN)
{
    std::lock_guard<std::mutex> lock(
        adcRuntimeMutex_);

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        adcRuntimeInfoBySlave_.size())
    {
        return false;
    }

    if (filterN < 1 ||
        filterN >
        DaoInternalAdcProcessingState::USER_FILTER_MAX_N)
    {
        return false;
    }

    DaoInternalAdcRuntimeInfo& runtimeInfo =
        adcRuntimeInfoBySlave_[
            runtimeIndex];

    runtimeInfo.processing.filterN =
        filterN;

    // 설정된 N개 샘플의 이동 평균을 계산합니다.
    runtimeInfo.processing.userFilterBuffer.fill(0.0);

    runtimeInfo.processing.userFilterIndex =
        0;

    runtimeInfo.processing.userFilterCount =
        0;

    runtimeInfo.processing.userFilterSum =
        0.0;

    runtimeInfo.processing.filteredValue =
        runtimeInfo.processing.medianFilteredValue;

    return true;
}


bool DaoEtherCATMaster::StartDaoAdcDiagnosticCapture(
    int physicalSlaveIndex,
    unsigned int targetSampleCount)
{
    std::lock_guard<std::mutex> lock(
        adcRuntimeMutex_);

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    if (targetSampleCount == 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        adcRuntimeInfoBySlave_.size())
    {
        return false;
    }

    DaoInternalAdcRuntimeInfo& runtimeInfo =
        adcRuntimeInfoBySlave_[
            runtimeIndex];

    if (!runtimeInfo.hasValidData)
    {
        return false;
    }

    // ADC 진단 캡처 상태와 수집된 샘플 수를 관리합니다.
    // ADC 진단 캡처 상태와 수집된 샘플 수를 관리합니다.
    runtimeInfo.diagnosticSamples.clear();

    // ADC 진단 캡처 상태와 수집된 샘플 수를 관리합니다.
    // ADC 진단 캡처 상태와 수집된 샘플 수를 관리합니다.
    runtimeInfo.diagnosticSamples.reserve(
        static_cast<std::size_t>(
            targetSampleCount));

    runtimeInfo.diagnosticSampleIndex =
        0;

    runtimeInfo.diagnosticTargetSampleCount =
        targetSampleCount;

    runtimeInfo.diagnosticCaptureActive =
        true;

    return true;
}


bool DaoEtherCATMaster::GetDaoAdcDiagnosticCaptureInfo(
    int physicalSlaveIndex,
    bool& captureActive,
    unsigned int& capturedSampleCount,
    unsigned int& targetSampleCount) const
{
    std::lock_guard<std::mutex> lock(
        adcRuntimeMutex_);

    captureActive = false;
    capturedSampleCount = 0;
    targetSampleCount = 0;

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        adcRuntimeInfoBySlave_.size())
    {
        return false;
    }

    const DaoInternalAdcRuntimeInfo& runtimeInfo =
        adcRuntimeInfoBySlave_[
            runtimeIndex];

    captureActive =
        runtimeInfo.diagnosticCaptureActive;

    capturedSampleCount =
        static_cast<unsigned int>(
            runtimeInfo.diagnosticSamples.size());

    targetSampleCount =
        runtimeInfo.diagnosticTargetSampleCount;

    return true;
}

bool DaoEtherCATMaster::GetDaoAdcRingBufferInfo(
    int physicalSlaveIndex,
    unsigned int& sampleCount,
    unsigned long long& overflowCount) const
{
    std::lock_guard<std::mutex> lock(
        adcRuntimeMutex_);

    sampleCount = 0;
    overflowCount = 0;

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        adcRuntimeInfoBySlave_.size())
    {
        return false;
    }

    const DaoInternalAdcRuntimeInfo& runtimeInfo =
        adcRuntimeInfoBySlave_[
            runtimeIndex];

    sampleCount =
        static_cast<unsigned int>(
            runtimeInfo.ringBufferCount);

    overflowCount =
        static_cast<unsigned long long>(
            runtimeInfo.ringBufferOverflowCount);

    return true;
}

bool DaoEtherCATMaster::ReadDaoAdcRingBuffer(
    int physicalSlaveIndex,
    DaoInternalAdcBufferedSample* samples,
    unsigned int maxSampleCount,
    unsigned int& readSampleCount)
{
    std::lock_guard<std::mutex> lock(
        adcRuntimeMutex_);

    readSampleCount = 0;

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    if (samples == nullptr)
    {
        return false;
    }

    if (maxSampleCount == 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        adcRuntimeInfoBySlave_.size())
    {
        return false;
    }

    DaoInternalAdcRuntimeInfo& runtimeInfo =
        adcRuntimeInfoBySlave_[
            runtimeIndex];

    const std::size_t availableCount =
        runtimeInfo.ringBufferCount;

    if (availableCount == 0)
    {
        return true;
    }

    const std::size_t requestedCount =
        static_cast<std::size_t>(
            maxSampleCount);

    const std::size_t actualReadCount =
        (availableCount < requestedCount)
        ? availableCount
        : requestedCount;

    // ----------------------------------------------------
    // 이 값은 처리된 프레임 또는 샘플의 누적 개수를 나타냅니다.
    // ----------------------------------------------------
    for (std::size_t i = 0;
        i < actualReadCount;
        ++i)
    {
        samples[i] =
            runtimeInfo.ringBuffer[
                runtimeInfo.ringBufferTail];

        runtimeInfo.ringBufferTail =
            (runtimeInfo.ringBufferTail + 1) %
            DaoInternalAdcRuntimeInfo::ADC_RING_BUFFER_SIZE;
    }

    runtimeInfo.ringBufferCount -=
        actualReadCount;

    readSampleCount =
        static_cast<unsigned int>(
            actualReadCount);

    return true;
}
bool DaoEtherCATMaster::ClearDaoAdcRingBuffer(
    int physicalSlaveIndex)
{
    std::lock_guard<std::mutex> lock(
        adcRuntimeMutex_);

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        adcRuntimeInfoBySlave_.size())
    {
        return false;
    }

    DaoInternalAdcRuntimeInfo& runtimeInfo =
        adcRuntimeInfoBySlave_[
            runtimeIndex];

    runtimeInfo.ringBufferHead = 0;
    runtimeInfo.ringBufferTail = 0;
    runtimeInfo.ringBufferCount = 0;

    runtimeInfo.ringBufferNextSampleIndex = 0;

    runtimeInfo.ringBufferOverflowCount = 0;

    return true;
}


bool DaoEtherCATMaster::GetDaoAdcDiagnosticSample(
    int physicalSlaveIndex,
    unsigned int sampleIndex,
    DaoInternalAdcDiagnosticSample& sample) const
{
    std::lock_guard<std::mutex> lock(
        adcRuntimeMutex_);

    sample = {};

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        adcRuntimeInfoBySlave_.size())
    {
        return false;
    }

    const DaoInternalAdcRuntimeInfo& runtimeInfo =
        adcRuntimeInfoBySlave_[
            runtimeIndex];

    if (sampleIndex >=
        runtimeInfo.diagnosticSamples.size())
    {
        return false;
    }

    sample =
        runtimeInfo.diagnosticSamples[
            static_cast<std::size_t>(
                sampleIndex)];

    return true;
}


bool DaoEtherCATMaster::GetServoRuntimeInfo(
    int physicalSlaveIndex,
    DaoInternalServoRuntimeInfo& outInfo) const
{
    std::lock_guard<std::mutex> lock(
        servoRuntimeMutex_);

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        servoRuntimeInfoBySlave_.size())
    {
        return false;
    }

    const DaoInternalServoRuntimeInfo& runtimeInfo =
        servoRuntimeInfoBySlave_[
            runtimeIndex];

    // Servo 런타임 정보를 동기화하여 복사합니다.
    // 요청된 정보를 출력 구조체에 복사합니다.
    if (!runtimeInfo.configured)
    {
        return false;
    }

    outInfo = runtimeInfo;

    return true;
}


bool DaoEtherCATMaster::GetIoRuntimeInfo(
    int physicalSlaveIndex,
    DaoInternalIoRuntimeInfo& outInfo) const
{
    std::lock_guard<std::mutex> lock(
        ioRuntimeMutex_);

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        ioRuntimeInfoBySlave_.size())
    {
        return false;
    }

    const DaoInternalIoRuntimeInfo& runtimeInfo =
        ioRuntimeInfoBySlave_[
            runtimeIndex];

    // FASTECH Ezi-IO 장치의 식별 정보와 입출력 크기를 확인합니다.
    // FASTECH Ezi-IO 장치의 식별 정보와 입출력 크기를 확인합니다.
    if (!runtimeInfo.configured)
    {
        return false;
    }

    outInfo = runtimeInfo;

    return true;
}

bool DaoEtherCATMaster::GetEncoderRuntimeInfo(
    int physicalSlaveIndex,
    DaoInternalEncoderRuntimeInfo& outInfo) const
{
    std::lock_guard<std::mutex> lock(
        encoderRuntimeMutex_);

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        encoderRuntimeInfoBySlave_.size())
    {
        return false;
    }

    const DaoInternalEncoderRuntimeInfo& runtimeInfo =
        encoderRuntimeInfoBySlave_[
            runtimeIndex];

    if (!runtimeInfo.configured)
    {
        return false;
    }

    outInfo = runtimeInfo;

    return true;
}

bool DaoEtherCATMaster::RequestServoOn(
    int physicalSlaveIndex)
{
    std::lock_guard<std::mutex> lock(
        servoRuntimeMutex_);

    if (!isOpen_)
    {
        return false;
    }

    if (!communicationRunning_.load())
    {
        return false;
    }

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        servoRuntimeInfoBySlave_.size())
    {
        return false;
    }

    DaoInternalServoRuntimeInfo& runtimeInfo =
        servoRuntimeInfoBySlave_[
            runtimeIndex];

    if (!runtimeInfo.configured ||
        !runtimeInfo.hasValidInputData)
    {
        return false;
    }

    // Servo의 Fault 상태를 확인하고 명령 결과에 반영합니다.
    if (runtimeInfo.fault)
    {
        return false;
    }

    if (runtimeInfo.stoActive)
    {
        return false;
    }

    // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
    // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
    if (runtimeInfo.commandState ==
        DAO_SERVO_COMMAND_STATE_ACCEPTED ||
        runtimeInfo.commandState ==
        DAO_SERVO_COMMAND_STATE_RUNNING)
    {
        return false;
    }

    runtimeInfo.commandId =
        nextServoCommandId_.fetch_add(1);

    runtimeInfo.commandType =
        DAO_SERVO_COMMAND_SERVO_ON;

    runtimeInfo.commandState =
        DAO_SERVO_COMMAND_STATE_ACCEPTED;

    runtimeInfo.commandStep =
        DAO_SERVO_STEP_SERVO_ON;

    runtimeInfo.commandResult = 0;

    runtimeInfo.commandStartFrameCount =
        runtimeInfo.totalFrameCount;

    // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
    // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
    if (runtimeInfo.operationEnabled)
    {
        runtimeInfo.commandState =
            DAO_SERVO_COMMAND_STATE_COMPLETED;

        runtimeInfo.commandResult = 1;
    }

    return true;
}

bool DaoEtherCATMaster::RequestServoOff(
    int physicalSlaveIndex)
{
    std::lock_guard<std::mutex> lock(
        servoRuntimeMutex_);

    if (!isOpen_)
    {
        return false;
    }

    if (!communicationRunning_.load())
    {
        return false;
    }

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        servoRuntimeInfoBySlave_.size())
    {
        return false;
    }

    DaoInternalServoRuntimeInfo& runtimeInfo =
        servoRuntimeInfoBySlave_[
            runtimeIndex];

    if (!runtimeInfo.configured ||
        !runtimeInfo.hasValidInputData)
    {
        return false;
    }

    // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
    // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
    if (runtimeInfo.commandState ==
        DAO_SERVO_COMMAND_STATE_ACCEPTED ||
        runtimeInfo.commandState ==
        DAO_SERVO_COMMAND_STATE_RUNNING)
    {
        return false;
    }

    runtimeInfo.commandId =
        nextServoCommandId_.fetch_add(1);

    runtimeInfo.commandType =
        DAO_SERVO_COMMAND_SERVO_OFF;

    runtimeInfo.commandState =
        DAO_SERVO_COMMAND_STATE_ACCEPTED;

    runtimeInfo.commandStep =
        DAO_SERVO_STEP_SERVO_OFF;

    runtimeInfo.commandResult = 0;

    runtimeInfo.commandStartFrameCount =
        runtimeInfo.totalFrameCount;

    // Statusword에서 CiA 402 상태를 추출합니다.
    // Statusword에서 CiA 402 상태를 추출합니다.
    if (runtimeInfo.cia402State == 0x0021)
    {
        runtimeInfo.commandState =
            DAO_SERVO_COMMAND_STATE_COMPLETED;

        runtimeInfo.commandResult = 1;
    }

    return true;
}

bool DaoEtherCATMaster::RequestServoHome(
    int physicalSlaveIndex,
    unsigned int timeoutMs)
{
    std::lock_guard<std::mutex> lock(
        servoRuntimeMutex_);

    if (!isOpen_)
    {
        return false;
    }

    if (!communicationRunning_.load())
    {
        return false;
    }

    if (timeoutMs == 0)
    {
        return false;
    }

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        servoRuntimeInfoBySlave_.size())
    {
        return false;
    }

    DaoInternalServoRuntimeInfo& runtimeInfo =
        servoRuntimeInfoBySlave_[
            runtimeIndex];

    if (!runtimeInfo.configured ||
        !runtimeInfo.hasValidInputData)
    {
        return false;
    }

    // Servo의 Fault 상태를 확인하고 명령 결과에 반영합니다.
    if (runtimeInfo.fault)
    {
        return false;
    }

	if (runtimeInfo.stoActive) // Servo Homing 명령과 제한 시간을 설정합니다.
    {
        return false;
    }

    // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
    if (runtimeInfo.commandState ==
        DAO_SERVO_COMMAND_STATE_ACCEPTED ||
        runtimeInfo.commandState ==
        DAO_SERVO_COMMAND_STATE_RUNNING)
    {
        return false;
    }

    runtimeInfo.commandId =
        nextServoCommandId_.fetch_add(1);

    runtimeInfo.commandType =
        DAO_SERVO_COMMAND_HOMING;

    runtimeInfo.commandState =
        DAO_SERVO_COMMAND_STATE_ACCEPTED;

    runtimeInfo.commandStep =
        DAO_SERVO_STEP_HOMING_PREPARE;

    runtimeInfo.commandResult = 0;

    runtimeInfo.commandStartFrameCount =
        runtimeInfo.totalFrameCount;

    runtimeInfo.homingStartFrameCount = 0;

    runtimeInfo.homingTimeoutMs =
        timeoutMs;

    runtimeInfo.mailboxRequestType =
        DAO_SERVO_MAILBOX_NONE;

    runtimeInfo.mailboxRequestState =
        DAO_SERVO_MAILBOX_STATE_IDLE;

    runtimeInfo.requestedOperationMode = 0;

    runtimeInfo.mailboxResult = 0;


    // Servo Homing 명령과 제한 시간을 설정합니다.
    runtimeInfo.homed = false;

    return true;
}



// --------------------------------------------------------
// 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
bool DaoEtherCATMaster::RequestServoMoveAbsolute(
    int physicalSlaveIndex,
    int targetPosition,
    unsigned int profileVelocity,
    unsigned int profileAcceleration,
    unsigned int profileDeceleration,
    unsigned int timeoutMs)
{
    std::lock_guard<std::mutex> lock(
        servoRuntimeMutex_);

    if (!isOpen_)
    {
        return false;
    }

    if (!communicationRunning_.load())
    {
        return false;
    }

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }
	// 아래 조건을 확인한 후 현재 처리 단계를 계속합니다.
    if (profileVelocity == 0)
    {
        return false;
    }

    constexpr unsigned int DEFAULT_PROFILE_ACCELERATION = 1000;
    constexpr unsigned int DEFAULT_PROFILE_DECELERATION = 1000;

    const unsigned int safeAcceleration =
        (profileAcceleration == 0)
        ? DEFAULT_PROFILE_ACCELERATION
        : profileAcceleration;

    const unsigned int safeDeceleration =
        (profileDeceleration == 0)
        ? DEFAULT_PROFILE_DECELERATION
        : profileDeceleration;

    

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
			physicalSlaveIndex);  // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.

    if (runtimeIndex >=
        servoRuntimeInfoBySlave_.size())
    {
        return false;
    }

    DaoInternalServoRuntimeInfo& runtimeInfo =
        servoRuntimeInfoBySlave_[
            runtimeIndex];

    if (!runtimeInfo.configured ||
        !runtimeInfo.hasValidInputData)
    {
        return false;
    }

    // Servo의 Fault 상태를 확인하고 명령 결과에 반영합니다.
    if (runtimeInfo.fault)
    {
        return false;
    }

	if (runtimeInfo.stoActive) // Servo 입력값에서 Limit, Home, STOP 및 STO 신호를 해석합니다.
    {
        return false;
    }

    // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
    // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
    if (runtimeInfo.commandState ==
        DAO_SERVO_COMMAND_STATE_ACCEPTED ||
        runtimeInfo.commandState ==
        DAO_SERVO_COMMAND_STATE_RUNNING)
    {
        return false;
    }

    runtimeInfo.commandId =
        nextServoCommandId_.fetch_add(1);

    runtimeInfo.commandType =
        DAO_SERVO_COMMAND_MOVE_ABSOLUTE;

    runtimeInfo.commandState =
        DAO_SERVO_COMMAND_STATE_ACCEPTED;

    runtimeInfo.commandStep =
        DAO_SERVO_STEP_MOVE_ABS_PREPARE;

    runtimeInfo.commandResult = 0;

    runtimeInfo.commandStartFrameCount =
        runtimeInfo.totalFrameCount;

    // --------------------------------------------------------
    // 목표 위치와 프로파일 값을 설정해 절대 위치 이동을 요청합니다.
    // --------------------------------------------------------
    runtimeInfo.moveTargetPosition =
        targetPosition;

    runtimeInfo.moveProfileVelocity =
        profileVelocity;

    runtimeInfo.moveProfileAcceleration =
		safeAcceleration;  // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.

    runtimeInfo.moveProfileDeceleration =
		safeDeceleration; // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.

    if (timeoutMs == 0)
    {
        const long long currentPosition =
            static_cast<long long>(
                runtimeInfo.latestInput.actualPosition);

        const long long targetPosition64 =
            static_cast<long long>(
                targetPosition);

        const long long positionDifference =
            targetPosition64 - currentPosition;

        const unsigned long long moveDistance =
            static_cast<unsigned long long>(
                positionDifference >= 0
                ? positionDifference
                : -positionDifference);

        // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
        // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
        const unsigned long long baseMoveTimeMs =
            (moveDistance * 1000ULL +
                static_cast<unsigned long long>(profileVelocity) - 1ULL)
            /
            static_cast<unsigned long long>(profileVelocity);

        // 아래 조건을 확인한 후 현재 처리 단계를 계속합니다.
        unsigned long long calculatedTimeoutMs =
            (baseMoveTimeMs * 2ULL) + 2000ULL;

        // 아래 조건을 확인한 후 현재 처리 단계를 계속합니다.
        if (calculatedTimeoutMs < 3000ULL)
        {
            calculatedTimeoutMs = 3000ULL;
        }

        const unsigned long long MAX_TIMEOUT_MS =
            static_cast<unsigned long long>(UINT_MAX);

        if (calculatedTimeoutMs > MAX_TIMEOUT_MS)
        {
            calculatedTimeoutMs = MAX_TIMEOUT_MS;
        }

        runtimeInfo.moveTimeoutMs =
            static_cast<unsigned int>(
                calculatedTimeoutMs);


    }
    else
    {
        runtimeInfo.moveTimeoutMs =
            timeoutMs;
    }

    runtimeInfo.moveTargetReachedWentLow =
        false;

    return true;
}


// --------------------------------------------------------
// 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
// 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
// --------------------------------------------------------
bool DaoEtherCATMaster::RequestServoVelocity(
    int physicalSlaveIndex,
    int targetVelocity,
    unsigned int acceleration,
    unsigned int deceleration)
{
    std::lock_guard<std::mutex> lock(
        servoRuntimeMutex_);

    if (!isOpen_)
    {
        return false;
    }

    if (!communicationRunning_.load())
    {
        return false;
    }

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        servoRuntimeInfoBySlave_.size())
    {
        return false;
    }

    DaoInternalServoRuntimeInfo& runtimeInfo =
        servoRuntimeInfoBySlave_[
            runtimeIndex];

    if (!runtimeInfo.configured ||
        !runtimeInfo.hasValidInputData)
    {
        return false;
    }

    // Servo의 Fault 상태를 확인하고 명령 결과에 반영합니다.
    if (runtimeInfo.fault)
    {
        return false;
    }

    // Servo 입력값에서 Limit, Home, STOP 및 STO 신호를 해석합니다.
    if (runtimeInfo.stoActive)
    {
        return false;
    }

    constexpr unsigned int DEFAULT_VELOCITY_ACCELERATION = 1000;
    constexpr unsigned int DEFAULT_VELOCITY_DECELERATION = 1000;

    const unsigned int safeAcceleration =
        (acceleration == 0)
        ? DEFAULT_VELOCITY_ACCELERATION
        : acceleration;

    const unsigned int safeDeceleration =
        (deceleration == 0)
        ? DEFAULT_VELOCITY_DECELERATION
        : deceleration;

    // --------------------------------------------------------
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    //
    // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
    // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
    // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
    // --------------------------------------------------------
    if (runtimeInfo.commandType ==
        DAO_SERVO_COMMAND_VELOCITY &&
        runtimeInfo.commandState ==
        DAO_SERVO_COMMAND_STATE_RUNNING)
    {
        runtimeInfo.velocityTarget =
            targetVelocity;

        runtimeInfo.velocityAcceleration =
            safeAcceleration;

        runtimeInfo.velocityDeceleration =
            safeDeceleration;

        return true;
    }

    // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
    // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
    if (runtimeInfo.commandState ==
        DAO_SERVO_COMMAND_STATE_ACCEPTED ||
        runtimeInfo.commandState ==
        DAO_SERVO_COMMAND_STATE_RUNNING)
    {
        return false;
    }

  
    runtimeInfo.commandId =
        nextServoCommandId_.fetch_add(1);

    runtimeInfo.commandType =
        DAO_SERVO_COMMAND_VELOCITY;

    runtimeInfo.commandState =
        DAO_SERVO_COMMAND_STATE_ACCEPTED;

    runtimeInfo.commandStep =
        DAO_SERVO_STEP_VELOCITY_PREPARE;

    runtimeInfo.commandResult = 0;

    runtimeInfo.commandStartFrameCount =
        runtimeInfo.totalFrameCount;

    runtimeInfo.velocityTarget =
        targetVelocity;

    runtimeInfo.velocityAcceleration =
        safeAcceleration;

    runtimeInfo.velocityDeceleration =
        safeDeceleration;

    return true;
}
// --------------------------------------------------------
// --------------------------------------------------------
// 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
// 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
// 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
// --------------------------------------------------------
bool DaoEtherCATMaster::RequestServoStop(
    int physicalSlaveIndex)
{
    std::lock_guard<std::mutex> lock(
        servoRuntimeMutex_);

    if (!isOpen_)
    {
        return false;
    }

    if (!communicationRunning_.load())
    {
        return false;
    }

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        servoRuntimeInfoBySlave_.size())
    {
        return false;
    }

    DaoInternalServoRuntimeInfo& runtimeInfo =
        servoRuntimeInfoBySlave_[
            runtimeIndex];

    if (!runtimeInfo.configured ||
        !runtimeInfo.hasValidInputData)
    {
        return false;
    }

    // Servo의 Fault 상태를 확인하고 명령 결과에 반영합니다.
    // Servo의 Fault 상태를 확인하고 명령 결과에 반영합니다.
    if (runtimeInfo.fault ||
        runtimeInfo.stoActive)
    {
        return false;
    }

    runtimeInfo.commandId =
        nextServoCommandId_.fetch_add(1);

    runtimeInfo.commandType =
        DAO_SERVO_COMMAND_STOP;

    runtimeInfo.commandState =
        DAO_SERVO_COMMAND_STATE_ACCEPTED;

    runtimeInfo.commandStep =
        DAO_SERVO_STEP_STOP_PREPARE;

    runtimeInfo.commandResult = 0;

    runtimeInfo.commandStartFrameCount =
        runtimeInfo.totalFrameCount;

    return true;
}

bool DaoEtherCATMaster::SetServoOutputCommand(
    int physicalSlaveIndex,
    const DaoInternalLsServoOutputPdo& command)
{
    std::lock_guard<std::mutex> lock(
        servoRuntimeMutex_);

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        servoRuntimeInfoBySlave_.size())
    {
        return false;
    }

    DaoInternalServoRuntimeInfo& runtimeInfo =
        servoRuntimeInfoBySlave_[
            runtimeIndex];

    if (!runtimeInfo.configured)
    {
        return false;
    }

    runtimeInfo.outputCommand =
        command;

    return true;
}
bool DaoEtherCATMaster::BeginServoCommand(
    int physicalSlaveIndex,
    int commandType)
{
    std::lock_guard<std::mutex> lock(
        servoRuntimeMutex_);

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        servoRuntimeInfoBySlave_.size())
    {
        return false;
    }

    DaoInternalServoRuntimeInfo& runtimeInfo =
        servoRuntimeInfoBySlave_[
            runtimeIndex];

    if (!runtimeInfo.configured)
    {
        return false;
    }

    runtimeInfo.commandId =
        nextServoCommandId_.fetch_add(1);

    runtimeInfo.commandType =
        commandType;

    runtimeInfo.commandState =
        DAO_SERVO_COMMAND_STATE_ACCEPTED;

    runtimeInfo.commandResult = 0;

    return true;
}


bool DaoEtherCATMaster::UpdateServoCommandState(
    int physicalSlaveIndex,
    int commandState,
    int commandResult)
{
    std::lock_guard<std::mutex> lock(
        servoRuntimeMutex_);

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        servoRuntimeInfoBySlave_.size())
    {
        return false;
    }

    DaoInternalServoRuntimeInfo& runtimeInfo =
        servoRuntimeInfoBySlave_[
            runtimeIndex];

    if (!runtimeInfo.configured)
    {
        return false;
    }

    runtimeInfo.commandState =
        commandState;

    runtimeInfo.commandResult =
        commandResult;

    return true;
}


bool DaoEtherCATMaster::SetIoOutputCommand(
    int physicalSlaveIndex,
    unsigned short outputValue)
{
    std::lock_guard<std::mutex> lock(
        ioRuntimeMutex_);

    if (physicalSlaveIndex <= 0)
    {
        return false;
    }

    const std::size_t runtimeIndex =
        static_cast<std::size_t>(
            physicalSlaveIndex);

    if (runtimeIndex >=
        ioRuntimeInfoBySlave_.size())
    {
        return false;
    }

    DaoInternalIoRuntimeInfo& runtimeInfo =
        ioRuntimeInfoBySlave_[
            runtimeIndex];

    if (!runtimeInfo.configured)
    {
        return false;
    }

    // Slave의 출력 PDO 크기와 입력 PDO 크기를 확인합니다.
    if (runtimeInfo.outputBytes == 1)
    {
        outputValue =
            static_cast<unsigned short>(
                outputValue & 0x00FF);
    }
    // Slave의 출력 PDO 크기와 입력 PDO 크기를 확인합니다.
    else if (runtimeInfo.outputBytes != 2)
    {
        return false;
    }

    runtimeInfo.outputCommand =
        outputValue;

    return true;
}
void DaoEtherCATMaster::ResetAdcRuntimeInfo()
{
    // --------------------------------------------------------
    // ADC 런타임 상태를 초기값으로 되돌립니다.
    // ADC 런타임 정보를 동기화하여 복사합니다.
    // ADC 런타임 정보를 동기화하여 복사합니다.
    // --------------------------------------------------------
    std::lock_guard<std::mutex> lock(
        adcRuntimeMutex_);

    // ADC 런타임 정보를 동기화하여 복사합니다.
    adcRuntimeInfoBySlave_.clear();

    // ADC 런타임 정보를 동기화하여 복사합니다.
    // ADC 런타임 정보를 동기화하여 복사합니다.
    // ADC 런타임 정보를 동기화하여 복사합니다.
    // ADC 런타임 정보를 동기화하여 복사합니다.
    if (slaveCount_ > 0)
    {
        adcRuntimeInfoBySlave_.resize(
            static_cast<std::size_t>(
                slaveCount_ + 1));

        for (int physicalSlaveIndex = 1;
            physicalSlaveIndex <= slaveCount_;
            ++physicalSlaveIndex)
        {
            DaoInternalAdcRuntimeInfo& runtimeInfo =
                adcRuntimeInfoBySlave_[
                    static_cast<std::size_t>(
                        physicalSlaveIndex)];

            runtimeInfo = {};

            runtimeInfo.physicalSlaveIndex =
                physicalSlaveIndex;

            runtimeInfo.expectedWkc =
                expectedWkc_;
        }
    }
}

void DaoEtherCATMaster::ResetServoRuntimeInfo() // Servo 런타임 상태와 출력 명령을 초기화합니다.
{
    std::lock_guard<std::mutex> lock(
        servoRuntimeMutex_);

    servoRuntimeInfoBySlave_.clear();

    if (slaveCount_ > 0)
    {
        servoRuntimeInfoBySlave_.resize(
            static_cast<std::size_t>(
                slaveCount_ + 1));

        for (int physicalSlaveIndex = 1;
            physicalSlaveIndex <= slaveCount_;
            ++physicalSlaveIndex)
        {
            DaoInternalServoRuntimeInfo& runtimeInfo =
                servoRuntimeInfoBySlave_[
                    static_cast<std::size_t>(
                        physicalSlaveIndex)];

            runtimeInfo = {};

            runtimeInfo.physicalSlaveIndex =
                physicalSlaveIndex;

            runtimeInfo.expectedWkc =
                expectedWkc_;
        }
    }
}


void DaoEtherCATMaster::ResetIoRuntimeInfo() // IO 입력 PDO를 읽어 최신 입력 상태를 갱신합니다.
{
    std::lock_guard<std::mutex> lock(
        ioRuntimeMutex_);

    ioRuntimeInfoBySlave_.clear();

    if (slaveCount_ > 0)
    {
        ioRuntimeInfoBySlave_.resize(
            static_cast<std::size_t>(
                slaveCount_ + 1));

        for (int physicalSlaveIndex = 1;
            physicalSlaveIndex <= slaveCount_;
            ++physicalSlaveIndex)
        {
            DaoInternalIoRuntimeInfo& runtimeInfo =
                ioRuntimeInfoBySlave_[
                    static_cast<std::size_t>(
                        physicalSlaveIndex)];

            runtimeInfo = {};

            runtimeInfo.physicalSlaveIndex =
                physicalSlaveIndex;

            runtimeInfo.expectedWkc =
                expectedWkc_;
        }
    }
}

void DaoEtherCATMaster::ResetEncoderRuntimeInfo()
{
    std::lock_guard<std::mutex> lock(
        encoderRuntimeMutex_);

    encoderRuntimeInfoBySlave_.clear();

    if (slaveCount_ > 0)
    {
        encoderRuntimeInfoBySlave_.resize(
            static_cast<std::size_t>(
                slaveCount_ + 1));

        for (int physicalSlaveIndex = 1;
            physicalSlaveIndex <= slaveCount_;
            ++physicalSlaveIndex)
        {
            DaoInternalEncoderRuntimeInfo& runtimeInfo =
                encoderRuntimeInfoBySlave_[
                    static_cast<std::size_t>(
                        physicalSlaveIndex)];

            runtimeInfo = {};

            runtimeInfo.physicalSlaveIndex =
                physicalSlaveIndex;

            runtimeInfo.expectedWkc =
                expectedWkc_;
        }
    }
}


void DaoEtherCATMaster::CommunicationThreadMain() // EtherCAT 주기 통신 스레드의 본체입니다.
{
    // --------------------------------------------------------
    // EtherCAT 주기 통신 스레드의 본체입니다.
    //
    // 2ms = 500Hz
    // --------------------------------------------------------
    constexpr auto COMMUNICATION_PERIOD =
        std::chrono::milliseconds(2);

    auto nextWakeTime =
        std::chrono::steady_clock::now();

    while (!communicationStopRequested_.load())
    {
        nextWakeTime +=
            COMMUNICATION_PERIOD;

        // ----------------------------------------------------
        // 통신 주기와 다음 실행 시각을 기준으로 루프를 유지합니다.
        //
        // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
        // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
        // ----------------------------------------------------
        for (int physicalSlaveIndex = 1;
            physicalSlaveIndex <= slaveCount_;
            ++physicalSlaveIndex)
        {
            ec_slavet& slave =
                context_.slavelist[
                    physicalSlaveIndex];

            if (!IsDaoAdcSlave(slave))
            {
                continue;
            }

            // Slave의 출력 PDO 크기와 입력 PDO 크기를 확인합니다.
            if (slave.Obytes != 4 ||
                slave.Ibytes != 24 ||
                slave.outputs == nullptr ||
                slave.inputs == nullptr)
            {
                continue;
            }

            std::memset(
                slave.outputs,
                0,
                static_cast<std::size_t>(
                    slave.Obytes));
        }

        // ----------------------------------------------------
        // Servo에 전송할 출력 PDO를 IO Map에 복사합니다.
        // Servo에 전송할 출력 PDO를 IO Map에 복사합니다.
        // ----------------------------------------------------
        PrepareServoAndIoOutputs();

        PrepareEncoderOutputs();

        // ----------------------------------------------------
        // Process Data를 송신하고 수신 WKC를 확인합니다.
        // ----------------------------------------------------
        ecx_send_processdata(
            &context_);

        const int actualWkc =
            ecx_receive_processdata(
                &context_,
                EC_TIMEOUTRET);

        // ----------------------------------------------------
        // Process Data를 송신하고 수신 WKC를 확인합니다.
        // Process Data를 송신하고 수신 WKC를 확인합니다.
        // ----------------------------------------------------
        CaptureServoAndIoInputs(
            actualWkc);


        CaptureEncoderInputs(
            actualWkc);

        // Process Data를 송신하고 수신 WKC를 확인합니다.
        // Process Data를 송신하고 수신 WKC를 확인합니다.
        ProcessServoCommands();

        const bool wkcValid =
            actualWkc >= expectedWkc_;

        // ----------------------------------------------------
        // Process Data를 송신하고 수신 WKC를 확인합니다.
        // ----------------------------------------------------
        for (int physicalSlaveIndex = 1;
            physicalSlaveIndex <= slaveCount_;
            ++physicalSlaveIndex)
        {
            ec_slavet& slave =
                context_.slavelist[
                    physicalSlaveIndex];

            if (!IsDaoAdcSlave(slave))
            {
                continue;
            }

            if (slave.Obytes != 4 ||
                slave.Ibytes != 24 ||
                slave.outputs == nullptr ||
                slave.inputs == nullptr)
            {
                continue;
            }

            DaoInternalAdcInputPdo latestData{};

            // PDO 메모리 포인터가 유효한지 확인합니다.
            if (wkcValid)
            {
                std::memcpy(
                    &latestData,
                    slave.inputs,
                    sizeof(DaoInternalAdcInputPdo));
            }

            const std::size_t runtimeIndex =
                static_cast<std::size_t>(
                    physicalSlaveIndex);

            {
                std::lock_guard<std::mutex> lock(
                    adcRuntimeMutex_);

                if (runtimeIndex >=
                    adcRuntimeInfoBySlave_.size())
                {
                    continue;
                }

                DaoInternalAdcRuntimeInfo& runtimeInfo =
                    adcRuntimeInfoBySlave_[
                        runtimeIndex];

                runtimeInfo.physicalSlaveIndex =
                    physicalSlaveIndex;

                runtimeInfo.communicationRunning =
                    true;

                runtimeInfo.lastWkc =
                    actualWkc;

                runtimeInfo.expectedWkc =
                    expectedWkc_;

                ++runtimeInfo.totalFrameCount;

                if (wkcValid)
                {
                    runtimeInfo.hasValidData =
                        true;

                    ++runtimeInfo.goodWkcFrameCount;
                    ++runtimeInfo.dataUpdateCount;

                    runtimeInfo.latestData =
                        latestData;

                    ProcessAdcSample(
                        runtimeInfo,
                        latestData.adcRaw0);

                    ProcessAdcSample(
                        runtimeInfo,
                        latestData.adcRaw1);

                    ProcessAdcSample(
                        runtimeInfo,
                        latestData.adcRaw2);

                    ProcessAdcSample(
                        runtimeInfo,
                        latestData.adcRaw3);
                }
                else
                {
                    ++runtimeInfo.badWkcFrameCount;
                }
            }
        }

        // ----------------------------------------------------
        // 통신 주기와 다음 실행 시각을 기준으로 루프를 유지합니다.
        // ----------------------------------------------------
        std::this_thread::sleep_until(
            nextWakeTime);

        // 통신 주기와 다음 실행 시각을 기준으로 루프를 유지합니다.
        const auto now =
            std::chrono::steady_clock::now();

        if (now - nextWakeTime >=
            std::chrono::milliseconds(20))
        {
            nextWakeTime = now;
        }
    }

    // --------------------------------------------------------
    // ADC 런타임 정보를 동기화하여 복사합니다.
    // --------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(
            adcRuntimeMutex_);

        for (std::size_t runtimeIndex = 1;
            runtimeIndex <
            adcRuntimeInfoBySlave_.size();
            ++runtimeIndex)
        {
            adcRuntimeInfoBySlave_[
                runtimeIndex]
                .communicationRunning = false;
        }
    }

    // --------------------------------------------------------
    // Servo 런타임 정보를 동기화하여 복사합니다.
    // Servo 런타임 정보를 동기화하여 복사합니다.
    // --------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(
            servoRuntimeMutex_);

        for (std::size_t runtimeIndex = 1;
            runtimeIndex <
            servoRuntimeInfoBySlave_.size();
            ++runtimeIndex)
        {
            servoRuntimeInfoBySlave_[
                runtimeIndex]
                .communicationRunning = false;
        }
    }

    // --------------------------------------------------------
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    // --------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(
            ioRuntimeMutex_);

        for (std::size_t runtimeIndex = 1;
            runtimeIndex <
            ioRuntimeInfoBySlave_.size();
            ++runtimeIndex)
        {
            ioRuntimeInfoBySlave_[
                runtimeIndex]
                .communicationRunning = false;
        }
    }

    {
        std::lock_guard<std::mutex> lock(
            encoderRuntimeMutex_);

        for (std::size_t runtimeIndex = 1;
            runtimeIndex <
            encoderRuntimeInfoBySlave_.size();
            ++runtimeIndex)
        {
            encoderRuntimeInfoBySlave_[
                runtimeIndex]
                .communicationRunning = false;
        }
    }
    

    communicationRunning_.store(false);
}

void DaoEtherCATMaster::ConfigureServoAndIoRuntimeInfo()
{
    // --------------------------------------------------------
    // Servo 런타임 상태와 출력 명령을 초기화합니다.
    // --------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(
            servoRuntimeMutex_);

        for (int physicalSlaveIndex = 1;
            physicalSlaveIndex <= slaveCount_;
            ++physicalSlaveIndex)
        {
            if (physicalSlaveIndex >=
                static_cast<int>(
                    servoRuntimeInfoBySlave_.size()))
            {
                continue;
            }

            DaoInternalServoRuntimeInfo& runtimeInfo =
                servoRuntimeInfoBySlave_[
                    static_cast<std::size_t>(
                        physicalSlaveIndex)];

            // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
            runtimeInfo = {};

            runtimeInfo.physicalSlaveIndex =
                physicalSlaveIndex;

            runtimeInfo.expectedWkc =
                expectedWkc_;

            // 정상 통신 여부를 판단하기 위한 예상 WKC 값입니다.
            if (!IsLsL7nhServo(
                physicalSlaveIndex))
            {
                continue;
            }

            const ec_slavet& slave =
                context_.slavelist[
                    physicalSlaveIndex];

            // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
            const bool outputSizeValid =
                slave.Obytes ==
                sizeof(
                    DaoInternalLsServoOutputPdo);

            const bool inputSizeValid =
                slave.Ibytes ==
                sizeof(
                    DaoInternalLsServoInputPdo);

            const bool outputPointerValid =
                slave.outputs != nullptr;

            const bool inputPointerValid =
                slave.inputs != nullptr;

            if (!outputSizeValid ||
                !inputSizeValid ||
                !outputPointerValid ||
                !inputPointerValid)
            {
                continue;
            }

            runtimeInfo.configured = true;

            // ----------------------------------------------------
            // 요청된 정보를 출력 구조체에 복사합니다.
            //
            // Servo에 전송할 출력 PDO를 IO Map에 복사합니다.
            // Servo에 전송할 출력 PDO를 IO Map에 복사합니다.
            // Servo에 전송할 출력 PDO를 IO Map에 복사합니다.
            // Servo에 전송할 출력 PDO를 IO Map에 복사합니다.
            // ----------------------------------------------------
            runtimeInfo.outputCommand = {};

            runtimeInfo.outputCommand.controlWord =
                0x0000;

            runtimeInfo.outputCommand.operationMode =
                1;  // Profile Position

            runtimeInfo.outputCommand.targetPosition =
                0;

            runtimeInfo.outputCommand.profileVelocity =
                1000;

            runtimeInfo.outputCommand.profileAcceleration =
                1000;

            runtimeInfo.outputCommand.profileDeceleration =
                1000;

            runtimeInfo.outputCommand.targetVelocity =
                0;

            runtimeInfo.outputCommand.touchProbeFunction =
                0;

            runtimeInfo.outputCommand.digitalOutputs =
                0;


            // Servo 입력 PDO를 읽어 최신 런타임 상태를 갱신합니다.
            runtimeInfo.latestInput = {};
            runtimeInfo.hasValidInputData = false;
        }
    }


  


    // --------------------------------------------------------
    // FASTECH Ezi-IO 장치의 식별 정보와 입출력 크기를 확인합니다.
    // --------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(
            ioRuntimeMutex_);

        for (int physicalSlaveIndex = 1;
            physicalSlaveIndex <= slaveCount_;
            ++physicalSlaveIndex)
        {
            if (physicalSlaveIndex >=
                static_cast<int>(
                    ioRuntimeInfoBySlave_.size()))
            {
                continue;
            }

            DaoInternalIoRuntimeInfo& runtimeInfo =
                ioRuntimeInfoBySlave_[
                    static_cast<std::size_t>(
                        physicalSlaveIndex)];

            runtimeInfo = {};

            runtimeInfo.physicalSlaveIndex =
                physicalSlaveIndex;

            runtimeInfo.expectedWkc =
                expectedWkc_;

            if (!IsFastechIo(
                physicalSlaveIndex))
            {
                continue;
            }

            const ec_slavet& slave =
                context_.slavelist[
                    physicalSlaveIndex];

            const int outputBytes =
                static_cast<int>(
                    slave.Obytes);

            const int inputBytes =
                static_cast<int>(
                    slave.Ibytes);

            const bool sizeValid =
                (outputBytes == 1 &&
                    inputBytes == 1) ||
                (outputBytes == 2 &&
                    inputBytes == 2);

            const bool outputPointerValid =
                slave.outputs != nullptr;

            const bool inputPointerValid =
                slave.inputs != nullptr;

            if (!sizeValid ||
                !outputPointerValid ||
                !inputPointerValid)
            {
                continue;
            }

			runtimeInfo.configured = true; // Slave의 출력 PDO 크기와 입력 PDO 크기를 확인합니다.
            runtimeInfo.outputBytes =
                outputBytes;

            runtimeInfo.inputBytes =
                inputBytes;

            runtimeInfo.outputCommand = 0;
            runtimeInfo.latestInput = 0;
            runtimeInfo.hasValidInputData = false;
        }
    }
}


void DaoEtherCATMaster::ConfigureEncoderRuntimeInfo()
{
    std::lock_guard<std::mutex> lock(
        encoderRuntimeMutex_);

    for (int physicalSlaveIndex = 1;
        physicalSlaveIndex <= slaveCount_;
        ++physicalSlaveIndex)
    {
        if (physicalSlaveIndex >=
            static_cast<int>(
                encoderRuntimeInfoBySlave_.size()))
        {
            continue;
        }

        DaoInternalEncoderRuntimeInfo& runtimeInfo =
            encoderRuntimeInfoBySlave_[
                static_cast<std::size_t>(
                    physicalSlaveIndex)];

        runtimeInfo = {};

        runtimeInfo.physicalSlaveIndex =
            physicalSlaveIndex;

        runtimeInfo.expectedWkc =
            expectedWkc_;

        if (!IsFastechEncoder(
            physicalSlaveIndex))
        {
            continue;
        }

        const ec_slavet& slave =
            context_.slavelist[
                physicalSlaveIndex];

        const bool outputSizeValid =
            slave.Obytes ==
            sizeof(
                DaoInternalFastechEncoderOutputPdo);

        const bool inputSizeValid =
            slave.Ibytes ==
            sizeof(
                DaoInternalFastechEncoderInputPdo);

        const bool outputPointerValid =
            slave.outputs != nullptr;

        const bool inputPointerValid =
            slave.inputs != nullptr;

        if (!outputSizeValid ||
            !inputSizeValid ||
            !outputPointerValid ||
            !inputPointerValid)
        {
            continue;
        }

        runtimeInfo.configured = true;

        runtimeInfo.outputCommand = {};

        // CH1 Count Enable = 3030:01 = bit 0   
        runtimeInfo.outputCommand.counterCommand = 0x01;

        runtimeInfo.latestInput = {};

        runtimeInfo.hasValidInputData =
            false;
    }
}

void DaoEtherCATMaster::PrepareServoAndIoOutputs()
{
    // --------------------------------------------------------
    // Servo에 전송할 출력 PDO를 IO Map에 복사합니다.
    //
    // Servo에 전송할 출력 PDO를 IO Map에 복사합니다.
    // Servo 런타임 정보를 동기화하여 복사합니다.
    // --------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(
            servoRuntimeMutex_);

        for (int physicalSlaveIndex = 1;
            physicalSlaveIndex <= slaveCount_;
            ++physicalSlaveIndex)
        {
            const std::size_t runtimeIndex =
                static_cast<std::size_t>(
                    physicalSlaveIndex);

            if (runtimeIndex >=
                servoRuntimeInfoBySlave_.size())
            {
                continue;
            }

            DaoInternalServoRuntimeInfo& runtimeInfo =
                servoRuntimeInfoBySlave_[
                    runtimeIndex];

            // Servo 런타임 정보를 동기화하여 복사합니다.
            // 요청된 정보를 출력 구조체에 복사합니다.
            if (!runtimeInfo.configured)
            {
                continue;
            }

            ec_slavet& slave =
                context_.slavelist[
                    physicalSlaveIndex];

            // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
            // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
            // Slave의 출력 PDO 크기와 입력 PDO 크기를 확인합니다.
            if (slave.outputs == nullptr ||
                slave.Obytes !=
                sizeof(
                    DaoInternalLsServoOutputPdo))
            {
                continue;
            }

            std::memcpy(
                slave.outputs,
                &runtimeInfo.outputCommand,
                sizeof(
                    DaoInternalLsServoOutputPdo));
        }
    }

    // --------------------------------------------------------
    // FASTECH Ezi-IO 장치의 식별 정보와 입출력 크기를 확인합니다.
    //
    // FASTECH Ezi-IO 장치의 식별 정보와 입출력 크기를 확인합니다.
    // FASTECH Ezi-IO 장치의 식별 정보와 입출력 크기를 확인합니다.
    // --------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(
            ioRuntimeMutex_);

        for (int physicalSlaveIndex = 1;
            physicalSlaveIndex <= slaveCount_;
            ++physicalSlaveIndex)
        {
            const std::size_t runtimeIndex =
                static_cast<std::size_t>(
                    physicalSlaveIndex);

            if (runtimeIndex >=
                ioRuntimeInfoBySlave_.size())
            {
                continue;
            }

            DaoInternalIoRuntimeInfo& runtimeInfo =
                ioRuntimeInfoBySlave_[
                    runtimeIndex];

            if (!runtimeInfo.configured)
            {
                continue;
            }

            ec_slavet& slave =
                context_.slavelist[
                    physicalSlaveIndex];

            if (slave.outputs == nullptr)
            {
                continue;
            }

            if (runtimeInfo.outputBytes == 1 &&
                slave.Obytes == 1)
            {
                const unsigned char outputValue =
                    static_cast<unsigned char>(
                        runtimeInfo.outputCommand &
                        0x00FF);

                std::memcpy(
                    slave.outputs,
                    &outputValue,
                    sizeof(outputValue));
            }
            else if (
                runtimeInfo.outputBytes == 2 &&
                slave.Obytes == 2)
            {
                const unsigned short outputValue =
                    runtimeInfo.outputCommand;

                std::memcpy(
                    slave.outputs,
                    &outputValue,
                    sizeof(outputValue));
            }
        }
    }
}


void DaoEtherCATMaster::PrepareEncoderOutputs()
{
    std::lock_guard<std::mutex> lock(
        encoderRuntimeMutex_);

    for (int physicalSlaveIndex = 1;
        physicalSlaveIndex <= slaveCount_;
        ++physicalSlaveIndex)
    {
        const std::size_t runtimeIndex =
            static_cast<std::size_t>(
                physicalSlaveIndex);

        if (runtimeIndex >=
            encoderRuntimeInfoBySlave_.size())
        {
            continue;
        }

        DaoInternalEncoderRuntimeInfo& runtimeInfo =
            encoderRuntimeInfoBySlave_[
                runtimeIndex];

        if (!runtimeInfo.configured)
        {
            continue;
        }

        ec_slavet& slave =
            context_.slavelist[
                physicalSlaveIndex];

        if (slave.outputs == nullptr ||
            slave.Obytes !=
            sizeof(
                DaoInternalFastechEncoderOutputPdo))
        {
            continue;
        }

        std::memcpy(
            slave.outputs,
            &runtimeInfo.outputCommand,
            sizeof(
                DaoInternalFastechEncoderOutputPdo));
    }
}

void DaoEtherCATMaster::CaptureServoAndIoInputs(
    int actualWkc)
{
    const bool wkcValid =
        actualWkc >= expectedWkc_;

    // --------------------------------------------------------
    // Process Data를 송신하고 수신 WKC를 확인합니다.
    // --------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(
            servoRuntimeMutex_);

        for (int physicalSlaveIndex = 1;
            physicalSlaveIndex <= slaveCount_;
            ++physicalSlaveIndex)
        {
            const std::size_t runtimeIndex =
                static_cast<std::size_t>(
                    physicalSlaveIndex);

            if (runtimeIndex >=
                servoRuntimeInfoBySlave_.size())
            {
                continue;
            }

            DaoInternalServoRuntimeInfo& runtimeInfo =
                servoRuntimeInfoBySlave_[
                    runtimeIndex];

            if (!runtimeInfo.configured)
            {
                continue;
            }

            runtimeInfo.communicationRunning =
                communicationRunning_.load();

            runtimeInfo.lastWkc =
                actualWkc;

            runtimeInfo.expectedWkc =
                expectedWkc_;

            ++runtimeInfo.totalFrameCount;

            if (!wkcValid)
            {
                ++runtimeInfo.badWkcFrameCount;
                continue;
            }

            ++runtimeInfo.goodWkcFrameCount;

            const ec_slavet& slave =
                context_.slavelist[
                    physicalSlaveIndex];

            if (slave.inputs == nullptr ||
                slave.Ibytes !=
                sizeof(
                    DaoInternalLsServoInputPdo))
            {
                continue;
            }

            std::memcpy(
                &runtimeInfo.latestInput,
                slave.inputs,
                sizeof(
                    DaoInternalLsServoInputPdo));

            runtimeInfo.hasValidInputData = true;

            // Statusword에서 CiA 402 상태를 추출합니다.
            UpdateServoDerivedState(
                runtimeInfo);

            ++runtimeInfo.inputUpdateCount;
        }
    }

    // --------------------------------------------------------
    // FASTECH Ezi-IO 장치의 식별 정보와 입출력 크기를 확인합니다.
    // --------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(
            ioRuntimeMutex_);

        for (int physicalSlaveIndex = 1;
            physicalSlaveIndex <= slaveCount_;
            ++physicalSlaveIndex)
        {
            const std::size_t runtimeIndex =
                static_cast<std::size_t>(
                    physicalSlaveIndex);

            if (runtimeIndex >=
                ioRuntimeInfoBySlave_.size())
            {
                continue;
            }

            DaoInternalIoRuntimeInfo& runtimeInfo =
                ioRuntimeInfoBySlave_[
                    runtimeIndex];

            if (!runtimeInfo.configured)
            {
                continue;
            }

            runtimeInfo.communicationRunning =
                communicationRunning_.load();

            runtimeInfo.lastWkc =
                actualWkc;

            runtimeInfo.expectedWkc =
                expectedWkc_;

            ++runtimeInfo.totalFrameCount;

            if (!wkcValid)
            {
                ++runtimeInfo.badWkcFrameCount;
                continue;
            }

            ++runtimeInfo.goodWkcFrameCount;

            const ec_slavet& slave =
                context_.slavelist[
                    physicalSlaveIndex];

            if (slave.inputs == nullptr)
            {
                continue;
            }

            if (runtimeInfo.inputBytes == 1 &&
                slave.Ibytes == 1)
            {
                unsigned char inputValue = 0;

                std::memcpy(
                    &inputValue,
                    slave.inputs,
                    sizeof(inputValue));

                runtimeInfo.latestInput =
                    static_cast<unsigned short>(
                        inputValue);
            }
            else if (
                runtimeInfo.inputBytes == 2 &&
                slave.Ibytes == 2)
            {
                unsigned short inputValue = 0;

                std::memcpy(
                    &inputValue,
                    slave.inputs,
                    sizeof(inputValue));

                runtimeInfo.latestInput =
                    inputValue;
            }
            else
            {
                continue;
            }

            runtimeInfo.hasValidInputData = true;

            ++runtimeInfo.inputUpdateCount;
        }
    }
}
void DaoEtherCATMaster::CaptureEncoderInputs(
    int actualWkc)
{
    const bool wkcValid =
        actualWkc >= expectedWkc_;

    std::lock_guard<std::mutex> lock(
        encoderRuntimeMutex_);

    for (int physicalSlaveIndex = 1;
        physicalSlaveIndex <= slaveCount_;
        ++physicalSlaveIndex)
    {
        const std::size_t runtimeIndex =
            static_cast<std::size_t>(
                physicalSlaveIndex);

        if (runtimeIndex >=
            encoderRuntimeInfoBySlave_.size())
        {
            continue;
        }

        DaoInternalEncoderRuntimeInfo& runtimeInfo =
            encoderRuntimeInfoBySlave_[
                runtimeIndex];

        if (!runtimeInfo.configured)
        {
            continue;
        }

        runtimeInfo.communicationRunning =
            communicationRunning_.load();

        runtimeInfo.lastWkc =
            actualWkc;

        runtimeInfo.expectedWkc =
            expectedWkc_;

        ++runtimeInfo.totalFrameCount;

        if (!wkcValid)
        {
            ++runtimeInfo.badWkcFrameCount;
            continue;
        }

        ++runtimeInfo.goodWkcFrameCount;

        const ec_slavet& slave =
            context_.slavelist[
                physicalSlaveIndex];

        if (slave.inputs == nullptr ||
            slave.Ibytes !=
            sizeof(
                DaoInternalFastechEncoderInputPdo))
        {
            continue;
        }

        std::memcpy(
            &runtimeInfo.latestInput,
            slave.inputs,
            sizeof(
                DaoInternalFastechEncoderInputPdo));

        runtimeInfo.hasValidInputData =
            true;

        ++runtimeInfo.inputUpdateCount;
    }
}



void DaoEtherCATMaster::ProcessServoCommands()
{
    std::lock_guard<std::mutex> lock(
        servoRuntimeMutex_);

    constexpr std::uint64_t
        SERVO_ON_TIMEOUT_FRAMES = 1000;

    for (int physicalSlaveIndex = 1;
        physicalSlaveIndex <= slaveCount_;
        ++physicalSlaveIndex)
    {
        const std::size_t runtimeIndex =
            static_cast<std::size_t>(
                physicalSlaveIndex);

        if (runtimeIndex >=
            servoRuntimeInfoBySlave_.size())
        {
            continue;
        }

        DaoInternalServoRuntimeInfo& runtimeInfo =
            servoRuntimeInfoBySlave_[
                runtimeIndex];

        if (!runtimeInfo.configured ||
            !runtimeInfo.hasValidInputData)
        {
            continue;
        }


        // ====================================================
        // Servo ON 명령을 등록하고 CiA 402 활성화 절차를 시작합니다.
        // ====================================================
        const bool isServoOnCommand =
            runtimeInfo.commandType ==
            DAO_SERVO_COMMAND_SERVO_ON;

        const bool isServoOffCommand =
            runtimeInfo.commandType ==
            DAO_SERVO_COMMAND_SERVO_OFF;

        const bool isHomingCommand =
            runtimeInfo.commandType ==
            DAO_SERVO_COMMAND_HOMING;

        const bool isMoveAbsoluteCommand =
            runtimeInfo.commandType ==
            DAO_SERVO_COMMAND_MOVE_ABSOLUTE;

        const bool isVelocityCommand =
            runtimeInfo.commandType ==
            DAO_SERVO_COMMAND_VELOCITY;

        const bool isStopCommand =
            runtimeInfo.commandType ==
            DAO_SERVO_COMMAND_STOP;


        // Servo ON 명령을 등록하고 CiA 402 활성화 절차를 시작합니다.
        if (!isServoOnCommand &&
            !isServoOffCommand &&
            !isHomingCommand &&
            !isMoveAbsoluteCommand &&
            !isVelocityCommand &&
            !isStopCommand)
        {
            continue;
        }

        // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
        if (runtimeInfo.commandState !=
            DAO_SERVO_COMMAND_STATE_ACCEPTED &&
            runtimeInfo.commandState !=
            DAO_SERVO_COMMAND_STATE_RUNNING)
        {
            continue;
        }


        // ====================================================
        // HOMING COMMAND
        // ====================================================
        if (isHomingCommand)
        {
            // ------------------------------------------------
            // Servo의 Fault 상태를 확인하고 명령 결과에 반영합니다.
            // ------------------------------------------------
            if (runtimeInfo.fault) 
            {
                runtimeInfo.commandState =
                    DAO_SERVO_COMMAND_STATE_ERROR;

                runtimeInfo.commandResult = -1;

                runtimeInfo.homed = false;

                continue;
            }

			if (runtimeInfo.stoActive) // Servo에 전송할 출력 PDO를 IO Map에 복사합니다.
            {
                runtimeInfo.outputCommand.controlWord =
                    0x000F;

                runtimeInfo.outputCommand.targetVelocity =
                    0;

                runtimeInfo.commandState =
                    DAO_SERVO_COMMAND_STATE_ERROR;

                runtimeInfo.commandResult =
                    -1;

                runtimeInfo.homed =
                    false;

                continue;
            }


            DaoInternalLsServoOutputPdo& command =
                runtimeInfo.outputCommand;


            // Servo에 전송할 출력 PDO를 IO Map에 복사합니다.
            // Servo 입력 PDO를 읽어 최신 런타임 상태를 갱신합니다.
            command.targetPosition =
                runtimeInfo.latestInput.actualPosition;

            command.targetVelocity = 0;

            command.touchProbeFunction = 0;

            command.digitalOutputs = 0;


            // =================================================
            // 1. HOMING_PREPARE
            //
            // Servo Homing 명령과 제한 시간을 설정합니다.
            // Servo Homing 명령과 제한 시간을 설정합니다.
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_HOMING_PREPARE)
            {
                runtimeInfo.commandState =
                    DAO_SERVO_COMMAND_STATE_RUNNING;

                // Statusword에서 CiA 402 상태를 추출합니다.
                // Statusword에서 CiA 402 상태를 추출합니다.
                if (runtimeInfo.cia402State != 0x0021)
                {
                    command.controlWord = 0x0006;
                    continue;
                }

                // Servo의 운전 모드를 SDO로 설정합니다.
                command.controlWord = 0x0006;

                // Servo의 운전 모드를 SDO로 설정합니다.
                command.operationMode = 6;

                runtimeInfo.commandStep =
                    DAO_SERVO_STEP_HOMING_MODE_REQUEST;

                continue;
            }


            // =================================================
            // 2. HOMING_MODE_REQUEST
            //
            // 0x6060 = 6
            // 요청된 정보를 출력 구조체에 복사합니다.
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_HOMING_MODE_REQUEST)
            {
                command.controlWord = 0x0006;

                command.operationMode = 6;

                // Servo의 운전 모드를 SDO로 설정합니다.
                // Servo의 운전 모드를 SDO로 설정합니다.
                if (runtimeInfo.latestInput.operationModeDisplay != 6)
                {
                    continue;
                }

                runtimeInfo.commandStep =
                    DAO_SERVO_STEP_HOMING_SERVO_ON;

                continue;
            }


            // =================================================
            // 3. HOMING_SERVO_ON
            //
            // Servo의 운전 모드를 SDO로 설정합니다.
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_HOMING_SERVO_ON)
            {
                command.operationMode = 6;


                // Servo의 운전 모드를 SDO로 설정합니다.
                if (runtimeInfo.cia402State == 0x0027)
                {
                    command.controlWord = 0x000F;

                    runtimeInfo.commandStep =
                        DAO_SERVO_STEP_HOMING_START;

                    continue;
                }


                // Ready To Switch On
                // -> Switched On
                if (runtimeInfo.cia402State == 0x0021)
                {
                    command.controlWord = 0x0007;
                    continue;
                }


                // Switched On
                // -> Operation Enabled
                if (runtimeInfo.cia402State == 0x0023)
                {
                    command.controlWord = 0x000F;
                    continue;
                }


                // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
                // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
                continue;
            }


            // =================================================
            // 4. HOMING_START
            //
            // Servo Homing 명령과 제한 시간을 설정합니다.
            //
            // 0x001F =
            // Operation Enabled + Homing Start
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_HOMING_START)
            {
                command.operationMode = 6;

                command.controlWord = 0x001F;


                // Servo Homing 명령과 제한 시간을 설정합니다.
                runtimeInfo.homingStartFrameCount =
                    runtimeInfo.totalFrameCount;

                // Servo 입력 PDO를 읽어 최신 런타임 상태를 갱신합니다.
                runtimeInfo.homingLastPosition =
                    runtimeInfo.latestInput.actualPosition;

                runtimeInfo.homingLastMoveFrameCount =
                    runtimeInfo.totalFrameCount;

                runtimeInfo.homingPositionMonitorStarted =
                    true;

                runtimeInfo.homingAttainedWentLow =
					false; // Servo 입력 PDO를 읽어 최신 런타임 상태를 갱신합니다.

                // Servo 입력 PDO를 읽어 최신 런타임 상태를 갱신합니다.
                runtimeInfo.moveStartPosition =
                    runtimeInfo.latestInput.actualPosition;

                // Servo 입력 PDO를 읽어 최신 런타임 상태를 갱신합니다.
                runtimeInfo.moveStartFrameCount =
                    runtimeInfo.totalFrameCount;

                runtimeInfo.commandStep =
                    DAO_SERVO_STEP_HOMING_RUNNING;

                continue;
            }


            // =================================================
            // 5. HOMING_RUNNING
            //
            // Statusword에서 CiA 402 상태를 추출합니다.
            // Statusword에서 CiA 402 상태를 추출합니다.
            //
            // StatusWord
            // Bit 12 : Homing Attained
            // Bit 13 : Homing Error
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_HOMING_RUNNING)
            {
                command.operationMode = 6;

                const unsigned short statusWord =
                    runtimeInfo.latestInput.statusWord;

                // ----------------------------------------------------
                // Servo 입력 PDO를 읽어 최신 런타임 상태를 갱신합니다.
                // ----------------------------------------------------
                if (runtimeInfo.homingPositionMonitorStarted)
                {
                    const int currentPosition =
                        runtimeInfo.latestInput.actualPosition;

                    const long long positionDifference =
                        static_cast<long long>(currentPosition) -
                        static_cast<long long>(
                            runtimeInfo.homingLastPosition);

                    const long long absoluteDifference =
                        positionDifference >= 0
                        ? positionDifference
                        : -positionDifference;

                    // 이 값은 처리된 프레임 또는 샘플의 누적 개수를 나타냅니다.
                    // 아래 조건을 확인한 후 현재 처리 단계를 계속합니다.
                    constexpr long long
                        HOMING_POSITION_CHANGE_THRESHOLD = 5;

                    if (absoluteDifference >
                        HOMING_POSITION_CHANGE_THRESHOLD)
                    {
                        runtimeInfo.homingLastPosition =
                            currentPosition;

                        runtimeInfo.homingLastMoveFrameCount =
                            runtimeInfo.totalFrameCount;
                    }
                }

                // ----------------------------------------------------
                // 이 값은 처리된 프레임 또는 샘플의 누적 개수를 나타냅니다.
                //
                // Servo Homing 명령과 제한 시간을 설정합니다.
                // Servo Homing 명령과 제한 시간을 설정합니다.
                // ----------------------------------------------------
                const std::uint64_t noMoveFrames =
                    runtimeInfo.totalFrameCount -
                    runtimeInfo.homingLastMoveFrameCount;
                //----------------------------------------------------

                const bool homingAttained =
                    (statusWord & 0x1000) != 0;

                const bool homingError =
                    (statusWord & 0x2000) != 0;

                // Statusword에서 CiA 402 상태를 추출합니다.
                // Statusword에서 CiA 402 상태를 추출합니다.
                if (!homingAttained)
                {
                    runtimeInfo.homingAttainedWentLow =
                        true;
                }

                // ------------------------------------------------
                // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.
                //
                // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.
                // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.
                // ------------------------------------------------
                const std::uint64_t elapsedHomingFrames =
                    runtimeInfo.totalFrameCount -
                    runtimeInfo.homingStartFrameCount;

                const std::uint64_t timeoutFrames =
                    (static_cast<std::uint64_t>(
                        runtimeInfo.homingTimeoutMs) + 1ULL) / 2ULL;


                // ---------------------------------------------
                // Homing Error
                // ---------------------------------------------
                if (homingError)
                {
                    // Servo Homing 명령과 제한 시간을 설정합니다.
                    command.controlWord = 0x000F;

                    runtimeInfo.commandState =
                        DAO_SERVO_COMMAND_STATE_ERROR;

                    runtimeInfo.commandResult = -3;

                    runtimeInfo.homed = false;

                    continue;
                }

                // ---------------------------------------------
               // Servo Homing 명령과 제한 시간을 설정합니다.
               // ---------------------------------------------
                if (runtimeInfo.homingAttainedWentLow &&
                    homingAttained &&
                    runtimeInfo.targetReached)
                {
                    command.controlWord = 0x000F;

                    runtimeInfo.commandStep =
                        DAO_SERVO_STEP_HOMING_FINISH;

                    continue;
                }

               
                // ------------------------------------------------
                // Homing Timeout
                // ------------------------------------------------
                if (elapsedHomingFrames >= timeoutFrames)
                {
                    // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.
                    command.controlWord = 0x000F;

                    // Servo의 운전 모드를 SDO로 설정합니다.
                    command.operationMode = 1;

                    command.targetPosition =
                        runtimeInfo.latestInput.actualPosition;

                    command.targetVelocity = 0;

                    runtimeInfo.homed = false;

                    runtimeInfo.commandState =
                        DAO_SERVO_COMMAND_STATE_TIMEOUT;

                    runtimeInfo.commandResult = -4;

                    continue;
                }


               


                // Servo Homing 명령과 제한 시간을 설정합니다.
                command.controlWord = 0x001F;

                continue;
            }


            // =================================================
            // 6. HOMING_FINISH
            //
            // Servo Homing 명령과 제한 시간을 설정합니다.
            // Servo Homing 명령과 제한 시간을 설정합니다.
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_HOMING_FINISH)
            {
                command.controlWord = 0x000F;

                command.operationMode = 1;

                command.targetPosition =
                    runtimeInfo.latestInput.actualPosition;

                command.targetVelocity = 0;

                runtimeInfo.commandStep =
                    DAO_SERVO_STEP_HOMING_RESTORE_MODE;

                continue;
            }


            // =================================================
            // 7. HOMING_RESTORE_MODE
            //
            // 요청된 정보를 출력 구조체에 복사합니다.
            // 요청된 정보를 출력 구조체에 복사합니다.
            //
            // 요청된 정보를 출력 구조체에 복사합니다.
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_HOMING_RESTORE_MODE)
            {
                command.controlWord = 0x000F;

                command.operationMode = 1;

                command.targetPosition =
                    runtimeInfo.latestInput.actualPosition;

                command.targetVelocity = 0;


                // Servo의 운전 모드를 SDO로 설정합니다.
                // Servo의 운전 모드를 SDO로 설정합니다.
                if (runtimeInfo.latestInput.operationModeDisplay != 1)
                {
                    continue;
                }


                // ====================================================
                // Servo Homing 명령과 제한 시간을 설정합니다.
                //
                // Servo Homing 명령과 제한 시간을 설정합니다.
                // Servo Homing 명령과 제한 시간을 설정합니다.
                // ====================================================

                runtimeInfo.homed = true;

                runtimeInfo.commandState =
                    DAO_SERVO_COMMAND_STATE_COMPLETED;

                runtimeInfo.commandResult = 1;

                continue;
            }


            // Servo Homing 명령과 제한 시간을 설정합니다.
            // Servo Homing 명령과 제한 시간을 설정합니다.
            continue;
        }



        // ====================================================
        // MOVE ABSOLUTE COMMAND
        // ====================================================
		if (isMoveAbsoluteCommand) // Profile Position Mode(1) + Servo ON + Move Start
        {
			if (runtimeInfo.fault) // Servo의 Fault 상태를 확인하고 명령 결과에 반영합니다.
            {
                runtimeInfo.commandState =
                    DAO_SERVO_COMMAND_STATE_ERROR;

                runtimeInfo.commandResult = -1;

                continue;
            }

			if (runtimeInfo.stoActive) // Servo에 전송할 출력 PDO를 IO Map에 복사합니다.
            {
                runtimeInfo.outputCommand.targetVelocity = 0;

                runtimeInfo.commandState =
                    DAO_SERVO_COMMAND_STATE_ERROR;

                runtimeInfo.commandResult = -1;

                continue;
            }



            DaoInternalLsServoOutputPdo& command =
                runtimeInfo.outputCommand;

            // =================================================
            // 1. MOVE_ABS_PREPARE
            //
            // 요청된 정보를 출력 구조체에 복사합니다.
            // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_MOVE_ABS_PREPARE)
            {
                runtimeInfo.commandState =
                    DAO_SERVO_COMMAND_STATE_RUNNING;

                command.controlWord = 0x000F;

                command.operationMode = 1;

                // Servo의 운전 모드를 SDO로 설정합니다.
                // Servo의 운전 모드를 SDO로 설정합니다.
                command.targetPosition =
                    runtimeInfo.latestInput.actualPosition;

                command.targetVelocity = 0;

                runtimeInfo.commandStep =
                    DAO_SERVO_STEP_MOVE_ABS_MODE_REQUEST;

                continue;
            }

            // =================================================
            // 2. MOVE_ABS_MODE_REQUEST
            //
            // 요청된 정보를 출력 구조체에 복사합니다.
            // 요청된 정보를 출력 구조체에 복사합니다.
            //
            // Servo의 운전 모드를 SDO로 설정합니다.
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_MOVE_ABS_MODE_REQUEST)
            {
                command.operationMode = 1;

                command.controlWord = 0x000F;

                // Servo 입력 PDO를 읽어 최신 런타임 상태를 갱신합니다.
                // Servo 입력 PDO를 읽어 최신 런타임 상태를 갱신합니다.
                command.targetPosition =
                    runtimeInfo.latestInput.actualPosition;

                command.targetVelocity = 0;

                // Servo의 운전 모드를 SDO로 설정합니다.
                if (runtimeInfo.latestInput.operationModeDisplay != 1)
                {
                    continue;
                }

                // 요청된 정보를 출력 구조체에 복사합니다.
                // 요청된 정보를 출력 구조체에 복사합니다.
                runtimeInfo.commandStep =
                    DAO_SERVO_STEP_MOVE_ABS_SERVO_ON;

                continue;
            }

            // =================================================
            // 3. MOVE_ABS_SERVO_ON
            //
            // 요청된 정보를 출력 구조체에 복사합니다.
            // 요청된 정보를 출력 구조체에 복사합니다.
            //
            // Servo의 운전 모드를 SDO로 설정합니다.
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_MOVE_ABS_SERVO_ON)
            {
                command.operationMode = 1;

                // Servo의 운전 모드를 SDO로 설정합니다.
                // Servo의 운전 모드를 SDO로 설정합니다.
                command.targetPosition =
                    runtimeInfo.latestInput.actualPosition;

                command.targetVelocity = 0;

                // Statusword에서 CiA 402 상태를 추출합니다.
                // Statusword에서 CiA 402 상태를 추출합니다.
                if (runtimeInfo.cia402State == 0x0027)
                {
                    command.controlWord = 0x000F;

                    runtimeInfo.commandStep =
                        DAO_SERVO_STEP_MOVE_ABS_START;

                    continue;
                }

                // Switch On Disabled
                // -> Ready To Switch On
                if (runtimeInfo.cia402State == 0x0040)
                {
                    command.controlWord = 0x0006;
                    continue;
                }

                // Ready To Switch On
                // -> Switched On
                if (runtimeInfo.cia402State == 0x0021)
                {
                    command.controlWord = 0x0007;
                    continue;
                }

                // Switched On
                // -> Operation Enabled
                if (runtimeInfo.cia402State == 0x0023)
                {
                    command.controlWord = 0x000F;
                    continue;
                }

                // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
                // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
                continue;
            }

            // =================================================
            // 4. MOVE_ABS_START
            //
            // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
            // 요청된 정보를 출력 구조체에 복사합니다.
            //
            // 요청된 정보를 출력 구조체에 복사합니다.
            // Servo의 운전 모드를 SDO로 설정합니다.
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_MOVE_ABS_START)
            {
                command.operationMode = 1;

                command.targetPosition =
                    runtimeInfo.moveTargetPosition;

                command.profileVelocity =
                    runtimeInfo.moveProfileVelocity;

                command.profileAcceleration =
                    runtimeInfo.moveProfileAcceleration;

                command.profileDeceleration =
                    runtimeInfo.moveProfileDeceleration;

                command.targetVelocity = 0;

                command.touchProbeFunction = 0;

                // Profile Position New Set-point
                // Servo 입력 PDO를 읽어 최신 런타임 상태를 갱신합니다.
                command.controlWord = 0x001F;

                // Servo 입력 PDO를 읽어 최신 런타임 상태를 갱신합니다.
                runtimeInfo.moveStartPosition =
                    runtimeInfo.latestInput.actualPosition;

                runtimeInfo.moveStartFrameCount =
                    runtimeInfo.totalFrameCount;

                runtimeInfo.commandStep =
                    DAO_SERVO_STEP_MOVE_ABS_RUNNING;

                continue;
            }

            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_MOVE_ABS_RUNNING)
            {
                command.operationMode = 1;

                command.targetPosition =
                    runtimeInfo.moveTargetPosition;

                command.profileVelocity =
                    runtimeInfo.moveProfileVelocity;

                command.profileAcceleration =
                    runtimeInfo.moveProfileAcceleration;

                command.profileDeceleration =
                    runtimeInfo.moveProfileDeceleration;

                command.targetVelocity = 0;

                // 목표 속도와 가감속 값을 설정해 속도 운전을 요청합니다.
                command.controlWord = 0x000F;

                // ------------------------------------------------
                // 요청된 정보를 출력 구조체에 복사합니다.
                // 요청된 정보를 출력 구조체에 복사합니다.
                // ------------------------------------------------
                if (!runtimeInfo.targetReached)
                {
                    runtimeInfo.moveTargetReachedWentLow =
                        true;
                }

                // ------------------------------------------------
                // 요청된 정보를 출력 구조체에 복사합니다.
                // 요청된 정보를 출력 구조체에 복사합니다.
                // ------------------------------------------------
                if (runtimeInfo.moveTargetReachedWentLow &&
                    runtimeInfo.targetReached)
                {
                    runtimeInfo.commandStep =
                        DAO_SERVO_STEP_MOVE_ABS_FINISH;

                    continue;
                }

                // ------------------------------------------------
                // 요청된 정보를 출력 구조체에 복사합니다.
                // ------------------------------------------------
                const std::uint64_t elapsedFrames =
                    runtimeInfo.totalFrameCount -
                    runtimeInfo.moveStartFrameCount;

                const std::uint64_t timeoutFrames =
                    (static_cast<std::uint64_t>(
                        runtimeInfo.moveTimeoutMs) + 1ULL)
                    / 2ULL;

                if (elapsedFrames >= timeoutFrames)
                {
                    // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.
                    command.controlWord = 0x000F;
                    command.operationMode = 1;
                    command.targetVelocity = 0;

                    runtimeInfo.commandState =
                        DAO_SERVO_COMMAND_STATE_TIMEOUT;

                    runtimeInfo.commandResult = -4;

                    continue;
                }

                // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
                continue;
            }


            // =================================================
            // 6. MOVE_ABS_FINISH
            //
            // 요청된 정보를 출력 구조체에 복사합니다.
            // Servo의 운전 모드를 SDO로 설정합니다.
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_MOVE_ABS_FINISH)
            {
                command.operationMode = 1;

                // Servo의 운전 모드를 SDO로 설정합니다.
                command.controlWord = 0x000F;

                command.targetPosition =
                    runtimeInfo.moveTargetPosition;

                command.profileVelocity =
                    runtimeInfo.moveProfileVelocity;

                command.profileAcceleration =
                    runtimeInfo.moveProfileAcceleration;

                command.profileDeceleration =
                    runtimeInfo.moveProfileDeceleration;

                command.targetVelocity = 0;

                runtimeInfo.commandState =
                    DAO_SERVO_COMMAND_STATE_COMPLETED;

                runtimeInfo.commandResult = 1;

                continue;
            }
            // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
            continue;
        }


        // ========================================================
        // Profile Velocity Command
        // ========================================================
        if (isVelocityCommand)
        {
            auto& command =
                runtimeInfo.outputCommand;

            // ----------------------------------------------------
            // 300. VELOCITY_PREPARE
            //
            // 요청된 정보를 출력 구조체에 복사합니다.
            // 요청된 정보를 출력 구조체에 복사합니다.
            // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
            // ----------------------------------------------------
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_VELOCITY_PREPARE)
            {
                runtimeInfo.commandState =
                    DAO_SERVO_COMMAND_STATE_RUNNING;

                // Servo의 운전 모드를 SDO로 설정합니다.
                command.targetPosition =
                    runtimeInfo.latestInput.actualPosition;

                // Servo의 운전 모드를 SDO로 설정합니다.
                command.operationMode = 1;

                // Servo의 운전 모드를 SDO로 설정합니다.
                command.controlWord = 0x000F;

                // 목표 속도와 가감속 값을 설정해 속도 운전을 요청합니다.
                command.targetVelocity = 0;

                command.profileAcceleration =
                    runtimeInfo.velocityAcceleration;

                command.profileDeceleration =
                    runtimeInfo.velocityDeceleration;

                runtimeInfo.commandStep =
                    DAO_SERVO_STEP_VELOCITY_MODE_REQUEST;

                continue;
            }

            // ----------------------------------------------------
            // 310. VELOCITY_MODE_REQUEST
            //
            // 요청된 정보를 출력 구조체에 복사합니다.
            // 요청된 정보를 출력 구조체에 복사합니다.
            // ----------------------------------------------------
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_VELOCITY_MODE_REQUEST)
            {
                // Servo의 운전 모드를 SDO로 설정합니다.
                command.operationMode = 3;

                // Servo의 운전 모드를 SDO로 설정합니다.
                command.targetVelocity = 0;

                command.profileAcceleration =
                    runtimeInfo.velocityAcceleration;

                command.profileDeceleration =
                    runtimeInfo.velocityDeceleration;

                // Servo의 운전 모드를 SDO로 설정합니다.
                command.controlWord = 0x000F;

                // Servo의 운전 모드를 SDO로 설정합니다.
                if (runtimeInfo.latestInput.operationModeDisplay != 3)
                {
                    continue;
                }

                runtimeInfo.commandStep =
                    DAO_SERVO_STEP_VELOCITY_SERVO_ON;

                continue;
            }

            // ----------------------------------------------------
            // 320. VELOCITY_SERVO_ON
            //
            // 요청된 정보를 출력 구조체에 복사합니다.
            // 요청된 정보를 출력 구조체에 복사합니다.
            // Servo의 운전 모드를 SDO로 설정합니다.
            // ----------------------------------------------------
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_VELOCITY_SERVO_ON)
            {
                command.operationMode = 3;

                // Servo의 운전 모드를 SDO로 설정합니다.
                command.targetVelocity = 0;

                command.profileAcceleration =
                    runtimeInfo.velocityAcceleration;

                command.profileDeceleration =
                    runtimeInfo.velocityDeceleration;

                const int ciaState =
                    runtimeInfo.cia402State;

                // Statusword에서 CiA 402 상태를 추출합니다.
                if (ciaState == 0x27)
                {
                    command.controlWord = 0x000F;

                    runtimeInfo.commandStep =
                        DAO_SERVO_STEP_VELOCITY_RUNNING;

                    continue;
                }

                // Switch On Disabled
                if (ciaState == 0x40)
                {
                    command.controlWord = 0x0006;
                    continue;
                }

                // Ready To Switch On
                if (ciaState == 0x21)
                {
                    command.controlWord = 0x0007;
                    continue;
                }

                // Switched On
                if (ciaState == 0x23)
                {
                    command.controlWord = 0x000F;
                    continue;
                }

                // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
                continue;
            }

            // ----------------------------------------------------
            // 330. VELOCITY_RUNNING
            //
            // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
            // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
            //
            // 요청된 정보를 출력 구조체에 복사합니다.
            // 요청된 정보를 출력 구조체에 복사합니다.
            // Servo의 운전 모드를 SDO로 설정합니다.
            // ----------------------------------------------------
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_VELOCITY_RUNNING)
            {
                command.operationMode = 3;

                command.profileAcceleration =
                    runtimeInfo.velocityAcceleration;

                command.profileDeceleration =
                    runtimeInfo.velocityDeceleration;

                // Servo의 Fault 상태를 확인하고 명령 결과에 반영합니다.
                command.controlWord = 0x000F;

                // Servo의 Fault 상태를 확인하고 명령 결과에 반영합니다.
                if (runtimeInfo.fault ||
                    runtimeInfo.stoActive)
                {
                    command.targetVelocity = 0;

                    runtimeInfo.commandState =
                        DAO_SERVO_COMMAND_STATE_ERROR;

                    runtimeInfo.commandResult = -1;

                    continue;
                }

                // 목표 속도와 가감속 값을 설정해 속도 운전을 요청합니다.
                if (runtimeInfo.stopInput)
                {
                    command.targetVelocity = 0;
                    continue;
                }

                // 목표 속도와 가감속 값을 설정해 속도 운전을 요청합니다.
                if (runtimeInfo.velocityTarget > 0 &&
                    runtimeInfo.positiveLimit)
                {
                    command.targetVelocity = 0;
                    continue;
                }

                // 목표 속도와 가감속 값을 설정해 속도 운전을 요청합니다.
                if (runtimeInfo.velocityTarget < 0 &&
                    runtimeInfo.negativeLimit)
                {
                    command.targetVelocity = 0;
                    continue;
                }

                // 목표 속도와 가감속 값을 설정해 속도 운전을 요청합니다.
                command.targetVelocity =
                    runtimeInfo.velocityTarget;

                continue;
            }

            continue;
        }


        // ========================================================
        // Common Servo Stop Command
        // ========================================================
        if (isStopCommand)
        {
            auto& command =
                runtimeInfo.outputCommand;

            // ----------------------------------------------------
            // 400. STOP_PREPARE
            //
            // 요청된 정보를 출력 구조체에 복사합니다.
            // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
            // ----------------------------------------------------
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_STOP_PREPARE)
            {
                runtimeInfo.commandState =
                    DAO_SERVO_COMMAND_STATE_RUNNING;

                // Servo의 운전 모드를 SDO로 설정합니다.
                command.controlWord = 0x000F;

                // Servo의 운전 모드를 SDO로 설정합니다.
                if (runtimeInfo.latestInput.operationModeDisplay == 3)
                {
                    command.operationMode = 3;
                    command.targetVelocity = 0;
                }
                else
                {
                    // Servo의 운전 모드를 SDO로 설정합니다.
                    command.operationMode =
                        runtimeInfo.latestInput.operationModeDisplay;
                }

                runtimeInfo.commandStep =
                    DAO_SERVO_STEP_STOPPING;

                continue;
            }

            // ----------------------------------------------------
            // 410. STOPPING
            //
            // 요청된 정보를 출력 구조체에 복사합니다.
            // 요청된 정보를 출력 구조체에 복사합니다.
            // ----------------------------------------------------
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_STOPPING)
            {
                const int currentMode =
                    runtimeInfo.latestInput.operationModeDisplay;

                // ------------------------------------------------
                // PV Mode
                // Servo의 운전 모드를 SDO로 설정합니다.
                // Servo의 운전 모드를 SDO로 설정합니다.
                // ------------------------------------------------
                if (currentMode == 3)
                {
                    command.operationMode = 3;
                    command.controlWord = 0x000F;
                    command.targetVelocity = 0;

                    runtimeInfo.commandStep =
                        DAO_SERVO_STEP_STOP_FINISH;

                    continue;
                }

                // ------------------------------------------------
                // PP Mode
                // 아래 조건을 확인한 후 현재 처리 단계를 계속합니다.
                //
                // 0x000F : Operation Enabled
                // 0x010F : Operation Enabled + Halt
                // ------------------------------------------------
                if (currentMode == 1)
                {
                    command.operationMode = 1;
                    command.controlWord = 0x010F;
                    command.targetVelocity = 0;

                    runtimeInfo.commandStep =
                        DAO_SERVO_STEP_STOP_FINISH;

                    continue;
                }

                // 목표 속도와 가감속 값을 설정해 속도 운전을 요청합니다.
                // 목표 속도와 가감속 값을 설정해 속도 운전을 요청합니다.
                command.controlWord = 0x000F;
                command.targetVelocity = 0;

                continue;
            }

            // ----------------------------------------------------
            // 420. STOP_FINISH
            //
            // 요청된 정보를 출력 구조체에 복사합니다.
            // 목표 속도와 가감속 값을 설정해 속도 운전을 요청합니다.
            // ----------------------------------------------------
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_STOP_FINISH)
            {
                command.targetVelocity = 0;

                runtimeInfo.commandState =
                    DAO_SERVO_COMMAND_STATE_STOPPED;

                runtimeInfo.commandResult = 1;

                continue;
            }

            continue;
        }
        // ====================================================
        // Servo ON / Servo OFF COMMAND
        // ====================================================

        // Servo의 Fault 상태를 확인하고 명령 결과에 반영합니다.
        //
        // Servo의 Fault 상태를 확인하고 명령 결과에 반영합니다.
        // Servo의 Fault 상태를 확인하고 명령 결과에 반영합니다.
        if (isServoOnCommand &&
            runtimeInfo.fault)
        {
            runtimeInfo.commandState =
                DAO_SERVO_COMMAND_STATE_ERROR;

            runtimeInfo.commandResult = -1;

            continue;
        }


        const std::uint64_t elapsedFrames =
            runtimeInfo.totalFrameCount -
            runtimeInfo.commandStartFrameCount;


        // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.
        if (elapsedFrames >=
            SERVO_ON_TIMEOUT_FRAMES)
        {
            runtimeInfo.commandState =
                DAO_SERVO_COMMAND_STATE_TIMEOUT;

            runtimeInfo.commandResult = -2;

            continue;
        }


        const unsigned short cia402State =
            runtimeInfo.cia402State;

        DaoInternalLsServoOutputPdo& command =
            runtimeInfo.outputCommand;


        // ====================================================
        // SERVO OFF
        //
        // Servo OFF 명령을 등록하고 구동 비활성화를 요청합니다.
        // Ready To Switch On = 0x0021
        // ====================================================
        if (isServoOffCommand)
        {
            command.targetPosition =
                runtimeInfo.latestInput.actualPosition;

            command.targetVelocity = 0;

            command.touchProbeFunction = 0;

            command.digitalOutputs = 0;


            // Statusword에서 CiA 402 상태를 추출합니다.
            if (cia402State == 0x0021)
            {
                command.controlWord = 0x0006;

                runtimeInfo.commandState =
                    DAO_SERVO_COMMAND_STATE_COMPLETED;

                runtimeInfo.commandResult = 1;

                continue;
            }


            // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
            // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
            command.controlWord = 0x0006;

            runtimeInfo.commandState =
                DAO_SERVO_COMMAND_STATE_RUNNING;

            continue;
        }


        // ====================================================
        // SERVO ON
        // ====================================================

        // Servo 입력 PDO를 읽어 최신 런타임 상태를 갱신합니다.
        command.targetPosition =
            runtimeInfo.latestInput.actualPosition;

        command.targetVelocity = 0;

        command.touchProbeFunction = 0;

        command.digitalOutputs = 0;


        // Operation Enabled
        if (cia402State == 0x0027)
        {
            command.controlWord = 0x000F;

            runtimeInfo.commandState =
                DAO_SERVO_COMMAND_STATE_COMPLETED;

            runtimeInfo.commandResult = 1;

            continue;
        }


        runtimeInfo.commandState =
            DAO_SERVO_COMMAND_STATE_RUNNING;


        // Switch On Disabled
        // -> Shutdown
        if (cia402State == 0x0040)
        {
            command.controlWord = 0x0006;
            continue;
        }


        // Ready To Switch On
        // -> Switch On
        if (cia402State == 0x0021)
        {
            command.controlWord = 0x0007;
            continue;
        }


        // Switched On
        // -> Enable Operation
        if (cia402State == 0x0023)
        {
            command.controlWord = 0x000F;
            continue;
        }


        // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
        // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    }
}



void DaoEtherCATMaster::UpdateServoDerivedState(
    DaoInternalServoRuntimeInfo& runtimeInfo)
{
    const unsigned short statusWord =
        runtimeInfo.latestInput.statusWord;

    // --------------------------------------------------------
    // Servo 입력 PDO를 읽어 최신 런타임 상태를 갱신합니다.
    //
    // Statusword에서 CiA 402 상태를 추출합니다.
    // Servo의 Fault 상태를 확인하고 명령 결과에 반영합니다.
    // Statusword에서 CiA 402 상태를 추출합니다.
    // Statusword에서 CiA 402 상태를 추출합니다.
    // --------------------------------------------------------

    // Fault
    if ((statusWord & 0x004F) == 0x0008)
    {
        runtimeInfo.cia402State = 0x0008;
    }
    // Switch On Disabled
    else if ((statusWord & 0x004F) == 0x0040)
    {
        runtimeInfo.cia402State = 0x0040;
    }
    // Ready To Switch On
    else if ((statusWord & 0x006F) == 0x0021)
    {
        runtimeInfo.cia402State = 0x0021;
    }
    // Switched On
    else if ((statusWord & 0x006F) == 0x0023)
    {
        runtimeInfo.cia402State = 0x0023;
    }
    // Operation Enabled
    else if ((statusWord & 0x006F) == 0x0027)
    {
        runtimeInfo.cia402State = 0x0027;
    }
    else
    {
        // Statusword에서 CiA 402 상태를 추출합니다.
        // Statusword에서 CiA 402 상태를 추출합니다.
        runtimeInfo.cia402State =
            static_cast<unsigned short>(
                statusWord & 0x006F);
    }

    runtimeInfo.fault =
        (statusWord & 0x0008) != 0;

    runtimeInfo.operationEnabled =
        runtimeInfo.cia402State == 0x0027;

    runtimeInfo.targetReached =
        (statusWord & 0x0400) != 0;

    // --------------------------------------------------------
    // Servo 입력 PDO를 읽어 최신 런타임 상태를 갱신합니다.
    // --------------------------------------------------------
    const unsigned int digitalInputs =
        runtimeInfo.latestInput.digitalInputs;

    runtimeInfo.negativeLimit =
		(digitalInputs & (1u << 0)) != 0;  // LS L7NH 0x60FD Bit 0 : Negative Limit Input

    runtimeInfo.positiveLimit =
		(digitalInputs & (1u << 1)) != 0;  //   LS L7NH 0x60FD Bit 1 : Positive Limit Input

    runtimeInfo.homeSensor =
		(digitalInputs & (1u << 2)) != 0;  //   LS L7NH 0x60FD Bit 2 : Home Sensor Input
   
    runtimeInfo.stopInput = 
		(digitalInputs & (1u << 19)) != 0; // LS L7NH 0x60FD Bit 19 : STOP Input

    runtimeInfo.stoActive =
		(digitalInputs & (1u << 31)) != 0;  // LS L7NH 0x60FD Bit 31 : STO Active
}

double DaoEtherCATMaster::ApplyAdcNotchFilter(
    double inputValue,
    double sampleRateHz,
    double notchFrequencyHz,
    double& x1,
    double& x2,
    double& y1,
	double& y2)  // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
{
    // --------------------------------------------------------
    // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
    // --------------------------------------------------------
    if (sampleRateHz <= 0.0 ||
        notchFrequencyHz <= 0.0 ||
        notchFrequencyHz >= (sampleRateHz * 0.5))
    {
        return inputValue;
    }

    // --------------------------------------------------------
    // 이 값은 처리된 프레임 또는 샘플의 누적 개수를 나타냅니다.
    //
    // 이 값은 처리된 프레임 또는 샘플의 누적 개수를 나타냅니다.
    // 아래 조건을 확인한 후 현재 처리 단계를 계속합니다.
    // 아래 조건을 확인한 후 현재 처리 단계를 계속합니다.
    //
    // 아래 조건을 확인한 후 현재 처리 단계를 계속합니다.
    // --------------------------------------------------------
    if (x1 == 0.0 &&
        x2 == 0.0 &&
        y1 == 0.0 &&
        y2 == 0.0)
    {
        x1 = inputValue;
        x2 = inputValue;
        y1 = inputValue;
        y2 = inputValue;

        return inputValue;
    }

    // --------------------------------------------------------
    // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
    //
    // Zero : notchFrequencyHz
    // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
    //
    // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    // --------------------------------------------------------
    constexpr double PI =
        3.14159265358979323846;

    constexpr double POLE_RADIUS =
        0.995;

    const double omega =
        2.0 * PI *
        notchFrequencyHz /
        sampleRateHz;

    const double cosOmega =
        std::cos(omega);

    // --------------------------------------------------------
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    // --------------------------------------------------------
    const double numeratorDc =
        2.0 - (2.0 * cosOmega);

    const double denominatorDc =
        1.0 -
        (2.0 * POLE_RADIUS * cosOmega) +
        (POLE_RADIUS * POLE_RADIUS);

    const double gain =
        denominatorDc /
        numeratorDc;

    // --------------------------------------------------------
    // Difference Equation
    // --------------------------------------------------------
    const double outputValue =
        gain *
        (
            inputValue -
            (2.0 * cosOmega * x1) +
            x2
            )
        +
        (2.0 * POLE_RADIUS * cosOmega * y1)
        -
        (POLE_RADIUS * POLE_RADIUS * y2);

    // --------------------------------------------------------
    // IO 입력 PDO를 읽어 최신 입력 상태를 갱신합니다.
    // --------------------------------------------------------
    x2 = x1;
    x1 = inputValue;

    y2 = y1;
    y1 = outputValue;

    return outputValue;
}


void DaoEtherCATMaster::ProcessAdcSample(
    DaoInternalAdcRuntimeInfo& runtimeInfo,
    std::int32_t rawSample)
{
    runtimeInfo.processing.latestRaw =
        rawSample;

    // --------------------------------------------------------
    // ADC 원시 샘플에 저역 통과 필터를 적용합니다.
    // --------------------------------------------------------
    if (!runtimeInfo.processing.lowLevelFilterInitialized)
    {
        runtimeInfo.processing.lowLevelFiltered =
            static_cast<double>(rawSample);

        runtimeInfo.processing.powerLineFiltered =
            static_cast<double>(rawSample);

        runtimeInfo.processing.lowLevelFilterInitialized =
            true;

        return;
    }

    // --------------------------------------------------------
    // ADC 원시 샘플에 저역 통과 필터를 적용합니다.
    // --------------------------------------------------------
    constexpr double LOW_LEVEL_FILTER_ALPHA = 0.1;

    runtimeInfo.processing.lowLevelFiltered =
        runtimeInfo.processing.lowLevelFiltered +
        LOW_LEVEL_FILTER_ALPHA *
        (
            static_cast<double>(rawSample) -
            runtimeInfo.processing.lowLevelFiltered
            );

    // --------------------------------------------------------
    // Power Line Notch Filter
    //
    // ADC 원시 샘플에 저역 통과 필터를 적용합니다.
    // ADC 원시 샘플에 저역 통과 필터를 적용합니다.
    // ADC 원시 샘플에 저역 통과 필터를 적용합니다.
    // --------------------------------------------------------
    runtimeInfo.processing.powerLineFiltered =
        runtimeInfo.processing.lowLevelFiltered;

    constexpr double ADC_EFFECTIVE_SAMPLE_RATE_HZ =
        2000.0;

    switch (runtimeInfo.processing.powerLineFilterMode)
    {
    case DaoInternalAdcPowerLineFilterMode::OFF:
        // ADC 원시 샘플에 저역 통과 필터를 적용합니다.
        runtimeInfo.processing.powerLineFiltered =
            runtimeInfo.processing.lowLevelFiltered;
        break;


    case DaoInternalAdcPowerLineFilterMode::HZ_50:
        // ADC 원시 샘플에 저역 통과 필터를 적용합니다.
        runtimeInfo.processing.powerLineFiltered =
            ApplyAdcNotchFilter(
                runtimeInfo.processing.lowLevelFiltered,
                ADC_EFFECTIVE_SAMPLE_RATE_HZ,
                50.0,
                runtimeInfo.processing.notch50X1,
                runtimeInfo.processing.notch50X2,
                runtimeInfo.processing.notch50Y1,
                runtimeInfo.processing.notch50Y2);
        break;


    case DaoInternalAdcPowerLineFilterMode::HZ_60:
        // ADC 원시 샘플에 저역 통과 필터를 적용합니다.
        runtimeInfo.processing.powerLineFiltered =
            ApplyAdcNotchFilter(
                runtimeInfo.processing.lowLevelFiltered,
                ADC_EFFECTIVE_SAMPLE_RATE_HZ,
                60.0,
                runtimeInfo.processing.notch60X1,
                runtimeInfo.processing.notch60X2,
                runtimeInfo.processing.notch60Y1,
                runtimeInfo.processing.notch60Y2);
        break;


    case DaoInternalAdcPowerLineFilterMode::HZ_120:
        // ADC 원시 샘플에 저역 통과 필터를 적용합니다.
        runtimeInfo.processing.powerLineFiltered =
            ApplyAdcNotchFilter(
                runtimeInfo.processing.lowLevelFiltered,
                ADC_EFFECTIVE_SAMPLE_RATE_HZ,
                120.0,
                runtimeInfo.processing.notch120X1,
                runtimeInfo.processing.notch120X2,
                runtimeInfo.processing.notch120Y1,
                runtimeInfo.processing.notch120Y2);
        break;


    case DaoInternalAdcPowerLineFilterMode::HZ_50_60:
    {
        // ADC 원시 샘플에 저역 통과 필터를 적용합니다.
        const double notch50Value =
            ApplyAdcNotchFilter(
                runtimeInfo.processing.lowLevelFiltered,
                ADC_EFFECTIVE_SAMPLE_RATE_HZ,
                50.0,
                runtimeInfo.processing.notch50X1,
                runtimeInfo.processing.notch50X2,
                runtimeInfo.processing.notch50Y1,
                runtimeInfo.processing.notch50Y2);

        runtimeInfo.processing.powerLineFiltered =
            ApplyAdcNotchFilter(
                notch50Value,
                ADC_EFFECTIVE_SAMPLE_RATE_HZ,
                60.0,
                runtimeInfo.processing.notch60X1,
                runtimeInfo.processing.notch60X2,
                runtimeInfo.processing.notch60Y1,
                runtimeInfo.processing.notch60Y2);
        break;
    }


    case DaoInternalAdcPowerLineFilterMode::HZ_60_120:
    {
        // ADC 원시 샘플에 저역 통과 필터를 적용합니다.
        const double notch60Value =
            ApplyAdcNotchFilter(
                runtimeInfo.processing.lowLevelFiltered,
                ADC_EFFECTIVE_SAMPLE_RATE_HZ,
                60.0,
                runtimeInfo.processing.notch60X1,
                runtimeInfo.processing.notch60X2,
                runtimeInfo.processing.notch60Y1,
                runtimeInfo.processing.notch60Y2);

        runtimeInfo.processing.powerLineFiltered =
            ApplyAdcNotchFilter(
                notch60Value,
                ADC_EFFECTIVE_SAMPLE_RATE_HZ,
                120.0,
                runtimeInfo.processing.notch120X1,
                runtimeInfo.processing.notch120X2,
                runtimeInfo.processing.notch120Y1,
                runtimeInfo.processing.notch120Y2);
        break;
    }


    default:
        runtimeInfo.processing.powerLineFiltered =
            runtimeInfo.processing.lowLevelFiltered;
        break;
    }

    // --------------------------------------------------------
    // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
    // --------------------------------------------------------
    if (runtimeInfo.processing.zeroInitialized)
    {
        runtimeInfo.processing.zeroedValue =
            runtimeInfo.processing.powerLineFiltered -
            runtimeInfo.processing.zeroOffset;
    }
    else
    {
        runtimeInfo.processing.zeroedValue =
            runtimeInfo.processing.powerLineFiltered;
    }

    // --------------------------------------------------------
    // 기준값과 안정된 ADC 샘플을 이용해 보정 계수를 계산합니다.
    // --------------------------------------------------------
    runtimeInfo.processing.calibratedValue =
        runtimeInfo.processing.zeroedValue *
        runtimeInfo.processing.calibrationScale;

    // --------------------------------------------------------
    // 3-Sample Median Filter
    //
    // 최근 샘플의 중앙값을 계산해 순간 잡음을 줄입니다.
    // 최근 샘플의 중앙값을 계산해 순간 잡음을 줄입니다.
    // --------------------------------------------------------
    {
        runtimeInfo.processing.medianBuffer[
            runtimeInfo.processing.medianIndex] =
            runtimeInfo.processing.calibratedValue;

            runtimeInfo.processing.medianIndex =
                (runtimeInfo.processing.medianIndex + 1) % 3;

            if (runtimeInfo.processing.medianCount < 3)
            {
                ++runtimeInfo.processing.medianCount;
            }

            if (runtimeInfo.processing.medianCount < 3)
            {
                // 최근 샘플의 중앙값을 계산해 순간 잡음을 줄입니다.
                // 최근 샘플의 중앙값을 계산해 순간 잡음을 줄입니다.
                runtimeInfo.processing.medianFilteredValue =
                    runtimeInfo.processing.calibratedValue;
            }
            else
            {
                const double a =
                    runtimeInfo.processing.medianBuffer[0];

                const double b =
                    runtimeInfo.processing.medianBuffer[1];

                const double c =
                    runtimeInfo.processing.medianBuffer[2];

                double medianValue = a;

                if ((a <= b && b <= c) ||
                    (c <= b && b <= a))
                {
                    medianValue = b;
                }
                else if ((b <= c && c <= a) ||
                    (a <= c && c <= b))
                {
                    medianValue = c;
                }

                runtimeInfo.processing.medianFilteredValue =
                    medianValue;
            }
    }

    // --------------------------------------------------------
    // 최근 샘플의 중앙값을 계산해 순간 잡음을 줄입니다.
    //
    // 최근 샘플의 중앙값을 계산해 순간 잡음을 줄입니다.
    // 최근 샘플의 중앙값을 계산해 순간 잡음을 줄입니다.
    // 최근 샘플의 중앙값을 계산해 순간 잡음을 줄입니다.
    // --------------------------------------------------------
    {
        unsigned int filterN =
            runtimeInfo.processing.filterN;

        if (filterN < 1)
        {
            filterN = 1;
        }

        if (filterN >
            DaoInternalAdcProcessingState::USER_FILTER_MAX_N)
        {
            filterN =
                DaoInternalAdcProcessingState::USER_FILTER_MAX_N;
        }

        // ----------------------------------------------------
        // 설정된 N개 샘플의 이동 평균을 계산합니다.
        // ----------------------------------------------------
        if (runtimeInfo.processing.userFilterCount < filterN)
        {
            runtimeInfo.processing.userFilterBuffer[
                runtimeInfo.processing.userFilterIndex] =
                runtimeInfo.processing.medianFilteredValue;

                runtimeInfo.processing.userFilterSum +=
                    runtimeInfo.processing.medianFilteredValue;

                ++runtimeInfo.processing.userFilterCount;

                runtimeInfo.processing.userFilterIndex =
                    (runtimeInfo.processing.userFilterIndex + 1) %
                    filterN;

                runtimeInfo.processing.filteredValue =
                    runtimeInfo.processing.userFilterSum /
                    static_cast<double>(
                        runtimeInfo.processing.userFilterCount);
        }
        else
        {
            // ------------------------------------------------
            // 요청된 정보를 출력 구조체에 복사합니다.
            // ------------------------------------------------
            runtimeInfo.processing.userFilterSum -=
                runtimeInfo.processing.userFilterBuffer[
                    runtimeInfo.processing.userFilterIndex];

            // ------------------------------------------------
            // 최근 샘플의 중앙값을 계산해 순간 잡음을 줄입니다.
            // ------------------------------------------------
            runtimeInfo.processing.userFilterBuffer[
                runtimeInfo.processing.userFilterIndex] =
                runtimeInfo.processing.medianFilteredValue;

                runtimeInfo.processing.userFilterSum +=
                    runtimeInfo.processing.medianFilteredValue;

                runtimeInfo.processing.userFilterIndex =
                    (runtimeInfo.processing.userFilterIndex + 1) %
                    filterN;

                runtimeInfo.processing.filteredValue =
                    runtimeInfo.processing.userFilterSum /
                    static_cast<double>(filterN);
        }
    }

    // --------------------------------------------------------
    // Stable Capture
    //
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    //
    // Zero:
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    //
    // Calibration:
    // 요청된 정보를 출력 구조체에 복사합니다.
    // 요청된 정보를 출력 구조체에 복사합니다.
    // --------------------------------------------------------
    if (runtimeInfo.processing.stableCaptureActive)
    {
        // ----------------------------------------------------
        // 요청된 정보를 출력 구조체에 복사합니다.
        // ----------------------------------------------------
        if (runtimeInfo.processing.stableCaptureWaitSamples > 0)
        {
            --runtimeInfo.processing.stableCaptureWaitSamples;
        }
        else if (
            runtimeInfo.processing.stableCaptureCollectedCount <
            runtimeInfo.processing.stableCaptureSampleCount)
        {
            // ------------------------------------------------
            // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
            // ------------------------------------------------
            runtimeInfo.processing.stableCaptureSum +=
                runtimeInfo.processing.powerLineFiltered;

            ++runtimeInfo.processing.stableCaptureCollectedCount;

            // ------------------------------------------------
            // 요청된 정보를 출력 구조체에 복사합니다.
            // ------------------------------------------------
            if (runtimeInfo.processing.stableCaptureCollectedCount ==
                runtimeInfo.processing.stableCaptureSampleCount)
            {
                const double stableAverage =
                    runtimeInfo.processing.stableCaptureSum /
                    static_cast<double>(
                        runtimeInfo.processing.stableCaptureCollectedCount);

                // ====================================================
                // 안정된 ADC 샘플을 모아 영점 오프셋을 계산합니다.
                // ====================================================
                if (runtimeInfo.processing.stableCaptureType ==
                    DaoInternalAdcStableCaptureType::ZERO)
                {
                    runtimeInfo.processing.zeroOffset =
                        stableAverage;

                    runtimeInfo.processing.zeroInitialized =
                        true;

                    runtimeInfo.processing.zeroedValue =
                        0.0;

                    runtimeInfo.processing.calibratedValue =
                        0.0;

                    runtimeInfo.processing.medianFilteredValue =
                        0.0;

                    // --------------------------------------------
                    // 최근 샘플의 중앙값을 계산해 순간 잡음을 줄입니다.
                    // 최근 샘플의 중앙값을 계산해 순간 잡음을 줄입니다.
                    // --------------------------------------------
                    runtimeInfo.processing.medianBuffer[0] = 0.0;
                    runtimeInfo.processing.medianBuffer[1] = 0.0;
                    runtimeInfo.processing.medianBuffer[2] = 0.0;

                    runtimeInfo.processing.medianIndex =
                        0;

                    runtimeInfo.processing.medianCount =
                        0;

                    // --------------------------------------------
                    // 요청된 정보를 출력 구조체에 복사합니다.
                    // --------------------------------------------
                    runtimeInfo.processing.userFilterBuffer.fill(0.0);

                    runtimeInfo.processing.userFilterIndex =
                        0;

                    runtimeInfo.processing.userFilterCount =
                        0;

                    runtimeInfo.processing.userFilterSum =
                        0.0;

                    runtimeInfo.processing.filteredValue =
                        0.0;
                }

                // ====================================================
                // 기준값과 안정된 ADC 샘플을 이용해 보정 계수를 계산합니다.
                // ====================================================
                else if (
                    runtimeInfo.processing.stableCaptureType ==
                    DaoInternalAdcStableCaptureType::CALIBRATION)
                {
                    const double calibrationSpan =
                        stableAverage -
                        runtimeInfo.processing.zeroOffset;

                    // --------------------------------------------
                    // 안정된 ADC 샘플을 모아 영점 오프셋을 계산합니다.
                    // 기준값과 안정된 ADC 샘플을 이용해 보정 계수를 계산합니다.
                    // --------------------------------------------
                    if (std::abs(calibrationSpan) >= 1.0)
                    {
                        runtimeInfo.processing.calibrationScale =
                            runtimeInfo.processing.stableCaptureReferenceValue /
                            calibrationSpan;

                        // 최근 샘플의 중앙값을 계산해 순간 잡음을 줄입니다.
                        runtimeInfo.processing.calibratedValue =
                            runtimeInfo.processing.zeroedValue *
                            runtimeInfo.processing.calibrationScale;

                        runtimeInfo.processing.medianFilteredValue =
                            runtimeInfo.processing.calibratedValue;

                        // ----------------------------------------
                        // 최근 샘플의 중앙값을 계산해 순간 잡음을 줄입니다.
                        // 최근 샘플의 중앙값을 계산해 순간 잡음을 줄입니다.
                        // ----------------------------------------
                        runtimeInfo.processing.medianBuffer[0] = 0.0;
                        runtimeInfo.processing.medianBuffer[1] = 0.0;
                        runtimeInfo.processing.medianBuffer[2] = 0.0;

                        runtimeInfo.processing.medianIndex =
                            0;

                        runtimeInfo.processing.medianCount =
                            0;

                        // ----------------------------------------
                        // 요청된 정보를 출력 구조체에 복사합니다.
                        // ----------------------------------------
                        runtimeInfo.processing.userFilterBuffer.fill(0.0);

                        runtimeInfo.processing.userFilterIndex =
                            0;

                        runtimeInfo.processing.userFilterCount =
                            0;

                        runtimeInfo.processing.userFilterSum =
                            0.0;

                        runtimeInfo.processing.filteredValue =
                            runtimeInfo.processing.calibratedValue;
                    }
                }

                // ------------------------------------------------
                // 요청된 정보를 출력 구조체에 복사합니다.
                // ------------------------------------------------
                runtimeInfo.processing.stableCaptureActive =
                    false;

                runtimeInfo.processing.stableCaptureType =
                    DaoInternalAdcStableCaptureType::NONE;
            }
        }
    }

    // --------------------------------------------------------
    // ADC Diagnostic Capture
    //
    // ADC 진단 캡처 상태와 수집된 샘플 수를 관리합니다.
    // ADC 진단 캡처 상태와 수집된 샘플 수를 관리합니다.
    //
    // ADC 진단 캡처 상태와 수집된 샘플 수를 관리합니다.
    // --------------------------------------------------------
    if (runtimeInfo.diagnosticCaptureActive)
    {
        if (runtimeInfo.diagnosticSamples.size() <
            runtimeInfo.diagnosticTargetSampleCount)
        {
            DaoInternalAdcDiagnosticSample sample{};

            sample.sampleIndex =
                runtimeInfo.diagnosticSampleIndex;

            sample.rawValue =
                rawSample;

            sample.lowLevelFiltered =
                runtimeInfo.processing.lowLevelFiltered;

            sample.powerLineFiltered =
                runtimeInfo.processing.powerLineFiltered;

            sample.zeroedValue =
                runtimeInfo.processing.zeroedValue;

            sample.calibratedValue =
                runtimeInfo.processing.calibratedValue;

            sample.medianFilteredValue =
                runtimeInfo.processing.medianFilteredValue;

            sample.filteredValue =
                runtimeInfo.processing.filteredValue;

            runtimeInfo.diagnosticSamples.push_back(
                sample);

            ++runtimeInfo.diagnosticSampleIndex;

            if (runtimeInfo.diagnosticSamples.size() ==
                runtimeInfo.diagnosticTargetSampleCount)
            {
                runtimeInfo.diagnosticCaptureActive =
                    false;
            }
        }
    }

    // --------------------------------------------------------
// ADC Runtime Ring Buffer Push
//
// 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
// 이 값은 처리된 프레임 또는 샘플의 누적 개수를 나타냅니다.
// --------------------------------------------------------
    {
        DaoInternalAdcBufferedSample bufferedSample{};

        bufferedSample.sampleIndex =
            runtimeInfo.ringBufferNextSampleIndex;

        bufferedSample.filteredValue =
            runtimeInfo.processing.filteredValue;

        ++runtimeInfo.ringBufferNextSampleIndex;

        // ----------------------------------------------------
        // 처리된 ADC 샘플을 링 버퍼에 추가하고 넘침 횟수를 관리합니다.
        // ----------------------------------------------------
        if (runtimeInfo.ringBufferCount <
            DaoInternalAdcRuntimeInfo::ADC_RING_BUFFER_SIZE)
        {
            runtimeInfo.ringBuffer[
                runtimeInfo.ringBufferHead] =
                bufferedSample;

                runtimeInfo.ringBufferHead =
                    (runtimeInfo.ringBufferHead + 1) %
                    DaoInternalAdcRuntimeInfo::ADC_RING_BUFFER_SIZE;

                ++runtimeInfo.ringBufferCount;
        }
        else
        {
            // ------------------------------------------------
            // 처리된 ADC 샘플을 링 버퍼에 추가하고 넘침 횟수를 관리합니다.
            //
            // 처리된 ADC 샘플을 링 버퍼에 추가하고 넘침 횟수를 관리합니다.
            // 처리된 ADC 샘플을 링 버퍼에 추가하고 넘침 횟수를 관리합니다.
            // ------------------------------------------------
            runtimeInfo.ringBuffer[
                runtimeInfo.ringBufferHead] =
                bufferedSample;

                runtimeInfo.ringBufferHead =
                    (runtimeInfo.ringBufferHead + 1) %
                    DaoInternalAdcRuntimeInfo::ADC_RING_BUFFER_SIZE;

                runtimeInfo.ringBufferTail =
                    (runtimeInfo.ringBufferTail + 1) %
                    DaoInternalAdcRuntimeInfo::ADC_RING_BUFFER_SIZE;

                ++runtimeInfo.ringBufferOverflowCount;

                // 처리된 ADC 샘플을 링 버퍼에 추가하고 넘침 횟수를 관리합니다.
        }
    }


}