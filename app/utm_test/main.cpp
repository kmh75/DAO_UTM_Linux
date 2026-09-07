#include "DaoUtm.Engine.h"

#include <cstdlib>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

namespace
{
    const char* MachineStateName(int state)
    {
        switch (state)
        {
        case UTM_MACHINE_INITIALIZING: return "INITIALIZING";
        case UTM_MACHINE_READY: return "READY";
        case UTM_MACHINE_MANUAL: return "MANUAL";
        case UTM_MACHINE_RUNNING: return "RUNNING";
        case UTM_MACHINE_STOPPING: return "STOPPING";
        case UTM_MACHINE_STOPPED: return "STOPPED";
        case UTM_MACHINE_EMERGENCY: return "EMERGENCY";
        case UTM_MACHINE_FAULT: return "FAULT";
        default: return "UNKNOWN";
        }
    }

    const char* DirectionName(int direction)
    {
        switch (direction)
        {
        case UTM_DIRECTION_UP: return "UP";
        case UTM_DIRECTION_DOWN: return "DOWN";
        case UTM_DIRECTION_NONE: return "NONE";
        case UTM_DIRECTION_UNKNOWN: return "CONFLICT";
        default: return "UNKNOWN";
        }
    }

    const char* StartupPhaseName(int phase)
    {
        switch (phase)
        {
        case UTM_STARTUP_CONFIG_LOADED: return "CONFIG_LOADED";
        case UTM_STARTUP_ADAPTER_SELECTING: return "ADAPTER_SELECTING";
        case UTM_STARTUP_ADAPTER_OPENING: return "ADAPTER_OPENING";
        case UTM_STARTUP_SLAVE_SCANNING: return "SLAVE_SCANNING";
        case UTM_STARTUP_DEVICE_CHECK: return "DEVICE_CHECK";
        case UTM_STARTUP_PDO_PREPARING: return "PDO_PREPARING";
        case UTM_STARTUP_COMMUNICATION_STARTING: return "COMM_STARTING";
        case UTM_STARTUP_COMMUNICATION_WAIT: return "COMM_WAIT";
        case UTM_STARTUP_SAFETY_CHECK: return "SAFETY_CHECK";
        case UTM_STARTUP_SERVO_PREPARING: return "SERVO_PREPARING";
        case UTM_STARTUP_SERVO_ON_WAIT: return "SERVO_ON_WAIT";
        case UTM_STARTUP_READY_STABILIZING: return "READY_STABILIZING";
        case UTM_STARTUP_COMPLETE: return "COMPLETE";
        case UTM_STARTUP_FAILED: return "FAILED";
        case UTM_STARTUP_SHUTDOWN_MOTION_STOP: return "SHUTDOWN_MOTION_STOP";
        case UTM_STARTUP_SHUTDOWN_SERVO_OFF: return "SHUTDOWN_SERVO_OFF";
        case UTM_STARTUP_SHUTDOWN_COMMUNICATION: return "SHUTDOWN_COMM";
        case UTM_STARTUP_SHUTDOWN_COMPLETE: return "SHUTDOWN_COMPLETE";
        default: return "NONE";
        }
    }

    const char* MotionTypeName(int type)
    {
        switch (type)
        {
        case UTM_MOTION_JOG: return "JOG";
        case UTM_MOTION_ABSOLUTE: return "ABSOLUTE";
        case UTM_MOTION_INCREMENTAL: return "INCREMENTAL";
        case UTM_MOTION_VELOCITY: return "VELOCITY";
        case UTM_MOTION_MOVE_TO_FORCE: return "MOVE_TO_FORCE";
        case UTM_MOTION_HOLD_FORCE: return "HOLD_FORCE";
        default: return "NONE";
        }
    }

    const char* ForcePhaseName(int phase)
    {
        switch (phase)
        {
        case UTM_FORCE_PHASE_APPROACH: return "APPROACH";
        case UTM_FORCE_PHASE_FINE_APPROACH: return "FINE_APPROACH";
        case UTM_FORCE_PHASE_HOLDING: return "HOLDING";
        case UTM_FORCE_PHASE_CORRECTING: return "CORRECTING";
        case UTM_FORCE_PHASE_COMPLETED: return "COMPLETED";
        case UTM_FORCE_PHASE_ABORTED: return "ABORTED";
        case UTM_FORCE_PHASE_FAILED: return "FAILED";
        default: return "NONE";
        }
    }

    const char* MotionStateName(int state)
    {
        switch (state)
        {
        case UTM_MOTION_STATE_IDLE: return "IDLE";
        case UTM_MOTION_STATE_PREPARING: return "PREPARING";
        case UTM_MOTION_STATE_COMMAND_SENT: return "COMMAND_SENT";
        case UTM_MOTION_STATE_MOVING: return "MOVING";
        case UTM_MOTION_STATE_STOPPING: return "STOPPING";
        case UTM_MOTION_STATE_COMPLETED: return "COMPLETED";
        case UTM_MOTION_STATE_ABORTED: return "ABORTED";
        case UTM_MOTION_STATE_FAILED: return "FAILED";
        default: return "UNKNOWN";
        }
    }

    void PrintRuntime()
    {
        UtmRuntimeInfoV5 runtime{};

        if (DaoUtm_GetRuntimeV5(&runtime) == 0)
        {
            std::cout << "Runtime read failed.\n";
            return;
        }

        const UtmRuntimeInfo& base = runtime.runtime.runtime.runtime.runtime;
        const UtmInputSnapshot& input = base.input;

        std::cout
            << "\nCycle                  : " << base.controlCycle << '\n'
            << "Machine State          : "
            << MachineStateName(base.machineState) << '\n'
            << "Startup Phase / Fault  : "
            << StartupPhaseName(runtime.runtime.runtime.startup.startupPhase)
            << " / " << runtime.runtime.runtime.startup.startupFault << '\n'
            << "Startup Complete       : "
            << runtime.runtime.runtime.startup.startupComplete << '\n'
            << "Adapter Name / Index   : "
            << runtime.runtime.runtime.startup.selectedAdapterName << " / "
            << runtime.runtime.runtime.startup.selectedAdapterIndex << '\n'
            << "Slave Count            : "
            << runtime.runtime.runtime.startup.slaveCount << '\n'
            << "Servo/ADC/IO/Encoder   : "
            << runtime.runtime.runtime.startup.servoFound << " / "
            << runtime.runtime.runtime.startup.adcFound << " / "
            << runtime.runtime.runtime.startup.ioFound << " / "
            << runtime.runtime.runtime.startup.encoderFound << '\n'
            << "Communication Valid    : " << input.communicationValid << '\n'
            << "ForceN / ForceValid    : "
            << input.forceN << " / " << input.forceValid << '\n'
            << "Servo Comm / Ready / On: "
            << input.servoCommunicationValid << " / "
            << input.servoReady << " / " << input.servoOn << '\n'
            << "Servo Fault / State    : "
            << input.servoFault << " / 0x"
            << std::hex << input.servoOperationState << std::dec << '\n'
            << "Emergency              : " << input.emergency << '\n'
            << "External Stop          : " << input.externalStop << '\n'
            << "Upper / Lower Limit    : "
            << input.upperLimit << " / " << input.lowerLimit << '\n'
            << "DI3 JogUp / DI4 JogDown: "
            << input.jogUp << " / " << input.jogDown << '\n'
            << "Jog Active             : " << runtime.runtime.runtime.runtime.jog.jogActive << '\n'
            << "Jog Direction          : "
            << DirectionName(runtime.runtime.runtime.runtime.jog.jogDirection) << '\n'
            << "Requested Direction    : "
            << DirectionName(runtime.runtime.runtime.runtime.jog.requestedMotionDirection) << '\n'
            << "Jog Speed mm/min       : "
            << runtime.runtime.runtime.runtime.jog.jogSpeedMmPerMin << '\n'
            << "Servo Target Velocity  : "
            << runtime.runtime.runtime.runtime.jog.servoTargetVelocity << '\n'
            << "Jog Conflict           : " << runtime.runtime.runtime.runtime.jog.jogConflict << '\n'
            << "Jog Source Mask        : 0x"
            << std::hex << runtime.runtime.runtime.runtime.jog.activeJogSourceMask
            << std::dec << '\n'
            << "Motion Active/Type     : " << runtime.runtime.motion.motionActive
            << " / " << MotionTypeName(runtime.runtime.motion.motionType) << '\n'
            << "Motion State/Direction : "
            << MotionStateName(runtime.runtime.motion.motionState) << " / "
            << DirectionName(runtime.runtime.motion.motionDirection) << '\n'
            << "Machine/Test Position  : "
            << runtime.runtime.motion.machinePositionMm << " / "
            << runtime.runtime.motion.testPositionMm << " mm\n"
            << "Zero Offset / Valid    : "
            << runtime.runtime.motion.testZeroOffsetMm << " / "
            << runtime.runtime.motion.positionZeroValid << '\n'
            << "Target/Incremental     : "
            << runtime.runtime.motion.targetPositionMm << " / "
            << runtime.runtime.motion.incrementalDistanceMm << " mm\n"
            << "Motion Speed mm/min    : "
            << runtime.runtime.motion.commandSpeedMmPerMin << '\n'
            << "Servo Actual/Target Pos: "
            << runtime.runtime.motion.servoActualPosition << " / "
            << runtime.runtime.motion.servoTargetPosition << '\n'
            << "Servo Target Velocity  : "
            << runtime.runtime.motion.servoTargetVelocity << '\n'
            << "Complete/Abort/Failure : "
            << runtime.runtime.motion.motionComplete << " / "
            << runtime.runtime.motion.motionAborted << " / "
            << runtime.runtime.motion.motionFailureReason << '\n'
            << "Force Phase            : " << ForcePhaseName(runtime.forceMotion.forcePhase) << '\n'
            << "Force Current/Target N : " << runtime.forceMotion.currentForceN
            << " / " << runtime.forceMotion.targetForceN << '\n'
            << "Force Error/Valid      : " << runtime.forceMotion.forceErrorN
            << " / " << runtime.forceMotion.forceValid << '\n'
            << "Force Speed mm/min     : " << runtime.forceMotion.currentCommandSpeedMmPerMin << '\n'
            << "Hold Elapsed/Target s  : " << runtime.forceMotion.holdTimeElapsedSec
            << " / " << runtime.forceMotion.holdTimeTargetSec << '\n'
            << "Travel/Maximum mm      : " << runtime.forceMotion.forceMotionTravelMm
            << " / " << runtime.forceMotion.maxTravelMm << '\n'
            << "Timeout Remain s       : " << runtime.forceMotion.timeoutRemainingSec << '\n'
            << "Stop Latched / Reason  : "
            << base.stop.latched << " / "
            << base.stop.primaryReason << "\n\n";
    }

    void PrintSequenceRuntime()
    {
        UtmRuntimeInfoV6 runtime{};
        if (DaoUtm_GetRuntimeV6(&runtime) == 0)
        {
            std::cout << "Sequence runtime read failed.\n";
            return;
        }
        const UtmSequenceRuntimeInfo& s = runtime.sequence;
        std::cout << "\nSequence loaded/validated/committed/running: "
            << s.sequenceLoaded << '/' << s.sequenceValidated << '/'
            << s.sequenceCommitted << '/' << s.sequenceRunning << '\n'
            << "Sequence state / step count : " << s.sequenceState
            << " / " << s.stepCount << '\n'
            << "Current step index/type/state: " << s.currentStepIndex
            << " / " << s.currentStepType << " / " << s.currentStepState << '\n'
            << "Loop depth/iteration/count  : " << s.loopDepth << " / "
            << s.currentLoopIteration << " / " << s.currentLoopCount << '\n'
            << "Sequence/step elapsed sec   : " << s.sequenceElapsedSec
            << " / " << s.stepElapsedSec << '\n'
            << "Last result/reason          : " << s.lastStepResult
            << " / " << s.lastStepCompletionReason << '\n'
            << "Validation error/step       : " << s.validationError
            << " / " << s.validationErrorStepIndex << '\n'
            << "Complete/aborted/failed     : " << s.sequenceComplete
            << " / " << s.sequenceAborted << " / " << s.sequenceFailed << '\n'
            << "Peak force/travel/elapsed   : "
            << s.stopCondition.peakForceMagnitudeN << " N / "
            << s.stopCondition.travelMm << " mm / "
            << s.stopCondition.elapsedSec << " sec\n"
            << "Recording id/active/event   : " << s.recordingSessionId
            << " / " << s.recordingActive << " / " << s.recordingLastEvent
            << "\n\n";
    }

    UtmSequenceDefinition MakePositionDemo()
    {
        UtmSequenceDefinition d{};
        d.stepCount = 6;
        for (unsigned int i = 0; i < d.stepCount; ++i) d.steps[i].stepIndex = i;
        d.steps[0].stepType = UTM_SEQUENCE_STEP_ZERO_POSITION;
        d.steps[0].timeoutMs = 2000;
        d.steps[1].stepType = UTM_SEQUENCE_STEP_WAIT_TIME;
        d.steps[1].durationSec = 0.5;
        d.steps[2].stepType = UTM_SEQUENCE_STEP_MOVE_ABSOLUTE;
        d.steps[2].positionMm = 1.0; d.steps[2].speedMmPerMin = 1.0;
        d.steps[2].acceleration = d.steps[2].deceleration = 1000;
        d.steps[2].timeoutMs = 30000;
        d.steps[3].stepType = UTM_SEQUENCE_STEP_WAIT_TIME;
        d.steps[3].durationSec = 0.5;
        d.steps[4] = d.steps[2]; d.steps[4].stepIndex = 4; d.steps[4].positionMm = 0.0;
        d.steps[5].stepType = UTM_SEQUENCE_STEP_END;
        return d;
    }

    UtmSequenceDefinition MakeForceDemo()
    {
        UtmSequenceDefinition d{};
        d.stepCount = 6;
        for (unsigned int i = 0; i < d.stepCount; ++i) d.steps[i].stepIndex = i;
        d.steps[0].stepType = UTM_SEQUENCE_STEP_ZERO_FORCE;
        d.steps[0].timeoutMs = 5000;
        d.steps[1].stepType = UTM_SEQUENCE_STEP_WAIT_TIME;
        d.steps[1].durationSec = 0.5;
        d.steps[2].stepType = UTM_SEQUENCE_STEP_MOVE_TO_FORCE;
        d.steps[2].direction = UTM_DIRECTION_COMPRESSION;
        d.steps[2].forceN = 0.5; d.steps[2].speedMmPerMin = 2.0;
        d.steps[2].acceleration = d.steps[2].deceleration = 1000;
        d.steps[2].timeoutMs = 30000;
        d.steps[2].stopConditions.enableMaxTravel = 1;
        d.steps[2].stopConditions.maxTravelMm = 5.0;
        d.steps[3].stepType = UTM_SEQUENCE_STEP_WAIT_TIME;
        d.steps[3].durationSec = 0.5;
        d.steps[4].stepType = UTM_SEQUENCE_STEP_HOLD_FORCE;
        d.steps[4].direction = UTM_DIRECTION_COMPRESSION;
        d.steps[4].forceN = 2.0; d.steps[4].speedMmPerMin = 2.0;
        d.steps[4].holdTimeSec = 2.0; d.steps[4].toleranceN = 0.1;
        d.steps[4].acceleration = d.steps[4].deceleration = 1000;
        d.steps[4].timeoutMs = 15000;
        d.steps[4].stopConditions.enableMaxTravel = 1;
        d.steps[4].stopConditions.maxTravelMm = 5.0;
        d.steps[5].stepType = UTM_SEQUENCE_STEP_END;
        return d;
    }

}

int main(int argc, char* argv[])
{
    std::cout
        << "DAO UTM Control Engine\n"
        << "Version: " << DaoUtm_GetVersion() << "\n\n";

    if (argc < 6)
    {
        std::cout
            << "No hardware operation was requested.\n"
            << "Usage for hardware testing:\n"
            << "  dao_utm_test <adapter-name> <servo-units-per-mm> "
            << "<min-jog-mm-min> <max-jog-mm-min> "
            << "<physical-jog-mm-min>\n";
        return 0;
    }

    UtmStartupConfig startupConfig{};
    std::strncpy(
        startupConfig.ethercatAdapterName,
        argv[1],
        sizeof(startupConfig.ethercatAdapterName) - 1);

    std::cout
        << "Adapter                 : " << argv[1] << '\n'
        << "Communication stabilize : "
        << startupConfig.communicationStabilizeMs << " ms\n"
        << "Servo ON timeout        : "
        << startupConfig.servoOnTimeoutMs << " ms\n"
        << "READY stabilize         : "
        << startupConfig.readyStabilizeMs << " ms\n";

    if (DaoUtm_InitializeV2(&startupConfig) == 0)
    {
        std::cout << "UTM initialization failed.\n";
        return 1;
    }

    UtmJogConfigV2 jogConfig{};
    jogConfig.config.servoUnitsPerMm = std::atof(argv[2]);
    jogConfig.minJogSpeedMmPerMin = std::atof(argv[3]);
    jogConfig.maxJogSpeedMmPerMin = std::atof(argv[4]);
    jogConfig.config.physicalJogSpeedMmPerMin =
        std::atof(argv[5]);

    if (DaoUtm_ConfigureJogV2(&jogConfig) == 0 ||
        DaoUtm_Start() == 0)
    {
        std::cout << "UTM Jog configuration/start failed.\n";
        DaoUtm_Shutdown();
        return 1;
    }

    int previousPhase = -1;
    const auto startupMonitorDeadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(20);

    while (std::chrono::steady_clock::now() <
        startupMonitorDeadline)
    {
        UtmRuntimeInfoV3 runtime{};
        (void)DaoUtm_GetRuntimeV3(&runtime);

        if (runtime.startup.startupPhase != previousPhase)
        {
            previousPhase = runtime.startup.startupPhase;
            std::cout
                << "Startup: "
                << StartupPhaseName(previousPhase)
                << " Fault=" << runtime.startup.startupFault
                << " Adapter=" << runtime.startup.selectedAdapterName
                << " Slaves=" << runtime.startup.slaveCount
                << " ServoOn="
                << runtime.runtime.runtime.input.servoOn
                << '\n';
        }

        if (runtime.startup.startupComplete != 0 ||
            runtime.startup.startupPhase == UTM_STARTUP_FAILED)
        {
            break;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(20));
    }

    double localJogSpeed =
        jogConfig.config.physicalJogSpeedMmPerMin;
    bool exitRequested = false;

    while (!exitRequested)
    {
        std::cout
            << "1. UTM Runtime Status\n"
            << "2. Local Jog Up Start\n"
            << "3. Local Jog Down Start\n"
            << "4. Local Jog Stop\n"
            << "5. Move Absolute\n"
            << "6. Move Incremental\n"
            << "7. Move Velocity Up\n"
            << "8. Move Velocity Down\n"
            << "9. Motion Stop\n"
            << "10. User Stop\n"
            << "11. Stop Acknowledge / Startup Retry\n"
            << "12. Shutdown\n"
            << "13. Set Position Zero\n"
            << "14. Clear Position Zero\n"
            << "15. Move To Force\n"
            << "16. Hold Force\n"
            << "17. Force Motion Stop\n"
            << "18. Sequence Runtime\n"
            << "19. Load Position Demo (no start)\n"
            << "20. Load Force Demo (no start)\n"
            << "21. Validate Sequence\n"
            << "22. Commit Sequence\n"
            << "23. Start Sequence\n"
            << "24. Stop Sequence\n"
            << "0. Exit\n"
            << "Select: ";

        int menu = -1;

        if (!(std::cin >> menu))
        {
            break;
        }

        switch (menu)
        {
        case 1:
            PrintRuntime();
            break;

        case 2:
        case 3:
            std::cout << "Jog speed mm/min [current "
                << localJogSpeed << "]: ";
            std::cin >> localJogSpeed;
            std::cout << "Jog Start: "
                << DaoUtm_StartJog(
                    menu == 2
                        ? UTM_DIRECTION_UP
                        : UTM_DIRECTION_DOWN,
                    localJogSpeed,
                    UTM_COMMAND_SOURCE_UI)
                << "\n\n";
            break;

        case 4:
            std::cout << "Jog Stop: "
                << DaoUtm_StopJog(UTM_COMMAND_SOURCE_UI)
                << "\n\n";
            break;

        case 5:
        case 6:
        {
            double distanceMm = 0.0;
            double speedMmPerMin = localJogSpeed;
            unsigned int acceleration = jogConfig.config.acceleration;
            unsigned int deceleration = jogConfig.config.deceleration;
            unsigned int timeoutMs = 30000;
            std::cout << (menu == 5 ? "Target Test Position mm: "
                                    : "Signed Incremental Distance mm: ");
            std::cin >> distanceMm;
            std::cout << "Speed mm/min: ";
            std::cin >> speedMmPerMin;
            std::cout << "Acceleration / Deceleration / Timeout ms: ";
            std::cin >> acceleration >> deceleration >> timeoutMs;
            const int result = menu == 5
                ? DaoUtm_MoveAbsolute(
                    distanceMm, speedMmPerMin,
                    acceleration, deceleration, timeoutMs,
                    UTM_COMMAND_SOURCE_UI)
                : DaoUtm_MoveIncremental(
                    distanceMm, speedMmPerMin,
                    acceleration, deceleration, timeoutMs,
                    UTM_COMMAND_SOURCE_UI);
            std::cout << "Position Motion: " << result << "\n\n";
            break;
        }

        case 7:
        case 8:
        {
            double speedMmPerMin = localJogSpeed;
            std::cout << "Speed mm/min: ";
            std::cin >> speedMmPerMin;
            std::cout << "Velocity Motion: "
                << DaoUtm_MoveVelocity(
                    menu == 7 ? UTM_DIRECTION_UP : UTM_DIRECTION_DOWN,
                    speedMmPerMin,
                    jogConfig.config.acceleration,
                    jogConfig.config.deceleration,
                    UTM_COMMAND_SOURCE_UI)
                << "\n\n";
            break;
        }

        case 9:
            std::cout << "Motion Stop: "
                << DaoUtm_StopMotion(UTM_COMMAND_SOURCE_UI)
                << "\n\n";
            break;

        case 10:
            DaoUtm_RequestUserStop();
            std::cout << "User Stop requested.\n\n";
            break;

        case 11:
        {
            UtmRuntimeInfoV3 runtime{};
            (void)DaoUtm_GetRuntimeV3(&runtime);
            const int result =
                runtime.startup.startupPhase == UTM_STARTUP_FAILED
                    ? DaoUtm_RetryStartup()
                    : DaoUtm_AcknowledgeStop();
            std::cout << "Acknowledge / Retry: "
                << result
                << "\n\n";
            break;
        }

        case 12:
            DaoUtm_Stop();
            exitRequested = true;
            break;

        case 13:
            std::cout << "Set Position Zero: " << DaoUtm_SetPositionZero() << "\n\n";
            break;

        case 14:
            std::cout << "Clear Position Zero: " << DaoUtm_ClearPositionZero() << "\n\n";
            break;

        case 15:
        {
            int direction = UTM_DIRECTION_DOWN;
            double target = 0.5, speed = 2.0, travel = 10.0;
            unsigned int timeoutMs = 30000;
            std::cout << "Direction (1=UP/TENSION, -1=DOWN/COMPRESSION): ";
            std::cin >> direction;
            std::cout << "Target N / Speed mm/min / Max travel mm / Timeout ms: ";
            std::cin >> target >> speed >> travel >> timeoutMs;
            std::cout << "Move To Force: " << DaoUtm_MoveToForce(direction, target,
                speed, jogConfig.config.acceleration, jogConfig.config.deceleration,
                travel, timeoutMs, UTM_COMMAND_SOURCE_UI) << "\n\n";
            break;
        }

        case 16:
        {
            int direction = UTM_DIRECTION_DOWN;
            double target = 30.0, hold = 60.0, tolerance = 0.1, travel = 10.0;
            unsigned int timeoutMs = 90000;
            std::cout << "Direction (1=UP/TENSION, -1=DOWN/COMPRESSION): ";
            std::cin >> direction;
            std::cout << "Target N / Hold sec / Tolerance N / Max travel mm / Timeout ms: ";
            std::cin >> target >> hold >> tolerance >> travel >> timeoutMs;
            std::cout << "Hold Force: " << DaoUtm_HoldForce(direction, target, hold,
                tolerance, travel, timeoutMs, UTM_COMMAND_SOURCE_UI) << "\n\n";
            break;
        }

        case 17:
            std::cout << "Force Motion Stop: "
                << DaoUtm_StopMotion(UTM_COMMAND_SOURCE_UI) << "\n\n";
            break;

        case 18:
            PrintSequenceRuntime();
            break;
        case 19:
        case 20:
        {
            const UtmSequenceDefinition demo = menu == 19
                ? MakePositionDemo() : MakeForceDemo();
            std::cout << "Load Demo Sequence: "
                << DaoUtm_LoadSequence(&demo)
                << " (not started)\n\n";
            break;
        }
        case 21:
            std::cout << "Validate Sequence: " << DaoUtm_ValidateSequence() << "\n\n";
            break;
        case 22:
            std::cout << "Commit Sequence: " << DaoUtm_CommitSequence() << "\n\n";
            break;
        case 23:
            std::cout << "Start Sequence: " << DaoUtm_StartSequence() << "\n\n";
            break;
        case 24:
            std::cout << "Stop Sequence: " << DaoUtm_StopSequence() << "\n\n";
            break;

        case 0:
            exitRequested = true;
            break;

        default:
            std::cout << "Unknown menu.\n\n";
            break;
        }
    }

    (void)DaoUtm_StopJog(UTM_COMMAND_SOURCE_UI);
    DaoUtm_Stop();
    DaoUtm_Shutdown();
    return 0;
}
