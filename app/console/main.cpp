#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <chrono>
#include <thread>

#include <soem/soem.h>

namespace
{
    constexpr std::size_t IO_MAP_SIZE = 4096;

    constexpr std::uint32_t DAO_VENDOR_ID = 0x000011C0;
    constexpr std::uint32_t DAO_ADC_PRODUCT_CODE = 0x0000DA01;

    constexpr std::uint16_t DAO_ADC_OUTPUT_BYTES = 4;
    constexpr std::uint16_t DAO_ADC_INPUT_BYTES = 24;

    /*
     * true:
     *   DAO ADC의 PDO 크기를 CoE/SDO로 질문하지 않고
     *   EEPROM/SII의 고정 PDO 정보를 사용합니다.
     *
     * false:
     *   SOEM 기본 방식으로 CoE PDO 읽기를 먼저 시도합니다.
     *
     * 이번 시험에서는 true로 둡니다.
     */
    constexpr bool FORCE_SII_ONLY_MAPPING = true;

    constexpr int PREOP_TIMEOUT_MS = 3000;
    constexpr int SAFEOP_TIMEOUT_MS = 12000;
    constexpr int OP_TIMEOUT_MS = 12000;

    /*
     * 2ms x 500회/초 x 60초 = 30,000 PDO 프레임
     * 프레임당 RAW 4개이므로 목표 수신량은 120,000개입니다.
     */
    constexpr int TEST_CYCLE_MS = 2;
    constexpr int TEST_SECONDS = 60;
    constexpr int TEST_CYCLE_COUNT =
        (1000 / TEST_CYCLE_MS) * TEST_SECONDS;
    constexpr int ADC_SAMPLES_PER_FRAME = 4;

    /*
     * true이면 60초 수신이 끝나고 EtherCAT을 안전 종료한 뒤
     * 저장해 둔 30,000개 프레임을 콘솔에 전부 출력합니다.
     */
    constexpr bool PRINT_ALL_FRAMES_AFTER_TEST = true;
}

#pragma pack(push, 1)

struct DaoAdcOutputPdo
{
    std::uint32_t gpioOutputs;
};

struct DaoAdcInputPdo
{
    std::uint32_t testCounter;
    std::int32_t adcRaw0;
    std::int32_t adcRaw1;
    std::int32_t adcRaw2;
    std::int32_t adcRaw3;
    std::uint32_t status;
};

#pragma pack(pop)

static_assert(
    sizeof(DaoAdcOutputPdo) == DAO_ADC_OUTPUT_BYTES,
    "DaoAdcOutputPdo must be 4 bytes.");

static_assert(
    sizeof(DaoAdcInputPdo) == DAO_ADC_INPUT_BYTES,
    "DaoAdcInputPdo must be 24 bytes.");

/*
 * 2ms 수신 중에는 콘솔 출력을 하지 않고 이 구조체에 저장합니다.
 * 콘솔 출력은 60초 시험이 끝나고 EtherCAT을 닫은 뒤 수행합니다.
 */
struct CapturedAdcFrame
{
    int cycle = 0;
    int wkc = 0;

    std::int64_t elapsedUs = 0;
    std::int64_t periodUs = 0;

    std::uint32_t counterDelta = 0;
    DaoAdcInputPdo data{};
};

struct AdapterInfo
{
    std::string description;
    std::string name;
};

static std::vector<AdapterInfo> FindAdapters()
{
    std::vector<AdapterInfo> result;

    ec_adaptert* adapterList = ec_find_adapters();

    for (ec_adaptert* adapter = adapterList;
        adapter != nullptr;
        adapter = adapter->next)
    {
        AdapterInfo info;

        if (adapter->desc != nullptr)
        {
            info.description = adapter->desc;
        }

        if (adapter->name != nullptr)
        {
            info.name = adapter->name;
        }

        result.push_back(info);
    }

    ec_free_adapters(adapterList);
    return result;
}

static void PrintSoemErrors(ecx_contextt& context)
{
    while (context.ecaterror)
    {
        std::cerr << ecx_elist2string(&context);
    }
}

static std::uint16_t GetBaseState(std::uint16_t state)
{
    /*
     * 하위 4비트:
     * 1 INIT
     * 2 PRE-OP
     * 4 SAFE-OP
     * 8 OP
     *
     * 상위 오류/ACK 비트는 제거합니다.
     */
    return static_cast<std::uint16_t>(state & 0x000F);
}

static void PrintSlaveState(
    const ecx_contextt& context,
    int slaveIndex)
{
    const ec_slavet& slave =
        context.slavelist[slaveIndex];

    std::cout
        << "Slave " << slaveIndex
        << " State=0x"
        << std::hex
        << std::uppercase
        << std::setw(4)
        << std::setfill('0')
        << static_cast<unsigned int>(slave.state)
        << " ALStatus=0x"
        << std::setw(4)
        << static_cast<unsigned int>(slave.ALstatuscode)
        << std::dec
        << std::nouppercase
        << std::setfill(' ')
        << " : "
        << ec_ALstatuscode2string(slave.ALstatuscode)
        << "\n";
}

static void PrintAllSlaveStates(ecx_contextt& context)
{
    ecx_readstate(&context);

    for (int slaveIndex = 1;
        slaveIndex <= context.slavecount;
        ++slaveIndex)
    {
        PrintSlaveState(context, slaveIndex);
    }
}

static bool IsDaoAdcIdentity(const ec_slavet& slave)
{
    return
        slave.eep_man == DAO_VENDOR_ID &&
        slave.eep_id == DAO_ADC_PRODUCT_CODE;
}

static int FindDaoAdcByIdentity(
    const ecx_contextt& context)
{
    for (int slaveIndex = 1;
        slaveIndex <= context.slavecount;
        ++slaveIndex)
    {
        if (IsDaoAdcIdentity(
            context.slavelist[slaveIndex]))
        {
            return slaveIndex;
        }
    }

    return 0;
}

static bool ValidateDaoAdcPdo(
    const ec_slavet& slave)
{
    return
        slave.Obytes == DAO_ADC_OUTPUT_BYTES &&
        slave.Ibytes == DAO_ADC_INPUT_BYTES;
}

static void ClearAdcOutput(ec_slavet& adcSlave)
{
    /*
     * ESI에는 4바이트 RxPDO가 존재하지만
     * 현재 ADC 응용에서 제어 용도로 사용하지 않습니다.
     *
     * 프레임 안의 출력 영역은 항상 0으로 유지합니다.
     */
    if (adcSlave.outputs != nullptr &&
        adcSlave.Obytes > 0)
    {
        std::memset(
            adcSlave.outputs,
            0,
            static_cast<std::size_t>(
                adcSlave.Obytes));
    }
}

static int ExchangeProcessData(
    ecx_contextt& context,
    int timeout = EC_TIMEOUTRET)
{
    ecx_send_processdata(&context);

    return ecx_receive_processdata(
        &context,
        timeout);
}

static bool RequestStateOnceAndWait(
    ecx_contextt& context,
    int slaveIndex,
    std::uint16_t requestedState,
    int timeoutMs,
    bool exchangeProcessData,
    ec_slavet* adcSlave)
{
    ecx_readstate(&context);

    ec_slavet& slave =
        context.slavelist[slaveIndex];

    if (GetBaseState(slave.state) ==
        requestedState)
    {
        std::cout
            << "Slave " << slaveIndex
            << " is already in requested state 0x"
            << std::hex
            << std::uppercase
            << requestedState
            << std::dec
            << ".\n";

        return true;
    }

    std::cout
        << "\nRequesting state 0x"
        << std::hex
        << std::uppercase
        << requestedState
        << std::dec
        << " for Slave "
        << slaveIndex
        << "...\n";

    /*
     * 중요:
     * 상태 요청은 한 번만 보냅니다.
     *
     * LAN9252가 AL Control 요청을 받은 뒤 처리 중이면
     * 같은 레지스터에 다시 쓰는 프레임의 WKC가 0이 될 수 있습니다.
     */
    slave.state = requestedState;

    const int stateWriteWkc =
        ecx_writestate(
            &context,
            static_cast<std::uint16_t>(
                slaveIndex));

    std::cout
        << "State request WKC : "
        << stateWriteWkc
        << "\n";

    const auto startTime =
        std::chrono::steady_clock::now();

    int lastPrintedSecond = -1;
    std::uint16_t previousState = 0xFFFF;

    while (true)
    {
        const auto now =
            std::chrono::steady_clock::now();

        const auto elapsedMs =
            std::chrono::duration_cast<
            std::chrono::milliseconds>(
                now - startTime)
            .count();

        if (elapsedMs >= timeoutMs)
        {
            break;
        }

        int processWkc = -1;

        /*
         * OP 전환 중에는 유효한 Process Data를
         * 계속 교환해야 합니다.
         */
        if (exchangeProcessData)
        {
            if (adcSlave != nullptr)
            {
                ClearAdcOutput(*adcSlave);
            }

            processWkc =
                ExchangeProcessData(context);
        }

        /*
         * 100ms 단위로 현재 상태를 확인합니다.
         * 상태 요청을 다시 쓰지는 않습니다.
         */
        (void)ecx_statecheck(
            &context,
            static_cast<std::uint16_t>(
                slaveIndex),
            requestedState,
            100000);

        ecx_readstate(&context);

        const std::uint16_t currentState =
            slave.state;

        const int elapsedSecond =
            static_cast<int>(
                elapsedMs / 1000);

        if (currentState != previousState ||
            elapsedSecond != lastPrintedSecond)
        {
            std::cout
                << "  Time="
                << std::setw(5)
                << elapsedMs
                << " ms"
                << " | State=0x"
                << std::hex
                << std::uppercase
                << std::setw(4)
                << std::setfill('0')
                << static_cast<unsigned int>(
                    currentState)
                << " | AL=0x"
                << std::setw(4)
                << static_cast<unsigned int>(
                    slave.ALstatuscode)
                << std::dec
                << std::nouppercase
                << std::setfill(' ');

            if (exchangeProcessData)
            {
                std::cout
                    << " | PDO WKC="
                    << processWkc;
            }

            std::cout << "\n";

            previousState =
                currentState;

            lastPrintedSecond =
                elapsedSecond;
        }

        if (GetBaseState(currentState) ==
            requestedState)
        {
            std::cout
                << "Slave "
                << slaveIndex
                << " reached state 0x"
                << std::hex
                << std::uppercase
                << requestedState
                << std::dec
                << ".\n";

            return true;
        }

        if ((currentState &
            EC_STATE_ERROR) != 0)
        {
            std::cerr
                << "Slave entered error state: "
                << ec_ALstatuscode2string(
                    slave.ALstatuscode)
                << "\n";

            return false;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(10));
    }

    std::cerr
        << "State transition timeout after "
        << timeoutMs
        << " ms.\n";

    PrintSlaveState(context, slaveIndex);

    return false;
}

static void PrintRawInputBytes(
    const ec_slavet& adcSlave)
{
    if (adcSlave.inputs == nullptr)
    {
        return;
    }

    std::cout
        << "\nFirst input PDO raw bytes:";

    for (std::uint32_t i = 0;
        i < adcSlave.Ibytes;
        ++i)
    {
        std::cout
            << " "
            << std::hex
            << std::uppercase
            << std::setw(2)
            << std::setfill('0')
            << static_cast<unsigned int>(
                adcSlave.inputs[i]);
    }

    std::cout
        << std::dec
        << std::nouppercase
        << std::setfill(' ')
        << "\n\n";
}

int main()
{
    std::cout
        << "========================================\n"
        << " DAO EtherCAT ADC SII Mapping Test\n"
        << "========================================\n\n";

    const std::vector<AdapterInfo> adapters =
        FindAdapters();

    if (adapters.empty())
    {
        std::cerr
            << "No compatible network adapters found.\n";

        return 1;
    }

    for (std::size_t i = 0;
        i < adapters.size();
        ++i)
    {
        std::cout
            << "[" << i << "] "
            << adapters[i].description
            << "\n    "
            << adapters[i].name
            << "\n\n";
    }

    std::cout
        << "Select EtherCAT adapter index: ";

    int selectedIndex = -1;
    std::cin >> selectedIndex;

    if (!std::cin ||
        selectedIndex < 0 ||
        selectedIndex >=
        static_cast<int>(
            adapters.size()))
    {
        std::cerr
            << "Invalid adapter index.\n";

        return 1;
    }

    const std::string adapterName =
        adapters[
            static_cast<std::size_t>(
                selectedIndex)].name;

    ecx_contextt context;

    std::memset(
        &context,
        0,
        sizeof(context));

    /*
     * SOEM의 자동 PRE-OP / SAFE-OP 요청을 막고
     * 프로그램에서 한 단계씩 처리합니다.
     */
    context.manualstatechange = 1;

    alignas(8)
        std::uint8_t ioMap[IO_MAP_SIZE] = {};

    std::cout
        << "\nOpening adapter:\n"
        << adapterName
        << "\n\n";

    if (ecx_init(
        &context,
        adapterName.c_str()) <= 0)
    {
        std::cerr
            << "ecx_init() failed.\n"
            << "Run Visual Studio as administrator.\n";

        return 1;
    }

    std::cout
        << "ecx_init() successful.\n"
        << "Manual state change mode: ON\n"
        << "Searching EtherCAT slaves...\n";

    const int detectedCount =
        ecx_config_init(&context);

    if (detectedCount <= 0)
    {
        std::cerr
            << "No EtherCAT slaves found.\n";

        ecx_close(&context);
        return 1;
    }

    std::cout
        << "Detected slave count: "
        << context.slavecount
        << "\n\n"
        << "State after slave discovery:\n";

    PrintAllSlaveStates(context);

    const int adcSlaveIndex =
        FindDaoAdcByIdentity(context);

    if (adcSlaveIndex <= 0)
    {
        std::cerr
            << "\nDAO ADC identity was not found.\n";

        ecx_close(&context);
        return 1;
    }

    ec_slavet& adcSlave =
        context.slavelist[adcSlaveIndex];

    std::cout
        << "\nDAO ADC identity detected.\n"
        << "EtherCAT Slave : "
        << adcSlaveIndex << "\n"
        << "Name           : "
        << adcSlave.name << "\n"
        << "Vendor ID      : 0x"
        << std::hex
        << std::uppercase
        << adcSlave.eep_man << "\n"
        << "Product Code   : 0x"
        << adcSlave.eep_id << "\n"
        << "Revision       : 0x"
        << adcSlave.eep_rev
        << std::dec
        << std::nouppercase
        << "\n";

    /*
     * 1단계: INIT → PRE-OP
     */
    if (!RequestStateOnceAndWait(
        context,
        adcSlaveIndex,
        EC_STATE_PRE_OP,
        PREOP_TIMEOUT_MS,
        false,
        nullptr))
    {
        std::cerr
            << "\nPRE-OP was not reached.\n";

        PrintSoemErrors(context);
        ecx_close(&context);
        return 1;
    }

    std::cout
        << "\nPreparing PDO mapping...\n";

    /*
     * 원래 mailbox protocol 정보를 저장합니다.
     */
    const std::uint16_t originalMbxProto =
        adcSlave.mbx_proto;

    const std::uint8_t originalCoeDetails =
        adcSlave.CoEdetails;

    std::cout
        << "Original Mailbox Protocol : 0x"
        << std::hex
        << std::uppercase
        << originalMbxProto << "\n"
        << "Original CoE Details      : 0x"
        << static_cast<unsigned int>(
            originalCoeDetails)
        << std::dec
        << std::nouppercase
        << "\n";

    if (FORCE_SII_ONLY_MAPPING)
    {
        /*
         * DAO ADC에 대해서만 CoE PDO 질의를 임시 차단합니다.
         *
         * mailbox 크기와 SM0/SM1 정보는 유지하므로
         * mailbox 자체를 제거하는 것은 아닙니다.
         */
        const std::uint16_t coeMask =
            static_cast<std::uint16_t>(
                ECT_MBXPROT_COE);

        adcSlave.mbx_proto =
            static_cast<std::uint16_t>(
                adcSlave.mbx_proto &
                static_cast<std::uint16_t>(
                    ~coeMask));

        adcSlave.CoEdetails = 0;

        std::cout
            << "PDO Mapping Mode          : "
            << "EEPROM/SII fixed mapping\n";
    }
    else
    {
        std::cout
            << "PDO Mapping Mode          : "
            << "SOEM default CoE mapping\n";
    }

    std::cout
        << "\nMapping process data...\n";

    const int mappedBytes =
        ecx_config_map_group(
            &context,
            ioMap,
            0);

    /*
     * PDO 매핑이 끝났으므로 SOEM 내부 장치 정보는
     * 원래 mailbox protocol 값으로 복구합니다.
     */
    adcSlave.mbx_proto =
        originalMbxProto;

    adcSlave.CoEdetails =
        originalCoeDetails;

    if (mappedBytes <= 0)
    {
        std::cerr
            << "PDO mapping failed.\n";

        PrintSoemErrors(context);
        ecx_close(&context);
        return 1;
    }

    ec_groupt& group =
        context.grouplist[0];

    const int expectedWkc =
        (group.outputsWKC * 2) +
        group.inputsWKC;

    std::cout
        << "PDO mapping successful.\n"
        << "Mapped IO bytes  : "
        << mappedBytes << "\n"
        << "Group Output WKC : "
        << group.outputsWKC << "\n"
        << "Group Input WKC  : "
        << group.inputsWKC << "\n"
        << "Expected WKC     : "
        << expectedWkc << "\n"
        << "ADC Output Bytes : "
        << adcSlave.Obytes << "\n"
        << "ADC Input Bytes  : "
        << adcSlave.Ibytes << "\n";

    if (!ValidateDaoAdcPdo(adcSlave))
    {
        std::cerr
            << "\nDAO ADC PDO size mismatch.\n"
            << "Expected Output="
            << DAO_ADC_OUTPUT_BYTES
            << ", Input="
            << DAO_ADC_INPUT_BYTES
            << "\n";

        ecx_close(&context);
        return 1;
    }

    ClearAdcOutput(adcSlave);

    /*
     * 2단계: PRE-OP → SAFE-OP
     *
     * 상태 명령은 한 번만 쓰고,
     * ESI의 9초 제한시간보다 넉넉한 12초 동안 기다립니다.
     */
    if (!RequestStateOnceAndWait(
        context,
        adcSlaveIndex,
        EC_STATE_SAFE_OP,
        SAFEOP_TIMEOUT_MS,
        false,
        &adcSlave))
    {
        std::cerr
            << "\nSAFE-OP was not reached.\n"
            << "Power-cycle the ADC board before "
            "the next test.\n";

        PrintSoemErrors(context);
        ecx_close(&context);
        return 1;
    }

    std::cout
        << "\nSAFE-OP reached.\n"
        << "Priming process data before OP...\n";

    for (int round = 1;
        round <= 10;
        ++round)
    {
        ClearAdcOutput(adcSlave);

        const int wkc =
            ExchangeProcessData(context);

        std::cout
            << "  Roundtrip "
            << std::setw(2)
            << round
            << " WKC="
            << wkc
            << "\n";

        std::this_thread::sleep_for(
            std::chrono::milliseconds(10));
    }

    /*
     * 3단계: SAFE-OP → OP
     *
     * OP 전환을 기다리는 동안 Process Data를 계속 교환합니다.
     */
    if (!RequestStateOnceAndWait(
        context,
        adcSlaveIndex,
        EC_STATE_OPERATIONAL,
        OP_TIMEOUT_MS,
        true,
        &adcSlave))
    {
        std::cerr
            << "\nOP was not reached.\n";

        PrintSoemErrors(context);
        ecx_close(&context);
        return 1;
    }

    std::cout
        << "\nOP reached successfully.\n"
        << "Starting ADC 2ms / 60-second validation.\n"
        << "Cycle Time       : "
        << TEST_CYCLE_MS << " ms\n"
        << "Test Time        : "
        << TEST_SECONDS << " sec\n"
        << "Target Frames    : "
        << TEST_CYCLE_COUNT << "\n"
        << "Samples / Frame  : "
        << ADC_SAMPLES_PER_FRAME << "\n"
        << "Target Samples   : "
        << static_cast<std::uint64_t>(
            TEST_CYCLE_COUNT) *
        ADC_SAMPLES_PER_FRAME
        << "\n\n";

    if (adcSlave.inputs == nullptr)
    {
        std::cerr
            << "ADC input pointer is null.\n";

        ecx_close(&context);
        return 1;
    }

    /*
     * 첫 입력 바이트는 시간 측정 시작 전에 한 번만 확인합니다.
     */
    PrintRawInputBytes(adcSlave);

    /*
     * 30,000개 프레임을 미리 확보합니다.
     * 시험 루프 안에서는 메모리 재할당이 발생하지 않습니다.
     */
    std::vector<CapturedAdcFrame> capturedFrames(
        static_cast<std::size_t>(
            TEST_CYCLE_COUNT));

    std::size_t capturedCount = 0;

    std::uint32_t previousCounter = 0;
    bool firstFrame = true;

    std::uint64_t goodWkcCount = 0;
    std::uint64_t badWkcCount = 0;

    std::uint64_t counterDeltaZeroCount = 0;
    std::uint64_t counterDeltaOneCount = 0;
    std::uint64_t counterDeltaGreaterThanOneCount = 0;
    std::uint64_t counterDeltaSum = 0;

    std::uint64_t periodSampleCount = 0;
    std::uint64_t periodSumUs = 0;
    std::int64_t minimumPeriodUs = 0;
    std::int64_t maximumPeriodUs = 0;
    std::uint64_t periodOver2500UsCount = 0;
    std::uint64_t periodOver3000UsCount = 0;
    std::uint64_t periodOver4000UsCount = 0;

    using TestClock =
        std::chrono::steady_clock;

    const auto testStart =
        TestClock::now();

    auto previousCycleStart =
        testStart;

    auto nextWakeup =
        testStart;

    std::cout
        << "Receiving 30,000 frames... "
        << "console output is paused for 60 seconds.\n";

    for (int cycle = 1;
        cycle <= TEST_CYCLE_COUNT;
        ++cycle)
    {
        nextWakeup +=
            std::chrono::milliseconds(
                TEST_CYCLE_MS);

        const auto cycleStart =
            TestClock::now();

        std::int64_t periodUs = 0;

        if (cycle > 1)
        {
            periodUs =
                std::chrono::duration_cast<
                std::chrono::microseconds>(
                    cycleStart -
                    previousCycleStart)
                .count();

            ++periodSampleCount;

            periodSumUs +=
                static_cast<std::uint64_t>(
                    periodUs);

            if (minimumPeriodUs == 0 ||
                periodUs < minimumPeriodUs)
            {
                minimumPeriodUs =
                    periodUs;
            }

            if (periodUs > maximumPeriodUs)
            {
                maximumPeriodUs =
                    periodUs;
            }

            if (periodUs > 2500)
            {
                ++periodOver2500UsCount;
            }

            if (periodUs > 3000)
            {
                ++periodOver3000UsCount;
            }

            if (periodUs > 4000)
            {
                ++periodOver4000UsCount;
            }
        }

        previousCycleStart =
            cycleStart;

        ClearAdcOutput(adcSlave);

        const int wkc =
            ExchangeProcessData(context);

        DaoAdcInputPdo frame{};

        std::memcpy(
            &frame,
            adcSlave.inputs,
            sizeof(frame));

        std::uint32_t counterDelta = 0;

        if (!firstFrame)
        {
            counterDelta =
                frame.testCounter -
                previousCounter;

            counterDeltaSum +=
                counterDelta;

            if (counterDelta == 0)
            {
                ++counterDeltaZeroCount;
            }
            else if (counterDelta == 1)
            {
                ++counterDeltaOneCount;
            }
            else
            {
                ++counterDeltaGreaterThanOneCount;
            }
        }

        previousCounter =
            frame.testCounter;

        firstFrame = false;

        if (wkc >= expectedWkc)
        {
            ++goodWkcCount;
        }
        else
        {
            ++badWkcCount;
        }

        CapturedAdcFrame& captured =
            capturedFrames[capturedCount];

        captured.cycle =
            cycle;

        captured.wkc =
            wkc;

        captured.elapsedUs =
            std::chrono::duration_cast<
            std::chrono::microseconds>(
                cycleStart -
                testStart)
            .count();

        captured.periodUs =
            periodUs;

        captured.counterDelta =
            counterDelta;

        captured.data =
            frame;

        ++capturedCount;

        /*
         * 절대시각 기준 2ms 대기입니다.
         * 처리시간이 조금 늦어져도 다음 목표시각을 누적 이동하지 않습니다.
         */
        std::this_thread::sleep_until(
            nextWakeup);
    }

    const auto testEnd =
        TestClock::now();

    const double actualTestSeconds =
        std::chrono::duration<double>(
            testEnd -
            testStart)
        .count();

    const double averagePeriodUs =
        periodSampleCount > 0
        ? static_cast<double>(
            periodSumUs) /
        static_cast<double>(
            periodSampleCount)
        : 0.0;

    const std::uint64_t receivedAdcSamples =
        goodWkcCount *
        ADC_SAMPLES_PER_FRAME;

    const double framesPerSecond =
        actualTestSeconds > 0.0
        ? static_cast<double>(
            goodWkcCount) /
        actualTestSeconds
        : 0.0;

    const double samplesPerSecond =
        actualTestSeconds > 0.0
        ? static_cast<double>(
            receivedAdcSamples) /
        actualTestSeconds
        : 0.0;

    std::cout
        << "\n========================================\n"
        << " ADC 2ms Validation Summary\n"
        << "========================================\n"
        << std::fixed
        << std::setprecision(3)
        << "Actual Test Time       : "
        << actualTestSeconds
        << " sec\n"
        << "Expected WKC           : "
        << expectedWkc << "\n"
        << "Captured Frames        : "
        << capturedCount << "\n"
        << "Good WKC Frames        : "
        << goodWkcCount << "\n"
        << "Bad WKC Frames         : "
        << badWkcCount << "\n"
        << "Frames / sec           : "
        << framesPerSecond << "\n"
        << "Received ADC Samples   : "
        << receivedAdcSamples << "\n"
        << "ADC Samples / sec      : "
        << samplesPerSecond << "\n\n"
        << "Counter Delta = 0      : "
        << counterDeltaZeroCount << "\n"
        << "Counter Delta = 1      : "
        << counterDeltaOneCount << "\n"
        << "Counter Delta > 1      : "
        << counterDeltaGreaterThanOneCount << "\n"
        << "Counter Delta Sum      : "
        << counterDeltaSum << "\n\n"
        << "Average Cycle Period   : "
        << averagePeriodUs << " us\n"
        << "Minimum Cycle Period   : "
        << minimumPeriodUs << " us\n"
        << "Maximum Cycle Period   : "
        << maximumPeriodUs << " us\n"
        << "Period > 2500 us       : "
        << periodOver2500UsCount << "\n"
        << "Period > 3000 us       : "
        << periodOver3000UsCount << "\n"
        << "Period > 4000 us       : "
        << periodOver4000UsCount << "\n\n"
        << std::defaultfloat;

    PrintAllSlaveStates(context);
    PrintSoemErrors(context);

    /*
     * 콘솔에 30,000줄을 출력하기 전에 EtherCAT부터 안전 종료합니다.
     */
    std::cout
        << "\nRequesting SAFE-OP before closing...\n";

    (void)RequestStateOnceAndWait(
        context,
        adcSlaveIndex,
        EC_STATE_SAFE_OP,
        3000,
        true,
        &adcSlave);

    std::cout
        << "\nRequesting INIT before closing...\n";

    (void)RequestStateOnceAndWait(
        context,
        adcSlaveIndex,
        EC_STATE_INIT,
        5000,
        false,
        nullptr);

    ecx_close(&context);

    std::cout
        << "\nEtherCAT adapter closed safely.\n";

    if (PRINT_ALL_FRAMES_AFTER_TEST)
    {
        std::cout
            << "\n========================================\n"
            << " All Captured ADC Frames\n"
            << "========================================\n"
            << std::left
            << std::setw(8) << "Cycle"
            << std::setw(12) << "TimeUs"
            << std::setw(10) << "PeriodUs"
            << std::setw(8) << "WKC"
            << std::setw(14) << "Counter"
            << std::setw(10) << "Delta"
            << std::setw(14) << "Raw0"
            << std::setw(14) << "Raw1"
            << std::setw(14) << "Raw2"
            << std::setw(14) << "Raw3"
            << "Status\n"
            << std::right;

        for (std::size_t i = 0;
            i < capturedCount;
            ++i)
        {
            const CapturedAdcFrame& captured =
                capturedFrames[i];

            std::cout
                << std::setw(8)
                << captured.cycle
                << std::setw(12)
                << captured.elapsedUs
                << std::setw(10)
                << captured.periodUs
                << std::setw(8)
                << captured.wkc
                << std::setw(14)
                << captured.data.testCounter
                << std::setw(10)
                << captured.counterDelta
                << std::setw(14)
                << captured.data.adcRaw0
                << std::setw(14)
                << captured.data.adcRaw1
                << std::setw(14)
                << captured.data.adcRaw2
                << std::setw(14)
                << captured.data.adcRaw3
                << "  0x"
                << std::hex
                << std::uppercase
                << std::setw(8)
                << std::setfill('0')
                << captured.data.status
                << std::dec
                << std::nouppercase
                << std::setfill(' ')
                << "\n";
        }
    }

    return 0;
}