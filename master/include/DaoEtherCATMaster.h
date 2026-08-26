#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <soem/soem.h>

struct DaoInternalSlaveInfo
{
    int listIndex;
    int physicalSlaveIndex;

    std::string name;

    unsigned int vendorId;
    unsigned int productCode;
    unsigned int revision;

    unsigned short state;
    unsigned short alStatusCode;
};

struct DaoInternalProcessDataMapInfo
{
    int mappedBytes;
    int outputWkc;
    int inputWkc;
    int expectedWkc;
};

struct DaoInternalSlavePdoInfo
{
    int listIndex;
    int physicalSlaveIndex;

    unsigned int outputBytes;
    unsigned int inputBytes;
};

// DAO ADC PDO 안전 검증 결과
struct DaoInternalAdcValidationInfo
{
    int physicalSlaveIndex = 0;

    bool processDataMapped = false;
    bool slaveIndexValid = false;
    bool identityValid = false;

    bool outputSizeValid = false;
    bool inputSizeValid = false;

    bool outputPointerValid = false;
    bool inputPointerValid = false;

    unsigned int actualOutputBytes = 0;
    unsigned int actualInputBytes = 0;
};

// SAFE-OP 상태에서 수행한
// 단일 Process Data 왕복 결과
struct DaoInternalProcessExchangeInfo
{
    int physicalSlaveIndex = 0;

    int actualWkc = 0;
    int expectedWkc = 0;

    bool safeOpStateValid = false;
    bool adcValidationPassed = false;
    bool outputCleared = false;
    bool wkcValid = false;
};

// SAFE-OP 상태에서 수행한 Process Data Priming 결과
struct DaoInternalPrimingInfo
{
    int physicalSlaveIndex = 0;

    int requestedRounds = 0;
    int completedRounds = 0;

    int expectedWkc = 0;
    int minimumWkc = 0;
    int maximumWkc = 0;
    int lastWkc = 0;

    int goodWkcCount = 0;
    int badWkcCount = 0;

    bool safeOpStateValid = false;
    bool adcValidationPassed = false;
    bool allRoundsValid = false;
};

// SAFE-OP에서 OP 상태로 전환한 결과
struct DaoInternalOperationalInfo
{
    int physicalSlaveIndex = 0;

    int expectedWkc = 0;
    int lastWkc = 0;

    int exchangeCount = 0;
    int goodWkcCount = 0;
    int badWkcCount = 0;

    bool adcValidationPassed = false;
    bool safeOpStateValid = false;
    bool primingPassed = false;

    bool stateWriteSucceeded = false;
    bool operationalReached = false;

    unsigned short finalState = 0;
    unsigned short alStatusCode = 0;
};


#pragma pack(push, 1)

// DAO ADC의 검증된 24바이트 Input PDO
struct DaoInternalAdcInputPdo
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
    sizeof(DaoInternalAdcInputPdo) == 24,
    "DaoInternalAdcInputPdo must be exactly 24 bytes.");


#pragma pack(push, 1)

// ------------------------------------------------------------
// LS Mecapion L7NH RxPDO 0x1601
// Master → Servo
//
// 0x6040 : Controlword
// 0x6060 : Modes of Operation
// 0x607A : Target Position
// 0x6081 : Profile Velocity
// 0x6083 : Profile Acceleration
// 0x6084 : Profile Deceleration
// 0x60FF : Target Velocity
// 0x60B8 : Touch Probe Function
// 0x60FE:01 : Digital Outputs
//
// Total: 29 bytes
// ------------------------------------------------------------
struct DaoInternalLsServoOutputPdo
{
    unsigned short controlWord;        // 0x6040 : 2 bytes
    signed char operationMode;         // 0x6060 : 1 byte

    int targetPosition;                // 0x607A : 4 bytes

    unsigned int profileVelocity;      // 0x6081 : 4 bytes
    unsigned int profileAcceleration;  // 0x6083 : 4 bytes
    unsigned int profileDeceleration;  // 0x6084 : 4 bytes

    int targetVelocity;                // 0x60FF : 4 bytes

    unsigned short touchProbeFunction; // 0x60B8 : 2 bytes
    unsigned int digitalOutputs;       // 0x60FE:01 : 4 bytes
};


// ------------------------------------------------------------
// LS Mecapion L7NH TxPDO 0x1A01
// Servo → Master
//
// 0x6041 : Statusword
// 0x6061 : Modes of Operation Display
// 0x6064 : Position Actual Value
// 0x60F4 : Following Error Actual Value
// 0x60B9 : Touch Probe Status
// 0x60BA : Touch Probe Position
// 0x60FD : Digital Inputs
//
// Total: 21 bytes
// ------------------------------------------------------------
struct DaoInternalLsServoInputPdo
{
    unsigned short statusWord;         // 0x6041 : 2 bytes
    signed char operationModeDisplay;  // 0x6061 : 1 byte

    int actualPosition;                // 0x6064 : 4 bytes
    int positionError;                 // 0x60F4 : 4 bytes

    unsigned short touchProbeStatus;   // 0x60B9 : 2 bytes
    int touchProbePosition;            // 0x60BA : 4 bytes
    unsigned int digitalInputs;        // 0x60FD : 4 bytes
};

// ------------------------------------------------------------
// FASTECH Ezi-IO IN8OUT8
//
// Output: 1 byte
// Input : 1 byte
// ------------------------------------------------------------
struct DaoInternalFastechIo8OutputPdo
{
    unsigned char outputs;
};

struct DaoInternalFastechIo8InputPdo
{
    unsigned char inputs;
};

// ------------------------------------------------------------
// FASTECH Ezi-IO IN16OUT16
//
// Output: 2 bytes
// Input : 2 bytes
// ------------------------------------------------------------
struct DaoInternalFastechIo16OutputPdo
{
    unsigned short outputs;
};

struct DaoInternalFastechIo16InputPdo
{
    unsigned short inputs;
};

#pragma pack(pop)
static_assert(
    sizeof(DaoInternalLsServoOutputPdo) == 29,
    "LS Servo Output PDO size must be 29 bytes.");

static_assert(
    sizeof(DaoInternalLsServoInputPdo) == 21,
    "LS Servo Input PDO size must be 21 bytes.");

static_assert(
    sizeof(DaoInternalFastechIo8OutputPdo) == 1,
    "FASTECH IO8 Output PDO size must be 1 byte.");

static_assert(
    sizeof(DaoInternalFastechIo8InputPdo) == 1,
    "FASTECH IO8 Input PDO size must be 1 byte.");

static_assert(
    sizeof(DaoInternalFastechIo16OutputPdo) == 2,
    "FASTECH IO16 Output PDO size must be 2 bytes.");

static_assert(
    sizeof(DaoInternalFastechIo16InputPdo) == 2,
    "FASTECH IO16 Input PDO size must be 2 bytes.");



// ------------------------------------------------------------
// Servo Command Type
//
// 장비의 시험 목적이 아니라
// Servo 축 자체가 수행 중인 명령 종류만 표현합니다.
// ------------------------------------------------------------
enum DaoInternalServoCommandType
{
    DAO_SERVO_COMMAND_NONE = 0,

    DAO_SERVO_COMMAND_SERVO_ON = 1,
    DAO_SERVO_COMMAND_SERVO_OFF = 2,

    DAO_SERVO_COMMAND_MOVE_ABSOLUTE = 3,
    DAO_SERVO_COMMAND_MOVE_RELATIVE = 4,

    DAO_SERVO_COMMAND_JOG_POSITIVE = 5,
    DAO_SERVO_COMMAND_JOG_NEGATIVE = 6,

    DAO_SERVO_COMMAND_HOMING = 7,

    DAO_SERVO_COMMAND_STOP = 8,
    DAO_SERVO_COMMAND_QUICK_STOP = 9,

    DAO_SERVO_COMMAND_ALARM_RESET = 10,
	DAO_SERVO_COMMAND_VELOCITY = 11 // 속도제어모드 운전
};


// ------------------------------------------------------------
// Servo Command State
//
// UI는 이 상태를 주기적으로 조회하여
// 다음 장비 시퀀스를 결정합니다.
// ------------------------------------------------------------
enum DaoInternalServoCommandState
{
    DAO_SERVO_COMMAND_STATE_IDLE = 0,

    DAO_SERVO_COMMAND_STATE_ACCEPTED = 1,
    DAO_SERVO_COMMAND_STATE_RUNNING = 2,

    DAO_SERVO_COMMAND_STATE_COMPLETED = 3,
    DAO_SERVO_COMMAND_STATE_STOPPED = 4,

    DAO_SERVO_COMMAND_STATE_ERROR = 5,
    DAO_SERVO_COMMAND_STATE_TIMEOUT = 6
};

// ------------------------------------------------------------
// Servo Internal Command Step
//
// commandState는 외부 UI가 보는 큰 상태이고,
// commandStep은 엔진 내부에서 명령을 진행하기 위한
// 세부 단계입니다.
// ------------------------------------------------------------
enum DaoInternalServoCommandStep
{
    DAO_SERVO_STEP_NONE = 0,

    // Servo ON
    DAO_SERVO_STEP_SERVO_ON = 10,

    // Servo OFF
    DAO_SERVO_STEP_SERVO_OFF = 20,

    // Homing
    DAO_SERVO_STEP_HOMING_PREPARE = 100,
    DAO_SERVO_STEP_HOMING_MODE_REQUEST = 110,
    DAO_SERVO_STEP_HOMING_SERVO_ON = 120,
    DAO_SERVO_STEP_HOMING_START = 130,
    DAO_SERVO_STEP_HOMING_RUNNING = 140,
    DAO_SERVO_STEP_HOMING_FINISH = 150,
    DAO_SERVO_STEP_HOMING_RESTORE_MODE = 160,

    // Move Absolute
    DAO_SERVO_STEP_MOVE_ABS_PREPARE = 200,
    DAO_SERVO_STEP_MOVE_ABS_MODE_REQUEST = 210,
    DAO_SERVO_STEP_MOVE_ABS_SERVO_ON = 220,
    DAO_SERVO_STEP_MOVE_ABS_START = 230,
    DAO_SERVO_STEP_MOVE_ABS_RUNNING = 240,
    DAO_SERVO_STEP_MOVE_ABS_FINISH = 250,
    // Profile Velocity
    DAO_SERVO_STEP_VELOCITY_PREPARE = 300,
    DAO_SERVO_STEP_VELOCITY_MODE_REQUEST = 310,
    DAO_SERVO_STEP_VELOCITY_SERVO_ON = 320,
    DAO_SERVO_STEP_VELOCITY_RUNNING = 330,
    DAO_SERVO_STEP_VELOCITY_STOPPING = 340,
    // Common Servo Stop
    DAO_SERVO_STEP_STOP_PREPARE = 400,
    DAO_SERVO_STEP_STOPPING = 410,
    DAO_SERVO_STEP_STOP_FINISH = 420

};

// ------------------------------------------------------------
// Servo Mailbox Request
//
// EtherCAT 순환 PDO와 별도로 처리해야 하는
// SDO 작업의 요청 상태만 보관합니다.
// 이번 단계에서는 실제 SDO를 전송하지 않습니다.
// ------------------------------------------------------------
enum DaoInternalServoMailboxRequestType
{
    DAO_SERVO_MAILBOX_NONE = 0,

    DAO_SERVO_MAILBOX_SET_OPERATION_MODE = 1
};

enum DaoInternalServoMailboxRequestState
{
    DAO_SERVO_MAILBOX_STATE_IDLE = 0,

    DAO_SERVO_MAILBOX_STATE_PENDING = 1,
    DAO_SERVO_MAILBOX_STATE_PROCESSING = 2,

    DAO_SERVO_MAILBOX_STATE_COMPLETED = 3,
    DAO_SERVO_MAILBOX_STATE_ERROR = 4
};

// ------------------------------------------------------------
// LS Servo Runtime Information
//
// 물리 Slave별로:
// - 현재 송신할 명령 PDO
// - 최근 수신한 상태 PDO
// - 통신 상태와 WKC 통계
// 를 보관합니다.
// ------------------------------------------------------------
struct DaoInternalServoRuntimeInfo
{
    
    int physicalSlaveIndex = 0;
    bool configured = false;
    bool communicationRunning = false;
    bool hasValidInputData = false;

    // CiA402 StatusWord 해석 결과
    unsigned short cia402State = 0;

    bool fault = false;
    bool operationEnabled = false;
    bool targetReached = false;

    // --------------------------------------------------------
    // LS L7NH 0x60FD Digital Inputs 해석 상태
    // --------------------------------------------------------
    bool negativeLimit = false;   // bit 0 : NOT
    bool positiveLimit = false;   // bit 1 : POT
    bool homeSensor = false;      // bit 2 : HOME
	bool stopInput = false;       // bit 19 : STOP
    bool stoActive = false;       // bit 31 : STO



    std::uint64_t commandStartFrameCount = 0; 

    // Homing 실제 시작 시점과 외부에서 받은 제한시간
    std::uint64_t homingStartFrameCount = 0;
    unsigned int homingTimeoutMs = 60000;


    // Move Absolute 명령 파라미터
    int moveTargetPosition = 0;

    unsigned int moveProfileVelocity = 1000;
    unsigned int moveProfileAcceleration = 1000;
    unsigned int moveProfileDeceleration = 1000;

    unsigned int moveTimeoutMs = 60000;

    // --------------------------------------------------------
    // Profile Velocity 명령 파라미터 속도제어모드입니다.
    // --------------------------------------------------------
    int velocityTarget = 0;

    unsigned int velocityAcceleration = 1000;
    unsigned int velocityDeceleration = 1000;

    // MoveAbs 실제 이동 시작 시점의 위치
    int moveStartPosition = 0;

    // MoveAbs 실제 이동 시작 Frame
    std::uint64_t moveStartFrameCount = 0;

    // MoveAbs 시작 후 Target Reached가
    // 실제로 한번 OFF 된 것을 확인했는지 표시
    bool moveTargetReachedWentLow = false;

    // Homing 중 실제 위치 변화 감시용
    int homingLastPosition = 0;

    // 마지막으로 위치 변화가 확인된 Frame
    std::uint64_t homingLastMoveFrameCount = 0;

    // 위치 감시가 시작되었는지 표시
    bool homingPositionMonitorStarted = false;
	// Homing Attained가 실제로 한번 OFF 된 것을 확인했는지 표시
	bool homingAttainedWentLow = false; // Homing Attained가 실제로 한번 OFF 된 것을 확인했는지 표시


    // 현재 Servo 축이 수행 중인 엔진 명령 상태
    std::uint64_t commandId = 0;

    int commandType =
        DAO_SERVO_COMMAND_NONE;

    int commandState =
        DAO_SERVO_COMMAND_STATE_IDLE;

    int commandStep =
		DAO_SERVO_STEP_NONE; // 엔진 내부에서 명령을 진행하기 위한 세부 단계

    int commandResult = 0;


    int mailboxRequestType =
        DAO_SERVO_MAILBOX_NONE;

    int mailboxRequestState =
        DAO_SERVO_MAILBOX_STATE_IDLE;

    signed char requestedOperationMode = 0;

    int mailboxResult = 0;

    // Homing이 정상 완료된 적이 있는지 표시
    bool homed = false;

    int lastWkc = 0;
    int expectedWkc = 0;

    uint64_t totalFrameCount = 0;
    uint64_t goodWkcFrameCount = 0;
    uint64_t badWkcFrameCount = 0;
    uint64_t inputUpdateCount = 0;

    // UI 또는 공개 DLL 명령이 수정할 출력 PDO 원본
    DaoInternalLsServoOutputPdo outputCommand{};

    // 통신 스레드가 최근 수신한 입력 PDO
    DaoInternalLsServoInputPdo latestInput{};
};


// ------------------------------------------------------------
// FASTECH IO Runtime Information
//
// IN8OUT8과 IN16OUT16을 공통 구조로 처리하기 위해
// 입력과 출력값은 최대 16비트로 저장합니다.
// 실제 PDO 크기는 inputBytes/outputBytes로 구분합니다.
// ------------------------------------------------------------
struct DaoInternalIoRuntimeInfo
{
    int physicalSlaveIndex = 0;
    bool configured = false;
    int inputBytes = 0;
    int outputBytes = 0;

    bool communicationRunning = false;
    bool hasValidInputData = false;

    int lastWkc = 0;
    int expectedWkc = 0;

    uint64_t totalFrameCount = 0;
    uint64_t goodWkcFrameCount = 0;
    uint64_t badWkcFrameCount = 0;
    uint64_t inputUpdateCount = 0;

    // 외부에서 요청한 출력값
    unsigned short outputCommand = 0;

    // 통신 스레드가 최근 수신한 입력값
    unsigned short latestInput = 0;
};

// OP 상태에서 수행한 DAO ADC 1회 읽기 결과
struct DaoInternalAdcReadInfo
{
    int physicalSlaveIndex = 0;

    int actualWkc = 0;
    int expectedWkc = 0;

    bool operationalStateValid = false;
    bool adcValidationPassed = false;
    bool outputCleared = false;
    bool wkcValid = false;
    bool inputCopied = false;

    DaoInternalAdcInputPdo data{};
};

enum class DaoInternalAdcPowerLineFilterMode
{
    OFF = 0,

    HZ_50 = 1,
    HZ_60 = 2,
    HZ_120 = 3,

    HZ_50_60 = 4,
    HZ_60_120 = 5
};

enum class DaoInternalAdcStableCaptureType // 안정 평균 취득용
{
    NONE = 0,
    ZERO = 1,
    CALIBRATION = 2
};

struct DaoInternalAdcDiagnosticSample
{
    std::uint64_t sampleIndex = 0;

    std::int32_t rawValue = 0;

    double lowLevelFiltered = 0.0;
    double powerLineFiltered = 0.0;

    double zeroedValue = 0.0;
    double calibratedValue = 0.0;

    double medianFilteredValue = 0.0;
    double filteredValue = 0.0;
};

//링버퍼를 위한 구조체 선언========================================
struct DaoInternalAdcBufferedSample
{
    std::uint64_t sampleIndex = 0;

    double filteredValue = 0.0;
};
// ------------------------------------------------------------
// ADC Signal Processing State
//
// 하나의 논리 ADC 측정계통이 사용하는
// 필터 / Zero / Calibration 상태를 보관합니다.
// ------------------------------------------------------------
struct DaoInternalAdcProcessingState
{
    // 가장 최근 ADC 원시값
    std::int32_t latestRaw = 0;

    // 저수준 기본 Filter 처리 후 값
    double lowLevelFiltered = 0.0;

    // 저수준 Filter가 첫 샘플로 초기화되었는지 표시
    bool lowLevelFilterInitialized = false;


    // ------------------------------------------------------------
    // Power Line Notch Filter
    //
    // OFF      : 사용 안 함
    // HZ_50    : 50Hz 제거
    // HZ_60    : 60Hz 제거
    // HZ_50_60 : 50Hz + 60Hz 제거
    // ------------------------------------------------------------
    DaoInternalAdcPowerLineFilterMode powerLineFilterMode =
        DaoInternalAdcPowerLineFilterMode::OFF;

    // 50Hz Notch 내부 상태
    double notch50X1 = 0.0;
    double notch50X2 = 0.0;
    double notch50Y1 = 0.0;
    double notch50Y2 = 0.0;

    // 60Hz Notch 내부 상태
    double notch60X1 = 0.0;
    double notch60X2 = 0.0;
    double notch60Y1 = 0.0;
    double notch60Y2 = 0.0;

    // 120Hz Notch 내부 상태
    double notch120X1 = 0.0;
    double notch120X2 = 0.0;
    double notch120Y1 = 0.0;
    double notch120Y2 = 0.0;

    // Notch 적용 후 값
    double powerLineFiltered = 0.0;

    // Zero Offset
    double zeroOffset = 0.0;

    // Zero가 사용자 요청으로 설정되었는지 표시
    bool zeroInitialized = false;

    // Zero 적용 후 값
    double zeroedValue = 0.0;



    // 사용자 Calibration Scale
    double calibrationScale = 1.0;

    // Calibration 적용 후 값
    double calibratedValue = 0.0;

    // ------------------------------------------------------------
    // 3-Sample Median Filter
    //
    // 순간적으로 한 Sample만 크게 튀는 값을 억제합니다.
    // Calibration 적용 후, N Moving Average 전에 사용합니다.
    // ------------------------------------------------------------
    double medianBuffer[3] = { 0.0, 0.0, 0.0 };

    unsigned int medianIndex = 0;
    unsigned int medianCount = 0;

    // Median 3 적용 후 값
    double medianFilteredValue = 0.0;

    // Zero / Calibration 안정 평균 취득용
    bool stableCaptureActive = false;






    DaoInternalAdcStableCaptureType stableCaptureType =
        DaoInternalAdcStableCaptureType::NONE;

    double stableCaptureReferenceValue = 0.0;

    unsigned int stableCaptureWaitSamples = 0;
    unsigned int stableCaptureSampleCount = 0;
    unsigned int stableCaptureCollectedCount = 0;
    double stableCaptureSum = 0.0;

    // 사용자 N Sample Moving Average
    unsigned int filterN = 16;

    static constexpr unsigned int USER_FILTER_MAX_N = 64;

    std::array<double, USER_FILTER_MAX_N>
        userFilterBuffer{};

    unsigned int userFilterIndex = 0;
    unsigned int userFilterCount = 0;

    double userFilterSum = 0.0;

    // 최종 사용자 표시값
    double filteredValue = 0.0;
};

// 2ms 순환통신에서 유지할 최신 DAO ADC 상태
struct DaoInternalAdcRuntimeInfo
{
    int physicalSlaveIndex = 0;

    bool communicationRunning = false;
    bool hasValidData = false;

    int lastWkc = 0;
    int expectedWkc = 0;

    std::uint64_t totalFrameCount = 0;
    std::uint64_t goodWkcFrameCount = 0;
    std::uint64_t badWkcFrameCount = 0;
    std::uint64_t dataUpdateCount = 0;

    DaoInternalAdcInputPdo latestData{};
    DaoInternalAdcProcessingState processing{};


    // ------------------------------------------------------------
    // ADC Diagnostic Capture
    //
    // Noise 분석용 연속 Sample 저장 상태입니다.
    // 실제 저장은 ProcessAdcSample()에서 수행합니다.
    // ------------------------------------------------------------
    bool diagnosticCaptureActive = false;

    std::uint64_t diagnosticSampleIndex = 0;

    unsigned int diagnosticTargetSampleCount = 0;

    std::vector<DaoInternalAdcDiagnosticSample>
        diagnosticSamples;

    // ------------------------------------------------------------
    // ADC Runtime Ring Buffer
    //
    // 실제 UI / 저장용 최종 FilteredValue Sample을
    // 순환 방식으로 보관합니다.
    // ------------------------------------------------------------
    static constexpr std::size_t ADC_RING_BUFFER_SIZE = 8192;

    std::array<DaoInternalAdcBufferedSample,
        ADC_RING_BUFFER_SIZE>
        ringBuffer{};

    std::size_t ringBufferHead = 0;
    std::size_t ringBufferTail = 0;
    std::size_t ringBufferCount = 0;

    std::uint64_t ringBufferNextSampleIndex = 0;

    // Buffer가 가득 찬 상태에서 새 Sample이 들어와
    // 가장 오래된 Sample을 덮어쓴 횟수입니다.
    std::uint64_t ringBufferOverflowCount = 0;

};


class DaoEtherCATMaster 
{
public:
    DaoEtherCATMaster();
    ~DaoEtherCATMaster();

    bool Open(const std::string& adapterName);
    void Close();

    bool IsOpen() const;

    int ScanSlaves();
    int GetSlaveCount() const;

    bool GetSlaveInfo(
        int slaveListIndex,
        DaoInternalSlaveInfo& slaveInfo) const;
    // 검색된 모든 Slave를 PRE-OP 상태로 전환합니다.
	bool RequestAllSlavesPreOp(); // 검색된 모든 Slave를 PRE-OP 상태로 전환합니다.

    bool RequestAllSlavesSafeOp(); // 검색된 모든 Slave를 SAFE-OP 상태로 전환합니다.

	bool RequestAllSlavesOperational();// 검색된 모든 Slave를 OP 상태로 전환합니다.

    bool RequestAllSlavesInit(); // 검색된 모든 Slave를 INIT 상태로 전환합니다.

    bool MapProcessData();

    bool GetProcessDataMapInfo(
        DaoInternalProcessDataMapInfo& mapInfo) const;

    bool GetSlavePdoInfo(
        int slaveListIndex,
        DaoInternalSlavePdoInfo& pdoInfo) const;

    // DAO ADC PDO를 읽거나 쓰기 전에 안전 조건만 검사합니다.

    bool ValidateDaoAdcPdo(
        int physicalSlaveIndex,
        DaoInternalAdcValidationInfo& validationInfo) const;

    // 검증된 DAO ADC Slave를 SAFE-OP 상태로 전환합니다.
    bool RequestDaoAdcSafeOp(
        int physicalSlaveIndex);

    // 검증된 DAO ADC를 대상으로 SAFE-OP 상태에서
    // Process Data를 정확히 한 번만 왕복합니다.
    bool ExchangeDaoAdcProcessDataOnce(
        int physicalSlaveIndex,
        DaoInternalProcessExchangeInfo& exchangeInfo);

    // 검증된 DAO ADC를 대상으로 SAFE-OP 상태에서
    // 지정한 횟수만큼 Process Data Priming을 수행합니다.
    bool PrimeDaoAdcProcessData(
        int physicalSlaveIndex,
        int roundCount,
        DaoInternalPrimingInfo& primingInfo);

    // 검증된 DAO ADC를 SAFE-OP에서 OP 상태로 전환합니다.
    //
    // 상태 요청은 한 번만 보내고,
    // OP 전환을 기다리는 동안 Process Data를 계속 교환합니다.
    bool RequestDaoAdcOperational(
        int physicalSlaveIndex,
        DaoInternalOperationalInfo& operationalInfo);

    // 검증된 DAO ADC를 대상으로 OP 상태에서
    // Process Data를 1회 왕복하고 Input PDO 24바이트를 복사합니다.
    bool ReadDaoAdcOnce(
        int physicalSlaveIndex,
        DaoInternalAdcReadInfo& readInfo);

    // 현재 보관 중인 DAO ADC 최신 상태를 반환합니다.
    bool GetDaoAdcRuntimeInfo(
        int physicalSlaveIndex,
        DaoInternalAdcRuntimeInfo& runtimeInfo) const;

    bool SetDaoAdcZero(
		int physicalSlaveIndex); // DAO ADC의 Zero Offset을 현재 샘플로 설정합니다.

    bool SetDaoAdcCalibration(
        int physicalSlaveIndex,
		double referenceValue); // DAO ADC의 Calibration Scale을 설정합니다.

    bool SetDaoAdcPowerLineFilterMode(
        int physicalSlaveIndex,
		DaoInternalAdcPowerLineFilterMode mode); // DAO ADC의 전원주파수 필터 모드를 설정합니다.

    bool SetDaoAdcFilterN(
        int physicalSlaveIndex,
        unsigned int filterN); //ui에서 필터값을 받아서 처리할함수선언

    bool StartDaoAdcDiagnosticCapture(
        int physicalSlaveIndex,
        unsigned int targetSampleCount); // DAO ADC 테스트수집

    bool GetDaoAdcRingBufferInfo(
        int physicalSlaveIndex,
        unsigned int& sampleCount,
        unsigned long long& overflowCount) const; //현재버퍼에 몇개 쌓았는지 오버플로생긴것을 확인하는 함수 선언

    bool ReadDaoAdcRingBuffer(
        int physicalSlaveIndex,
        DaoInternalAdcBufferedSample* samples,
        unsigned int maxSampleCount,
        unsigned int& readSampleCount); //

    bool ClearDaoAdcRingBuffer(
        int physicalSlaveIndex); //링버퍼 클리어함수 선언


    bool GetDaoAdcDiagnosticCaptureInfo(
        int physicalSlaveIndex,
        bool& captureActive,
        unsigned int& capturedSampleCount,
        unsigned int& targetSampleCount) const;

    bool GetDaoAdcDiagnosticSample(
        int physicalSlaveIndex,
        unsigned int sampleIndex,
        DaoInternalAdcDiagnosticSample& sample) const;

    // 현재 보관 중인 LS Servo 최신 상태를 반환합니다.
    bool GetServoRuntimeInfo(
        int physicalSlaveIndex,
        DaoInternalServoRuntimeInfo& runtimeInfo) const;

    // 현재 보관 중인 FASTECH IO 최신 상태를 반환합니다.
    bool GetIoRuntimeInfo(
        int physicalSlaveIndex,
        DaoInternalIoRuntimeInfo& runtimeInfo) const;

    bool RequestServoOn(
		int physicalSlaveIndex); // LS Servo를 On 상태로 전환합니다. 비동기

    bool RequestServoOff(
		int physicalSlaveIndex); // LS Servo를 Off 상태로 전환합니다. 비동기

    bool RequestServoHome(
        int physicalSlaveIndex,
		unsigned int timeoutMs); // LS Servo를 Homing 상태로 전환합니다. 비동기 외부시간 적용

    bool RequestServoMoveAbsolute(
        int physicalSlaveIndex,
        int targetPosition,
        unsigned int profileVelocity,
        unsigned int profileAcceleration,
        unsigned int profileDeceleration,
		unsigned int timeoutMs);  // LS Servo를 절대 위치로 이동합니다. 비동기 외부시간 적용

    bool RequestServoVelocity(
        int physicalSlaveIndex,
        int targetVelocity,
        unsigned int acceleration,
		unsigned int deceleration); // LS Servo를 속도제어모드로 운전합니다. 비동기

    bool RequestServoStop(
		int physicalSlaveIndex); // LS Servo를 정지합니다. 비동기

    // LS Servo에 송신할 Output PDO 명령을 저장합니다.
    bool SetServoOutputCommand(
        int physicalSlaveIndex,
        const DaoInternalLsServoOutputPdo& command);

    
    
    bool BeginServoCommand(
        int physicalSlaveIndex,
        int commandType);

    bool UpdateServoCommandState(
        int physicalSlaveIndex,
        int commandState,
        int commandResult);



    // FASTECH IO에 송신할 출력값을 저장합니다.
    bool SetIoOutputCommand(
        int physicalSlaveIndex,
        unsigned short outputValue);
    
	void StopCommunication(); // 통신 스레드 종료 요청

	bool IsCommunicationRunning() const; // 통신 스레드가 실행 중인지 확인

	bool StartCommunication();// 통신 스레드 시작

    // 반드시 PRE-OP 상태에서 호출합니다.
    bool ConfigureLsL7nhProfilePositionMode(
        int physicalSlaveIndex,
        unsigned int profileVelocity,
        unsigned int profileAcceleration,
        unsigned int profileDeceleration);

    // LS L7NH의 CiA402 운전모드 0x6060을 설정합니다.
    //
    // mode:
    // 1 = Profile Position
    // 3 = Profile Velocity
    // 6 = Homing
    bool ConfigureLsL7nhOperationMode(
        int physicalSlaveIndex,
        signed char mode);


	

private:
   
    static constexpr std::size_t IO_MAP_SIZE = 4096;

    struct SavedCoeInfo
    {
        int physicalSlaveIndex = 0;
        std::uint16_t mailboxProtocol = 0;
        std::uint8_t coeDetails = 0;
    };
    void ResetProcessDataMap();
    void ResetAdcRuntimeInfo();
    void ResetServoRuntimeInfo();
    void ResetIoRuntimeInfo();
 

	void ConfigureServoAndIoRuntimeInfo(); // Servo와 EtherCAT IO의 런타임 정보를 초기화합니다.

	void PrepareServoAndIoOutputs(); // Servo와 EtherCAT IO의 출력 PDO를 초기화합니다.

    void CaptureServoAndIoInputs(
		int actualWkc);   // Servo와 EtherCAT IO의 입력 PDO를 캡처합니다.

	void ProcessServoCommands(); // Servo 명령 상태를 갱신하고, 완료된 명령을 정리합니다.

    void UpdateServoDerivedState(
		DaoInternalServoRuntimeInfo& runtimeInfo); // Servo의 CiA402 상태를 해석하여 derived state를 갱신합니다.

    void ProcessAdcSample(
        DaoInternalAdcRuntimeInfo& runtimeInfo,
		std::int32_t rawSample); // DAO ADC 원시 샘플을 처리하여 필터/Zero/Calibration을 적용하고 최종 표시값을 갱신합니다.

    double ApplyAdcNotchFilter(
        double inputValue,
        double sampleRateHz,
        double notchFrequencyHz,
        double& x1,
        double& x2,
        double& y1,
		double& y2); // Notch Filter를 적용합니다.

    bool IsDaoAdcSlave(
		const ec_slavet& slave) const; // DAO ADC Slave인지 확인합니다.

    bool IsLsL7nhServo(
		int physicalSlaveIndex) const; // LS L7NH Servo인지 확인합니다.
   
    bool ConfigureLsL7nhBasicPdo(
        int physicalSlaveIndex);

    // LS L7NH Profile Position 기본 운전모드와
    // 속도/가속/감속을 SDO로 설정합니다.
    //
  



    bool IsFastechIo(
		int physicalSlaveIndex) const; // Fastech EtherCAT IO인지 확인합니다.

    // EtherCAT cyclic communication thread
    std::thread communicationThread_;

    std::atomic<bool> communicationRunning_{ false };
    std::atomic<bool> communicationStopRequested_{ false };

	void CommunicationThreadMain(); // 통신 스레드 메인 루프


private:
  
    ecx_contextt context_;
    bool isOpen_;
    int slaveCount_;
    alignas(8)
    std::array<std::uint8_t, IO_MAP_SIZE> ioMap_;

    int mappedBytes_;
    int outputWkc_;
    int inputWkc_;
    int expectedWkc_;
    bool processDataMapped_;

    // 물리 Slave 번호를 인덱스로 사용합니다.
    // 0번은 전체 Slave용이므로 사용하지 않고,
    // 실제 Slave 1번부터 저장합니다.
    std::vector<DaoInternalAdcRuntimeInfo>
        adcRuntimeInfoBySlave_;
    // 물리 Slave 번호를 그대로 인덱스로 사용합니다.
// index 0은 EtherCAT에서 사용하지 않습니다.
    std::vector<DaoInternalServoRuntimeInfo>
        servoRuntimeInfoBySlave_;

    std::vector<DaoInternalIoRuntimeInfo>
        ioRuntimeInfoBySlave_;

	std::atomic<std::uint64_t> nextServoCommandId_{ 1 }; // Servo 명령 ID를 순차적으로 생성합니다.

    // Protects ADC runtime information accessed by
    // the communication thread and external API calls.
    mutable std::mutex adcRuntimeMutex_;

    // Protects Servo runtime information accessed by
    // the communication thread and external API calls.
    mutable std::mutex servoRuntimeMutex_;
    mutable std::mutex ioRuntimeMutex_;

};

