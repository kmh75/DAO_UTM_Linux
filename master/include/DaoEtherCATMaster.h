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

// DAO ADC PDO ���� ���� ���
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

// SAFE-OP ���¿��� ������
// ���� Process Data �պ� ���
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

// SAFE-OP ���¿��� ������ Process Data Priming ���
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

// SAFE-OP���� OP ���·� ��ȯ�� ���
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

// DAO ADC�� ������ 24����Ʈ Input PDO
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
// Master �� Servo
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
// Servo �� Master
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
// ����� ���� ������ �ƴ϶�
// Servo �� ��ü�� ���� ���� ���� ������ ǥ���մϴ�.
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
	DAO_SERVO_COMMAND_VELOCITY = 11 // �ӵ������� ����
};


// ------------------------------------------------------------
// Servo Command State
//
// UI�� �� ���¸� �ֱ������� ��ȸ�Ͽ�
// ���� ��� �������� �����մϴ�.
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
// commandState�� �ܺ� UI�� ���� ū �����̰�,
// commandStep�� ���� ���ο��� ������ �����ϱ� ����
// ���� �ܰ��Դϴ�.
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
// EtherCAT ��ȯ PDO�� ������ ó���ؾ� �ϴ�
// SDO �۾��� ��û ���¸� �����մϴ�.
// �̹� �ܰ迡���� ���� SDO�� �������� �ʽ��ϴ�.
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
// ���� Slave����:
// - ���� �۽��� ���� PDO
// - �ֱ� ������ ���� PDO
// - ��� ���¿� WKC ���
// �� �����մϴ�.
// ------------------------------------------------------------
struct DaoInternalServoRuntimeInfo
{
    
    int physicalSlaveIndex = 0;
    bool configured = false;
    bool communicationRunning = false;
    bool hasValidInputData = false;

    // CiA402 StatusWord �ؼ� ���
    unsigned short cia402State = 0;

    bool fault = false;
    bool operationEnabled = false;
    bool targetReached = false;

    // --------------------------------------------------------
    // LS L7NH 0x60FD Digital Inputs �ؼ� ����
    // --------------------------------------------------------
    bool negativeLimit = false;   // bit 0 : NOT
    bool positiveLimit = false;   // bit 1 : POT
    bool homeSensor = false;      // bit 2 : HOME
	bool stopInput = false;       // bit 19 : STOP
    bool stoActive = false;       // bit 31 : STO



    std::uint64_t commandStartFrameCount = 0; 

    // Homing ���� ���� ������ �ܺο��� ���� ���ѽð�
    std::uint64_t homingStartFrameCount = 0;
    unsigned int homingTimeoutMs = 60000;


    // Move Absolute ���� �Ķ����
    int moveTargetPosition = 0;

    unsigned int moveProfileVelocity = 1000;
    unsigned int moveProfileAcceleration = 1000;
    unsigned int moveProfileDeceleration = 1000;

    unsigned int moveTimeoutMs = 60000;

    // --------------------------------------------------------
    // Profile Velocity ���� �Ķ���� �ӵ��������Դϴ�.
    // --------------------------------------------------------
    int velocityTarget = 0;

    unsigned int velocityAcceleration = 1000;
    unsigned int velocityDeceleration = 1000;

    // MoveAbs ���� �̵� ���� ������ ��ġ
    int moveStartPosition = 0;

    // MoveAbs ���� �̵� ���� Frame
    std::uint64_t moveStartFrameCount = 0;

    // MoveAbs ���� �� Target Reached��
    // ������ �ѹ� OFF �� ���� Ȯ���ߴ��� ǥ��
    bool moveTargetReachedWentLow = false;

    // Homing �� ���� ��ġ ��ȭ ���ÿ�
    int homingLastPosition = 0;

    // ���������� ��ġ ��ȭ�� Ȯ�ε� Frame
    std::uint64_t homingLastMoveFrameCount = 0;

    // ��ġ ���ð� ���۵Ǿ����� ǥ��
    bool homingPositionMonitorStarted = false;
	// Homing Attained�� ������ �ѹ� OFF �� ���� Ȯ���ߴ��� ǥ��
	bool homingAttainedWentLow = false; // Homing Attained�� ������ �ѹ� OFF �� ���� Ȯ���ߴ��� ǥ��


    // ���� Servo ���� ���� ���� ���� ���� ����
    std::uint64_t commandId = 0;

    int commandType =
        DAO_SERVO_COMMAND_NONE;

    int commandState =
        DAO_SERVO_COMMAND_STATE_IDLE;

    int commandStep =
		DAO_SERVO_STEP_NONE; // ���� ���ο��� ������ �����ϱ� ���� ���� �ܰ�

    int commandResult = 0;


    int mailboxRequestType =
        DAO_SERVO_MAILBOX_NONE;

    int mailboxRequestState =
        DAO_SERVO_MAILBOX_STATE_IDLE;

    signed char requestedOperationMode = 0;

    int mailboxResult = 0;

    // Homing�� ���� �Ϸ�� ���� �ִ��� ǥ��
    bool homed = false;

    int lastWkc = 0;
    int expectedWkc = 0;

    uint64_t totalFrameCount = 0;
    uint64_t goodWkcFrameCount = 0;
    uint64_t badWkcFrameCount = 0;
    uint64_t inputUpdateCount = 0;

    // UI �Ǵ� ���� DLL ������ ������ ��� PDO ����
    DaoInternalLsServoOutputPdo outputCommand{};

    // ��� �����尡 �ֱ� ������ �Է� PDO
    DaoInternalLsServoInputPdo latestInput{};
};


// ------------------------------------------------------------
// FASTECH IO Runtime Information
//
// IN8OUT8�� IN16OUT16�� ���� ������ ó���ϱ� ����
// �Է°� ��°��� �ִ� 16��Ʈ�� �����մϴ�.
// ���� PDO ũ��� inputBytes/outputBytes�� �����մϴ�.
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

    // �ܺο��� ��û�� ��°�
    unsigned short outputCommand = 0;

    // ��� �����尡 �ֱ� ������ �Է°�
    unsigned short latestInput = 0;
};

// ------------------------------------------------------------
// FASTECH CNT02 Encoder Runtime Information
// ------------------------------------------------------------
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

    // Master -> CNT02
    DaoInternalFastechEncoderOutputPdo outputCommand{};

    // CNT02 -> Master
    DaoInternalFastechEncoderInputPdo latestInput{};
};

// OP ���¿��� ������ DAO ADC 1ȸ �б� ���
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

enum class DaoInternalAdcStableCaptureType // ���� ��� ����
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

//�����۸� ���� ����ü ����========================================
struct DaoInternalAdcBufferedSample
{
    std::uint64_t sampleIndex = 0;

    double filteredValue = 0.0;
};
// ------------------------------------------------------------
// ADC Signal Processing State
//
// �ϳ��� ���� ADC ���������� ����ϴ�
// ���� / Zero / Calibration ���¸� �����մϴ�.
// ------------------------------------------------------------
struct DaoInternalAdcProcessingState
{
    // ���� �ֱ� ADC ���ð�
    std::int32_t latestRaw = 0;

    // ������ �⺻ Filter ó�� �� ��
    double lowLevelFiltered = 0.0;

    // ������ Filter�� ù ���÷� �ʱ�ȭ�Ǿ����� ǥ��
    bool lowLevelFilterInitialized = false;


    // ------------------------------------------------------------
    // Power Line Notch Filter
    //
    // OFF      : ��� �� ��
    // HZ_50    : 50Hz ����
    // HZ_60    : 60Hz ����
    // HZ_50_60 : 50Hz + 60Hz ����
    // ------------------------------------------------------------
    DaoInternalAdcPowerLineFilterMode powerLineFilterMode =
        DaoInternalAdcPowerLineFilterMode::OFF;

    // 50Hz Notch ���� ����
    double notch50X1 = 0.0;
    double notch50X2 = 0.0;
    double notch50Y1 = 0.0;
    double notch50Y2 = 0.0;

    // 60Hz Notch ���� ����
    double notch60X1 = 0.0;
    double notch60X2 = 0.0;
    double notch60Y1 = 0.0;
    double notch60Y2 = 0.0;

    // 120Hz Notch ���� ����
    double notch120X1 = 0.0;
    double notch120X2 = 0.0;
    double notch120Y1 = 0.0;
    double notch120Y2 = 0.0;

    // Notch ���� �� ��
    double powerLineFiltered = 0.0;

    // Zero Offset
    double zeroOffset = 0.0;

    // Zero�� ����� ��û���� �����Ǿ����� ǥ��
    bool zeroInitialized = false;

    // Zero ���� �� ��
    double zeroedValue = 0.0;



    // ����� Calibration Scale
    double calibrationScale = 1.0;

    // Calibration ���� �� ��
    double calibratedValue = 0.0;

    // ------------------------------------------------------------
    // 3-Sample Median Filter
    //
    // ���������� �� Sample�� ũ�� Ƣ�� ���� �����մϴ�.
    // Calibration ���� ��, N Moving Average ���� ����մϴ�.
    // ------------------------------------------------------------
    double medianBuffer[3] = { 0.0, 0.0, 0.0 };

    unsigned int medianIndex = 0;
    unsigned int medianCount = 0;

    // Median 3 ���� �� ��
    double medianFilteredValue = 0.0;

    // Zero / Calibration ���� ��� ����
    bool stableCaptureActive = false;






    DaoInternalAdcStableCaptureType stableCaptureType =
        DaoInternalAdcStableCaptureType::NONE;

    double stableCaptureReferenceValue = 0.0;

    unsigned int stableCaptureWaitSamples = 0;
    unsigned int stableCaptureSampleCount = 0;
    unsigned int stableCaptureCollectedCount = 0;
    double stableCaptureSum = 0.0;

    // ����� N Sample Moving Average
    unsigned int filterN = 16;

    static constexpr unsigned int USER_FILTER_MAX_N = 64;

    std::array<double, USER_FILTER_MAX_N>
        userFilterBuffer{};

    unsigned int userFilterIndex = 0;
    unsigned int userFilterCount = 0;

    double userFilterSum = 0.0;

    // ���� ����� ǥ�ð�
    double filteredValue = 0.0;
};

// 2ms ��ȯ��ſ��� ������ �ֽ� DAO ADC ����
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
    // Noise �м��� ���� Sample ���� �����Դϴ�.
    // ���� ������ ProcessAdcSample()���� �����մϴ�.
    // ------------------------------------------------------------
    bool diagnosticCaptureActive = false;

    std::uint64_t diagnosticSampleIndex = 0;

    unsigned int diagnosticTargetSampleCount = 0;

    std::vector<DaoInternalAdcDiagnosticSample>
        diagnosticSamples;

    // ------------------------------------------------------------
    // ADC Runtime Ring Buffer
    //
    // ���� UI / ����� ���� FilteredValue Sample��
    // ��ȯ ������� �����մϴ�.
    // ------------------------------------------------------------
    static constexpr std::size_t ADC_RING_BUFFER_SIZE = 8192;

    std::array<DaoInternalAdcBufferedSample,
        ADC_RING_BUFFER_SIZE>
        ringBuffer{};

    std::size_t ringBufferHead = 0;
    std::size_t ringBufferTail = 0;
    std::size_t ringBufferCount = 0;

    std::uint64_t ringBufferNextSampleIndex = 0;

    // Buffer�� ���� �� ���¿��� �� Sample�� ����
    // ���� ������ Sample�� ��� Ƚ���Դϴ�.
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
    // �˻��� ��� Slave�� PRE-OP ���·� ��ȯ�մϴ�.
	bool RequestAllSlavesPreOp(); // �˻��� ��� Slave�� PRE-OP ���·� ��ȯ�մϴ�.

    bool RequestAllSlavesSafeOp(); // �˻��� ��� Slave�� SAFE-OP ���·� ��ȯ�մϴ�.

	bool RequestAllSlavesOperational();// �˻��� ��� Slave�� OP ���·� ��ȯ�մϴ�.

    bool RequestAllSlavesInit(); // �˻��� ��� Slave�� INIT ���·� ��ȯ�մϴ�.

    bool MapProcessData();

    bool GetProcessDataMapInfo(
        DaoInternalProcessDataMapInfo& mapInfo) const;

    bool GetSlavePdoInfo(
        int slaveListIndex,
        DaoInternalSlavePdoInfo& pdoInfo) const;

    // DAO ADC PDO�� �аų� ���� ���� ���� ���Ǹ� �˻��մϴ�.

    bool ValidateDaoAdcPdo(
        int physicalSlaveIndex,
        DaoInternalAdcValidationInfo& validationInfo) const;

    // ������ DAO ADC Slave�� SAFE-OP ���·� ��ȯ�մϴ�.
    bool RequestDaoAdcSafeOp(
        int physicalSlaveIndex);

    // ������ DAO ADC�� ������� SAFE-OP ���¿���
    // Process Data�� ��Ȯ�� �� ���� �պ��մϴ�.
    bool ExchangeDaoAdcProcessDataOnce(
        int physicalSlaveIndex,
        DaoInternalProcessExchangeInfo& exchangeInfo);

    // ������ DAO ADC�� ������� SAFE-OP ���¿���
    // ������ Ƚ����ŭ Process Data Priming�� �����մϴ�.
    bool PrimeDaoAdcProcessData(
        int physicalSlaveIndex,
        int roundCount,
        DaoInternalPrimingInfo& primingInfo);

    // ������ DAO ADC�� SAFE-OP���� OP ���·� ��ȯ�մϴ�.
    //
    // ���� ��û�� �� ���� ������,
    // OP ��ȯ�� ��ٸ��� ���� Process Data�� ��� ��ȯ�մϴ�.
    bool RequestDaoAdcOperational(
        int physicalSlaveIndex,
        DaoInternalOperationalInfo& operationalInfo);

    // ������ DAO ADC�� ������� OP ���¿���
    // Process Data�� 1ȸ �պ��ϰ� Input PDO 24����Ʈ�� �����մϴ�.
    bool ReadDaoAdcOnce(
        int physicalSlaveIndex,
        DaoInternalAdcReadInfo& readInfo);

    // ���� ���� ���� DAO ADC �ֽ� ���¸� ��ȯ�մϴ�.
    bool GetDaoAdcRuntimeInfo(
        int physicalSlaveIndex,
        DaoInternalAdcRuntimeInfo& runtimeInfo) const;

    bool SetDaoAdcZero(
		int physicalSlaveIndex); // DAO ADC�� Zero Offset�� ���� ���÷� �����մϴ�.

    bool SetDaoAdcCalibration(
        int physicalSlaveIndex,
		double referenceValue); // DAO ADC�� Calibration Scale�� �����մϴ�.

    bool SetDaoAdcPowerLineFilterMode(
        int physicalSlaveIndex,
		DaoInternalAdcPowerLineFilterMode mode); // DAO ADC�� �������ļ� ���� ��带 �����մϴ�.

    bool SetDaoAdcFilterN(
        int physicalSlaveIndex,
        unsigned int filterN); //ui���� ���Ͱ��� �޾Ƽ� ó�����Լ�����

    bool StartDaoAdcDiagnosticCapture(
        int physicalSlaveIndex,
        unsigned int targetSampleCount); // DAO ADC �׽�Ʈ����

    bool GetDaoAdcRingBufferInfo(
        int physicalSlaveIndex,
        unsigned int& sampleCount,
        unsigned long long& overflowCount) const; //������ۿ� � �׾Ҵ��� �����÷λ������ Ȯ���ϴ� �Լ� ����

    bool ReadDaoAdcRingBuffer(
        int physicalSlaveIndex,
        DaoInternalAdcBufferedSample* samples,
        unsigned int maxSampleCount,
        unsigned int& readSampleCount); //

    bool ClearDaoAdcRingBuffer(
        int physicalSlaveIndex); //������ Ŭ�����Լ� ����


    bool GetDaoAdcDiagnosticCaptureInfo(
        int physicalSlaveIndex,
        bool& captureActive,
        unsigned int& capturedSampleCount,
        unsigned int& targetSampleCount) const;

    bool GetDaoAdcDiagnosticSample(
        int physicalSlaveIndex,
        unsigned int sampleIndex,
        DaoInternalAdcDiagnosticSample& sample) const;

    // ���� ���� ���� LS Servo �ֽ� ���¸� ��ȯ�մϴ�.
    bool GetServoRuntimeInfo(
        int physicalSlaveIndex,
        DaoInternalServoRuntimeInfo& runtimeInfo) const;

    // ���� ���� ���� FASTECH IO �ֽ� ���¸� ��ȯ�մϴ�.
    bool GetIoRuntimeInfo(
        int physicalSlaveIndex,
        DaoInternalIoRuntimeInfo& runtimeInfo) const;

    bool GetEncoderRuntimeInfo(
        int physicalSlaveIndex,
        DaoInternalEncoderRuntimeInfo& runtimeInfo) const;

    bool RequestServoOn(
		int physicalSlaveIndex); // LS Servo�� On ���·� ��ȯ�մϴ�. �񵿱�

    bool RequestServoOff(
		int physicalSlaveIndex); // LS Servo�� Off ���·� ��ȯ�մϴ�. �񵿱�

    bool RequestServoHome(
        int physicalSlaveIndex,
		unsigned int timeoutMs); // LS Servo�� Homing ���·� ��ȯ�մϴ�. �񵿱� �ܺνð� ����

    bool RequestServoMoveAbsolute(
        int physicalSlaveIndex,
        int targetPosition,
        unsigned int profileVelocity,
        unsigned int profileAcceleration,
        unsigned int profileDeceleration,
		unsigned int timeoutMs);  // LS Servo�� ���� ��ġ�� �̵��մϴ�. �񵿱� �ܺνð� ����

    bool RequestServoVelocity(
        int physicalSlaveIndex,
        int targetVelocity,
        unsigned int acceleration,
		unsigned int deceleration); // LS Servo�� �ӵ�������� �����մϴ�. �񵿱�

    bool RequestServoStop(
		int physicalSlaveIndex); // LS Servo�� �����մϴ�. �񵿱�

    // LS Servo�� �۽��� Output PDO ������ �����մϴ�.
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



    // FASTECH IO�� �۽��� ��°��� �����մϴ�.
    bool SetIoOutputCommand(
        int physicalSlaveIndex,
        unsigned short outputValue);
    
	void StopCommunication(); // ��� ������ ���� ��û

	bool IsCommunicationRunning() const; // ��� �����尡 ���� ������ Ȯ��

	bool StartCommunication();// ��� ������ ����

    // �ݵ�� PRE-OP ���¿��� ȣ���մϴ�.
    bool ConfigureLsL7nhProfilePositionMode(
        int physicalSlaveIndex,
        unsigned int profileVelocity,
        unsigned int profileAcceleration,
        unsigned int profileDeceleration);

    // LS L7NH�� CiA402 ������� 0x6060�� �����մϴ�.
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

	void ConfigureServoAndIoRuntimeInfo(); // Servo�� EtherCAT IO�� ��Ÿ�� ������ �ʱ�ȭ�մϴ�.

	void PrepareServoAndIoOutputs(); // Servo�� EtherCAT IO�� ��� PDO�� �ʱ�ȭ�մϴ�.

    void CaptureServoAndIoInputs(
		int actualWkc);   // Servo�� EtherCAT IO�� �Է� PDO�� ĸó�մϴ�.

	void ProcessServoCommands(); // Servo ���� ���¸� �����ϰ�, �Ϸ�� ������ �����մϴ�.

    void UpdateServoDerivedState(
		DaoInternalServoRuntimeInfo& runtimeInfo); // Servo�� CiA402 ���¸� �ؼ��Ͽ� derived state�� �����մϴ�.

    void ProcessAdcSample(
        DaoInternalAdcRuntimeInfo& runtimeInfo,
		std::int32_t rawSample); // DAO ADC ���� ������ ó���Ͽ� ����/Zero/Calibration�� �����ϰ� ���� ǥ�ð��� �����մϴ�.

    void ConfigureEncoderRuntimeInfo(); // FASTECH CNT02 Encoder�� ������ �ʱ�ȭ�մϴ�.
    
    void PrepareEncoderOutputs();

    void CaptureEncoderInputs(int actualWkc);



    double ApplyAdcNotchFilter(
        double inputValue,
        double sampleRateHz,
        double notchFrequencyHz,
        double& x1,
        double& x2,
        double& y1,
		double& y2); // Notch Filter�� �����մϴ�.

    bool IsDaoAdcSlave(
		const ec_slavet& slave) const; // DAO ADC Slave���� Ȯ���մϴ�.

    bool IsLsL7nhServo(
		int physicalSlaveIndex) const; // LS L7NH Servo���� Ȯ���մϴ�.
   
    bool ConfigureLsL7nhBasicPdo(
        int physicalSlaveIndex);

    // LS L7NH Profile Position �⺻ ��������
    // �ӵ�/����/������ SDO�� �����մϴ�.
    //
  



    bool IsFastechIo(
		int physicalSlaveIndex) const; // Fastech EtherCAT IO���� Ȯ���մϴ�.

    bool IsFastechEncoder(
        int physicalSlaveIndex) const;

    // EtherCAT cyclic communication thread
    std::thread communicationThread_;

    std::atomic<bool> communicationRunning_{ false };
    std::atomic<bool> communicationStopRequested_{ false };

	void CommunicationThreadMain(); // ��� ������ ���� ����


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

    // ���� Slave ��ȣ�� �ε����� ����մϴ�.
    // 0���� ��ü Slave���̹Ƿ� ������� �ʰ�,
    // ���� Slave 1������ �����մϴ�.
    std::vector<DaoInternalAdcRuntimeInfo>
        adcRuntimeInfoBySlave_;
    // ���� Slave ��ȣ�� �״�� �ε����� ����մϴ�.
// index 0�� EtherCAT���� ������� �ʽ��ϴ�.
    std::vector<DaoInternalServoRuntimeInfo>
        servoRuntimeInfoBySlave_;

    std::vector<DaoInternalIoRuntimeInfo>
        ioRuntimeInfoBySlave_;

	std::atomic<std::uint64_t> nextServoCommandId_{ 1 }; // Servo ���� ID�� ���������� �����մϴ�.

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
