#pragma once

#include <cstdint>

enum UtmMachineState
{
    UTM_MACHINE_INITIALIZING = 0,
    UTM_MACHINE_READY,
    UTM_MACHINE_MANUAL,
    UTM_MACHINE_RUNNING,
    UTM_MACHINE_STOPPING,
    UTM_MACHINE_STOPPED,
    UTM_MACHINE_EMERGENCY,
    UTM_MACHINE_FAULT
};

enum UtmStartupPhase
{
    UTM_STARTUP_NONE = 0,
    UTM_STARTUP_CONFIG_LOADED,
    UTM_STARTUP_ADAPTER_SELECTING,
    UTM_STARTUP_ADAPTER_OPENING,
    UTM_STARTUP_SLAVE_SCANNING,
    UTM_STARTUP_DEVICE_CHECK,
    UTM_STARTUP_PDO_PREPARING,
    UTM_STARTUP_COMMUNICATION_STARTING,
    UTM_STARTUP_COMMUNICATION_WAIT,
    UTM_STARTUP_SAFETY_CHECK,
    UTM_STARTUP_SERVO_PREPARING,
    UTM_STARTUP_SERVO_ON_WAIT,
    UTM_STARTUP_READY_STABILIZING,
    UTM_STARTUP_COMPLETE,
    UTM_STARTUP_FAILED,
    UTM_STARTUP_SHUTDOWN_MOTION_STOP,
    UTM_STARTUP_SHUTDOWN_SERVO_OFF,
    UTM_STARTUP_SHUTDOWN_COMMUNICATION,
    UTM_STARTUP_SHUTDOWN_COMPLETE
};

enum UtmStartupFault
{
    UTM_STARTUP_FAULT_NONE = 0,
    UTM_STARTUP_FAULT_CONFIG_INVALID,
    UTM_STARTUP_FAULT_ADAPTER_NOT_FOUND,
    UTM_STARTUP_FAULT_ADAPTER_OPEN_FAILED,
    UTM_STARTUP_FAULT_SLAVE_SCAN_FAILED,
    UTM_STARTUP_FAULT_SERVO_NOT_FOUND,
    UTM_STARTUP_FAULT_ADC_NOT_FOUND,
    UTM_STARTUP_FAULT_IO_NOT_FOUND,
    UTM_STARTUP_FAULT_ENCODER_REQUIRED_NOT_FOUND,
    UTM_STARTUP_FAULT_PDO_PREPARE_FAILED,
    UTM_STARTUP_FAULT_COMMUNICATION_START_FAILED,
    UTM_STARTUP_FAULT_COMMUNICATION_TIMEOUT,
    UTM_STARTUP_FAULT_COMMUNICATION_STALE,
    UTM_STARTUP_FAULT_SERVO_FAULT,
    UTM_STARTUP_FAULT_SERVO_ON_FAILED,
    UTM_STARTUP_FAULT_SERVO_ON_TIMEOUT,
    UTM_STARTUP_FAULT_EMERGENCY_ACTIVE,
    UTM_STARTUP_FAULT_EXTERNAL_STOP_ACTIVE
};

enum UtmStopReason
{
    UTM_STOP_NONE = 0,
    UTM_STOP_EMERGENCY,
    UTM_STOP_UPPER_LIMIT,
    UTM_STOP_LOWER_LIMIT,
    UTM_STOP_EXTERNAL_STOP,
    UTM_STOP_SERVO_FAULT,
    UTM_STOP_SERVO_NOT_READY,
    UTM_STOP_COMMUNICATION_FAULT,
    UTM_STOP_USER_STOP,
    UTM_STOP_JOG_CONFLICT,
    UTM_STOP_OVERLOAD,
    UTM_STOP_TARGET_POSITION,
    UTM_STOP_TARGET_FORCE,
    UTM_STOP_BREAK_DETECTED,
    UTM_STOP_TIMEOUT,
    UTM_STOP_CALIBRATION_FAULT
};

enum UtmStopAction
{
    UTM_STOP_ACTION_NONE = 0,
    UTM_STOP_ACTION_INVALIDATE_MOTION,
    UTM_STOP_ACTION_CONTROLLED_STOP,
    UTM_STOP_ACTION_QUICK_STOP,
    UTM_STOP_ACTION_DISABLE_MOTION
};

enum UtmMotionDirection
{
    UTM_DIRECTION_DOWN = -1,
    UTM_DIRECTION_COMPRESSION = UTM_DIRECTION_DOWN,
    UTM_DIRECTION_NONE = 0,
    UTM_DIRECTION_UP = 1,
    UTM_DIRECTION_TENSION = UTM_DIRECTION_UP,
    UTM_DIRECTION_UNKNOWN = 2
};

enum UtmMotionType
{
    UTM_MOTION_NONE = 0,
    UTM_MOTION_JOG,
    UTM_MOTION_ABSOLUTE,
    UTM_MOTION_INCREMENTAL,
    UTM_MOTION_VELOCITY,
    UTM_MOTION_MOVE_TO_FORCE,
    UTM_MOTION_HOLD_FORCE
};

enum UtmMotionState
{
    UTM_MOTION_STATE_IDLE = 0,
    UTM_MOTION_STATE_PREPARING,
    UTM_MOTION_STATE_COMMAND_SENT,
    UTM_MOTION_STATE_MOVING,
    UTM_MOTION_STATE_STOPPING,
    UTM_MOTION_STATE_COMPLETED,
    UTM_MOTION_STATE_ABORTED,
    UTM_MOTION_STATE_FAILED
};

enum UtmMotionFailureReason
{
    UTM_MOTION_FAILURE_NONE = 0,
    UTM_MOTION_FAILURE_INVALID_REQUEST,
    UTM_MOTION_FAILURE_NOT_READY,
    UTM_MOTION_FAILURE_BUSY,
    UTM_MOTION_FAILURE_POSITION_OVERFLOW,
    UTM_MOTION_FAILURE_VELOCITY_OVERFLOW,
    UTM_MOTION_FAILURE_LIMIT_BLOCKED,
    UTM_MOTION_FAILURE_COMMAND_REJECTED,
    UTM_MOTION_FAILURE_SERVO_ERROR,
    UTM_MOTION_FAILURE_SERVO_TIMEOUT,
    UTM_MOTION_FAILURE_SAFETY_STOP,
    UTM_MOTION_FAILURE_USER_STOP,
    UTM_MOTION_FAILURE_FORCE_INVALID,
    UTM_MOTION_FAILURE_MAX_TRAVEL,
    UTM_MOTION_FAILURE_TIMEOUT
};

enum UtmForcePhase
{
    UTM_FORCE_PHASE_NONE = 0,
    UTM_FORCE_PHASE_APPROACH,
    UTM_FORCE_PHASE_FINE_APPROACH,
    UTM_FORCE_PHASE_HOLDING,
    UTM_FORCE_PHASE_CORRECTING,
    UTM_FORCE_PHASE_COMPLETED,
    UTM_FORCE_PHASE_ABORTED,
    UTM_FORCE_PHASE_FAILED
};

enum UtmCommandSource
{
    UTM_COMMAND_SOURCE_UI = 0,
    UTM_COMMAND_SOURCE_EXTERNAL_GO,
    UTM_COMMAND_SOURCE_DIGITAL_JOG,
    UTM_COMMAND_SOURCE_INTERNAL,
    UTM_COMMAND_SOURCE_REMOTE,
    UTM_COMMAND_SOURCE_SEQUENCER,
    UTM_COMMAND_SOURCE_CALIBRATION
};

enum UtmJogSourceMask
{
    UTM_JOG_SOURCE_MASK_UI =
        1U << UTM_COMMAND_SOURCE_UI,
    UTM_JOG_SOURCE_MASK_DIGITAL =
        1U << UTM_COMMAND_SOURCE_DIGITAL_JOG,
    UTM_JOG_SOURCE_MASK_REMOTE =
        1U << UTM_COMMAND_SOURCE_REMOTE
};

enum UtmCommandType
{
    UTM_COMMAND_NONE = 0,
    UTM_COMMAND_ENTER_READY,
    UTM_COMMAND_ENTER_MANUAL,
    UTM_COMMAND_EXIT_MANUAL,
    UTM_COMMAND_START,
    UTM_COMMAND_STOP,
    UTM_COMMAND_ACKNOWLEDGE_STOP,
    UTM_COMMAND_JOG_UP_REQUEST,
    UTM_COMMAND_JOG_DOWN_REQUEST,
    UTM_COMMAND_JOG_STOP_REQUEST
};

struct UtmIoMapping
{
    unsigned char emergencyBit = 0;
    unsigned char upperLimitBit = 1;
    unsigned char lowerLimitBit = 2;
    unsigned char jogUpBit = 3;
    unsigned char jogDownBit = 4;
    unsigned char externalGoBit = 5;
    unsigned char externalStopBit = 6;
    unsigned char reservedBit = 7;

    unsigned short activeLowMask = 0;
};

struct UtmEngineConfig
{
    int adapterIndex = 0;

    int logicalServoIndex = 0;
    int logicalAdcIndex = 0;
    int logicalIoIndex = 0;

    int useOptionalEncoder = 0;
    int logicalEncoderIndex = 0;
    int encoderChannel = 1;

    unsigned long long controlPeriodNs = 2000000ULL;
    unsigned int staleInputTimeoutMs = 20;

    UtmIoMapping ioMapping{};
};

struct UtmStartupConfig
{
    char ethercatAdapterName[128]{};

    int logicalServoIndex = 0;
    int logicalAdcIndex = 0;
    int logicalIoIndex = 0;
    int logicalEncoderIndex = 0;
    int encoderRequired = 0;

    int autoServoOn = 1;

    unsigned int communicationStabilizeMs = 500;
    unsigned int communicationTimeoutMs = 5000;
    unsigned int servoOnTimeoutMs = 3000;
    unsigned int readyStabilizeMs = 300;
    unsigned int shutdownTimeoutMs = 3000;

    unsigned long long controlPeriodNs = 2000000ULL;
    unsigned int staleInputTimeoutMs = 20;
    UtmIoMapping ioMapping{};
};

struct UtmSourceFreshness
{
    unsigned long long updateCount = 0;
    unsigned long long lastFreshTimestampNs = 0;
    unsigned long long staleDurationNs = 0;

    int runtimeRead = 0;
    int validInput = 0;
    int freshInput = 0;
    int communicationValid = 0;
};

struct UtmInputSnapshot
{
    unsigned long long timestampNs = 0;
    unsigned long long cycle = 0;

    double forceN = 0.0;
    int forceValid = 0;

    std::int32_t servoActualPosition = 0;
    int servoPositionValid = 0;

    std::int32_t servoActualVelocity = 0;
    int servoVelocityAvailable = 0;

    int encoderPresent = 0;
    int encoderValid = 0;
    std::int32_t encoderSignedCount = 0;
    double encoderPosition = 0.0;

    unsigned short rawDigitalInputs = 0;

    int emergency = 0;
    int upperLimit = 0;
    int lowerLimit = 0;
    int jogUp = 0;
    int jogDown = 0;
    int externalGo = 0;
    int externalStop = 0;

    int servoCommunicationValid = 0;
    int servoReady = 0;
    int servoOn = 0;
    int servoFault = 0;

    unsigned short servoStatusWord = 0;
    unsigned short servoOperationState = 0;
    signed char servoOperationMode = 0;

    int servoNegativeLimitInput = 0;
    int servoPositiveLimitInput = 0;
    int servoHomeInput = 0;
    int servoStopInput = 0;
    int servoStoActive = 0;

    int basicCommunicationRunning = 0;
    int requiredDevicesValid = 0;
    int communicationValid = 0;

    UtmSourceFreshness servoSource{};
    UtmSourceFreshness adcSource{};
    UtmSourceFreshness ioSource{};
    UtmSourceFreshness encoderSource{};
};

struct UtmSafetyContext
{
    int motionActive = 0;
    int requestedDirection = UTM_DIRECTION_NONE;
    int machineState = UTM_MACHINE_INITIALIZING;
};

struct UtmJogConfig
{
    // Servo target velocity 변환: mm/min * servoUnitsPerMm / 60.
    double servoUnitsPerMm = 0.0;
    double physicalJogSpeedMmPerMin = 0.0;

    // Basic Engine Servo Profile Velocity의 UU/s^2 단위입니다.
    unsigned int acceleration = 1000;
    unsigned int deceleration = 1000;

    // UTM UP을 Servo 양의 방향으로 매핑하면 +1, 반대이면 -1입니다.
    int servoDirectionSign = 1;
};

// 기존 UtmJogConfig ABI를 유지하면서 Jog speed 범위를 추가합니다.
struct UtmJogConfigV2
{
    UtmJogConfig config{};
    double minJogSpeedMmPerMin = 0.0;
    double maxJogSpeedMmPerMin = 0.0;
};

// Raw engineering force is converted to an UP-positive logical axis first.
// Set forceDirectionSign to +1 or -1 to match the loadcell installation.
struct UtmForceControlConfig
{
    double approachSpeedMmPerMin = 2.0;
    double mediumSpeedMmPerMin = 0.5;
    double fineSpeedMmPerMin = 0.1;
    double reverseSpeedMmPerMin = 0.05;
    double mediumErrorN = 5.0;
    double fineErrorN = 1.0;
    double toleranceN = 0.1;
    unsigned int acceleration = 1000;
    unsigned int deceleration = 1000;
    double maxTravelMm = 10.0;
    double timeoutSec = 60.0;
    int forceDirectionSign = 1;
};

struct UtmMotionCommandV2
{
    int motionType = UTM_MOTION_NONE;
    int direction = UTM_DIRECTION_NONE;
    double targetPositionMm = 0.0;
    double incrementalDistanceMm = 0.0;
    double speedMmPerMin = 0.0;
    unsigned int acceleration = 0;
    unsigned int deceleration = 0;
    unsigned int timeoutMs = 0;
    double targetForceN = 0.0;
    double holdTimeSec = 0.0;
    double maxTravelMm = 0.0;
    double toleranceN = 0.0;
    int source = UTM_COMMAND_SOURCE_UI;
};

struct UtmJogRuntimeInfo
{
    int jogActive = 0;
    int jogDirection = UTM_DIRECTION_NONE;
    double jogSpeedMmPerMin = 0.0;
    int jogConflict = 0;
    unsigned int activeJogSourceMask = 0;
    int requestedMotionDirection = UTM_DIRECTION_NONE;
    int servoTargetVelocity = 0;
};

struct UtmStopRequest
{
    int requested = 0;
    int latched = 0;

    int primaryReason = UTM_STOP_NONE;
    unsigned long long reasonMask = 0;
    int action = UTM_STOP_ACTION_NONE;

    unsigned long long firstTimestampNs = 0;
    unsigned long long firstCycle = 0;
    unsigned long long lastTimestampNs = 0;
    unsigned long long lastCycle = 0;
    unsigned long long latchSequence = 0;
};

struct UtmCommandRequest
{
    unsigned long long commandId = 0;
    unsigned long long commandEpoch = 0;
    unsigned long long requestedTimestampNs = 0;

    int source = UTM_COMMAND_SOURCE_UI;
    int type = UTM_COMMAND_NONE;
    int requestedDirection = UTM_DIRECTION_NONE;
};

struct UtmRuntimeInfo
{
    unsigned long long publishedTimestampNs = 0;
    unsigned long long controlCycle = 0;

    int machineState = UTM_MACHINE_INITIALIZING;
    UtmInputSnapshot input{};
    UtmStopRequest stop{};

    unsigned long long commandEpoch = 0;
    unsigned long long lastAcceptedCommandId = 0;
    int lastAcceptedCommand = UTM_COMMAND_NONE;

    int initialized = 0;
    int controlLoopRunning = 0;

    int requiredServoPresent = 0;
    int requiredAdcPresent = 0;
    int requiredIoPresent = 0;
    int optionalEncoderPresent = 0;

    unsigned long long cycleOverrunCount = 0;
    unsigned long long lastCycleTimeNs = 0;
    unsigned long long maximumCycleTimeNs = 0;
};

// 기존 UtmRuntimeInfo ABI를 유지하면서 Jog Runtime을 제공합니다.
struct UtmRuntimeInfoV2
{
    UtmRuntimeInfo runtime{};
    UtmJogRuntimeInfo jog{};
};

struct UtmStartupRuntimeInfo
{
    int startupPhase = UTM_STARTUP_NONE;
    int startupFault = UTM_STARTUP_FAULT_NONE;
    int startupComplete = 0;

    char selectedAdapterName[128]{};
    int selectedAdapterIndex = -1;
    int slaveCount = 0;

    int servoFound = 0;
    int adcFound = 0;
    int ioFound = 0;
    int encoderFound = 0;
};

struct UtmRuntimeInfoV3
{
    UtmRuntimeInfoV2 runtime{};
    UtmStartupRuntimeInfo startup{};
};

struct UtmGeneralMotionRuntimeInfo
{
    int motionActive = 0;
    int motionType = UTM_MOTION_NONE;
    int motionState = UTM_MOTION_STATE_IDLE;
    int motionDirection = UTM_DIRECTION_NONE;

    double machinePositionMm = 0.0;
    double testPositionMm = 0.0;
    double testZeroOffsetMm = 0.0;
    int positionZeroValid = 0;

    double targetPositionMm = 0.0;
    double incrementalDistanceMm = 0.0;
    double commandSpeedMmPerMin = 0.0;

    int servoActualPosition = 0;
    int servoTargetPosition = 0;
    int servoTargetVelocity = 0;

    int motionSource = UTM_COMMAND_SOURCE_INTERNAL;
    int motionComplete = 0;
    int motionAborted = 0;
    int motionFailureReason = UTM_MOTION_FAILURE_NONE;
};

struct UtmRuntimeInfoV4
{
    UtmRuntimeInfoV3 runtime{};
    UtmGeneralMotionRuntimeInfo motion{};
};

struct UtmForceMotionRuntimeInfo
{
    int forceMotionActive = 0;
    int forcePhase = UTM_FORCE_PHASE_NONE;
    double currentForceN = 0.0;
    double targetForceN = 0.0;
    double signedTargetForceN = 0.0;
    double forceErrorN = 0.0;
    int forceValid = 0;
    double currentCommandSpeedMmPerMin = 0.0;
    double holdTimeTargetSec = 0.0;
    double holdTimeElapsedSec = 0.0;
    double forceToleranceN = 0.0;
    int forceTargetReached = 0;
    int forceHoldStable = 0;
    double forceMotionStartPositionMm = 0.0;
    double forceMotionTravelMm = 0.0;
    double maxTravelMm = 0.0;
    double timeoutElapsedSec = 0.0;
    double timeoutRemainingSec = 0.0;
};

struct UtmRuntimeInfoV5
{
    UtmRuntimeInfoV4 runtime{};
    UtmForceMotionRuntimeInfo forceMotion{};
};

enum UtmCompletionReason
{
    UTM_COMPLETION_NONE = 0,
    UTM_COMPLETION_TARGET_POSITION,
    UTM_COMPLETION_TARGET_FORCE,
    UTM_COMPLETION_BREAK_DETECTED,
    UTM_COMPLETION_MAX_FORCE_REACHED,
    UTM_COMPLETION_MAX_TRAVEL_REACHED,
    UTM_COMPLETION_TIME_COMPLETED,
    UTM_COMPLETION_INPUT_MATCHED,
    UTM_COMPLETION_OUTPUT_COMPLETED,
    UTM_COMPLETION_ZERO_COMPLETED,
    UTM_COMPLETION_END
};

struct UtmMachineProtectionConfig
{
    int overloadEnabled = 1;
    double maxAllowedForceN = 1000.0;
};

struct UtmStopConditionConfig
{
    int enableMaxForce = 0;
    double maxForceN = 0.0;
    int enableMaxTravel = 0;
    double maxTravelMm = 0.0;
    int enableTimeout = 0;
    double timeoutSec = 0.0;
    int enableBreakDetection = 0;
    double minimumBreakPeakN = 0.0;
    double breakDropPercent = 0.0;
    unsigned int breakConfirmMs = 0;
};

enum UtmSequenceStepType
{
    UTM_SEQUENCE_STEP_NONE = 0,
    UTM_SEQUENCE_STEP_ZERO_FORCE,
    UTM_SEQUENCE_STEP_ZERO_POSITION,
    UTM_SEQUENCE_STEP_ZERO_ENCODER,
    UTM_SEQUENCE_STEP_MOVE_ABSOLUTE,
    UTM_SEQUENCE_STEP_MOVE_INCREMENTAL,
    UTM_SEQUENCE_STEP_MOVE_VELOCITY,
    UTM_SEQUENCE_STEP_MOVE_TO_FORCE,
    UTM_SEQUENCE_STEP_HOLD_FORCE,
    UTM_SEQUENCE_STEP_WAIT_TIME,
    UTM_SEQUENCE_STEP_WAIT_INPUT,
    UTM_SEQUENCE_STEP_SET_OUTPUT,
    UTM_SEQUENCE_STEP_PULSE_OUTPUT,
    UTM_SEQUENCE_STEP_LOOP_START,
    UTM_SEQUENCE_STEP_LOOP_END,
    UTM_SEQUENCE_STEP_END
};

enum UtmSequenceState
{
    UTM_SEQUENCE_STATE_EMPTY = 0,
    UTM_SEQUENCE_STATE_EDITING,
    UTM_SEQUENCE_STATE_VALIDATED,
    UTM_SEQUENCE_STATE_COMMITTED,
    UTM_SEQUENCE_STATE_RUNNING,
    UTM_SEQUENCE_STATE_STOPPING,
    UTM_SEQUENCE_STATE_COMPLETED,
    UTM_SEQUENCE_STATE_ABORTED,
    UTM_SEQUENCE_STATE_FAILED
};

enum UtmSequenceStepState
{
    UTM_SEQUENCE_STEP_STATE_IDLE = 0,
    UTM_SEQUENCE_STEP_STATE_STARTING,
    UTM_SEQUENCE_STEP_STATE_RUNNING,
    UTM_SEQUENCE_STEP_STATE_COMPLETED,
    UTM_SEQUENCE_STEP_STATE_ABORTED,
    UTM_SEQUENCE_STEP_STATE_FAILED
};

enum UtmSequenceValidationError
{
    UTM_SEQUENCE_VALIDATION_NONE = 0,
    UTM_SEQUENCE_VALIDATION_STEP_COUNT,
    UTM_SEQUENCE_VALIDATION_END_REQUIRED,
    UTM_SEQUENCE_VALIDATION_UNSUPPORTED_STEP,
    UTM_SEQUENCE_VALIDATION_INVALID_PARAMETER,
    UTM_SEQUENCE_VALIDATION_INVALID_DIRECTION,
    UTM_SEQUENCE_VALIDATION_INVALID_IO_BIT,
    UTM_SEQUENCE_VALIDATION_LOOP_PAIRING,
    UTM_SEQUENCE_VALIDATION_LOOP_COUNT,
    UTM_SEQUENCE_VALIDATION_LOOP_DEPTH,
    UTM_SEQUENCE_VALIDATION_ENCODER_UNAVAILABLE,
    UTM_SEQUENCE_VALIDATION_FORCE_LIMIT_OVERLOAD,
    UTM_SEQUENCE_VALIDATION_VELOCITY_STOP_REQUIRED
};

constexpr unsigned int UTM_SEQUENCE_MAX_STEPS = 256;
constexpr unsigned int UTM_SEQUENCE_MAX_LOOP_DEPTH = 8;

// Fixed-size POD transport record. Units: N, mm, mm/min, seconds.
struct UtmSequenceStep
{
    unsigned int stepIndex = 0;
    int stepType = UTM_SEQUENCE_STEP_NONE;
    int direction = UTM_DIRECTION_NONE;
    double positionMm = 0.0;
    double distanceMm = 0.0;
    double speedMmPerMin = 0.0;
    double forceN = 0.0;
    double holdTimeSec = 0.0;
    double toleranceN = 0.0;
    double durationSec = 0.0;
    unsigned int acceleration = 0;
    unsigned int deceleration = 0;
    unsigned int timeoutMs = 0;
    unsigned int ioBit = 0;
    int ioState = 0;
    unsigned int loopCount = 0;
    UtmStopConditionConfig stopConditions{};
    unsigned int sourceMetadata = 0;
};

struct UtmSequenceDefinition
{
    unsigned int abiVersion = 1;
    unsigned int stepCount = 0;
    UtmSequenceStep steps[UTM_SEQUENCE_MAX_STEPS]{};
};

struct UtmStopConditionRuntime
{
    int active = 0;
    double peakForceMagnitudeN = 0.0;
    double currentForceMagnitudeN = 0.0;
    double travelMm = 0.0;
    double elapsedSec = 0.0;
    unsigned int breakConfirmElapsedMs = 0;
    int completionReason = UTM_COMPLETION_NONE;
};

struct UtmSequenceRuntimeInfo
{
    int sequenceLoaded = 0;
    int sequenceValidated = 0;
    int sequenceCommitted = 0;
    int sequenceRunning = 0;
    int sequenceState = UTM_SEQUENCE_STATE_EMPTY;
    unsigned int stepCount = 0;
    unsigned int currentStepIndex = 0;
    int currentStepType = UTM_SEQUENCE_STEP_NONE;
    int currentStepState = UTM_SEQUENCE_STEP_STATE_IDLE;
    unsigned int loopDepth = 0;
    unsigned int currentLoopIteration = 0;
    unsigned int currentLoopCount = 0;
    double sequenceElapsedSec = 0.0;
    double stepElapsedSec = 0.0;
    int lastStepResult = UTM_SEQUENCE_STEP_STATE_IDLE;
    int lastStepCompletionReason = UTM_COMPLETION_NONE;
    int validationError = UTM_SEQUENCE_VALIDATION_NONE;
    int validationErrorStepIndex = -1;
    int sequenceComplete = 0;
    int sequenceAborted = 0;
    int sequenceFailed = 0;
    unsigned short sequenceOwnedOutputs = 0;
    unsigned long long recordingSessionId = 0;
    int recordingActive = 0;
    int recordingLastEvent = 0; // 1=start, 2=complete, 3=abort
    UtmStopConditionRuntime stopCondition{};
};

struct UtmRuntimeInfoV6
{
    UtmRuntimeInfoV5 runtime{};
    UtmSequenceRuntimeInfo sequence{};
};

struct UtmCommunicationRuntimeInfoV1
{
    unsigned int version=1;
    int communicationState=0;
    unsigned long long incidentGeneration=0;
    int recoveryActive=0,recoveryStage=0,recoverySucceeded=0,recoveryFailed=0;
    unsigned int recoveryAttempt=0;
    unsigned long long recoveryElapsedMs=0,totalIncidentCount=0,recoveredIncidentCount=0,recoveryFailureCount=0;
    unsigned long long maximumRecoveryDurationMs=0,lastIncidentTimestampNs=0;
    int expectedWkc=0,currentWkc=0,minimumWkc=0;
    unsigned long long consecutiveBadWkc=0,maximumConsecutiveBadWkc=0;
    unsigned int consecutiveGoodWkc=0,rollingHourIncidentCount=0;
    int communicationUnstableWarning=0,incidentMotionActive=0,motionInterrupted=0;
    int failedSlaveIndex=0;unsigned short failedSlaveState=0,failedSlaveAlStatus=0;
    int incidentCommandType=0,incidentCommandSource=0,incidentSequenceRunning=0,incidentSequenceStep=0;
    int incidentCalibrationActive=0,postRecoveryServoState=0,safeAlignmentResult=0;
};

constexpr unsigned int UTM_MAX_ADAPTERS = 32;

struct UtmAdapterInfo
{
    char name[128]{};
    char description[256]{};
};

struct UtmConnectionConfig
{
    UtmStartupConfig startup{};
    UtmJogConfigV2 jog{};
    UtmForceControlConfig force{};
    UtmMachineProtectionConfig protection{};
};

struct UtmCalibrationRuntimeInfo
{
    double engineeringForceN = 0.0;
    int adcRaw0 = 0;
    int adcRaw1 = 0;
    int adcRaw2 = 0;
    int adcRaw3 = 0;
    double lowLevelFiltered = 0.0;
    double powerLineFiltered = 0.0;
    double zeroedValue = 0.0;
    double calibratedValue = 0.0;
    int forceValid = 0;
    int forceZeroValid = 0;
    int forceCalibrationValid = 0;
    double forceZeroOffset = 0.0;
    double forceCalibrationScale = 1.0;
    int forceCaptureActive = 0;
    int forceCaptureType = 0;
    unsigned int forceCaptureCollectedCount = 0;
    unsigned int forceCaptureSampleCount = 0;
    int encoderPresent = 0;
    int encoderValid = 0;
    unsigned int encoderRawCount = 0;
    int encoderSignedCount = 0;
    double encoderEngineeringPosition = 0.0;
    double encoderCalibrationScale = 1.0;
    int encoderResetState = 0;
    int encoderResetCompletedStatus = 0;
    int positionZeroValid = 0;
    double testZeroOffsetMm = 0.0;
    int lowLevelFilterEnabled = 1;
    double lowLevelFilterAlpha = 0.1;
    int powerLineFilterMode = 0;
    int medianFilterEnabled = 1;
    unsigned int movingAverageSampleCount = 16;
};

enum UtmComplianceCalibrationMode
{
    UTM_COMPLIANCE_MODE_COMPRESSION = 1,
    UTM_COMPLIANCE_MODE_TENSION = 2
};

enum UtmComplianceCalibrationState
{
    UTM_COMPLIANCE_CAL_IDLE = 0,
    UTM_COMPLIANCE_CAL_VALIDATING,
    UTM_COMPLIANCE_CAL_ZEROING_FORCE,
    UTM_COMPLIANCE_CAL_CAPTURING_REFERENCE,
    UTM_COMPLIANCE_CAL_PRECHECK_APPROACH,
    UTM_COMPLIANCE_CAL_PRECHECK_STABILIZING,
    UTM_COMPLIANCE_CAL_STOPPING_PRECHECK,
    UTM_COMPLIANCE_CAL_PRECHECK_PASSED,
    UTM_COMPLIANCE_CAL_WAITING_FULL_START,
    UTM_COMPLIANCE_CAL_APPROACHING_TARGET,
    UTM_COMPLIANCE_CAL_FINE_APPROACH,
    UTM_COMPLIANCE_CAL_STABILIZING,
    UTM_COMPLIANCE_CAL_CAPTURING_POINT,
    UTM_COMPLIANCE_CAL_NEXT_POINT,
    UTM_COMPLIANCE_CAL_RELEASING_FORCE,
    UTM_COMPLIANCE_CAL_STOPPING_RELEASE,
    UTM_COMPLIANCE_CAL_RETURNING,
    UTM_COMPLIANCE_CAL_COMPLETE_PENDING_SAVE,
    UTM_COMPLIANCE_CAL_ABORTING,
    UTM_COMPLIANCE_CAL_ABORTED,
    UTM_COMPLIANCE_CAL_FAULTED,
    UTM_COMPLIANCE_CAL_PRECHECK_EXPIRED
};

enum UtmComplianceCalibrationFault
{
    UTM_COMPLIANCE_FAULT_NONE = 0,
    UTM_COMPLIANCE_FAULT_INVALID_CONFIG,
    UTM_COMPLIANCE_FAULT_MACHINE_NOT_READY,
    UTM_COMPLIANCE_FAULT_FORCE_ZERO,
    UTM_COMPLIANCE_FAULT_EMERGENCY,
    UTM_COMPLIANCE_FAULT_EXTERNAL_STOP,
    UTM_COMPLIANCE_FAULT_SERVO,
    UTM_COMPLIANCE_FAULT_UPPER_LIMIT,
    UTM_COMPLIANCE_FAULT_LOWER_LIMIT,
    UTM_COMPLIANCE_FAULT_COMMUNICATION,
    UTM_COMPLIANCE_FAULT_OVERLOAD,
    UTM_COMPLIANCE_FAULT_HARD_FORCE,
    UTM_COMPLIANCE_FAULT_MAX_TRAVEL,
    UTM_COMPLIANCE_FAULT_PRECHECK_TRAVEL,
    UTM_COMPLIANCE_FAULT_TIMEOUT,
    UTM_COMPLIANCE_FAULT_RELEASE_TRAVEL,
    UTM_COMPLIANCE_FAULT_RELEASE_TIMEOUT,
    UTM_COMPLIANCE_FAULT_WRONG_FORCE_POLARITY,
    UTM_COMPLIANCE_FAULT_WRONG_POSITION_DIRECTION,
    UTM_COMPLIANCE_FAULT_INSUFFICIENT_FORCE_RISE,
    UTM_COMPLIANCE_FAULT_FORCE_JUMP,
    UTM_COMPLIANCE_FAULT_OVERSHOOT,
    UTM_COMPLIANCE_FAULT_OPPOSITE_FORCE,
    UTM_COMPLIANCE_FAULT_FORCE_INVALID,
    UTM_COMPLIANCE_FAULT_MOTION,
    UTM_COMPLIANCE_FAULT_USER_ABORT,
    UTM_COMPLIANCE_FAULT_COMMUNICATION_INTERRUPTED
};

constexpr unsigned int UTM_COMPLIANCE_MAX_POINTS = 64;

struct UtmComplianceCalibrationConfigV1
{
    unsigned int abiVersion = 1;
    int mode = UTM_COMPLIANCE_MODE_COMPRESSION;
    int motionDirection = UTM_DIRECTION_DOWN;
    double loadcellCapacityN = 0.0;
    double maximumForceN = 0.0;
    double manufacturerLimitN = 0.0; // 0 means unavailable
    double forceStepN = 0.0;
    double approachSpeedMmPerMin = 0.0;
    double calibrationSpeedMmPerMin = 0.0;
    double fineSpeedMmPerMin = 0.0;
    double returnSpeedMmPerMin = 0.0;
    double maximumTravelMm = 0.0;
    double precheckForceN = 0.0;
    double precheckMaximumTravelMm = 0.0;
    double forceToleranceN = 0.0;
    double releaseForceThresholdN = 0.0;
    double releaseMaximumTravelMm = 0.0;
    double minimumForceRiseN = 0.0;
    double forceRiseTravelThresholdMm = 0.0;
    double maximumForceJumpN = 0.0;
    double overshootGuardN = 0.0;
    double oppositeForceGuardN = 0.0;
    unsigned int stabilizationTimeMs = 300;
    unsigned int minimumStableSampleCount = 50;
    unsigned int pointTimeoutMs = 30000;
    unsigned int releaseTimeoutMs = 10000;
    unsigned int precheckConfirmationTimeoutMs = 30000;
};

struct UtmComplianceCalibrationPoint
{
    double forceN = 0.0;
    double deformationMm = 0.0;
};

struct UtmComplianceCalibrationRuntimeV1
{
    unsigned int abiVersion = 1;
    unsigned long long sessionId = 0;
    int state = UTM_COMPLIANCE_CAL_IDLE;
    int fault = UTM_COMPLIANCE_FAULT_NONE;
    int mode = UTM_COMPLIANCE_MODE_COMPRESSION;
    int active = 0;
    int precheckPassed = 0;
    int pendingResult = 0;
    double targetForceN = 0.0;
    double currentForceN = 0.0;
    double directionalForceN = 0.0;
    double referenceMachinePositionMm = 0.0;
    double currentMachinePositionMm = 0.0;
    double currentDeformationMm = 0.0;
    double travelUsedMm = 0.0;
    double allowedMaximumForceN = 0.0;
    double currentCommandSpeedMmPerMin = 0.0;
    unsigned int capturedPointCount = 0;
    unsigned int targetCount = 0;
    unsigned int currentTargetIndex = 0;
    unsigned int stableSampleCount = 0;
    unsigned int guardFlags = 0;
};

struct UtmAdcFilterConfig
{
    int lowLevelFilterEnabled = 1;
    double lowLevelFilterAlpha = 0.1;
    int powerLineFilterMode = 0;
    int medianFilterEnabled = 1;
    unsigned int movingAverageSampleCount = 16;
};

enum UtmDisplayForceState
{
    UTM_DISPLAY_FORCE_INVALID = 0,
    UTM_DISPLAY_FORCE_STABILIZING = 1,
    UTM_DISPLAY_FORCE_VALID = 2
};

enum UtmDisplayForceResetReason
{
    UTM_DISPLAY_FORCE_RESET_NONE = 0,
    UTM_DISPLAY_FORCE_RESET_RECONNECT,
    UTM_DISPLAY_FORCE_RESET_FORCE_ZERO,
    UTM_DISPLAY_FORCE_RESET_FORCE_CALIBRATION,
    UTM_DISPLAY_FORCE_RESET_CALIBRATION_SCALE,
    UTM_DISPLAY_FORCE_RESET_ADC_FILTER,
    UTM_DISPLAY_FORCE_RESET_SAMPLE_COUNT,
    UTM_DISPLAY_FORCE_RESET_FORCE_INVALID
};

struct UtmDisplayForceRuntimeInfo
{
    double displayForceN = 0.0;
    int state = UTM_DISPLAY_FORCE_INVALID;
    unsigned int validSampleCount = 0;
    unsigned int configuredSampleCount = 20;
    unsigned long long generation = 0;
    unsigned long long resetCount = 0;
    int lastResetReason = UTM_DISPLAY_FORCE_RESET_NONE;
};
