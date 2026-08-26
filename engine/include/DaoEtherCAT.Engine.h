#pragma once

#ifdef _WIN32

    #ifdef DAOETHERCATENGINE_EXPORTS
        #define DAO_ENGINE_API __declspec(dllexport)
    #else
        #define DAO_ENGINE_API __declspec(dllimport)
    #endif

#else

    #define DAO_ENGINE_API

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

// ��ü Process Data ���� ���
struct DaoProcessDataMapInfo
{
    int mappedBytes;
    int outputWkc;
    int inputWkc;
    int expectedWkc;
};

// Slave�� PDO ũ�� ����
struct DaoSlavePdoInfo
{
    int listIndex;
    int physicalSlaveIndex;

    unsigned int outputBytes;
    unsigned int inputBytes;
};

// DAO ADC PDO ���� ���� ���
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

// SAFE-OP ���¿��� ������
// ���� Process Data �պ� ���
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

// SAFE-OP ���¿��� ������
// Process Data Priming ���
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

// SAFE-OP���� OP ���·� ��ȯ�� ���
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

// DAO ADC�� ������ 24����Ʈ Input PDO
struct DaoAdcInputPdo
{
    unsigned int testCounter;

    int adcRaw0;
    int adcRaw1;
    int adcRaw2;
    int adcRaw3;

    unsigned int status;
};

// OP ���¿��� ������ DAO ADC 1ȸ �б� ���
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

// DAO ADC �ֽ� ��Ÿ�� ����
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
    // ������ �⺻ Filter ó�� �� �ֽ� ADC ��
    double lowLevelFiltered;

    // 50/60Hz Power Line Notch ���� �� ��
    double powerLineFiltered;

    // Zero ���� �� �ֽ� ADC ��
    double zeroedValue;

    // Calibration ���� �� �ֽ� ADC ��
    double calibratedValue;

    // Zero / Calibration Stable Capture ���� ����
    int stableCaptureActive;

    // 0 = NONE
    // 1 = ZERO
    // 2 = CALIBRATION
    int stableCaptureType;

    // ������� ���� ������ Sample ����
    unsigned int stableCaptureCollectedCount;

    // ��ǥ Sample ����
    unsigned int stableCaptureSampleCount;

    // ����� N Filter ���� �� ���� ǥ�ð�
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
// LS L7NH Servo ������ Output PDO
// Master �� Servo
// �� 12����Ʈ
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


// LS Servo �ֽ� ��Ÿ�� ����
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


// FASTECH IO �ֽ� ��Ÿ�� ����
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

// �������� ����ϴ� ���� ��ġ ����
enum DaoDeviceType
{
    DAO_DEVICE_UNKNOWN = 0,
    DAO_DEVICE_SERVO   = 1,
    DAO_DEVICE_ADC     = 2,
    DAO_DEVICE_IO      = 3
};


enum DaoAdcPowerLineFilterMode // DAO ADC �������͸��
{
    DAO_ADC_POWER_FILTER_OFF = 0,
    DAO_ADC_POWER_FILTER_50HZ = 1,
    DAO_ADC_POWER_FILTER_60HZ = 2,
    DAO_ADC_POWER_FILTER_120HZ = 3,
    DAO_ADC_POWER_FILTER_50_60HZ = 4,
    DAO_ADC_POWER_FILTER_60_120HZ = 5
};

enum DaoServoCommandState  // Servo ���� ����
{
    DAO_SERVO_STATE_IDLE = 0,
    DAO_SERVO_STATE_ACCEPTED = 1,
    DAO_SERVO_STATE_RUNNING = 2,
    DAO_SERVO_STATE_COMPLETED = 3,
    DAO_SERVO_STATE_STOPPED = 4,
    DAO_SERVO_STATE_ERROR = 5,
    DAO_SERVO_STATE_TIMEOUT = 6
};

enum DaoServoCommandType // Servo ���� ����
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

enum DaoServoCommandResult  // Servo ���� ���� ���
{
    DAO_SERVO_RESULT_NONE = 0,
    DAO_SERVO_RESULT_SUCCESS = 1,

    DAO_SERVO_RESULT_ERROR = -1,
    DAO_SERVO_RESULT_STATE_TIMEOUT = -2,
    DAO_SERVO_RESULT_HOMING_ERROR = -3,
    DAO_SERVO_RESULT_MOTION_TIMEOUT = -4
};

// ���� ��ġ ��� ����
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

    // ������ ����͸� EtherCAT ��ſ����� ���ϴ�.
// ����: 1, ����: 0
    DAO_ENGINE_API int DaoEngine_OpenAdapter(
        int adapterIndex);

    // ���� ���� EtherCAT ����͸� �ݽ��ϴ�.
    DAO_ENGINE_API void DaoEngine_CloseAdapter();

    // ����Ͱ� ���� �ִ��� ��ȯ�մϴ�.
    // ����: 1, ����: 0
    DAO_ENGINE_API int DaoEngine_IsAdapterOpen();

    // ���� ���� ����Ϳ��� EtherCAT Slave�� �˻��մϴ�.
    // ��ȯ��: �߰ߵ� Slave ����
    DAO_ENGINE_API int DaoEngine_ScanSlaves();

    // ������ �˻����� �߰ߵ� Slave ������ ��ȯ�մϴ�.
    DAO_ENGINE_API int DaoEngine_GetSlaveCount();

    // ������ Slave �˻� ������� ������ Slave ������ �����ɴϴ�.
    // slaveListIndex�� 0���� �����մϴ�.
    // ����: 1, ����: 0
    DAO_ENGINE_API int DaoEngine_GetSlaveInfo(
        int slaveListIndex,
        DaoSlaveInfo* slaveInfo);

    // ������ ������ ���� ��ġ ������ ��ȯ�մϴ�.
    DAO_ENGINE_API int DaoEngine_GetLogicalDeviceCount(
        int deviceType);

    // ������ ���� ��ġ ������ ��ȯ�մϴ�.
    // ����: 1, ����: 0
    DAO_ENGINE_API int DaoEngine_GetLogicalDeviceInfo(
        int deviceType,
        int logicalIndex,
        DaoLogicalDeviceInfo* deviceInfo);

    // �˻��� ��� EtherCAT Slave�� PRE-OP ���·� ��ȯ�մϴ�.
    // ����: 1, ����: 0
    DAO_ENGINE_API int DaoEngine_RequestAllSlavesPreOp();


    // �˻��� ��� EtherCAT Slave�� SAFE-OP ���·� ��ȯ�մϴ�.
    // ����: 1, ����: 0
    DAO_ENGINE_API int DaoEngine_RequestAllSlavesSafeOp();


    // �˻��� ��� EtherCAT Slave�� OP ���·� ��ȯ�մϴ�.
    //
    // ��ü PDO Priming�� ���� ������ ��
    // ��� Slave�� �Բ� OP ���·� ��ȯ�մϴ�.
    //
    // ����: 1
    // ����: 0
    DAO_ENGINE_API int DaoEngine_RequestAllSlavesOperational();

    // �˻��� ��� EtherCAT Slave�� INIT ���·� ��ȯ�մϴ�.
    // ����: 1, ����: 0
    DAO_ENGINE_API int DaoEngine_RequestAllSlavesInit();

    // �˻��� EtherCAT Slave���� Process Data�� IO Map�� ��ġ�մϴ�.
    // ����: 1, ����: 0
    DAO_ENGINE_API int DaoEngine_MapProcessData();

    // ������ Process Data ���� ����� �����ɴϴ�.
    // ����: 1, ����: 0
    DAO_ENGINE_API int DaoEngine_GetProcessDataMapInfo(
        DaoProcessDataMapInfo* mapInfo);

    // ������ Slave�� PDO ����� ũ�⸦ �����ɴϴ�.
    // slaveListIndex�� 0���� �����մϴ�.
    // ����: 1, ����: 0
    DAO_ENGINE_API int DaoEngine_GetSlavePdoInfo(
        int slaveListIndex,
        DaoSlavePdoInfo* pdoInfo);

    // ������ ���� Slave�� DAO ADC�� ������ PDO ������
    // �����ϴ��� �˻��մϴ�.
    //
    // ����: 1
    // ����: 0
    DAO_ENGINE_API int DaoEngine_ValidateDaoAdcPdo(
        int physicalSlaveIndex,
        DaoAdcValidationInfo* validationInfo);

    // ������ DAO ADC Slave�� SAFE-OP ���·� ��ȯ�մϴ�.
    //
    // ����: 1
    // ����: 0
    DAO_ENGINE_API int DaoEngine_RequestDaoAdcSafeOp(
        int physicalSlaveIndex);

    // ������ DAO ADC�� ������� SAFE-OP ���¿���
    // Process Data�� ��Ȯ�� �� �� �պ��մϴ�.
    //
    // ����: 1
    // ����: 0
    DAO_ENGINE_API int DaoEngine_ExchangeDaoAdcProcessDataOnce(
        int physicalSlaveIndex,
        DaoProcessExchangeInfo* exchangeInfo);

    // ������ DAO ADC�� ������� SAFE-OP ���¿���
    // ������ Ƚ����ŭ Process Data Priming�� �����մϴ�.
    //
    // ����: 1
    // ����: 0
    DAO_ENGINE_API int DaoEngine_PrimeDaoAdcProcessData(
        int physicalSlaveIndex,
        int roundCount,
        DaoPrimingInfo* primingInfo);

    // ������ DAO ADC�� SAFE-OP���� OP ���·� ��ȯ�մϴ�.
    //
    // ����: 1
    // ����: 0
    DAO_ENGINE_API int DaoEngine_RequestDaoAdcOperational(
        int physicalSlaveIndex,
        DaoOperationalInfo* operationalInfo);
    // ������ DAO ADC�� ������� OP ���¿���
    // Process Data�� 1ȸ �պ��ϰ� Input PDO 24����Ʈ�� �н��ϴ�.
    //
    // ����: 1
    // ����: 0
    DAO_ENGINE_API int DaoEngine_ReadDaoAdcOnce(
        int physicalSlaveIndex,
        DaoAdcReadInfo* readInfo);

    // ������ ���� Slave�� �ֽ� ADC ��Ÿ�� ������ ��ȯ�մϴ�.
    //
    // ����: 1
    // ����: 0
    DAO_ENGINE_API int DaoEngine_GetDaoAdcRuntimeInfo(
        int physicalSlaveIndex,
        DaoAdcRuntimeInfo* runtimeInfo);

    DAO_ENGINE_API int DaoEngine_GetAdcRuntimeInfo(
        int logicalAdcIndex,
        DaoAdcRuntimeInfo* runtimeInfo); 

    DAO_ENGINE_API int DaoEngine_SetAdcZero(
		int logicalAdcIndex); // ������ ���� ADC�� Zero�� �����մϴ�. �񵿱�

    DAO_ENGINE_API int DaoEngine_SetAdcCalibration(
        int logicalAdcIndex,
		double referenceValue); // ������ ���� ADC�� Calibration Scale�� �����մϴ�. �񵿱�

    DAO_ENGINE_API int DaoEngine_SetAdcPowerLineFilterMode(
        int logicalAdcIndex,
        int mode); // �������͸�弱��

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

    // ������ ���� Servo�� �ֽ� ��Ÿ�� ������ ��ȯ�մϴ�.
    // logicalServoIndex�� 0���� �����մϴ�.
    // ����: 1, ����: 0
    DAO_ENGINE_API int DaoEngine_GetServoRuntimeInfo(
        int logicalServoIndex,
        DaoServoRuntimeInfo* runtimeInfo);

    // ������ ���� IO�� �ֽ� ��Ÿ�� ������ ��ȯ�մϴ�.
    // logicalIoIndex�� 0���� �����մϴ�.
    // ����: 1, ����: 0
    DAO_ENGINE_API int DaoEngine_GetIoRuntimeInfo(
        int logicalIoIndex,
        DaoIoRuntimeInfo* runtimeInfo);

    DAO_ENGINE_API int DaoEngine_ServoOn( 
		int logicalServoIndex); // ������ ���� Servo�� On ���·� ��ȯ�մϴ�. �񵿱�

    DAO_ENGINE_API int DaoEngine_ServoOff(
		int logicalServoIndex); // ������ ���� Servo�� Off ���·� ��ȯ�մϴ�.�񵿱�

    DAO_ENGINE_API int DaoEngine_ServoHome(
        int logicalServoIndex,
        unsigned int timeoutMs); // ������ ���� Servo�� Homing ���·� ��ȯ�մϴ�.�񵿱�


    DAO_ENGINE_API int DaoEngine_ServoMoveAbsolute(
        int logicalServoIndex,
        int targetPosition,
        unsigned int profileVelocity,
        unsigned int profileAcceleration,
        unsigned int profileDeceleration,
		unsigned int timeoutMs); // ������ ���� Servo�� ���� ��ġ�� �̵��մϴ�.�񵿱�

    DAO_ENGINE_API int DaoEngine_ServoVelocity(
        int logicalServoIndex,
        int targetVelocity,
        unsigned int acceleration,
		unsigned int deceleration);  // ������ ���� Servo�� �ӵ�������� �����մϴ�.�񵿱�

    DAO_ENGINE_API int DaoEngine_ServoJogPositive(
        int logicalServoIndex,
        int speed,
        unsigned int acceleration,
		unsigned int deceleration); // ������ ���� Servo�� ������ jog �ӵ�������� �����մϴ�.�񵿱�

    DAO_ENGINE_API int DaoEngine_ServoJogNegative(
        int logicalServoIndex,
        int speed,
        unsigned int acceleration,
		unsigned int deceleration); // ������ ���� Servo�� ������ jog �ӵ�������� �����մϴ�.�񵿱�

    DAO_ENGINE_API int DaoEngine_ServoStop(
		int logicalServoIndex); // ������ ���� Servo�� ������ŵ�ϴ�.�񵿱�

    // ������ ���� Servo�� Output PDO ������ �����մϴ�.
    // logicalServoIndex�� 0���� �����մϴ�.
    // ����: 1, ����: 0
    DAO_ENGINE_API int DaoEngine_SetServoOutputCommand(
        int logicalServoIndex,
        const DaoServoOutputPdo* command);


    // ������ ���� Servo�� CiA402 ������带 �����մϴ�.
    //
    // mode:
    // 1 = Profile Position
    // 3 = Profile Velocity
    // 6 = Homing
    //
    // ��ȯ����� ������ ���¿��� ȣ���ؾ� �մϴ�.
    // ����: 1, ����: 0
    DAO_ENGINE_API int DaoEngine_SetServoOperationMode(
        int logicalServoIndex,
        signed char mode);

    // ������ ���� IO�� ��°��� �����մϴ�.
    // logicalIoIndex�� 0���� �����մϴ�.
    // ����: 1, ����: 0
    DAO_ENGINE_API int DaoEngine_SetIoOutputCommand(
        int logicalIoIndex,
        unsigned short outputValue);

    // EtherCAT ��ȯ��� �����带 �����մϴ�.
    // ����: 1
    // ����: 0
    DAO_ENGINE_API int DaoEngine_StartCommunication();

    // EtherCAT ��ȯ��� �����带 �����մϴ�.
    DAO_ENGINE_API void DaoEngine_StopCommunication();

    // ���� ��ȯ��� �����尡 ���� ������ ��ȯ�մϴ�.
    // ���� ��: 1
    // ����: 0
    DAO_ENGINE_API int DaoEngine_IsCommunicationRunning();

}