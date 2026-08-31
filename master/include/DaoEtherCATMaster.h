#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
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

// 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
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

// 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
// Process Data를 송신하고 수신 WKC를 확인합니다.
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

// 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
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

// 정상 통신 여부를 판단하기 위한 예상 WKC 값입니다.
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

// PDO 구조체는 EtherCAT 매핑 크기와 일치하도록 바이트 단위로 정렬합니다.
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
// 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
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
// 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
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


// ------------------------------------------------------------
// FASTECH Ezi-IO EtherCAT CNT02
//
// RxPDO : Master -> CNT02
//
// 0x1600 : Counter Command                  8 bit
// 0x1601 : Latch / External Reset Command 32 bit
// 0x1602 : Preset Value CH1 / CH2          64 bit
//
// Total : 13 bytes
// ------------------------------------------------------------
struct DaoInternalFastechEncoderOutputPdo
{
    std::uint8_t counterCommand;          // 3030h : 8 bits
    std::uint32_t latchResetCommand;      // 3031h : 32 bits

    std::uint32_t presetValueCh1;         // 3032:01
    std::uint32_t presetValueCh2;         // 3032:02
};


// ------------------------------------------------------------
// FASTECH Ezi-IO EtherCAT CNT02
//
// TxPDO : CNT02 -> Master
//
// 0x1A00 : Counter Status                 32 bit
// 0x1A01 : Latch / Reset Status           32 bit
//
// 0x1A02 :
//   CH1 Present Counter
//   CH1 Latch A
//   CH1 Latch B
//   CH1 Phase Z Latch
//   CH2 Present Counter
//   CH2 Latch A
//   CH2 Latch B
//   CH2 Phase Z Latch
//
// 0x1A03 :
//   CH1 Pulse Rate
//   CH1 Comparison Reference
//   CH2 Pulse Rate
//   CH2 Comparison Reference
//
// Total : 56 bytes
// ------------------------------------------------------------
struct DaoInternalFastechEncoderInputPdo
{
    std::uint32_t counterStatus;          // 3020h
    std::uint32_t latchResetStatus;       // 3021h

    std::uint32_t presentCounterCh1;      // 3022:01
    std::uint32_t latchACh1;              // 3023:01
    std::uint32_t latchBCh1;              // 3024:01
    std::uint32_t phaseZLatchCh1;         // 3025:01

    std::uint32_t presentCounterCh2;      // 3022:02
    std::uint32_t latchACh2;              // 3023:02
    std::uint32_t latchBCh2;              // 3024:02
    std::uint32_t phaseZLatchCh2;         // 3025:02

    std::uint32_t pulseRateCh1;           // 3026:01
    std::uint32_t comparisonReferenceCh1; // 3027:01

    std::uint32_t pulseRateCh2;           // 3026:02
    std::uint32_t comparisonReferenceCh2; // 3027:02
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

static_assert(
    sizeof(DaoInternalFastechEncoderOutputPdo) == 13,
    "FASTECH CNT02 Output PDO size must be 13 bytes.");

static_assert(
    sizeof(DaoInternalFastechEncoderInputPdo) == 56,
    "FASTECH CNT02 Input PDO size must be 56 bytes.");    



// ------------------------------------------------------------
// Servo Command Type
//
// 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
// 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
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
	DAO_SERVO_COMMAND_VELOCITY = 11 // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
};


// ------------------------------------------------------------
// Servo Command State
//
// 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
// 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
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
// 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.
// 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
// 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
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
// 요청값의 유효성을 확인한 후 내부 Master에 전달합니다.
// 요청값의 유효성을 확인한 후 내부 Master에 전달합니다.
// 요청값의 유효성을 확인한 후 내부 Master에 전달합니다.
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
// 요청된 정보를 출력 구조체에 복사합니다.
// 요청된 정보를 출력 구조체에 복사합니다.
// 요청된 정보를 출력 구조체에 복사합니다.
// 요청된 정보를 출력 구조체에 복사합니다.
// 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
// ------------------------------------------------------------
struct DaoInternalServoRuntimeInfo
{
    
    int physicalSlaveIndex = 0;
    bool configured = false;
    bool communicationRunning = false;
    bool hasValidInputData = false;

    // Statusword에서 CiA 402 상태를 추출합니다.
    unsigned short cia402State = 0;

    bool fault = false;
    bool operationEnabled = false;
    bool targetReached = false;

    // --------------------------------------------------------
    // Servo 입력값에서 Limit, Home, STOP 및 STO 신호를 해석합니다.
    // --------------------------------------------------------
    bool negativeLimit = false;   // bit 0 : NOT
    bool positiveLimit = false;   // bit 1 : POT
    bool homeSensor = false;      // bit 2 : HOME
	bool stopInput = false;       // bit 19 : STOP
    bool stoActive = false;       // bit 31 : STO



    std::uint64_t commandStartFrameCount = 0; 

    // Servo Homing 명령과 제한 시간을 설정합니다.
    std::uint64_t homingStartFrameCount = 0;
    unsigned int homingTimeoutMs = 60000;


    // Servo Homing 명령과 제한 시간을 설정합니다.
    int moveTargetPosition = 0;

    unsigned int moveProfileVelocity = 1000;
    unsigned int moveProfileAcceleration = 1000;
    unsigned int moveProfileDeceleration = 1000;

    unsigned int moveTimeoutMs = 60000;

    // --------------------------------------------------------
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    // --------------------------------------------------------
    int velocityTarget = 0;

    unsigned int velocityAcceleration = 1000;
    unsigned int velocityDeceleration = 1000;

    // 이 값은 처리된 프레임 또는 샘플의 누적 개수를 나타냅니다.
    int moveStartPosition = 0;

    // 이 값은 해당 처리의 실행 상태와 결과를 나타냅니다.
    std::uint64_t moveStartFrameCount = 0;

    // Servo Homing 명령과 제한 시간을 설정합니다.
    // Servo Homing 명령과 제한 시간을 설정합니다.
    bool moveTargetReachedWentLow = false;

    // Servo Homing 명령과 제한 시간을 설정합니다.
    int homingLastPosition = 0;

    // Servo Homing 명령과 제한 시간을 설정합니다.
    std::uint64_t homingLastMoveFrameCount = 0;

    // Servo Homing 명령과 제한 시간을 설정합니다.
    bool homingPositionMonitorStarted = false;
	// Servo Homing 명령과 제한 시간을 설정합니다.
	bool homingAttainedWentLow = false; // Servo Homing 명령과 제한 시간을 설정합니다.


    // Servo Homing 명령과 제한 시간을 설정합니다.
    std::uint64_t commandId = 0;

    int commandType =
        DAO_SERVO_COMMAND_NONE;

    int commandState =
        DAO_SERVO_COMMAND_STATE_IDLE;

    int commandStep =
		DAO_SERVO_STEP_NONE; // 비동기 Servo 명령의 상태와 완료 결과를 갱신합니다.

    int commandResult = 0;


    int mailboxRequestType =
        DAO_SERVO_MAILBOX_NONE;

    int mailboxRequestState =
        DAO_SERVO_MAILBOX_STATE_IDLE;

    signed char requestedOperationMode = 0;

    int mailboxResult = 0;

    // Process Data를 송신하고 수신 WKC를 확인합니다.
    bool homed = false;

    int lastWkc = 0;
    int expectedWkc = 0;

    uint64_t totalFrameCount = 0;
    uint64_t goodWkcFrameCount = 0;
    uint64_t badWkcFrameCount = 0;
    uint64_t inputUpdateCount = 0;

    // Servo에 전송할 출력 PDO를 IO Map에 복사합니다.
    DaoInternalLsServoOutputPdo outputCommand{};

    // Servo에 전송할 출력 PDO를 IO Map에 복사합니다.
    DaoInternalLsServoInputPdo latestInput{};
};


// ------------------------------------------------------------
// FASTECH IO Runtime Information
//
// Slave의 출력 PDO 크기와 입력 PDO 크기를 확인합니다.
// 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
// 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
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

    // Servo에 전송할 출력 PDO를 IO Map에 복사합니다.
    unsigned short outputCommand = 0;

    // Servo에 전송할 출력 PDO를 IO Map에 복사합니다.
    unsigned short latestInput = 0;
};

// ------------------------------------------------------------
// FASTECH CNT02 Encoder Runtime Information
// ------------------------------------------------------------
enum DaoInternalEncoderResetState
{
    DAO_INTERNAL_ENCODER_RESET_IDLE = 0,
    DAO_INTERNAL_ENCODER_RESET_IN_PROGRESS = 1,
    DAO_INTERNAL_ENCODER_RESET_COMPLETED = 2,
    DAO_INTERNAL_ENCODER_RESET_FAILED = 3
};

struct DaoInternalEncoderRuntimeInfo
{
    int physicalSlaveIndex = 0;

    bool configured = false;
    bool communicationRunning = false;
    bool hasValidInputData = false;

    int lastWkc = 0;
    int expectedWkc = 0;

    std::uint64_t totalFrameCount = 0;
    std::uint64_t goodWkcFrameCount = 0;
    std::uint64_t badWkcFrameCount = 0;
    std::uint64_t inputUpdateCount = 0;

    std::int32_t signedCountCh1 = 0;
    std::int32_t signedCountCh2 = 0;

    double calibrationScaleCh1 = 1.0;
    double calibrationScaleCh2 = 1.0;

    double engineeringValueCh1 = 0.0;
    double engineeringValueCh2 = 0.0;

    DaoInternalEncoderResetState resetStateCh1 =
        DAO_INTERNAL_ENCODER_RESET_IDLE;

    DaoInternalEncoderResetState resetStateCh2 =
        DAO_INTERNAL_ENCODER_RESET_IDLE;

    // Master -> CNT02
    DaoInternalFastechEncoderOutputPdo outputCommand{};

    // CNT02 -> Master
    DaoInternalFastechEncoderInputPdo latestInput{};
};

// Process Data를 송신하고 수신 WKC를 확인합니다.
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

enum class DaoInternalAdcStableCaptureType // 안정된 ADC 샘플을 모아 영점 오프셋을 계산합니다.
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

// 설정된 N개 샘플의 이동 평균을 계산합니다.
struct DaoInternalAdcBufferedSample
{
    std::uint64_t sampleIndex = 0;

    double filteredValue = 0.0;
};
// ------------------------------------------------------------
// ADC Signal Processing State
//
// ADC 런타임 상태를 초기값으로 되돌립니다.
// ADC 런타임 상태를 초기값으로 되돌립니다.
// ------------------------------------------------------------
struct DaoInternalAdcProcessingState
{
    // ADC 런타임 상태를 초기값으로 되돌립니다.
    std::int32_t latestRaw = 0;

    // ADC 원시 샘플에 저역 통과 필터를 적용합니다.
    double lowLevelFiltered = 0.0;

    // ADC 원시 샘플에 저역 통과 필터를 적용합니다.
    bool lowLevelFilterInitialized = false;


    // ------------------------------------------------------------
    // Power Line Notch Filter
    //
    // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
    // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
    // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
    // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
    // ------------------------------------------------------------
    DaoInternalAdcPowerLineFilterMode powerLineFilterMode =
        DaoInternalAdcPowerLineFilterMode::OFF;

    // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
    double notch50X1 = 0.0;
    double notch50X2 = 0.0;
    double notch50Y1 = 0.0;
    double notch50Y2 = 0.0;

    // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
    double notch60X1 = 0.0;
    double notch60X2 = 0.0;
    double notch60Y1 = 0.0;
    double notch60Y2 = 0.0;

    // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
    double notch120X1 = 0.0;
    double notch120X2 = 0.0;
    double notch120Y1 = 0.0;
    double notch120Y2 = 0.0;

    // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
    double powerLineFiltered = 0.0;

    // Zero Offset
    double zeroOffset = 0.0;

    // 안정된 ADC 샘플을 모아 영점 오프셋을 계산합니다.
    bool zeroInitialized = false;

    // 엔진과 내부 상태를 초기화합니다.
    double zeroedValue = 0.0;



    // 기준값과 안정된 ADC 샘플을 이용해 보정 계수를 계산합니다.
    double calibrationScale = 1.0;

    // 최근 샘플의 중앙값을 계산해 순간 잡음을 줄입니다.
    double calibratedValue = 0.0;

    // ------------------------------------------------------------
    // 3-Sample Median Filter
    //
    // 최근 샘플의 중앙값을 계산해 순간 잡음을 줄입니다.
    // 최근 샘플의 중앙값을 계산해 순간 잡음을 줄입니다.
    // ------------------------------------------------------------
    double medianBuffer[3] = { 0.0, 0.0, 0.0 };

    unsigned int medianIndex = 0;
    unsigned int medianCount = 0;

    // 최근 샘플의 중앙값을 계산해 순간 잡음을 줄입니다.
    double medianFilteredValue = 0.0;

    // 최근 샘플의 중앙값을 계산해 순간 잡음을 줄입니다.
    bool stableCaptureActive = false;






    DaoInternalAdcStableCaptureType stableCaptureType =
        DaoInternalAdcStableCaptureType::NONE;

    double stableCaptureReferenceValue = 0.0;

    unsigned int stableCaptureWaitSamples = 0;
    unsigned int stableCaptureSampleCount = 0;
    unsigned int stableCaptureCollectedCount = 0;
    double stableCaptureSum = 0.0;

    // 설정된 N개 샘플의 이동 평균을 계산합니다.
    unsigned int filterN = 16;

    static constexpr unsigned int USER_FILTER_MAX_N = 64;

    std::array<double, USER_FILTER_MAX_N>
        userFilterBuffer{};

    unsigned int userFilterIndex = 0;
    unsigned int userFilterCount = 0;

    double userFilterSum = 0.0;

    // 설정된 N개 샘플의 이동 평균을 계산합니다.
    double filteredValue = 0.0;
};

// 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
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
    // ADC 진단 캡처 상태와 수집된 샘플 수를 관리합니다.
    // ADC 진단 캡처 상태와 수집된 샘플 수를 관리합니다.
    // ------------------------------------------------------------
    bool diagnosticCaptureActive = false;

    std::uint64_t diagnosticSampleIndex = 0;

    unsigned int diagnosticTargetSampleCount = 0;

    std::vector<DaoInternalAdcDiagnosticSample>
        diagnosticSamples;

    // ------------------------------------------------------------
    // ADC Runtime Ring Buffer
    //
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    // ------------------------------------------------------------
    static constexpr std::size_t ADC_RING_BUFFER_SIZE = 8192;

    std::array<DaoInternalAdcBufferedSample,
        ADC_RING_BUFFER_SIZE>
        ringBuffer{};

    std::size_t ringBufferHead = 0;
    std::size_t ringBufferTail = 0;
    std::size_t ringBufferCount = 0;

    std::uint64_t ringBufferNextSampleIndex = 0;

    // 처리된 ADC 샘플을 링 버퍼에 추가하고 넘침 횟수를 관리합니다.
    // 처리된 ADC 샘플을 링 버퍼에 추가하고 넘침 횟수를 관리합니다.
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

    bool RequestAllSlavesSafeOp(); // 검색된 모든 Slave를 PRE-OP 상태로 전환합니다.

	bool RequestAllSlavesOperational();// 검색된 모든 Slave를 SAFE-OP 상태로 전환합니다.

    bool RequestAllSlavesInit(); // 검색된 모든 Slave를 OP 상태로 전환합니다.

    bool MapProcessData();

    bool GetProcessDataMapInfo(
        DaoInternalProcessDataMapInfo& mapInfo) const;

    bool GetSlavePdoInfo(
        int slaveListIndex,
        DaoInternalSlavePdoInfo& pdoInfo) const;

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.

    bool ValidateDaoAdcPdo(
        int physicalSlaveIndex,
        DaoInternalAdcValidationInfo& validationInfo) const;

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    bool RequestDaoAdcSafeOp(
        int physicalSlaveIndex);

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    bool ExchangeDaoAdcProcessDataOnce(
        int physicalSlaveIndex,
        DaoInternalProcessExchangeInfo& exchangeInfo);

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    bool PrimeDaoAdcProcessData(
        int physicalSlaveIndex,
        int roundCount,
        DaoInternalPrimingInfo& primingInfo);

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    //
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    bool RequestDaoAdcOperational(
        int physicalSlaveIndex,
        DaoInternalOperationalInfo& operationalInfo);

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    bool ReadDaoAdcOnce(
        int physicalSlaveIndex,
        DaoInternalAdcReadInfo& readInfo);

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    bool GetDaoAdcRuntimeInfo(
        int physicalSlaveIndex,
        DaoInternalAdcRuntimeInfo& runtimeInfo) const;

    bool SetDaoAdcZero(
		int physicalSlaveIndex); // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.

    bool SetDaoAdcCalibration(
        int physicalSlaveIndex,
		double referenceValue); // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.

    bool SetDaoAdcPowerLineFilterMode(
        int physicalSlaveIndex,
		DaoInternalAdcPowerLineFilterMode mode); // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.

    bool SetDaoAdcFilterN(
        int physicalSlaveIndex,
        unsigned int filterN); // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.

    bool StartDaoAdcDiagnosticCapture(
        int physicalSlaveIndex,
        unsigned int targetSampleCount); // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.

    bool GetDaoAdcRingBufferInfo(
        int physicalSlaveIndex,
        unsigned int& sampleCount,
        unsigned long long& overflowCount) const; // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.

    bool ReadDaoAdcRingBuffer(
        int physicalSlaveIndex,
        DaoInternalAdcBufferedSample* samples,
        unsigned int maxSampleCount,
        unsigned int& readSampleCount); //

    bool ClearDaoAdcRingBuffer(
        int physicalSlaveIndex); // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.


    bool GetDaoAdcDiagnosticCaptureInfo(
        int physicalSlaveIndex,
        bool& captureActive,
        unsigned int& capturedSampleCount,
        unsigned int& targetSampleCount) const;

    bool GetDaoAdcDiagnosticSample(
        int physicalSlaveIndex,
        unsigned int sampleIndex,
        DaoInternalAdcDiagnosticSample& sample) const;

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    bool GetServoRuntimeInfo(
        int physicalSlaveIndex,
        DaoInternalServoRuntimeInfo& runtimeInfo) const;

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    bool GetIoRuntimeInfo(
        int physicalSlaveIndex,
        DaoInternalIoRuntimeInfo& runtimeInfo) const;

    bool GetEncoderRuntimeInfo(
        int physicalSlaveIndex,
        DaoInternalEncoderRuntimeInfo& runtimeInfo) const;

    bool ConfigureFastechEncoderCountDirection(
        int physicalSlaveIndex,
        int channel,
        std::uint8_t direction);

    bool ResetFastechEncoderCounter(
        int physicalSlaveIndex,
        int channel,
        unsigned int timeoutMs);

    bool SetEncoderCalibrationScale(
        int physicalSlaveIndex,
        int channel,
        double calibrationScale);

    bool CalibrateEncoder(
        int physicalSlaveIndex,
        int channel,
        double referenceValue);

    bool RequestServoOn(
		int physicalSlaveIndex); // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.

    bool RequestServoOff(
		int physicalSlaveIndex); // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.

    bool RequestServoHome(
        int physicalSlaveIndex,
		unsigned int timeoutMs); // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.

    bool RequestServoMoveAbsolute(
        int physicalSlaveIndex,
        int targetPosition,
        unsigned int profileVelocity,
        unsigned int profileAcceleration,
        unsigned int profileDeceleration,
		unsigned int timeoutMs);  // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.

    bool RequestServoVelocity(
        int physicalSlaveIndex,
        int targetVelocity,
        unsigned int acceleration,
		unsigned int deceleration); // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.

    bool RequestServoStop(
		int physicalSlaveIndex); // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
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



    // 주기 통신 스레드의 중지를 요청합니다.
    bool SetIoOutputCommand(
        int physicalSlaveIndex,
        unsigned short outputValue);
    
	void StopCommunication(); // 통신 스레드의 실행 상태를 확인합니다.

	bool IsCommunicationRunning() const; // 통신 스레드의 실행 상태를 확인합니다.

	bool StartCommunication();// 통신 스레드의 실행 상태를 확인합니다.

    // 주기 통신 스레드의 실행을 시작합니다.
    bool ConfigureLsL7nhProfilePositionMode(
        int physicalSlaveIndex,
        unsigned int profileVelocity,
        unsigned int profileAcceleration,
        unsigned int profileDeceleration);

    // Servo Homing 명령과 제한 시간을 설정합니다.
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
    void ResetEncoderRuntimeInfo();

	void ConfigureServoAndIoRuntimeInfo(); // Process Data를 송신하고 수신 WKC를 확인합니다.

	void PrepareServoAndIoOutputs(); // Process Data를 송신하고 수신 WKC를 확인합니다.

    void CaptureServoAndIoInputs(
		int actualWkc);   // Process Data를 송신하고 수신 WKC를 확인합니다.

	void ProcessServoCommands(); // Process Data를 송신하고 수신 WKC를 확인합니다.

    void UpdateServoDerivedState(
		DaoInternalServoRuntimeInfo& runtimeInfo); // ADC 샘플의 필터링, 영점 및 보정 처리를 순서대로 수행합니다.

    void ProcessAdcSample(
        DaoInternalAdcRuntimeInfo& runtimeInfo,
		std::int32_t rawSample); // ADC 샘플의 필터링, 영점 및 보정 처리를 순서대로 수행합니다.

    void ConfigureEncoderRuntimeInfo(); // Process Data를 송신하고 수신 WKC를 확인합니다.
    
    void PrepareEncoderOutputs();

    void CaptureEncoderInputs(int actualWkc);

    bool SetEncoderCountEnable(
        int physicalSlaveIndex,
        int channel,
        bool enable);

    bool WaitEncoderCountEnabled(
        int physicalSlaveIndex,
        int channel,
        bool expectedEnabled,
        unsigned int timeoutMs) const;

    bool SetEncoderResetCommand(
        int physicalSlaveIndex,
        int channel,
        bool execute);

    bool WaitEncoderResetCompleted(
        int physicalSlaveIndex,
        int channel,
        bool expectedCompleted,
        std::uint64_t inputUpdateCountBeforeCommand,
        std::chrono::steady_clock::time_point deadline) const;

    bool GetEncoderInputUpdateCount(
        int physicalSlaveIndex,
        std::uint64_t& inputUpdateCount) const;

    void SetEncoderResetState(
        int physicalSlaveIndex,
        int channel,
        DaoInternalEncoderResetState state);

    static std::int32_t ConvertEncoderRawCountToSigned(
        std::uint32_t rawCount);

    bool StartCommunicationUnlocked();

    void StopCommunicationUnlocked();



    double ApplyAdcNotchFilter(
        double inputValue,
        double sampleRateHz,
        double notchFrequencyHz,
        double& x1,
        double& x2,
        double& y1,
		double& y2); // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.

    bool IsDaoAdcSlave(
		const ec_slavet& slave) const; // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.

    bool IsLsL7nhServo(
		int physicalSlaveIndex) const; // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
   
    bool ConfigureLsL7nhBasicPdo(
        int physicalSlaveIndex);

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    //
  



    bool IsFastechIo(
		int physicalSlaveIndex) const; // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.

    bool IsFastechEncoder(
        int physicalSlaveIndex) const;

    // EtherCAT cyclic communication thread
    std::thread communicationThread_;

    std::atomic<bool> communicationRunning_{ false };
    std::atomic<bool> communicationStopRequested_{ false };

    mutable std::mutex communicationControlMutex_;

	void CommunicationThreadMain(); // 통신 스레드의 실행 상태를 확인합니다.


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

    // 정상 통신 여부를 판단하기 위한 예상 WKC 값입니다.
    // Process Data 매핑이 완료된 경우에만 다음 처리를 수행합니다.
    // ADC 런타임 정보를 동기화하여 복사합니다.
    std::vector<DaoInternalAdcRuntimeInfo>
        adcRuntimeInfoBySlave_;
    // ADC 런타임 정보를 동기화하여 복사합니다.
    // ADC 런타임 정보를 동기화하여 복사합니다.
    std::vector<DaoInternalServoRuntimeInfo>
        servoRuntimeInfoBySlave_;

    std::vector<DaoInternalIoRuntimeInfo>
        ioRuntimeInfoBySlave_;

	std::atomic<std::uint64_t> nextServoCommandId_{ 1 }; // IO 입력 PDO를 읽어 최신 입력 상태를 갱신합니다.

    std::vector<DaoInternalEncoderRuntimeInfo>
    encoderRuntimeInfoBySlave_;
    // Protects ADC runtime information accessed by
    // the communication thread and external API calls.
    mutable std::mutex adcRuntimeMutex_;

    // Protects Servo runtime information accessed by
    // the communication thread and external API calls.
    mutable std::mutex servoRuntimeMutex_;
    mutable std::mutex ioRuntimeMutex_;
    mutable std::mutex encoderRuntimeMutex_;

};
