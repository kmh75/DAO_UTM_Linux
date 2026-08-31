#pragma once

#include <cstdint>

#ifdef _WIN32

    #ifdef DAOETHERCATENGINE_EXPORTS
        #define DAO_ENGINE_API __declspec(dllexport)
    #else
        #define DAO_ENGINE_API __declspec(dllimport)
    #endif

#else

    #ifdef DAOETHERCATENGINE_EXPORTS
        #define DAO_ENGINE_API \
            __attribute__((visibility("default")))
    #else
        #define DAO_ENGINE_API
    #endif

#endif
struct DaoAdapterInfo
{
    char name[512];
    char description[512];
};

struct DaoSlaveInfo
{
    int listIndex;
    int physicalSlaveIndex;

    char name[128];

    unsigned int vendorId;
    unsigned int productCode;
    unsigned int revision;

    unsigned short state;
    unsigned short alStatusCode;
};

// Slave의 현재 상태와 AL 상태 코드를 갱신합니다.
struct DaoProcessDataMapInfo
{
    int mappedBytes;
    int outputWkc;
    int inputWkc;
    int expectedWkc;
};

// 정상 통신 여부를 판단하기 위한 예상 WKC 값입니다.
struct DaoSlavePdoInfo
{
    int listIndex;
    int physicalSlaveIndex;

    unsigned int outputBytes;
    unsigned int inputBytes;
};

// 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
struct DaoAdcValidationInfo
{
    int physicalSlaveIndex;

    int processDataMapped;
    int slaveIndexValid;
    int identityValid;

    int outputSizeValid;
    int inputSizeValid;

    int outputPointerValid;
    int inputPointerValid;

    unsigned int actualOutputBytes;
    unsigned int actualInputBytes;
};

// 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
// Process Data를 송신하고 수신 WKC를 확인합니다.
struct DaoProcessExchangeInfo
{
    int physicalSlaveIndex;

    int actualWkc;
    int expectedWkc;

    int safeOpStateValid;
    int adcValidationPassed;
    int outputCleared;
    int wkcValid;
};

// 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
// 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
struct DaoPrimingInfo
{
    int physicalSlaveIndex;

    int requestedRounds;
    int completedRounds;

    int expectedWkc;
    int minimumWkc;
    int maximumWkc;
    int lastWkc;

    int goodWkcCount;
    int badWkcCount;

    int safeOpStateValid;
    int adcValidationPassed;
    int allRoundsValid;
};

// 정상 통신 여부를 판단하기 위한 예상 WKC 값입니다.
struct DaoOperationalInfo
{
    int physicalSlaveIndex;

    int expectedWkc;
    int lastWkc;

    int exchangeCount;
    int goodWkcCount;
    int badWkcCount;

    int adcValidationPassed;
    int safeOpStateValid;
    int primingPassed;

    int stateWriteSucceeded;
    int operationalReached;

    unsigned short finalState;
    unsigned short alStatusCode;
};

// Slave의 현재 상태와 AL 상태 코드를 갱신합니다.
struct DaoAdcInputPdo
{
    unsigned int testCounter;

    int adcRaw0;
    int adcRaw1;
    int adcRaw2;
    int adcRaw3;

    unsigned int status;
};

// Process Data를 송신하고 수신 WKC를 확인합니다.
struct DaoAdcReadInfo
{
    int physicalSlaveIndex;

    int actualWkc;
    int expectedWkc;

    int operationalStateValid;
    int adcValidationPassed;
    int outputCleared;
    int wkcValid;
    int inputCopied;

    DaoAdcInputPdo data;
};

static_assert(
    sizeof(DaoAdcInputPdo) == 24,
    "DaoAdcInputPdo must be exactly 24 bytes.");

// 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
struct DaoAdcRuntimeInfo
{
    int physicalSlaveIndex;

    int communicationRunning;
    int hasValidData;

    int lastWkc;
    int expectedWkc;

    unsigned long long totalFrameCount;
    unsigned long long goodWkcFrameCount;
    unsigned long long badWkcFrameCount;
    unsigned long long dataUpdateCount;

    DaoAdcInputPdo latestData;
    // ADC 입력 PDO 한 프레임을 런타임 구조체로 복사합니다.
    double lowLevelFiltered;

    // ADC 원시 샘플에 저역 통과 필터를 적용합니다.
    double powerLineFiltered;

    // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
    double zeroedValue;

    // 이 값은 해당 처리의 실행 상태와 결과를 나타냅니다.
    double calibratedValue;

    // 안정된 ADC 샘플을 모아 영점 오프셋을 계산합니다.
    int stableCaptureActive;

    // 0 = NONE
    // 1 = ZERO
    // 2 = CALIBRATION
    int stableCaptureType;

    // 기준값과 안정된 ADC 샘플을 이용해 보정 계수를 계산합니다.
    unsigned int stableCaptureCollectedCount;

    // 설정된 N개 샘플의 이동 평균을 계산합니다.
    unsigned int stableCaptureSampleCount;

    // 설정된 N개 샘플의 이동 평균을 계산합니다.
    double filteredValue;
};


struct DaoAdcDiagnosticSample
{
    unsigned long long sampleIndex;

    int rawValue;

    double lowLevelFiltered;
    double powerLineFiltered;

    double zeroedValue;
    double calibratedValue;

    double medianFilteredValue;
    double filteredValue;
};

struct DaoAdcBufferedSample 
{
    unsigned long long sampleIndex;
    double filteredValue;
};

// ------------------------------------------------------------
// PDO 구조체는 EtherCAT 매핑 크기와 일치하도록 바이트 단위로 정렬합니다.
// PDO 구조체는 EtherCAT 매핑 크기와 일치하도록 바이트 단위로 정렬합니다.
// PDO 구조체는 EtherCAT 매핑 크기와 일치하도록 바이트 단위로 정렬합니다.
// ------------------------------------------------------------
#pragma pack(push, 1)

struct DaoServoOutputPdo
{
    unsigned short controlWord;
    signed char operationMode;

    int targetPosition;

    unsigned int profileVelocity;
    unsigned int profileAcceleration;
    unsigned int profileDeceleration;

    int targetVelocity;

    unsigned short touchProbeFunction;
    unsigned int digitalOutputs;
};

struct DaoServoInputPdo
{
    unsigned short statusWord;
    signed char operationModeDisplay;

    int actualPosition;
    int positionError;

    unsigned short touchProbeStatus;
    int touchProbePosition;
    unsigned int digitalInputs;
};

#pragma pack(pop)

static_assert(
    sizeof(DaoServoOutputPdo) == 29,
    "DaoServoOutputPdo must be exactly 29 bytes.");

static_assert(
    sizeof(DaoServoInputPdo) == 21,
    "DaoServoInputPdo must be exactly 21 bytes.");


// 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
struct DaoServoRuntimeInfo
{
    int physicalSlaveIndex;

    int configured;
    int communicationRunning;
    int hasValidInputData;

    unsigned short cia402State;

    int fault;
    int operationEnabled;
    int targetReached;

    int negativeLimit;   // LS L7NH 0x60FD Bit 0  : Negative Limit Input
    int positiveLimit;   // LS L7NH 0x60FD Bit 1  : Positive Limit Input
    int homeSensor;      // LS L7NH 0x60FD Bit 2  : Home Sensor Input
    int stopInput;       // LS L7NH 0x60FD Bit 19 : STOP Input
    int stoActive;       // LS L7NH 0x60FD Bit 31 : STO Active


    unsigned long long commandId;

    int commandType;
    int commandState;
    int commandStep;
    int commandResult;

    int mailboxRequestType;
    int mailboxRequestState;
    signed char requestedOperationMode;
    int mailboxResult;

    int homed;

    int lastWkc;
    int expectedWkc;

    unsigned long long totalFrameCount;
    unsigned long long goodWkcFrameCount;
    unsigned long long badWkcFrameCount;
    unsigned long long inputUpdateCount;

    DaoServoOutputPdo outputCommand;
    DaoServoInputPdo latestInput;
};


// 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
struct DaoIoRuntimeInfo
{
    int physicalSlaveIndex;

    int configured;
    int inputBytes;
    int outputBytes;

    int communicationRunning;
    int hasValidInputData;

    int lastWkc;
    int expectedWkc;

    unsigned long long totalFrameCount;
    unsigned long long goodWkcFrameCount;
    unsigned long long badWkcFrameCount;
    unsigned long long inputUpdateCount;

    unsigned short outputCommand;
    unsigned short latestInput;
};

// ------------------------------------------------------------
// FASTECH CNT02 Encoder Runtime Information
// ------------------------------------------------------------
struct DaoEncoderRuntimeInfo
{
    int physicalSlaveIndex;

    int configured;
    int communicationRunning;
    int hasValidInputData;

    int lastWkc;
    int expectedWkc;

    unsigned long long totalFrameCount;
    unsigned long long goodWkcFrameCount;
    unsigned long long badWkcFrameCount;
    unsigned long long inputUpdateCount;

    unsigned int presentCounterCh1;
    unsigned int presentCounterCh2;

    std::int32_t signedCountCh1;
    std::int32_t signedCountCh2;

    double calibrationScaleCh1;
    double calibrationScaleCh2;

    double engineeringValueCh1;
    double engineeringValueCh2;

    int resetStateCh1;
    int resetStateCh2;

    int resetCompletedStatusCh1;
    int resetCompletedStatusCh2;

    unsigned int pulseRateCh1;
    unsigned int pulseRateCh2;

    unsigned char counterCommand;
};

enum DaoEncoderCountDirection
{
    DAO_ENCODER_COUNT_DIRECTION_FORWARD = 0,
    DAO_ENCODER_COUNT_DIRECTION_REVERSE = 1
};

enum DaoEncoderResetState
{
    DAO_ENCODER_RESET_IDLE = 0,
    DAO_ENCODER_RESET_IN_PROGRESS = 1,
    DAO_ENCODER_RESET_COMPLETED = 2,
    DAO_ENCODER_RESET_FAILED = 3
};

// 이 값은 처리된 프레임 또는 샘플의 누적 개수를 나타냅니다.
enum DaoDeviceType
{
    DAO_DEVICE_UNKNOWN = 0,
    DAO_DEVICE_SERVO   = 1,
    DAO_DEVICE_ADC     = 2,
    DAO_DEVICE_IO      = 3,
    DAO_DEVICE_ENCODER = 4
};


enum DaoAdcPowerLineFilterMode // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.
{
    DAO_ADC_POWER_FILTER_OFF = 0,
    DAO_ADC_POWER_FILTER_50HZ = 1,
    DAO_ADC_POWER_FILTER_60HZ = 2,
    DAO_ADC_POWER_FILTER_120HZ = 3,
    DAO_ADC_POWER_FILTER_50_60HZ = 4,
    DAO_ADC_POWER_FILTER_60_120HZ = 5
};

enum DaoServoCommandState  // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
{
    DAO_SERVO_STATE_IDLE = 0,
    DAO_SERVO_STATE_ACCEPTED = 1,
    DAO_SERVO_STATE_RUNNING = 2,
    DAO_SERVO_STATE_COMPLETED = 3,
    DAO_SERVO_STATE_STOPPED = 4,
    DAO_SERVO_STATE_ERROR = 5,
    DAO_SERVO_STATE_TIMEOUT = 6
};

enum DaoServoCommandType // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.
{
    DAO_SERVO_CMD_NONE = 0,
    DAO_SERVO_CMD_SERVO_ON = 1,
    DAO_SERVO_CMD_SERVO_OFF = 2,
    DAO_SERVO_CMD_MOVE_ABSOLUTE = 3,
    DAO_SERVO_CMD_MOVE_RELATIVE = 4,
    DAO_SERVO_CMD_JOG_POSITIVE = 5,
    DAO_SERVO_CMD_JOG_NEGATIVE = 6,
    DAO_SERVO_CMD_HOMING = 7,
    DAO_SERVO_CMD_STOP = 8,
    DAO_SERVO_CMD_QUICK_STOP = 9,
    DAO_SERVO_CMD_ALARM_RESET = 10,
    DAO_SERVO_CMD_VELOCITY = 11
};

enum DaoServoCommandResult  // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
{
    DAO_SERVO_RESULT_NONE = 0,
    DAO_SERVO_RESULT_SUCCESS = 1,

    DAO_SERVO_RESULT_ERROR = -1,
    DAO_SERVO_RESULT_STATE_TIMEOUT = -2,
    DAO_SERVO_RESULT_HOMING_ERROR = -3,
    DAO_SERVO_RESULT_MOTION_TIMEOUT = -4
};

// 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.
struct DaoLogicalDeviceInfo
{
    int deviceType;
    int logicalIndex;
    int physicalSlaveIndex;

    char name[128];

    unsigned int vendorId;
    unsigned int productCode;
    unsigned int revision;
};

extern "C"
{

	// Returns the version string of the DAO EtherCAT Engine.
    DAO_ENGINE_API const char* DaoEngine_GetVersion();

    DAO_ENGINE_API int DaoEngine_Initialize();
    DAO_ENGINE_API void DaoEngine_Shutdown();
    DAO_ENGINE_API int DaoEngine_IsInitialized();

    DAO_ENGINE_API int DaoEngine_GetAdapterCount();

    DAO_ENGINE_API int DaoEngine_GetAdapterInfo(
        int adapterIndex,
        DaoAdapterInfo* adapterInfo);

    // 네트워크 어댑터를 열고 SOEM 컨텍스트를 초기화합니다.
// 네트워크 어댑터를 열고 SOEM 컨텍스트를 초기화합니다.
    DAO_ENGINE_API int DaoEngine_OpenAdapter(
        int adapterIndex);

    // 네트워크 어댑터를 열고 SOEM 컨텍스트를 초기화합니다.
    DAO_ENGINE_API void DaoEngine_CloseAdapter();

    // 통신을 중지하고 열린 네트워크 어댑터를 닫습니다.
    // 통신을 중지하고 열린 네트워크 어댑터를 닫습니다.
    DAO_ENGINE_API int DaoEngine_IsAdapterOpen();

    // 연결된 EtherCAT Slave를 검색합니다.
    // 연결된 EtherCAT Slave를 검색합니다.
    DAO_ENGINE_API int DaoEngine_ScanSlaves();

    // 연결된 EtherCAT Slave를 검색합니다.
    DAO_ENGINE_API int DaoEngine_GetSlaveCount();

    // 요청된 정보를 출력 구조체에 복사합니다.
    // 요청된 정보를 출력 구조체에 복사합니다.
    // 요청된 정보를 출력 구조체에 복사합니다.
    DAO_ENGINE_API int DaoEngine_GetSlaveInfo(
        int slaveListIndex,
        DaoSlaveInfo* slaveInfo);

    // 요청된 정보를 출력 구조체에 복사합니다.
    DAO_ENGINE_API int DaoEngine_GetLogicalDeviceCount(
        int deviceType);

    // 요청된 정보를 출력 구조체에 복사합니다.
    // 요청된 정보를 출력 구조체에 복사합니다.
    DAO_ENGINE_API int DaoEngine_GetLogicalDeviceInfo(
        int deviceType,
        int logicalIndex,
        DaoLogicalDeviceInfo* deviceInfo);

    // 검색된 모든 Slave를 PRE-OP 상태로 전환합니다.
    // 검색된 모든 Slave를 PRE-OP 상태로 전환합니다.
    DAO_ENGINE_API int DaoEngine_RequestAllSlavesPreOp();


    // 검색된 모든 Slave를 PRE-OP 상태로 전환합니다.
    // 검색된 모든 Slave를 SAFE-OP 상태로 전환합니다.
    DAO_ENGINE_API int DaoEngine_RequestAllSlavesSafeOp();


    // 검색된 모든 Slave를 SAFE-OP 상태로 전환합니다.
    //
    // 검색된 모든 Slave를 OP 상태로 전환합니다.
    // 검색된 모든 Slave를 OP 상태로 전환합니다.
    //
    // 검색된 모든 Slave를 OP 상태로 전환합니다.
    // 검색된 모든 Slave를 OP 상태로 전환합니다.
    DAO_ENGINE_API int DaoEngine_RequestAllSlavesOperational();

    // 검색된 모든 Slave를 OP 상태로 전환합니다.
    // 검색된 모든 Slave를 OP 상태로 전환합니다.
    DAO_ENGINE_API int DaoEngine_RequestAllSlavesInit();

    // 검색된 모든 Slave를 INIT 상태로 전환합니다.
    // 검색된 모든 Slave를 INIT 상태로 전환합니다.
    DAO_ENGINE_API int DaoEngine_MapProcessData();

    // 전체 Slave의 Process Data 영역을 IO Map에 매핑합니다.
    // 전체 Slave의 Process Data 영역을 IO Map에 매핑합니다.
    DAO_ENGINE_API int DaoEngine_GetProcessDataMapInfo(
        DaoProcessDataMapInfo* mapInfo);

    // 요청된 정보를 출력 구조체에 복사합니다.
    // 요청된 정보를 출력 구조체에 복사합니다.
    // 요청된 정보를 출력 구조체에 복사합니다.
    DAO_ENGINE_API int DaoEngine_GetSlavePdoInfo(
        int slaveListIndex,
        DaoSlavePdoInfo* pdoInfo);

    // DAO ADC의 PDO 구성과 메모리 매핑 상태를 검증합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    //
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    DAO_ENGINE_API int DaoEngine_ValidateDaoAdcPdo(
        int physicalSlaveIndex,
        DaoAdcValidationInfo* validationInfo);

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    //
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    DAO_ENGINE_API int DaoEngine_RequestDaoAdcSafeOp(
        int physicalSlaveIndex);

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    //
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    DAO_ENGINE_API int DaoEngine_ExchangeDaoAdcProcessDataOnce(
        int physicalSlaveIndex,
        DaoProcessExchangeInfo* exchangeInfo);

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    //
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    DAO_ENGINE_API int DaoEngine_PrimeDaoAdcProcessData(
        int physicalSlaveIndex,
        int roundCount,
        DaoPrimingInfo* primingInfo);

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    //
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    DAO_ENGINE_API int DaoEngine_RequestDaoAdcOperational(
        int physicalSlaveIndex,
        DaoOperationalInfo* operationalInfo);
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    //
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    DAO_ENGINE_API int DaoEngine_ReadDaoAdcOnce(
        int physicalSlaveIndex,
        DaoAdcReadInfo* readInfo);

    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    //
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    // 논리 장치 인덱스를 실제 EtherCAT Slave 인덱스로 변환합니다.
    DAO_ENGINE_API int DaoEngine_GetDaoAdcRuntimeInfo(
        int physicalSlaveIndex,
        DaoAdcRuntimeInfo* runtimeInfo);

    DAO_ENGINE_API int DaoEngine_GetAdcRuntimeInfo(
        int logicalAdcIndex,
        DaoAdcRuntimeInfo* runtimeInfo); 

    DAO_ENGINE_API int DaoEngine_SetAdcZero(
		int logicalAdcIndex); // 안정된 ADC 샘플을 모아 영점 오프셋을 계산합니다.

    DAO_ENGINE_API int DaoEngine_SetAdcCalibration(
        int logicalAdcIndex,
		double referenceValue); // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.

    DAO_ENGINE_API int DaoEngine_SetAdcPowerLineFilterMode(
        int logicalAdcIndex,
        int mode); // 설정된 50 Hz 또는 60 Hz 전원 주파수 제거 필터를 적용합니다.

    DAO_ENGINE_API int DaoEngine_SetAdcFilterN(
        int logicalAdcIndex,
        unsigned int filterN); //---------------------------------------


    DAO_ENGINE_API int DaoEngine_StartAdcDiagnosticCapture(
        int logicalAdcIndex,
        unsigned int targetSampleCount);

    DAO_ENGINE_API int DaoEngine_GetAdcDiagnosticCaptureInfo(
        int logicalAdcIndex,
        int* captureActive,
        unsigned int* capturedSampleCount,
        unsigned int* targetSampleCount);

    DAO_ENGINE_API int DaoEngine_GetAdcDiagnosticSample(
        int logicalAdcIndex,
        unsigned int sampleIndex,
        DaoAdcDiagnosticSample* sample);


    DAO_ENGINE_API int DaoEngine_GetAdcRingBufferInfo(
        int logicalAdcIndex,
        unsigned int* sampleCount,
        unsigned long long* overflowCount);

    DAO_ENGINE_API int DaoEngine_ReadAdcRingBuffer(
        int logicalAdcIndex,
        DaoAdcBufferedSample* samples,
        unsigned int maxSampleCount,
        unsigned int* readSampleCount);

    DAO_ENGINE_API int DaoEngine_ClearAdcRingBuffer(
        int logicalAdcIndex);

    // 처리된 ADC 샘플을 링 버퍼에 추가하고 넘침 횟수를 관리합니다.
    // Servo 런타임 정보를 동기화하여 복사합니다.
    // Servo 런타임 정보를 동기화하여 복사합니다.
    DAO_ENGINE_API int DaoEngine_GetServoRuntimeInfo(
        int logicalServoIndex,
        DaoServoRuntimeInfo* runtimeInfo);

    // IO 입력 PDO를 읽어 최신 입력 상태를 갱신합니다.
    // IO 입력 PDO를 읽어 최신 입력 상태를 갱신합니다.
    // IO 입력 PDO를 읽어 최신 입력 상태를 갱신합니다.
    DAO_ENGINE_API int DaoEngine_GetIoRuntimeInfo(
        int logicalIoIndex,
        DaoIoRuntimeInfo* runtimeInfo);

    DAO_ENGINE_API int DaoEngine_GetEncoderRuntimeInfo(
        int logicalEncoderIndex,
        DaoEncoderRuntimeInfo* runtimeInfo);

    DAO_ENGINE_API int DaoEngine_SetEncoderCountDirection(
        int logicalEncoderIndex,
        int channel,
        int direction);

    DAO_ENGINE_API int DaoEngine_ResetEncoderCounter(
        int logicalEncoderIndex,
        int channel,
        unsigned int timeoutMs);

    DAO_ENGINE_API int DaoEngine_SetEncoderCalibrationScale(
        int logicalEncoderIndex,
        int channel,
        double calibrationScale);

    DAO_ENGINE_API int DaoEngine_CalibrateEncoder(
        int logicalEncoderIndex,
        int channel,
        double referenceValue);

    DAO_ENGINE_API int DaoEngine_ServoOn( 
		int logicalServoIndex); // Servo ON 명령을 등록하고 CiA 402 활성화 절차를 시작합니다.

    DAO_ENGINE_API int DaoEngine_ServoOff(
		int logicalServoIndex); // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.

    DAO_ENGINE_API int DaoEngine_ServoHome(
        int logicalServoIndex,
        unsigned int timeoutMs); // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.


    DAO_ENGINE_API int DaoEngine_ServoMoveAbsolute(
        int logicalServoIndex,
        int targetPosition,
        unsigned int profileVelocity,
        unsigned int profileAcceleration,
        unsigned int profileDeceleration,
		unsigned int timeoutMs); // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.

    DAO_ENGINE_API int DaoEngine_ServoVelocity(
        int logicalServoIndex,
        int targetVelocity,
        unsigned int acceleration,
		unsigned int deceleration);  // 목표 속도와 가감속 값을 설정해 속도 운전을 요청합니다.

    DAO_ENGINE_API int DaoEngine_ServoJogPositive(
        int logicalServoIndex,
        int speed,
        unsigned int acceleration,
		unsigned int deceleration); // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.

    DAO_ENGINE_API int DaoEngine_ServoJogNegative(
        int logicalServoIndex,
        int speed,
        unsigned int acceleration,
		unsigned int deceleration); // 현재 Servo 운전 명령을 정지 상태로 전환합니다.

    DAO_ENGINE_API int DaoEngine_ServoStop(
		int logicalServoIndex); // 현재 Servo 운전 명령을 정지 상태로 전환합니다.

    // 현재 Servo 운전 명령을 정지 상태로 전환합니다.
    // 요청값의 유효성을 확인한 후 내부 Master에 전달합니다.
    // 요청값의 유효성을 확인한 후 내부 Master에 전달합니다.
    DAO_ENGINE_API int DaoEngine_SetServoOutputCommand(
        int logicalServoIndex,
        const DaoServoOutputPdo* command);


    // Servo Homing 명령과 제한 시간을 설정합니다.
    //
    // mode:
    // 1 = Profile Position
    // 3 = Profile Velocity
    // 6 = Homing
    //
    // Servo의 운전 모드를 SDO로 설정합니다.
    // Servo의 운전 모드를 SDO로 설정합니다.
    DAO_ENGINE_API int DaoEngine_SetServoOperationMode(
        int logicalServoIndex,
        signed char mode);

    // IO 출력 명령을 장치의 출력 PDO에 반영합니다.
    // IO 출력 명령을 장치의 출력 PDO에 반영합니다.
    // IO 출력 명령을 장치의 출력 PDO에 반영합니다.
    DAO_ENGINE_API int DaoEngine_SetIoOutputCommand(
        int logicalIoIndex,
        unsigned short outputValue);

    // 주기 통신 스레드의 실행을 시작합니다.
    // 주기 통신 스레드의 실행을 시작합니다.
    // 주기 통신 스레드의 실행을 시작합니다.
    DAO_ENGINE_API int DaoEngine_StartCommunication();

    // 주기 통신 스레드의 실행을 시작합니다.
    DAO_ENGINE_API void DaoEngine_StopCommunication();

    // 통신 스레드의 실행 상태를 확인합니다.
    // 통신 스레드의 실행 상태를 확인합니다.
    // 통신 스레드의 실행 상태를 확인합니다.
    DAO_ENGINE_API int DaoEngine_IsCommunicationRunning();

}
