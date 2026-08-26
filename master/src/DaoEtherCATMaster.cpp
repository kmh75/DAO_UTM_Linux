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
	StopCommunication(); // 통신 스레드 종료 요청
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

    // 이전 사용 흔적이 남지 않도록 컨텍스트를 초기화합니다.
    std::memset(
        &context_,
        0,
        sizeof(context_));

    context_.manualstatechange = 1;
	slaveCount_ = 0; // 스캔 전까지는 슬레이브 수를 0으로 초기화합니다.
    ResetProcessDataMap();
	ResetAdcRuntimeInfo(); // DAO ADC 최신 상태를 초기화합니다.

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
    // 1. 순환통신 스레드를 먼저 완전히 종료
    // --------------------------------------------------------
    StopCommunication();

    if (!isOpen_)
    {
        slaveCount_ = 0;
        ResetProcessDataMap();
        ResetAdcRuntimeInfo();
        ResetServoRuntimeInfo();
        ResetIoRuntimeInfo();
        return;
    }

    // --------------------------------------------------------
    // 2. 검색된 Slave가 있다면 안전하게 상태를 내립니다.
    //
    // OP → SAFE-OP → INIT
    //
    // 상태 전환 실패가 있더라도
    // 최종 ecx_close()는 반드시 수행합니다.
    // --------------------------------------------------------
    if (slaveCount_ > 0)
    {
        (void)RequestAllSlavesSafeOp();
        (void)RequestAllSlavesInit();
    }

    // --------------------------------------------------------
    // 3. EtherCAT 어댑터 종료
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
    // 이미 실행 중이면 중복으로 시작하지 않습니다.
    if (communicationRunning_.load())
    {
        return false;
    }

    // 이전 스레드 객체가 아직 join 가능한 상태라면
    // 먼저 완전히 정리합니다.
    if (communicationThread_.joinable())
    {
        communicationThread_.join();
    }

    // EtherCAT 어댑터와 PDO 매핑이 준비되지 않았다면
    // 통신 스레드를 시작하지 않습니다.
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
    // 스레드가 종료되도록 요청합니다.
    communicationStopRequested_.store(true);

    // 실제 통신 스레드가 생성돼 있다면
    // 완전히 끝날 때까지 기다립니다.
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
		ResetAdcRuntimeInfo();// DAO ADC 최신 상태를 초기화합니다.
        return 0;
    }

    ResetProcessDataMap();
	ResetAdcRuntimeInfo();// DAO ADC 최신 상태를 초기화합니다.

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

    // 새로 검색된 물리 Slave 개수에 맞춰
    // 런타임 저장공간을 다시 생성합니다.
    ResetAdcRuntimeInfo();
    ResetServoRuntimeInfo();
    ResetIoRuntimeInfo();

    return slaveCount_;
}

int DaoEtherCATMaster::GetSlaveCount() const
{
    return slaveCount_;
}
bool DaoEtherCATMaster::RequestAllSlavesPreOp()
{
    // 어댑터가 열려 있지 않으면 상태 전환 불가
    if (!isOpen_)
    {
        return false;
    }

    // 검색된 Slave가 없으면 상태 전환 불가
    if (slaveCount_ <= 0)
    {
        return false;
    }

    // Slave 0번은 SOEM에서 전체 Slave를 의미합니다.
    context_.slavelist[0].state =
        EC_STATE_PRE_OP;

    // PRE-OP 상태 요청을 한 번 전송합니다.
    const int writeResult =
        ecx_writestate(
            &context_,
            0);

    if (writeResult <= 0)
    {
        return false;
    }

    // 모든 Slave가 PRE-OP에 도달할 때까지 기다립니다.
    const uint16 reachedState =
        ecx_statecheck(
            &context_,
            0,
            EC_STATE_PRE_OP,
            EC_TIMEOUTSTATE);

    // 최신 상태를 다시 읽습니다.
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

    // 외부 목록은 0부터 시작하지만
    // SOEM의 Slave 번호는 1부터 시작합니다.
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
    // 1. 기본 실행 조건 확인
    // --------------------------------------------------------
    if (!isOpen_)
    {
        return false;
    }

    if (slaveCount_ <= 0)
    {
        return false;
    }

    // 상태 전환 중에는 순환통신 스레드가 없어야 합니다.
    if (communicationRunning_.load())
    {
        return false;
    }

    // --------------------------------------------------------
    // 2. 최신 EtherCAT 상태 확인
    // --------------------------------------------------------
    ecx_readstate(&context_);

    const std::uint16_t currentBaseState =
        static_cast<std::uint16_t>(
            context_.slavelist[0].state & 0x000F);

    // 모든 Slave가 이미 SAFE-OP이면 성공입니다.
    if (currentBaseState == EC_STATE_SAFE_OP)
    {
        return true;
    }

    // --------------------------------------------------------
    // 3. 전체 Slave에 SAFE-OP 상태 요청
    //
    // SOEM의 Slave 0번은 전체 Slave를 의미합니다.
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
    // 4. 모든 Slave가 SAFE-OP에 도달할 때까지 대기
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
    // 1. 기본 조건 확인
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
    // 2. 전체 Slave가 SAFE-OP인지 확인
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
    // 3. OP 요청 전 Process Data 10회 Priming
    // --------------------------------------------------------
    constexpr int PRIMING_ROUNDS = 10;

    for (int round = 0;
        round < PRIMING_ROUNDS;
        ++round)
    {
        // DAO ADC Output PDO는 항상 0으로 유지합니다.
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

        // Servo와 IO의 현재 출력 명령을 PDO에 반영합니다.
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
    // 4. 전체 Slave에 OP 상태 요청
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
    // 5. Process Data를 계속 교환하면서 OP 도달 대기
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

        // ADC 출력 0 유지
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
    // 1. 기본 실행 조건 확인
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
    // 2. 최신 상태 확인
    // --------------------------------------------------------
    ecx_readstate(&context_);

    const std::uint16_t currentBaseState =
        static_cast<std::uint16_t>(
            context_.slavelist[0].state & 0x000F);

    // 이미 INIT이면 성공입니다.
    if (currentBaseState == EC_STATE_INIT)
    {
        return true;
    }

    // --------------------------------------------------------
    // 3. 전체 Slave에 INIT 상태 요청
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
    // 4. 모든 Slave가 INIT에 도달할 때까지 대기
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
    // SOEM의 물리 Slave 번호는 1부터 시작합니다.
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
    // 1. LS L7NH 여부와 Slave 번호 확인
    // --------------------------------------------------------
    if (!IsLsL7nhServo(
        physicalSlaveIndex))
    {
        return false;
    }

    // PDO Mapping / Assignment는 PRE-OP에서 수행합니다.
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

    // RxPDO Assignment 비활성화
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


    // RxPDO 0x1601 Mapping 비활성화
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


    // RxPDO Mapping Entry 개수 확정
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


    // RxPDO 0x1601을 SM2에 할당
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

    // TxPDO Assignment 비활성화
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


    // TxPDO 0x1A01 Mapping 비활성화
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


    // TxPDO Mapping Entry 개수 확정
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


    // TxPDO 0x1A01을 SM3에 할당
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
    // LS L7NH Profile Position 설정
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

    // 지원하는 CiA402 운전모드만 허용합니다.
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
    // SOEM의 물리 Slave 번호는 1부터 시작합니다.
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

bool DaoEtherCATMaster::MapProcessData()
{
    // 어댑터가 열리지 않은 상태에서는 매핑할 수 없습니다.
    if (!isOpen_)
    {
        ResetProcessDataMap();
        return false;
    }

    // 검색된 Slave가 없으면 매핑할 수 없습니다.
    if (slaveCount_ <= 0)
    {
        ResetProcessDataMap();
        return false;
    }

    // 이전 매핑 결과를 모두 초기화합니다.
    ResetProcessDataMap();


    // --------------------------------------------------------
    // LS L7NH Servo PDO Assignment 설정
    //
    // Process Data를 매핑하기 전에
    // RxPDO는 0x1601 하나,
    // TxPDO는 0x1A01 하나만 사용하도록 고정합니다.
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

    // DAO ADC의 원래 CoE 관련 정보를 임시 저장합니다.
    std::vector<SavedCoeInfo> savedCoeInfoList;

    // Slave 최대 개수만큼 미리 확보합니다.
    savedCoeInfoList.reserve(
        static_cast<std::size_t>(slaveCount_));

    // --------------------------------------------------------
    // DAO ADC에만 CoE PDO 질의를 임시 차단합니다.
    //
    // LS Servo와 EtherCAT IO에는 이 처리를 적용하지 않습니다.
    // --------------------------------------------------------
    for (int physicalSlaveIndex = 1;
        physicalSlaveIndex <= slaveCount_;
        ++physicalSlaveIndex)
    {
        ec_slavet& slave =
            context_.slavelist[physicalSlaveIndex];

        // DAO ADC가 아니면 기존 SOEM 매핑 방식을 그대로 사용합니다.
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

        // CoE 프로토콜 비트만 임시로 제거합니다.
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
    // 전체 Slave의 Process Data를 IO Map에 배치합니다.
    // --------------------------------------------------------

    mappedBytes_ =
        ecx_config_map_group(
            &context_,
            ioMap_.data(),
            0);

    // --------------------------------------------------------
    // 매핑 성공 여부와 관계없이
    // DAO ADC의 Mailbox/CoE 정보를 반드시 원상복구합니다.
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

    // 매핑에 실패한 경우 모든 결과를 초기화합니다.
    if (mappedBytes_ <= 0)
    {
        ResetProcessDataMap();
        return false;
    }

    // Group 0의 WKC 정보를 저장합니다.
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

    // PDO 매핑 후에야 Expected WKC가 확정되므로,
    // 물리 Slave별 런타임 정보에도 새 값을 반영합니다.
    ResetAdcRuntimeInfo();
    ResetServoRuntimeInfo();
    ResetIoRuntimeInfo();
    ConfigureServoAndIoRuntimeInfo();
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
    // 1. 이전 값이 남지 않도록 결과 구조체 초기화
    // --------------------------------------------------------
    validationInfo = {};

    validationInfo.physicalSlaveIndex =
        physicalSlaveIndex;

    // --------------------------------------------------------
    // 2. Process Data 매핑 완료 여부 확인
    // --------------------------------------------------------
    validationInfo.processDataMapped =
        processDataMapped_;

    if (!processDataMapped_)
    {
        return false;
    }

    // --------------------------------------------------------
    // 3. 물리 Slave 번호 범위 확인
    //
    // SOEM의 실제 Slave 번호는 1부터 시작합니다.
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
    // 4. DAO ADC Identity 재확인
    // --------------------------------------------------------
    validationInfo.identityValid =
        IsDaoAdcSlave(slave);

    if (!validationInfo.identityValid)
    {
        return false;
    }

    // --------------------------------------------------------
    // 5. 실제 PDO 크기 저장
    // --------------------------------------------------------
    validationInfo.actualOutputBytes =
        static_cast<unsigned int>(
            slave.Obytes);

    validationInfo.actualInputBytes =
        static_cast<unsigned int>(
            slave.Ibytes);

    // DAO ADC의 검증된 PDO 크기
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
    // 6. PDO 포인터 유효성 확인
    //
    // 포인터 주소만 확인합니다.
    // 아직 해당 메모리를 읽거나 쓰지 않습니다.
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
    // 모든 안전 조건 통과
    // --------------------------------------------------------
    return true;
}

bool DaoEtherCATMaster::RequestDaoAdcSafeOp(
    int physicalSlaveIndex)
{
    // 순환통신 중에는 EtherCAT 상태 전환을 허용하지 않습니다.
    if (communicationRunning_.load())
    {
        return false;
    }

    // --------------------------------------------------------
    // 1. 먼저 DAO ADC PDO 안전 검증을 다시 수행합니다.
    // --------------------------------------------------------
    DaoInternalAdcValidationInfo validationInfo{};

    if (!ValidateDaoAdcPdo(
        physicalSlaveIndex,
        validationInfo))
    {
        return false;
    }

    // --------------------------------------------------------
    // 2. 물리 Slave 객체를 가져옵니다.
    //
    // 위 검증에서 Slave 번호 범위와 Identity,
    // PDO 크기 및 포인터까지 모두 확인됐습니다.
    // --------------------------------------------------------
    ec_slavet& slave =
        context_.slavelist[
            physicalSlaveIndex];

    // 최신 상태를 읽습니다.
    ecx_readstate(&context_);

    const std::uint16_t currentBaseState =
        static_cast<std::uint16_t>(
            slave.state & 0x000F);

    // 이미 SAFE-OP이면 성공으로 처리합니다.
    if (currentBaseState ==
        EC_STATE_SAFE_OP)
    {
        return true;
    }

    // PRE-OP 상태가 아니면 SAFE-OP 요청을 보내지 않습니다.
    if (currentBaseState !=
        EC_STATE_PRE_OP)
    {
        return false;
    }

    // --------------------------------------------------------
    // 3. SAFE-OP 상태 요청은 한 번만 전송합니다.
    //
    // LAN9252 처리 중 같은 상태 명령을 반복해서 쓰지 않습니다.
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
    // 4. 상태 요청을 반복하지 않고 SAFE-OP 도달만 기다립니다.
    //
    // 기존 검증 프로젝트의 SAFE-OP 대기시간과 동일하게
    // 최대 12초를 허용합니다.
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

    // 최신 상태와 AL Status Code를 다시 읽습니다.
    ecx_readstate(&context_);

    const std::uint16_t finalBaseState =
        static_cast<std::uint16_t>(
            slave.state & 0x000F);

    // SAFE-OP 도달과 오류 없음까지 모두 확인합니다.
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
    // 1. 이전 결과 초기화
    // --------------------------------------------------------
    exchangeInfo = {};

    exchangeInfo.physicalSlaveIndex =
        physicalSlaveIndex;

    exchangeInfo.expectedWkc =
        expectedWkc_;

    // 통신 스레드가 Process Data를 교환 중이면
    // 외부의 단발 송수신은 허용하지 않습니다.
    if (communicationRunning_.load())
    {
        return false;
    }

    // --------------------------------------------------------
    // 2. Process Data 매핑 여부 확인
    // --------------------------------------------------------
    if (!processDataMapped_)
    {
        return false;
    }

    // --------------------------------------------------------
    // 3. DAO ADC PDO 안전 검증 재실행
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
    // 4. 최신 EtherCAT 상태 확인
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
    // 5. 검증된 Output PDO 4바이트만 0으로 초기화
    //
    // ValidateDaoAdcPdo()에서 다음을 이미 확인했습니다.
    // - DAO ADC Identity
    // - Output Bytes = 4
    // - Output 포인터 유효
    //
    // 실제 Obytes 값만 사용하므로 그 이상은 건드리지 않습니다.
    // --------------------------------------------------------
    std::memset(
        slave.outputs,
        0,
        static_cast<std::size_t>(
            slave.Obytes));

    exchangeInfo.outputCleared = true;

    // --------------------------------------------------------
    // 6. Process Data 정확히 1회 송수신
    // --------------------------------------------------------
    ecx_send_processdata(&context_);

    exchangeInfo.actualWkc =
        ecx_receive_processdata(
            &context_,
            EC_TIMEOUTRET);

    // --------------------------------------------------------
    // 7. WKC 검사
    //
    // 현재 DAO ADC 단독 구성의 예상 WKC는 3입니다.
    // 범용 엔진에서는 저장된 expectedWkc_와 비교합니다.
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
    // 1. 결과 초기화
    // --------------------------------------------------------
    primingInfo = {};

    primingInfo.physicalSlaveIndex =
        physicalSlaveIndex;

    primingInfo.requestedRounds =
        roundCount;

    primingInfo.expectedWkc =
        expectedWkc_;

    // 통신 스레드와 Priming 송수신이 겹치지 않도록 차단합니다.
    if (communicationRunning_.load())
    {
        return false;
    }

    // --------------------------------------------------------
    // 2. Priming 횟수 범위 검사
    //
    // 잘못된 값이나 과도한 반복을 막습니다.
    // 현재 시험에서는 10회를 사용합니다.
    // --------------------------------------------------------
    constexpr int MIN_PRIMING_ROUNDS = 1;
    constexpr int MAX_PRIMING_ROUNDS = 100;

    if (roundCount < MIN_PRIMING_ROUNDS ||
        roundCount > MAX_PRIMING_ROUNDS)
    {
        return false;
    }

    // --------------------------------------------------------
    // 3. ADC 안전 검증
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
    // 4. SAFE-OP 상태 확인
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
    // 5. 지정된 횟수만큼 Process Data 왕복
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

        // 첫 번째 결과로 최소/최대값 초기화
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

            // 한 번이라도 실패하면 즉시 중단합니다.
            break;
        }
    }

    // --------------------------------------------------------
    // 6. 전체 Priming 성공 여부 판단
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
    // 1. 결과 구조체 초기화
    // --------------------------------------------------------
    operationalInfo = {};

    operationalInfo.physicalSlaveIndex =
        physicalSlaveIndex;

    operationalInfo.expectedWkc =
        expectedWkc_;

    // 이미 순환통신 중이라면 상태 전환 및 Priming을 수행하지 않습니다.
    if (communicationRunning_.load())
    {
        return false;
    }

    // --------------------------------------------------------
    // 2. DAO ADC PDO 안전 검증
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
    // 3. 현재 Slave 상태 확인
    // --------------------------------------------------------
    ecx_readstate(&context_);

    ec_slavet& slave =
        context_.slavelist[
            physicalSlaveIndex];

    std::uint16_t baseState =
        static_cast<std::uint16_t>(
            slave.state & 0x000F);

    // 이미 OP 상태라면 정상으로 처리합니다.
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

    // OP 전환은 SAFE-OP 상태에서만 허용합니다.
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
    // 4. OP 요청 전 10회 Priming 재확인
    //
    // 한 번이라도 WKC가 예상보다 작으면
    // OP 요청 자체를 보내지 않습니다.
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
    // 5. OP 상태 요청은 정확히 한 번만 전송
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
    // 6. OP 전환 대기
    //
    // 상태 요청은 다시 쓰지 않습니다.
    // 기다리는 동안 Process Data만 계속 교환합니다.
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
        // 검증된 DAO ADC Output PDO 크기만큼만 0으로 초기화
        //
        // ValidateDaoAdcPdo()에서 다음을 확인했습니다.
        // - DAO ADC Identity
        // - Output Bytes = 4
        // - Output 포인터 유효
        // ----------------------------------------------------
        std::memset(
            slave.outputs,
            0,
            static_cast<std::size_t>(
                slave.Obytes));

        // Process Data 1회 왕복
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

        // 현재 상태만 확인합니다.
        // 상태 명령을 다시 보내지는 않습니다.
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

        // OP 도달 확인
        if (baseState ==
            EC_STATE_OPERATIONAL)
        {
            operationalInfo.operationalReached =
                slave.ALstatuscode == 0;

            return
                operationalInfo.operationalReached;
        }

        // EtherCAT Error 상태가 들어오면 즉시 중단
        if ((slave.state &
            EC_STATE_ERROR) != 0)
        {
            return false;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(10));
    }

    // --------------------------------------------------------
    // 7. 시간 초과 후 최종 상태 저장
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
    // 1. 결과 구조체 초기화
    // --------------------------------------------------------
    readInfo = {};

    readInfo.physicalSlaveIndex =
        physicalSlaveIndex;

    readInfo.expectedWkc =
        expectedWkc_;

    // 순환통신 스레드가 실행 중일 때는
    // 별도의 Process Data 왕복을 허용하지 않습니다.
    // 최신 ADC 값은 Runtime 조회 API로 읽어야 합니다.
    if (communicationRunning_.load())
    {
        return false;
    }

    // --------------------------------------------------------
    // 2. DAO ADC PDO 안전 검증
    //
    // 다음 조건을 다시 확인합니다.
    // - PDO 매핑 완료
    // - Slave 번호 유효
    // - DAO ADC Identity 일치
    // - Output 4바이트
    // - Input 24바이트
    // - 입출력 포인터 유효
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
    // 3. 최신 EtherCAT 상태 확인
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
    // 4. 검증된 Output PDO 4바이트만 0으로 초기화
    //
    // slave.Obytes가 정확히 4인지 앞에서 확인했습니다.
    // --------------------------------------------------------
    std::memset(
        slave.outputs,
        0,
        static_cast<std::size_t>(
            slave.Obytes));

    readInfo.outputCleared = true;

    // --------------------------------------------------------
    // 5. Process Data 정확히 1회 왕복
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
    // 6. Input PDO 24바이트만 안전하게 복사
    //
    // ValidateDaoAdcPdo()에서 다음을 확인했습니다.
    // - slave.inputs != nullptr
    // - slave.Ibytes == 24
    //
    // 포인터를 구조체로 직접 캐스팅하지 않고
    // memcpy로 로컬 구조체에 정확히 24바이트만 복사합니다.
    // --------------------------------------------------------
    std::memcpy(
        &readInfo.data,
        slave.inputs,
        sizeof(DaoInternalAdcInputPdo));

    readInfo.inputCopied = true;

    // --------------------------------------------------------
    // 7. 물리 Slave별 ADC 런타임 정보 갱신
    //
    // 앞으로 2ms 통신 스레드와 외부 조회 API가
    // 동시에 접근할 수 있으므로 mutex로 보호합니다.
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

        // 현재는 반복 통신 스레드가 아니므로 false입니다.
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
    // 일반 Zero용 Stable Capture 시작
    //
    // 유효 ADC Sample을 정확히 600개 수집합니다.
    // 정상 약 2000 sample/sec 기준으로 약 0.3초입니다.
    //
    // 시간으로 종료하지 않고 실제 수집 Sample 개수로
    // Zero 평균값을 결정합니다.
    // ----------------------------------------------------
    constexpr unsigned int ZERO_CAPTURE_SAMPLES = 600;

    runtimeInfo.processing.stableCaptureActive =
        true;

    runtimeInfo.processing.stableCaptureType =
        DaoInternalAdcStableCaptureType::ZERO;

    runtimeInfo.processing.stableCaptureReferenceValue =
        0.0;

    // 일반 Zero는 별도 안정화 대기 없이
    // 바로 600 Sample 평균을 시작합니다.
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
    // Calibration Stable Capture 시작
    //
    // 약 2000 sample/sec 기준:
    //
    // 1) 최초 2000 Sample은 안정화 구간으로 버립니다.
    // 2) 이후 유효 Sample을 정확히 4000개 수집합니다.
    // 3) 4000개 평균값으로 Calibration Scale을 계산합니다.
    //
    // 실제 완료 판단은 시간이 아니라 Sample 개수입니다.
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

    // 지원하는 Mode만 허용합니다.
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
    // Notch Mode가 변경되면 이전 Filter History를 폐기합니다.
    //
    // 다른 Mode에서 사용하던 x/y 상태를 그대로 쓰면
    // 순간적인 과도응답이 생길 수 있습니다.
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

    // 현재 값으로 다시 시작합니다.
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

    // N값이 변경되면 기존 Moving Average 이력을 폐기합니다.
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

    // 기존 Diagnostic Capture 결과는 버리고
    // 새 Capture를 시작합니다.
    runtimeInfo.diagnosticSamples.clear();

    // 측정 중 vector 재할당이 발생하지 않도록
    // 필요한 Sample 수만큼 미리 메모리를 확보합니다.
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
    // 가장 오래된 Sample부터 순서대로 읽습니다.
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

    // LS Servo Identity와 PDO 검증을 통과한
    // Runtime만 외부에 반환합니다.
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

    // FASTECH IO Identity와 PDO 검증을 통과한
    // Runtime만 외부에 반환합니다.
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

    // Fault 상태에서는 Servo ON 명령을 접수하지 않습니다.
    if (runtimeInfo.fault)
    {
        return false;
    }

    if (runtimeInfo.stoActive)
    {
        return false;
    }

    // 다른 명령이 실행 중이면 새 Servo ON 명령을
    // 덮어쓰지 않습니다.
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

    // 이미 Operation Enabled라면
    // 불필요한 CiA402 전환을 다시 수행하지 않습니다.
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

    // 다른 일반 명령이 진행 중이면
    // 이번 단계에서는 Servo OFF 요청을 덮어쓰지 않습니다.
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

    // 이미 Ready To Switch On이면
    // 우리가 정의한 Servo OFF 상태입니다.
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

    // Fault 상태에서는 Homing을 시작하지 않습니다.
    if (runtimeInfo.fault)
    {
        return false;
    }

	if (runtimeInfo.stoActive) // STO가 활성화되어 있으면 Homing을 시작하지 않습니다.
    {
        return false;
    }

    // 다른 명령이 진행 중이면 Home 명령을 덮어쓰지 않습니다.
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


    // 새 Homing을 시작하므로 이전 완료 상태는 제거합니다.
    runtimeInfo.homed = false;

    return true;
}



// --------------------------------------------------------
//위치결정 제어용 Output Command를 설정합니다.
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
	// Profile Velocity가 0이면 위치이동을 접수하지 않습니다.
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
			physicalSlaveIndex);  // 0번은 EtherCAT Master 자신이므로 1부터 시작합니다.

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

    // Fault 상태에서는 위치이동을 접수하지 않습니다.
    if (runtimeInfo.fault)
    {
        return false;
    }

	if (runtimeInfo.stoActive) // STO가 활성화되어 있으면 위치이동을 접수하지 않습니다.
    {
        return false;
    }

    // 다른 Servo 명령이 진행 중이면
    // 새 MoveAbs 명령으로 덮어쓰지 않습니다.
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
    // Move Absolute 명령 파라미터 저장
    // --------------------------------------------------------
    runtimeInfo.moveTargetPosition =
        targetPosition;

    runtimeInfo.moveProfileVelocity =
        profileVelocity;

    runtimeInfo.moveProfileAcceleration =
		safeAcceleration;  // 0이면 기본값으로 대체

    runtimeInfo.moveProfileDeceleration =
		safeDeceleration; // 0이면 기본값으로 대체

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

        // 거리 / 속도 기준 이동시간
        // 현재 Position, Velocity가 같은 User Unit 계열이라는 전제입니다.
        const unsigned long long baseMoveTimeMs =
            (moveDistance * 1000ULL +
                static_cast<unsigned long long>(profileVelocity) - 1ULL)
            /
            static_cast<unsigned long long>(profileVelocity);

        // 예상시간의 2배 + 2초 여유
        unsigned long long calculatedTimeoutMs =
            (baseMoveTimeMs * 2ULL) + 2000ULL;

        // 너무 짧은 이동도 최소 3초는 확보
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
// Profile Velocity 제어 명령을 접수합니다.
// 실제 속도 출력은 ProcessServoCommands()에서 처리합니다.
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

    // Fault 상태에서는 속도운전을 접수하지 않습니다.
    if (runtimeInfo.fault)
    {
        return false;
    }

    // STO 상태에서는 어떠한 속도운전도 접수하지 않습니다.
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
    // 이미 Velocity 운전 중이라면
    // 새로운 속도값으로 갱신할 수 있습니다.
    //
    // +값 : 정방향
    // -값 : 역방향
    //  0  : 감속 정지
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

    // 다른 Servo 명령이 진행 중이면
    // 새 속도명령으로 덮어쓰지 않습니다.
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
// 현재 Servo 운전을 정지 명령으로 전환합니다.
// 실제 정지 동작은 ProcessServoCommands()에서 처리합니다.
// Servo OFF가 아니라 감속 정지 + 토크 유지입니다.
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

    // Fault / STO 상태에서는 일반 Stop 명령으로
    // 상태를 덮어쓰지 않습니다.
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

    // IN8OUT8은 하위 8비트만 허용합니다.
    if (runtimeInfo.outputBytes == 1)
    {
        outputValue =
            static_cast<unsigned short>(
                outputValue & 0x00FF);
    }
    // IN16OUT16은 16비트 전체를 사용합니다.
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
    // ADC 런타임 저장공간 전체를 변경하므로
    // 외부 조회 또는 통신 스레드 접근과 충돌하지 않게
    // mutex로 보호합니다.
    // --------------------------------------------------------
    std::lock_guard<std::mutex> lock(
        adcRuntimeMutex_);

    // 기존 물리 Slave별 ADC 런타임 정보를 모두 제거합니다.
    adcRuntimeInfoBySlave_.clear();

    // SOEM의 물리 Slave 번호는 1부터 시작합니다.
    // 0번은 전체 Slave용이므로 사용하지 않지만,
    // 물리 번호를 그대로 vector index로 사용하기 위해
    // slaveCount + 1 크기로 확보합니다.
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

void DaoEtherCATMaster::ResetServoRuntimeInfo() // 서보 런타임 정보 초기화
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


void DaoEtherCATMaster::ResetIoRuntimeInfo() //런타임IO정보 초기화
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
void DaoEtherCATMaster::CommunicationThreadMain() // EtherCAT 순환통신 스레드 메인 루프
{
    // --------------------------------------------------------
    // EtherCAT 순환통신 목표 주기
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
        // 1. 검증된 모든 DAO ADC Output PDO를 0으로 유지
        //
        // 아직 Servo와 IO는 등록되지 않았으므로
        // 현재는 ADC Output 4바이트만 처리합니다.
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

            // 검증된 DAO ADC PDO 크기와 포인터만 허용합니다.
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
        // LS Servo와 FASTECH IO의 최신 출력 명령을
        // 실제 Output PDO 메모리에 반영합니다.
        // ----------------------------------------------------
        PrepareServoAndIoOutputs();

        // ----------------------------------------------------
        // 2. 전체 EtherCAT Process Data를 정확히 한 번 송수신
        // ----------------------------------------------------
        ecx_send_processdata(
            &context_);

        const int actualWkc =
            ecx_receive_processdata(
                &context_,
                EC_TIMEOUTRET);

        // ----------------------------------------------------
        // 이번 프레임의 LS Servo와 FASTECH IO 입력 PDO를
        // 각 장치의 Runtime 저장공간에 반영합니다.
        // ----------------------------------------------------
        CaptureServoAndIoInputs(
            actualWkc);

        // 최신 Servo 상태를 기준으로
        // 각 축의 비동기 명령 상태머신을 진행합니다.
        ProcessServoCommands();

        const bool wkcValid =
            actualWkc >= expectedWkc_;

        // ----------------------------------------------------
        // 3. 각 DAO ADC의 Input PDO를 런타임 저장공간에 반영
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

            // WKC가 정상일 때만 새 ADC 데이터를 복사합니다.
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
        // 4. 다음 2ms 절대시각까지 대기
        // ----------------------------------------------------
        std::this_thread::sleep_until(
            nextWakeTime);

        // PC가 오래 정지한 경우 밀린 주기를 연속 수행하지 않습니다.
        const auto now =
            std::chrono::steady_clock::now();

        if (now - nextWakeTime >=
            std::chrono::milliseconds(20))
        {
            nextWakeTime = now;
        }
    }

    // --------------------------------------------------------
    // 스레드 종료 시 모든 ADC Runtime의 실행 상태를 false로 변경
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
    // 스레드 종료 시 모든 Servo Runtime의
    // 실행 상태를 false로 변경
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
    // 스레드 종료 시 모든 IO Runtime의
    // 실행 상태를 false로 변경
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

    communicationRunning_.store(false);
}

void DaoEtherCATMaster::ConfigureServoAndIoRuntimeInfo()
{
    // --------------------------------------------------------
    // LS Servo Runtime 구성
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

            // 기본값으로 다시 초기화
            runtimeInfo = {};

            runtimeInfo.physicalSlaveIndex =
                physicalSlaveIndex;

            runtimeInfo.expectedWkc =
                expectedWkc_;

            // LS L7NH가 아니면 여기까지만 유지
            if (!IsLsL7nhServo(
                physicalSlaveIndex))
            {
                continue;
            }

            const ec_slavet& slave =
                context_.slavelist[
                    physicalSlaveIndex];

            // PDO 크기가 정확히 맞는 경우만 활성화
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
            // Servo 초기 Output PDO 기본값
            //
            // 실제 위치는 아직 첫 Input PDO를 받기 전이므로
            // Target Position은 0으로 시작합니다.
            // 첫 정상 입력을 받은 뒤 상태머신에서
            // 현재 실제 위치로 다시 맞춥니다.
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


            // 입력은 아직 유효하지 않음
            runtimeInfo.latestInput = {};
            runtimeInfo.hasValidInputData = false;
        }
    }



    // --------------------------------------------------------
    // FASTECH IO Runtime 구성
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

			runtimeInfo.configured = true; // FASTECH IO로 구성됨
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

void DaoEtherCATMaster::PrepareServoAndIoOutputs()
{
    // --------------------------------------------------------
    // LS Servo 출력 PDO 준비
    //
    // 외부 API가 보관한 outputCommand를
    // 실제 EtherCAT Output PDO 메모리로 복사합니다.
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

            // PDO 크기와 포인터 검증을 통과한
            // LS Servo만 처리합니다.
            if (!runtimeInfo.configured)
            {
                continue;
            }

            ec_slavet& slave =
                context_.slavelist[
                    physicalSlaveIndex];

            // ConfigureServoAndIoRuntimeInfo()에서
            // 이미 12바이트와 포인터를 검증했지만,
            // 실제 복사 직전에도 한 번 더 확인합니다.
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
    // FASTECH IO 출력 PDO 준비
    //
    // IN8OUT8은 1바이트,
    // IN16OUT16은 2바이트만 복사합니다.
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
void DaoEtherCATMaster::CaptureServoAndIoInputs(
    int actualWkc)
{
    const bool wkcValid =
        actualWkc >= expectedWkc_;

    // --------------------------------------------------------
    // LS Servo 입력 PDO 저장
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

            // 최신 StatusWord를 엔진 공통 상태로 해석합니다.
            UpdateServoDerivedState(
                runtimeInfo);

            ++runtimeInfo.inputUpdateCount;
        }
    }

    // --------------------------------------------------------
    // FASTECH IO 입력 PDO 저장
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
        // 현재 실행 중인 명령 종류 확인
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


        // 현재 상태머신에서 처리하는 명령이 아니면 무시
        if (!isServoOnCommand &&
            !isServoOffCommand &&
            !isHomingCommand &&
            !isMoveAbsoluteCommand &&
            !isVelocityCommand &&
            !isStopCommand)
        {
            continue;
        }

        // ACCEPTED 또는 RUNNING 상태의 명령만 진행
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
            // Fault 상태에서는 Homing 진행 금지
            // ------------------------------------------------
            if (runtimeInfo.fault) 
            {
                runtimeInfo.commandState =
                    DAO_SERVO_COMMAND_STATE_ERROR;

                runtimeInfo.commandResult = -1;

                runtimeInfo.homed = false;

                continue;
            }

			if (runtimeInfo.stoActive) // STO Active 상태에서는 Homing 진행 금지
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


            // Homing 상태머신 전체에서 공통으로
            // 안전한 출력값을 유지합니다.
            command.targetPosition =
                runtimeInfo.latestInput.actualPosition;

            command.targetVelocity = 0;

            command.touchProbeFunction = 0;

            command.digitalOutputs = 0;


            // =================================================
            // 1. HOMING_PREPARE
            //
            // Servo를 Ready To Switch On(0x21) 상태로
            // 내린 뒤 Homing Mode 변경을 준비합니다.
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_HOMING_PREPARE)
            {
                runtimeInfo.commandState =
                    DAO_SERVO_COMMAND_STATE_RUNNING;

                // Ready To Switch On 상태가 아니면
                // Shutdown 명령을 유지합니다.
                if (runtimeInfo.cia402State != 0x0021)
                {
                    command.controlWord = 0x0006;
                    continue;
                }

                // Servo OFF 상태 확인 완료
                command.controlWord = 0x0006;

                // Homing Mode 요청
                command.operationMode = 6;

                runtimeInfo.commandStep =
                    DAO_SERVO_STEP_HOMING_MODE_REQUEST;

                continue;
            }


            // =================================================
            // 2. HOMING_MODE_REQUEST
            //
            // 0x6060 = 6
            // 0x6061 Mode Display = 6 확인
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_HOMING_MODE_REQUEST)
            {
                command.controlWord = 0x0006;

                command.operationMode = 6;

                // Drive가 실제 Homing Mode로
                // 변경될 때까지 기다립니다.
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
            // Homing Mode 상태에서 Servo ON
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_HOMING_SERVO_ON)
            {
                command.operationMode = 6;


                // 이미 Operation Enabled
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


                // 정의하지 않은 상태에서는
                // 임의로 다음 단계로 진행하지 않습니다.
                continue;
            }


            // =================================================
            // 4. HOMING_START
            //
            // Homing Start bit를 올립니다.
            //
            // 0x001F =
            // Operation Enabled + Homing Start
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_HOMING_START)
            {
                command.operationMode = 6;

                command.controlWord = 0x001F;


                // 실제 Homing 동작이 시작되는 시점
                runtimeInfo.homingStartFrameCount =
                    runtimeInfo.totalFrameCount;

                // Homing 위치 변화 감시 초기화
                runtimeInfo.homingLastPosition =
                    runtimeInfo.latestInput.actualPosition;

                runtimeInfo.homingLastMoveFrameCount =
                    runtimeInfo.totalFrameCount;

                runtimeInfo.homingPositionMonitorStarted =
                    true;

                runtimeInfo.homingAttainedWentLow =
					false; // Homing Attained Bit가 Low로 떨어진 적이 있는지

                // 실제 Move 시작 위치 저장
                runtimeInfo.moveStartPosition =
                    runtimeInfo.latestInput.actualPosition;

                // 실제 Move 시작 Frame 저장
                runtimeInfo.moveStartFrameCount =
                    runtimeInfo.totalFrameCount;

                runtimeInfo.commandStep =
                    DAO_SERVO_STEP_HOMING_RUNNING;

                continue;
            }


            // =================================================
            // 5. HOMING_RUNNING
            //
            // Drive가 설정된 Homing Method에 따라
            // 자체 Homing 프로세스를 수행합니다.
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
                // Homing 중 실제 위치 변화 감시
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

                    // ±5 count 정도의 작은 위치 흔들림은
                    // 실제 이동으로 판단하지 않습니다.
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
                // 마지막 위치 변화 이후 경과 Frame 계산
                //
                // EtherCAT 주기 = 2ms
                // 아직 에러 판정은 하지 않습니다.
                // ----------------------------------------------------
                const std::uint64_t noMoveFrames =
                    runtimeInfo.totalFrameCount -
                    runtimeInfo.homingLastMoveFrameCount;
                //----------------------------------------------------

                const bool homingAttained =
                    (statusWord & 0x1000) != 0;

                const bool homingError =
                    (statusWord & 0x2000) != 0;

                // 새 Homing이 실제로 시작된 후
                // Homing Attained Bit가 한번 LOW가 되는 것을 확인합니다.
                if (!homingAttained)
                {
                    runtimeInfo.homingAttainedWentLow =
                        true;
                }

                // ------------------------------------------------
                // Homing Timeout 검사
                //
                // EtherCAT 주기 = 2ms
                // timeoutMs를 필요한 PDO Frame 수로 변환합니다.
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
                    // Homing Start bit 해제
                    command.controlWord = 0x000F;

                    runtimeInfo.commandState =
                        DAO_SERVO_COMMAND_STATE_ERROR;

                    runtimeInfo.commandResult = -3;

                    runtimeInfo.homed = false;

                    continue;
                }

                // ---------------------------------------------
               // Homing 완료 
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
                    // Homing Start bit 해제
                    command.controlWord = 0x000F;

                    // 다음 PDO부터 Profile Position Mode로 복귀 요청
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


               


                // 아직 Homing 진행 중
                command.controlWord = 0x001F;

                continue;
            }


            // =================================================
            // 6. HOMING_FINISH
            //
            // Homing 완료 후 Start bit를 해제하고
            // Profile Position Mode(1) 복귀를 요청합니다.
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
            // 0x6060 = 1을 유지하면서
            // 0x6061 Mode Display가 실제 1인지 확인합니다.
            //
            // 이번 단계에서는 아직 COMPLETED 처리하지 않습니다.
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_HOMING_RESTORE_MODE)
            {
                command.controlWord = 0x000F;

                command.operationMode = 1;

                command.targetPosition =
                    runtimeInfo.latestInput.actualPosition;

                command.targetVelocity = 0;


                // Drive가 실제 Profile Position Mode로
                // 복귀할 때까지 기다립니다.
                if (runtimeInfo.latestInput.operationModeDisplay != 1)
                {
                    continue;
                }


                // ====================================================
                // Homing 전체 프로세스 정상 완료
                //
                // Drive가 Profile Position Mode(1)로
                // 실제 복귀한 것까지 확인한 뒤 완료 처리합니다.
                // ====================================================

                runtimeInfo.homed = true;

                runtimeInfo.commandState =
                    DAO_SERVO_COMMAND_STATE_COMPLETED;

                runtimeInfo.commandResult = 1;

                continue;
            }


            // 정의하지 않은 Homing Step에서는
            // 임의 동작을 만들지 않습니다.
            continue;
        }



        // ====================================================
        // MOVE ABSOLUTE COMMAND
        // ====================================================
		if (isMoveAbsoluteCommand) // Profile Position Mode(1) + Servo ON + Move Start
        {
			if (runtimeInfo.fault) // Fault 상태에서는 이동 금지
            {
                runtimeInfo.commandState =
                    DAO_SERVO_COMMAND_STATE_ERROR;

                runtimeInfo.commandResult = -1;

                continue;
            }

			if (runtimeInfo.stoActive) // STO Active 상태에서는 이동 금지
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
            // 아직 이동 시작은 하지 않습니다.
            // Profile Position Mode(1) 요청만 준비합니다.
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_MOVE_ABS_PREPARE)
            {
                runtimeInfo.commandState =
                    DAO_SERVO_COMMAND_STATE_RUNNING;

                command.controlWord = 0x000F;

                command.operationMode = 1;

                // 아직 실제 목표위치는 보내지 않고
                // 현재 위치를 유지합니다.
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
            // Profile Position Mode(1)를 계속 요청하면서
            // Drive의 Mode Display(0x6061)가 실제 1인지 확인합니다.
            //
            // 아직 목표 위치 이동은 시작하지 않습니다.
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_MOVE_ABS_MODE_REQUEST)
            {
                command.operationMode = 1;

                command.controlWord = 0x000F;

                // 아직 실제 목표위치를 보내지 않고
                // 현재 위치를 유지합니다.
                command.targetPosition =
                    runtimeInfo.latestInput.actualPosition;

                command.targetVelocity = 0;

                // Drive가 실제 Profile Position Mode가 될 때까지 대기
                if (runtimeInfo.latestInput.operationModeDisplay != 1)
                {
                    continue;
                }

                // Mode 1 확인 완료.
                // 다음 단계는 Servo ON 확인/전환입니다.
                runtimeInfo.commandStep =
                    DAO_SERVO_STEP_MOVE_ABS_SERVO_ON;

                continue;
            }

            // =================================================
            // 3. MOVE_ABS_SERVO_ON
            //
            // Profile Position Mode(1) 확인 후
            // Servo를 Operation Enabled(0x27)까지 올립니다.
            //
            // 아직 목표 위치 이동은 시작하지 않습니다.
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_MOVE_ABS_SERVO_ON)
            {
                command.operationMode = 1;

                // 아직 실제 목표위치를 넣지 않고
                // 현재 위치를 유지합니다.
                command.targetPosition =
                    runtimeInfo.latestInput.actualPosition;

                command.targetVelocity = 0;

                // 이미 Operation Enabled이면
                // 다음 Move Start 준비 단계로 이동
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

                // 예상하지 못한 상태에서는
                // 임의로 다음 단계로 진행하지 않습니다.
                continue;
            }

            // =================================================
            // 4. MOVE_ABS_START
            //
            // Profile Position Mode(1) + Servo ON 확인 후
            // 목표 위치 / 속도 / 가속 / 감속 값을 PDO에 준비합니다.
            //
            // 아직 New Set-point bit는 올리지 않습니다.
            // 따라서 이번 단계에서는 실제 이동을 시작하지 않습니다.
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
                // Bit 4를 올려 실제 절대위치 이동을 시작합니다.
                command.controlWord = 0x001F;

                // 실제 Move Absolute 시작 시점 기록
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

                // New Set-point bit는 해제 상태 유지
                command.controlWord = 0x000F;

                // ------------------------------------------------
                // 1. 이동 시작 후 Target Reached가
                //    한번 LOW가 되었는지 확인
                // ------------------------------------------------
                if (!runtimeInfo.targetReached)
                {
                    runtimeInfo.moveTargetReachedWentLow =
                        true;
                }

                // ------------------------------------------------
                // 2. LOW 확인 후 다시 HIGH가 되면
                //    정상 이동 완료
                // ------------------------------------------------
                if (runtimeInfo.moveTargetReachedWentLow &&
                    runtimeInfo.targetReached)
                {
                    runtimeInfo.commandStep =
                        DAO_SERVO_STEP_MOVE_ABS_FINISH;

                    continue;
                }

                // ------------------------------------------------
                // 3. 아직 완료되지 않았다면 Timeout 확인
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
                    // Timeout이 발생해도 Servo OFF는 하지 않습니다.
                    command.controlWord = 0x000F;
                    command.operationMode = 1;
                    command.targetVelocity = 0;

                    runtimeInfo.commandState =
                        DAO_SERVO_COMMAND_STATE_TIMEOUT;

                    runtimeInfo.commandResult = -4;

                    continue;
                }

                // 아직 이동 중
                continue;
            }


            // =================================================
            // 6. MOVE_ABS_FINISH
            //
            // 목표 위치 도달 확인 후
            // Move Absolute 명령을 정상 완료 처리합니다.
            // =================================================
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_MOVE_ABS_FINISH)
            {
                command.operationMode = 1;

                // Servo는 계속 Operation Enabled 유지
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
            // 이후 단계는 다음 작업에서 구현합니다.
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
            // PV 모드로 바꾸기 전에
            // Target Velocity를 반드시 0으로 만들어 둡니다.
            // 이 단계에서는 실제 이동하지 않습니다.
            // ----------------------------------------------------
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_VELOCITY_PREPARE)
            {
                runtimeInfo.commandState =
                    DAO_SERVO_COMMAND_STATE_RUNNING;

                // 현재 위치 유지
                command.targetPosition =
                    runtimeInfo.latestInput.actualPosition;

                // 아직 기존 PP 모드 유지
                command.operationMode = 1;

                // Servo ON 상태 유지
                command.controlWord = 0x000F;

                // PV 전환 전에 속도는 반드시 0
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
            // Profile Velocity Mode(3)를 요청합니다.
            // Target Velocity는 아직 0으로 유지합니다.
            // ----------------------------------------------------
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_VELOCITY_MODE_REQUEST)
            {
                // PV Mode 요청
                command.operationMode = 3;

                // 모드 전환 중에는 실제 이동 금지
                command.targetVelocity = 0;

                command.profileAcceleration =
                    runtimeInfo.velocityAcceleration;

                command.profileDeceleration =
                    runtimeInfo.velocityDeceleration;

                // Servo 상태는 유지
                command.controlWord = 0x000F;

                // Drive가 실제로 PV Mode로 전환됐는지 확인
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
            // PV Mode 상태에서 CiA402 Operation Enabled를
            // 확보합니다.
            // Target Velocity는 아직 0으로 유지합니다.
            // ----------------------------------------------------
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_VELOCITY_SERVO_ON)
            {
                command.operationMode = 3;

                // 아직 실제 이동하지 않음
                command.targetVelocity = 0;

                command.profileAcceleration =
                    runtimeInfo.velocityAcceleration;

                command.profileDeceleration =
                    runtimeInfo.velocityDeceleration;

                const int ciaState =
                    runtimeInfo.cia402State;

                // 이미 Operation Enabled 상태
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

                // 모르는 상태에서는 임의로 진행하지 않음
                continue;
            }

            // ----------------------------------------------------
            // 330. VELOCITY_RUNNING
            //
            // Profile Velocity Mode에서 실제 Target Velocity를
            // 0x60FF로 출력합니다.
            //
            // +값 : 정방향
            // -값 : 역방향
            //  0  :  감속 정지, Servo ON 유지, 토크 유지
            // ----------------------------------------------------
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_VELOCITY_RUNNING)
            {
                command.operationMode = 3;

                command.profileAcceleration =
                    runtimeInfo.velocityAcceleration;

                command.profileDeceleration =
                    runtimeInfo.velocityDeceleration;

                // Servo Operation Enabled 유지
                command.controlWord = 0x000F;

                // Fault 또는 STO 발생 시 속도명령 차단
                if (runtimeInfo.fault ||
                    runtimeInfo.stoActive)
                {
                    command.targetVelocity = 0;

                    runtimeInfo.commandState =
                        DAO_SERVO_COMMAND_STATE_ERROR;

                    runtimeInfo.commandResult = -1;

                    continue;
                }

                // STOP 입력이 들어오면 이동 금지
                if (runtimeInfo.stopInput)
                {
                    command.targetVelocity = 0;
                    continue;
                }

                // 정방향 이동 중 Positive Limit이면 이동 금지
                if (runtimeInfo.velocityTarget > 0 &&
                    runtimeInfo.positiveLimit)
                {
                    command.targetVelocity = 0;
                    continue;
                }

                // 역방향 이동 중 Negative Limit이면 이동 금지
                if (runtimeInfo.velocityTarget < 0 &&
                    runtimeInfo.negativeLimit)
                {
                    command.targetVelocity = 0;
                    continue;
                }

                // 실제 Profile Velocity 출력
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
            // 현재 모드를 확인하고 정지 준비만 합니다.
            // 실제 정지 동작은 다음 Step에서 처리합니다.
            // ----------------------------------------------------
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_STOP_PREPARE)
            {
                runtimeInfo.commandState =
                    DAO_SERVO_COMMAND_STATE_RUNNING;

                // Servo ON 상태는 유지합니다.
                command.controlWord = 0x000F;

                // PV 모드라면 우선 Target Velocity를 0으로 준비
                if (runtimeInfo.latestInput.operationModeDisplay == 3)
                {
                    command.operationMode = 3;
                    command.targetVelocity = 0;
                }
                else
                {
                    // PP 또는 기타 위치계열은 현재 모드 유지
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
            // 현재 운전모드에 따라 실제 정지 명령을 수행합니다.
            // Servo ON 상태는 유지합니다.
            // ----------------------------------------------------
            if (runtimeInfo.commandStep ==
                DAO_SERVO_STEP_STOPPING)
            {
                const int currentMode =
                    runtimeInfo.latestInput.operationModeDisplay;

                // ------------------------------------------------
                // PV Mode
                // Target Velocity를 0으로 내려 감속 정지합니다.
                // Servo ON / 토크는 유지합니다.
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
                // CiA402 Halt bit를 사용하여 감속 정지합니다.
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

                // 현재 모드가 예상하지 못한 값이면
                // 임의 동작하지 않고 Servo ON 상태만 유지합니다.
                command.controlWord = 0x000F;
                command.targetVelocity = 0;

                continue;
            }

            // ----------------------------------------------------
            // 420. STOP_FINISH
            //
            // 정지 명령 완료 처리
            // Servo ON 상태는 유지합니다.
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

        // Servo ON은 Fault 상태에서 진행하지 않습니다.
        //
        // Servo OFF는 정지 방향 명령이므로
        // Fault 상태에서도 아래 OFF 처리까지 허용합니다.
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


        // 2ms × 1000 = 약 2초
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
        // 엔진의 Servo OFF 완료 상태:
        // Ready To Switch On = 0x0021
        // ====================================================
        if (isServoOffCommand)
        {
            command.targetPosition =
                runtimeInfo.latestInput.actualPosition;

            command.targetVelocity = 0;

            command.touchProbeFunction = 0;

            command.digitalOutputs = 0;


            // Servo OFF 완료
            if (cia402State == 0x0021)
            {
                command.controlWord = 0x0006;

                runtimeInfo.commandState =
                    DAO_SERVO_COMMAND_STATE_COMPLETED;

                runtimeInfo.commandResult = 1;

                continue;
            }


            // Operation Enabled / Switched On 등에서는
            // Shutdown 명령으로 0x21까지 내립니다.
            command.controlWord = 0x0006;

            runtimeInfo.commandState =
                DAO_SERVO_COMMAND_STATE_RUNNING;

            continue;
        }


        // ====================================================
        // SERVO ON
        // ====================================================

        // 위치 명령에 의한 예상하지 못한 이동 방지
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


        // 그 외의 예상하지 못한 CiA402 상태에서는
        // 임의의 추가 명령을 만들지 않습니다.
    }
}



void DaoEtherCATMaster::UpdateServoDerivedState(
    DaoInternalServoRuntimeInfo& runtimeInfo)
{
    const unsigned short statusWord =
        runtimeInfo.latestInput.statusWord;

    // --------------------------------------------------------
    // CiA402 상태를 엔진 내부 공통값으로 정규화합니다.
    //
    // LS L7NH의 실제 StatusWord에는
    // Quick Stop 등의 상태 비트가 함께 포함되므로
    // 단순히 0x006F 마스크 결과를 그대로 상태값으로
    // 사용하면 Switch On Disabled가 0x0060으로 보일 수 있습니다.
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
        // 아직 정의하지 않은 상태는
        // 원래 마스크 결과를 진단용으로 남깁니다.
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
    // LS L7NH 0x60FD Digital Inputs 상태 해석
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
	double& y2)  // 2차 IIR Notch Filter
{
    // --------------------------------------------------------
    // 기본 입력값 검증
    // --------------------------------------------------------
    if (sampleRateHz <= 0.0 ||
        notchFrequencyHz <= 0.0 ||
        notchFrequencyHz >= (sampleRateHz * 0.5))
    {
        return inputValue;
    }

    // --------------------------------------------------------
    // 첫 Sample 초기화
    //
    // ADC 값이 약 8백만 count 수준이므로
    // Filter 내부 상태를 0에서 시작시키면
    // 처음 순간 큰 과도응답이 발생할 수 있습니다.
    //
    // 상태가 모두 0이면 현재 입력값으로 초기화합니다.
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
    // 2차 IIR Notch Filter
    //
    // Zero : notchFrequencyHz
    // Pole : 동일 주파수, radius = r
    //
    // r이 1.0에 가까울수록
    // 제거 대역이 좁아지고 원래 신호 보존성이 좋아집니다.
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
    // DC Gain = 1이 되도록 Gain 보정
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
    // 다음 Sample용 상태 갱신
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
    // 저수준 Filter 첫 샘플 초기화
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
    // 저수준 1차 Low-Pass Filter
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
    // 기본값은 Low-Level Filter 결과 그대로 사용합니다.
    // HZ_60 모드에서는
    // 60Hz -> 120Hz 순서로 Notch를 통과시킵니다.
    // --------------------------------------------------------
    runtimeInfo.processing.powerLineFiltered =
        runtimeInfo.processing.lowLevelFiltered;

    constexpr double ADC_EFFECTIVE_SAMPLE_RATE_HZ =
        2000.0;

    switch (runtimeInfo.processing.powerLineFilterMode)
    {
    case DaoInternalAdcPowerLineFilterMode::OFF:
        // 아무 Notch도 적용하지 않습니다.
        runtimeInfo.processing.powerLineFiltered =
            runtimeInfo.processing.lowLevelFiltered;
        break;


    case DaoInternalAdcPowerLineFilterMode::HZ_50:
        // 50Hz만 제거
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
        // 60Hz만 제거
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
        // 120Hz만 제거
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
        // 50Hz 제거 후 60Hz 제거
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
        // 60Hz 제거 후 120Hz 제거
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
    // Zero 적용
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
    // Calibration 적용
    // --------------------------------------------------------
    runtimeInfo.processing.calibratedValue =
        runtimeInfo.processing.zeroedValue *
        runtimeInfo.processing.calibrationScale;

    // --------------------------------------------------------
    // 3-Sample Median Filter
    //
    // 순간적으로 한 Sample만 크게 튀는 값을 제거하기 위한
    // 작은 Median Filter입니다.
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
                // 초기 1~2 Sample은 Median 3 계산이 불가능하므로
                // 현재 Calibration 값을 그대로 사용합니다.
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
    // 사용자 N Sample Moving Average Filter
    //
    // 주의:
    // calibratedValue가 아니라
    // Median 3 처리 후 값인 medianFilteredValue를 사용합니다.
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
        // 아직 Buffer가 N개 채워지지 않은 초기 구간
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
            // 가장 오래된 Sample 제거
            // ------------------------------------------------
            runtimeInfo.processing.userFilterSum -=
                runtimeInfo.processing.userFilterBuffer[
                    runtimeInfo.processing.userFilterIndex];

            // ------------------------------------------------
            // 새로운 Median Filter 결과 저장
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
    // 목표 Sample 개수를 정확히 수집합니다.
    //
    // Zero:
    //   정확히 600 Sample
    //
    // Calibration:
    //   안정화 Sample 버림
    //   + 정확히 지정된 Sample 수 평균
    // --------------------------------------------------------
    if (runtimeInfo.processing.stableCaptureActive)
    {
        // ----------------------------------------------------
        // 안정화 대기 Sample은 평균에 포함하지 않습니다.
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
            // 유효 Sample 정확히 1개 합산
            // ------------------------------------------------
            runtimeInfo.processing.stableCaptureSum +=
                runtimeInfo.processing.powerLineFiltered;

            ++runtimeInfo.processing.stableCaptureCollectedCount;

            // ------------------------------------------------
            // 정확히 목표 Sample 개수에 도달한 경우에만 완료
            // ------------------------------------------------
            if (runtimeInfo.processing.stableCaptureCollectedCount ==
                runtimeInfo.processing.stableCaptureSampleCount)
            {
                const double stableAverage =
                    runtimeInfo.processing.stableCaptureSum /
                    static_cast<double>(
                        runtimeInfo.processing.stableCaptureCollectedCount);

                // ====================================================
                // ZERO Capture 완료
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
                    // Zero 기준이 변경되었으므로
                    // Median 3의 이전 기준 데이터 폐기
                    // --------------------------------------------
                    runtimeInfo.processing.medianBuffer[0] = 0.0;
                    runtimeInfo.processing.medianBuffer[1] = 0.0;
                    runtimeInfo.processing.medianBuffer[2] = 0.0;

                    runtimeInfo.processing.medianIndex =
                        0;

                    runtimeInfo.processing.medianCount =
                        0;

                    // --------------------------------------------
                    // 이전 Zero 기준의 N Filter 데이터도 폐기
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
                // CALIBRATION Capture 완료
                // ====================================================
                else if (
                    runtimeInfo.processing.stableCaptureType ==
                    DaoInternalAdcStableCaptureType::CALIBRATION)
                {
                    const double calibrationSpan =
                        stableAverage -
                        runtimeInfo.processing.zeroOffset;

                    // --------------------------------------------
                    // Span이 지나치게 작으면 잘못된 교정이므로
                    // 기존 Calibration Scale을 유지합니다.
                    // --------------------------------------------
                    if (std::abs(calibrationSpan) >= 1.0)
                    {
                        runtimeInfo.processing.calibrationScale =
                            runtimeInfo.processing.stableCaptureReferenceValue /
                            calibrationSpan;

                        // 새로운 Scale을 현재 최신값에 적용
                        runtimeInfo.processing.calibratedValue =
                            runtimeInfo.processing.zeroedValue *
                            runtimeInfo.processing.calibrationScale;

                        runtimeInfo.processing.medianFilteredValue =
                            runtimeInfo.processing.calibratedValue;

                        // ----------------------------------------
                        // Calibration Scale이 변경되었으므로
                        // Median 3의 이전 Scale 데이터 폐기
                        // ----------------------------------------
                        runtimeInfo.processing.medianBuffer[0] = 0.0;
                        runtimeInfo.processing.medianBuffer[1] = 0.0;
                        runtimeInfo.processing.medianBuffer[2] = 0.0;

                        runtimeInfo.processing.medianIndex =
                            0;

                        runtimeInfo.processing.medianCount =
                            0;

                        // ----------------------------------------
                        // 이전 Scale 기준의 N Filter 데이터 폐기
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
                // Stable Capture 종료
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
    // Noise 분석용으로 현재 Sample의 처리 단계별 값을
    // 메모리에 저장합니다.
    //
    // 목표 Sample 개수에 정확히 도달하면 자동 종료합니다.
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
// 최종 사용자용 FilteredValue를
// 모든 처리 Sample마다 Ring Buffer에 저장합니다.
// --------------------------------------------------------
    {
        DaoInternalAdcBufferedSample bufferedSample{};

        bufferedSample.sampleIndex =
            runtimeInfo.ringBufferNextSampleIndex;

        bufferedSample.filteredValue =
            runtimeInfo.processing.filteredValue;

        ++runtimeInfo.ringBufferNextSampleIndex;

        // ----------------------------------------------------
        // Buffer에 빈 공간이 있는 경우
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
            // Buffer가 가득 찬 경우
            //
            // 가장 오래된 Sample 하나를 버리고
            // 새 Sample을 저장합니다.
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

                // count는 이미 MAX 상태이므로 그대로 유지합니다.
        }
    }


}