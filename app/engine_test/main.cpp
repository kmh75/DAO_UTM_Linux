#include <iostream>
#include <cstring>
#include <cstddef>
#include <chrono>
#include <thread>
#include <iomanip>
#include <limits>
#include <fstream>


#include "DaoEtherCAT.Engine.h"

namespace
{
    constexpr int SERVO_INDEX = 0;
    constexpr int IO_INDEX = 0;

    bool ReadServoRuntime(
        int servoIndex,
        DaoServoRuntimeInfo& runtime)
    {
        return DaoEngine_GetServoRuntimeInfo(
            servoIndex,
            &runtime) == 1;
    }

    unsigned short GetCiA402State(
        const DaoServoRuntimeInfo& runtime)
    {
        return static_cast<unsigned short>(
            runtime.latestInput.statusWord &
            0x006F);
    }

    bool WaitForServoState(
        int servoIndex,
        unsigned short expectedState,
        int timeoutMs,
        DaoServoRuntimeInfo& outRuntime)
    {
        const auto startTime =
            std::chrono::steady_clock::now();

        while (true)
        {
            if (ReadServoRuntime(
                servoIndex,
                outRuntime) &&
                outRuntime.hasValidInputData == 1)
            {
                if (GetCiA402State(
                    outRuntime) == expectedState)
                {
                    return true;
                }
            }

            const auto now =
                std::chrono::steady_clock::now();

            const auto elapsedMs =
                std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    now - startTime)
                .count();

            if (elapsedMs >= timeoutMs)
            {
                return false;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
        }
    }

    
    bool SendServoCommand(
        int servoIndex,
        unsigned short controlWord,
        int targetPosition)
    {
        // 현재 엔진이 보관 중인 전체 Output PDO를 먼저 읽어서
        // 새로 추가된 Mode / Velocity / Acc / Dec 값을 보존합니다.
        DaoServoRuntimeInfo runtime{};

        if (DaoEngine_GetServoRuntimeInfo(
            servoIndex,
            &runtime) == 0)
        {
            return false;
        }

        DaoServoOutputPdo command =
            runtime.outputCommand;

        // 이번 호출에서 변경해야 하는 값만 수정합니다.
        command.controlWord =
            controlWord;

        command.targetPosition =
            targetPosition;

        command.touchProbeFunction =
            0;

        command.digitalOutputs =
            0;

        return DaoEngine_SetServoOutputCommand(
            servoIndex,
            &command) == 1;
    }

    void PrintServoRuntime(
        int servoIndex)
    {
        DaoServoRuntimeInfo runtime{};

        const int result =
            DaoEngine_GetServoRuntimeInfo(
                servoIndex,
                &runtime);

        std::cout
            << "\nServo Runtime\n";

        std::cout
            << "  Read result       : "
            << result
            << '\n';

        if (result == 0)
        {
            std::cout << '\n';
            return;
        }

        std::cout
            << "  Physical Slave    : "
            << runtime.physicalSlaveIndex
            << '\n';

        std::cout
            << "  Configured        : "
            << runtime.configured
            << '\n';

        std::cout
            << "  Communication     : "
            << runtime.communicationRunning
            << '\n';

        std::cout
            << "  Valid input       : "
            << runtime.hasValidInputData
            << '\n';

        std::cout
            << "  Statusword        : 0x"
            << std::hex
            << std::uppercase
            << runtime.latestInput.statusWord
            << '\n';

        std::cout
            << "  CiA402 state      : 0x"
            << GetCiA402State(runtime)
            << std::dec
            << std::nouppercase
            << '\n';

        std::cout
            << "  Actual position   : "
            << runtime.latestInput.actualPosition
            << '\n';
        std::cout
            << "  Operation mode    : "
            << static_cast<int>(
                runtime.outputCommand.operationMode)
            << '\n';

        std::cout
            << "  Mode display      : "
            << static_cast<int>(
                runtime.latestInput.operationModeDisplay)
            << '\n';

        std::cout
            << "  Profile velocity  : "
            << runtime.outputCommand.profileVelocity
            << '\n';

        std::cout
            << "  Profile accel     : "
            << runtime.outputCommand.profileAcceleration
            << '\n';

        std::cout
            << "  Profile decel     : "
            << runtime.outputCommand.profileDeceleration
            << '\n';

        std::cout
            << "  Target velocity   : "
            << runtime.outputCommand.targetVelocity
            << '\n';

        std::cout
            << "  Last / Expected WKC: "
            << runtime.lastWkc
            << " / "
            << runtime.expectedWkc
            << '\n';

        std::cout
            << "  Total frames      : "
            << runtime.totalFrameCount
            << "\n\n";
    }

    bool TestAsyncServoOn(
        int servoIndex)
    {
        std::cout
            << "\nAsync Servo ON Test\n";

        const int requestResult =
            DaoEngine_ServoOn(
                servoIndex);

        std::cout
            << "  Request result : "
            << requestResult
            << '\n';

        if (requestResult == 0)
        {
            std::cout
                << "Servo ON request was rejected.\n\n";

            return false;
        }

        constexpr int TEST_TIMEOUT_MS =
            65000;

        const auto startTime =
            std::chrono::steady_clock::now();

        int printCounter = 0;

        while (true)
        {
            DaoServoRuntimeInfo runtime{};

            if (DaoEngine_GetServoRuntimeInfo(
                servoIndex,
                &runtime) == 0)
            {
                std::cout
                    << "Runtime read failed.\n\n";

                return false;
            }

            if ((printCounter++ % 10) == 0)
            {
                std::cout
                    << "  CommandId="
                    << runtime.commandId
                    << " Type="
                    << runtime.commandType
                    << " State="
                    << runtime.commandState
                    << " Result="
                    << runtime.commandResult
                    << " CiA402=0x"
                    << std::hex
                    << std::uppercase
                    << runtime.cia402State
                    << std::dec
                    << std::nouppercase
                    << '\n';
            }

            if (runtime.commandState ==
                3) // DAO_SERVO_COMMAND_STATE_COMPLETED
            {
                std::cout
                    << "Async Servo ON completed.\n\n";

                return true;
            }

            if (runtime.commandState ==
                5 || // ERROR
                runtime.commandState ==
                6)  // TIMEOUT
            {
                std::cout
                    << "Async Servo ON failed.\n\n";

                return false;
            }

            const auto now =
                std::chrono::steady_clock::now();

            const auto elapsedMs =
                std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    now - startTime)
                .count();

            if (elapsedMs >=
                TEST_TIMEOUT_MS)
            {
                std::cout
                    << "Test-side timeout.\n\n";

                return false;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(20));
        }
    }


    bool TestAsyncServoOff(
        int servoIndex)
    {
        std::cout
            << "\nAsync Servo OFF Test\n";

        const int requestResult =
            DaoEngine_ServoOff(
                servoIndex);

        std::cout
            << "  Request result : "
            << requestResult
            << '\n';

        if (requestResult == 0)
        {
            std::cout
                << "Servo OFF request was rejected.\n\n";

            return false;
        }

        constexpr int TEST_TIMEOUT_MS =
            3000;

        const auto startTime =
            std::chrono::steady_clock::now();

        int printCounter = 0;

        while (true)
        {
            DaoServoRuntimeInfo runtime{};

            if (DaoEngine_GetServoRuntimeInfo(
                servoIndex,
                &runtime) == 0)
            {
                std::cout
                    << "Runtime read failed.\n\n";

                return false;
            }

            if ((printCounter++ % 10) == 0)
            {
                std::cout
                    << "  CommandId="
                    << runtime.commandId
                    << " Type="
                    << runtime.commandType
                    << " State="
                    << runtime.commandState
                    << " Result="
                    << runtime.commandResult
                    << " CiA402=0x"
                    << std::hex
                    << std::uppercase
                    << runtime.cia402State
                    << std::dec
                    << std::nouppercase
                    << '\n';
            }

            if (runtime.commandState ==
                3)
            {
                std::cout
                    << "Async Servo OFF completed.\n\n";

                return true;
            }

            if (runtime.commandState ==
                5 ||
                runtime.commandState ==
                6)
            {
                std::cout
                    << "Async Servo OFF failed.\n\n";

                return false;
            }

            const auto now =
                std::chrono::steady_clock::now();

            const auto elapsedMs =
                std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    now - startTime)
                .count();

            if (elapsedMs >=
                TEST_TIMEOUT_MS)
            {
                std::cout
                    << "Test-side timeout.\n\n";

                return false;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(20));
        }
    }

    bool TestHomeRequestOnly(
        int servoIndex)
    {
        std::cout
            << "\nHome PDO Mode Request Test\n";

        const int requestResult =
            DaoEngine_ServoHome(
                servoIndex,
                60000);

        std::cout
            << "  Request result : "
            << requestResult
            << '\n';

        if (requestResult == 0)
        {
            std::cout
                << "Home request was rejected.\n\n";

            return false;
        }

        constexpr int TEST_TIMEOUT_MS =
            65000;

        const auto startTime =
            std::chrono::steady_clock::now();

        while (true)
        {
            DaoServoRuntimeInfo runtime{};

            if (DaoEngine_GetServoRuntimeInfo(
                servoIndex,
                &runtime) == 0)
            {
                std::cout
                    << "Runtime read failed.\n\n";

                return false;
            }

            // ====================================================
            // Home 명령 최종 완료 확인
            //
            // commandState = COMPLETED(3)
            // homed        = true
            // CiA402       = Operation Enabled(0x27)
            // OperationMode / ModeDisplay = Profile Position(1)
            // ====================================================
            if (runtime.commandState == 3 &&
                runtime.homed &&
                runtime.cia402State == 0x0027 &&
                runtime.outputCommand.operationMode == 1 &&
                runtime.latestInput.operationModeDisplay == 1)
            {
                std::cout
                    << "  CommandId     : "
                    << runtime.commandId
                    << '\n';

                std::cout
                    << "  CommandType   : "
                    << runtime.commandType
                    << '\n';

                std::cout
                    << "  CommandState  : "
                    << runtime.commandState
                    << '\n';

                std::cout
                    << "  CommandStep   : "
                    << runtime.commandStep
                    << '\n';

                std::cout
                    << "  Homed         : "
                    << runtime.homed
                    << '\n';

                std::cout
                    << "  CiA402        : 0x"
                    << std::hex
                    << std::uppercase
                    << runtime.cia402State
                    << std::dec
                    << std::nouppercase
                    << '\n';

                std::cout
                    << "  OperationMode : "
                    << static_cast<int>(
                        runtime.outputCommand.operationMode)
                    << '\n';

                std::cout
                    << "  ModeDisplay   : "
                    << static_cast<int>(
                        runtime.latestInput.operationModeDisplay)
                    << '\n';

                std::cout
                    << "  WKC           : "
                    << runtime.lastWkc
                    << " / "
                    << runtime.expectedWkc
                    << "\n\n";

                std::cout
                    << "Home completed successfully.\n\n";

                return true;
            }

            if (runtime.commandState == 5 ||
                runtime.commandState == 6)
            {
                std::cout
                    << "Home PDO mode change failed.\n\n";

                return false;
            }

            const auto now =
                std::chrono::steady_clock::now();

            const auto elapsedMs =
                std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    now - startTime)
                .count();

            if (elapsedMs >= TEST_TIMEOUT_MS)
            {
                std::cout
                    << "Home PDO mode change timeout.\n";

                std::cout
                    << "  Test elapsed ms : "
                    << elapsedMs
                    << '\n';

                std::cout
                    << "  CommandState  : "
                    << runtime.commandState
                    << '\n';



                std::cout
                    << "  CommandStep   : "
                    << runtime.commandStep
                    << '\n';

                std::cout
                    << "  CiA402        : 0x"
                    << std::hex
                    << std::uppercase
                    << runtime.cia402State
                    << std::dec
                    << std::nouppercase
                    << '\n';

                std::cout
                    << "  OperationMode : "
                    << static_cast<int>(
                        runtime.outputCommand.operationMode)
                    << '\n';

                std::cout
                    << "  ModeDisplay   : "
                    << static_cast<int>(
                        runtime.latestInput.operationModeDisplay)
                    << "\n\n";

                return false;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(20));

            

        }
    }

    bool TestMoveAbsPrepareOnly(
        int servoIndex)
    {
        std::cout
            << "\nMove Absolute Prepare Test\n";

        const int requestResult =
            DaoEngine_ServoMoveAbsolute(
                servoIndex,
                5000,   // target position
                1000,    // velocity
                1000,    // acceleration
                1000,    // deceleration
                5000);  // timeout

        std::cout
            << "  Request result : "
            << requestResult
            << '\n';

        if (requestResult == 0)
        {
            std::cout
                << "MoveAbs request was rejected.\n\n";

            return false;
        }

        constexpr int TEST_TIMEOUT_MS =
           15000;

        const auto startTime =
            std::chrono::steady_clock::now();

        while (true)
        {
            DaoServoRuntimeInfo runtime{};

            if (DaoEngine_GetServoRuntimeInfo(
                servoIndex,
                &runtime) == 0)
            {
                std::cout
                    << "Runtime read failed.\n\n";

                return false;
            }

            // MOVE_ABS_START(250)까지 도달했는지 확인
            if (runtime.commandState == 3 &&
                runtime.commandStep == 250 &&
                runtime.outputCommand.operationMode == 1 &&
                runtime.latestInput.operationModeDisplay == 1)
            {
                std::cout
                    << "  CommandId     : "
                    << runtime.commandId
                    << '\n';

                std::cout
                    << "  CommandType   : "
                    << runtime.commandType
                    << '\n';

                std::cout
                    << "  CommandState  : "
                    << runtime.commandState
                    << '\n';

                std::cout
                    << "  CommandStep   : "
                    << runtime.commandStep
                    << '\n';

                std::cout
                    << "  CiA402        : 0x"
                    << std::hex
                    << std::uppercase
                    << runtime.cia402State
                    << std::dec
                    << std::nouppercase
                    << '\n';

                std::cout
                    << "  Target        : "
                    << runtime.outputCommand.targetPosition
                    << '\n';

                std::cout
                    << "  Actual        : "
                    << runtime.latestInput.actualPosition
                    << '\n';

                std::cout
                    << "  Velocity      : "
                    << runtime.outputCommand.profileVelocity
                    << '\n';

                std::cout
                    << "  Acceleration  : "
                    << runtime.outputCommand.profileAcceleration
                    << '\n';

                std::cout
                    << "  Deceleration  : "
                    << runtime.outputCommand.profileDeceleration
                    << '\n';

                std::cout
                    << "  ControlWord   : 0x"
                    << std::hex
                    << std::uppercase
                    << runtime.outputCommand.controlWord
                    << std::dec
                    << std::nouppercase
                    << "\n\n";

                std::cout
                    << "MoveAbs completed successfully.\n\n";

                return true;
            }

            const auto now =
                std::chrono::steady_clock::now();

            const auto elapsedMs =
                std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    now - startTime)
                .count();

            if (elapsedMs >= TEST_TIMEOUT_MS)
            {
                std::cout
                    << "MoveAbs prepare timeout.\n";

                std::cout
                    << "  CommandState : "
                    << runtime.commandState
                    << '\n';

                std::cout
                    << "  CommandStep  : "
                    << runtime.commandStep
                    << "\n\n";

                return false;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(20));
        }
    }


    bool TestVelocityModeOnly(
        int servoIndex)
    {
        std::cout
            << "\nProfile Velocity Mode Test\n";

        const int requestResult =
            DaoEngine_ServoVelocity(
                servoIndex,
                0,       // target velocity : 0
                1000,    // acceleration
                1000);   // deceleration

        std::cout
            << "  Request result : "
            << requestResult
            << '\n';

        if (requestResult == 0)
        {
            std::cout
                << "Velocity request was rejected.\n\n";

            return false;
        }

        constexpr int TEST_TIMEOUT_MS =
            3000;

        const auto startTime =
            std::chrono::steady_clock::now();

        while (true)
        {
            DaoServoRuntimeInfo runtime{};

            if (DaoEngine_GetServoRuntimeInfo(
                servoIndex,
                &runtime) == 0)
            {
                std::cout
                    << "Runtime read failed.\n\n";

                return false;
            }

            if (runtime.commandState == 2 &&
                runtime.commandStep == 330 &&
                runtime.cia402State == 0x0027 &&
                runtime.outputCommand.operationMode == 3 &&
                runtime.latestInput.operationModeDisplay == 3 &&
                runtime.outputCommand.targetVelocity == 0)
            {
                std::cout
                    << "  CommandId     : "
                    << runtime.commandId
                    << '\n';

                std::cout
                    << "  CommandType   : "
                    << runtime.commandType
                    << '\n';

                std::cout
                    << "  CommandState  : "
                    << runtime.commandState
                    << '\n';

                std::cout
                    << "  CommandStep   : "
                    << runtime.commandStep
                    << '\n';

                std::cout
                    << "  CiA402        : 0x"
                    << std::hex
                    << std::uppercase
                    << runtime.cia402State
                    << std::dec
                    << std::nouppercase
                    << '\n';

                std::cout
                    << "  OperationMode : "
                    << static_cast<int>(
                        runtime.outputCommand.operationMode)
                    << '\n';

                std::cout
                    << "  ModeDisplay   : "
                    << static_cast<int>(
                        runtime.latestInput.operationModeDisplay)
                    << '\n';

                std::cout
                    << "  TargetVelocity: "
                    << runtime.outputCommand.targetVelocity
                    << "\n\n";

                std::cout
                    << "PV Mode entered successfully.\n\n";

                return true;
            }

            const auto now =
                std::chrono::steady_clock::now();

            const auto elapsedMs =
                std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    now - startTime)
                .count();

            if (elapsedMs >= TEST_TIMEOUT_MS)
            {
                std::cout
                    << "PV Mode test timeout.\n";

                std::cout
                    << "  CommandState : "
                    << runtime.commandState
                    << '\n';

                std::cout
                    << "  CommandStep  : "
                    << runtime.commandStep
                    << '\n';

                std::cout
                    << "  ModeDisplay  : "
                    << static_cast<int>(
                        runtime.latestInput.operationModeDisplay)
                    << "\n\n";

                return false;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(20));
        }
    }


    bool TestVelocityForwardAndStop(
        int servoIndex)
    {
        std::cout
            << "\nPV Forward / Torque Hold Stop Test\n";

        // ----------------------------------------------------
        // 1. 정방향 속도운전 요청
        // ----------------------------------------------------
        const int startResult =
            DaoEngine_ServoVelocity(
                servoIndex,
                1000,    // +1000 UU/s : 정방향
                1000,    // acceleration
                1000);   // deceleration

        std::cout
            << "  Start request : "
            << startResult
            << '\n';

        if (startResult == 0)
        {
            std::cout
                << "Velocity start request rejected.\n\n";

            return false;
        }

        // PV Mode 진입 및 실제 속도 출력 대기
        constexpr int START_TIMEOUT_MS = 3000;

        const auto startWaitTime =
            std::chrono::steady_clock::now();

        while (true)
        {
            DaoServoRuntimeInfo runtime{};

            if (DaoEngine_GetServoRuntimeInfo(
                servoIndex,
                &runtime) == 0)
            {
                std::cout
                    << "Runtime read failed.\n\n";

                return false;
            }

            if (runtime.commandState == 2 &&
                runtime.commandStep == 330 &&
                runtime.latestInput.operationModeDisplay == 3 &&
                runtime.outputCommand.targetVelocity == 1000)
            {
                std::cout
                    << "  PV running\n";

                std::cout
                    << "  ModeDisplay    : "
                    << static_cast<int>(
                        runtime.latestInput.operationModeDisplay)
                    << '\n';

                std::cout
                    << "  TargetVelocity : "
                    << runtime.outputCommand.targetVelocity
                    << '\n';

                break;
            }

            const auto now =
                std::chrono::steady_clock::now();

            const auto elapsedMs =
                std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    now - startWaitTime)
                .count();

            if (elapsedMs >= START_TIMEOUT_MS)
            {
                std::cout
                    << "PV start timeout.\n\n";

                return false;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(20));
        }

        // ----------------------------------------------------
        // 2. 실제로 2초 동안 정방향 운전
        // ----------------------------------------------------
        std::cout
            << "  Running for 2 seconds...\n";

        std::this_thread::sleep_for(
            std::chrono::milliseconds(2000));

        // ----------------------------------------------------
        // 3. Target Velocity = 0
        //    감속 정지 + Servo 토크 유지
        // ----------------------------------------------------
        const int stopResult =
            DaoEngine_ServoVelocity(
                servoIndex,
                0,       // 감속 정지 + Servo 토크 유지
                1000,
                1000);

        std::cout
            << "  Stop request  : "
            << stopResult
            << '\n';

        if (stopResult == 0)
        {
            std::cout
                << "Velocity zero request rejected.\n\n";

            return false;
        }

        // TargetVelocity가 실제로 0으로 적용됐는지만 확인
        constexpr int STOP_TIMEOUT_MS = 3000;

        const auto stopWaitTime =
            std::chrono::steady_clock::now();

        while (true)
        {
            DaoServoRuntimeInfo runtime{};

            if (DaoEngine_GetServoRuntimeInfo(
                servoIndex,
                &runtime) == 0)
            {
                std::cout
                    << "Runtime read failed.\n\n";

                return false;
            }

            if (runtime.commandState == 2 &&
                runtime.commandStep == 330 &&
                runtime.outputCommand.targetVelocity == 0)
            {
                std::cout
                    << "  TargetVelocity : 0\n";

                std::cout
                    << "PV deceleration stop + torque hold applied.\n\n";

                return true;
            }

            const auto now =
                std::chrono::steady_clock::now();

            const auto elapsedMs =
                std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    now - stopWaitTime)
                .count();

            if (elapsedMs >= STOP_TIMEOUT_MS)
            {
                std::cout
                    << "PV zero velocity timeout.\n\n";

                return false;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(20));
        }
    }


    bool TestVelocityForwardAndServoStop(
        int servoIndex)
    {
        std::cout
            << "\nPV Forward / ServoStop Test\n";

        // 1. 정방향 PV 운전
        const int startResult =
            DaoEngine_ServoVelocity(
                servoIndex,
                1000,
                1000,
                1000);

        std::cout
            << "  Start request : "
            << startResult
            << '\n';

        if (startResult == 0)
        {
            std::cout
                << "Velocity start request rejected.\n\n";
            return false;
        }

        // 2. 2초 운전
        std::this_thread::sleep_for(
            std::chrono::milliseconds(2000));

        // 3. 공통 ServoStop 명령
        const int stopResult =
            DaoEngine_ServoStop(
                servoIndex);

        std::cout
            << "  ServoStop request : "
            << stopResult
            << '\n';

        if (stopResult == 0)
        {
            std::cout
                << "ServoStop request rejected.\n\n";
            return false;
        }

        // 4. STOPPED 상태 확인
        constexpr int STOP_TIMEOUT_MS = 3000;

        const auto startTime =
            std::chrono::steady_clock::now();

        while (true)
        {
            DaoServoRuntimeInfo runtime{};

            if (DaoEngine_GetServoRuntimeInfo(
                servoIndex,
                &runtime) == 0)
            {
                std::cout
                    << "Runtime read failed.\n\n";
                return false;
            }

            if (runtime.commandState == 4 &&
                runtime.commandStep == 420)
            {
                std::cout
                    << "  CommandState : "
                    << runtime.commandState
                    << '\n';

                std::cout
                    << "  CommandStep  : "
                    << runtime.commandStep
                    << '\n';

                std::cout
                    << "  ControlWord  : 0x"
                    << std::hex
                    << std::uppercase
                    << runtime.outputCommand.controlWord
                    << std::dec
                    << std::nouppercase
                    << '\n';

                std::cout
                    << "ServoStop completed.\n\n";

                return true;
            }

            const auto now =
                std::chrono::steady_clock::now();

            const auto elapsedMs =
                std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    now - startTime)
                .count();

            if (elapsedMs >= STOP_TIMEOUT_MS)
            {
                std::cout
                    << "ServoStop timeout.\n";

                std::cout
                    << "  CommandState : "
                    << runtime.commandState
                    << '\n';

                std::cout
                    << "  CommandStep  : "
                    << runtime.commandStep
                    << "\n\n";

                return false;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(20));
        }
    }

    bool TestVelocityReverseAndStop(
        int servoIndex)
    {
        std::cout
            << "\nPV Reverse / Torque Hold Stop Test\n";

        // ----------------------------------------------------
        // 1. 역방향 속도운전 요청
        // ----------------------------------------------------
        const int startResult =
            DaoEngine_ServoVelocity(
                servoIndex,
                -1000,   // -1000 UU/s : 역방향
                1000,    // acceleration
                1000);   // deceleration

        std::cout
            << "  Start request : "
            << startResult
            << '\n';

        if (startResult == 0)
        {
            std::cout
                << "Reverse velocity request rejected.\n\n";

            return false;
        }

        constexpr int START_TIMEOUT_MS = 3000;

        const auto startWaitTime =
            std::chrono::steady_clock::now();

        while (true)
        {
            DaoServoRuntimeInfo runtime{};

            if (DaoEngine_GetServoRuntimeInfo(
                servoIndex,
                &runtime) == 0)
            {
                std::cout
                    << "Runtime read failed.\n\n";

                return false;
            }

            if (runtime.commandState == 2 &&
                runtime.commandStep == 330 &&
                runtime.latestInput.operationModeDisplay == 3 &&
                runtime.outputCommand.targetVelocity == -1000)
            {
                std::cout
                    << "  PV reverse running\n";

                std::cout
                    << "  ModeDisplay    : "
                    << static_cast<int>(
                        runtime.latestInput.operationModeDisplay)
                    << '\n';

                std::cout
                    << "  TargetVelocity : "
                    << runtime.outputCommand.targetVelocity
                    << '\n';

                break;
            }

            const auto now =
                std::chrono::steady_clock::now();

            const auto elapsedMs =
                std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    now - startWaitTime)
                .count();

            if (elapsedMs >= START_TIMEOUT_MS)
            {
                std::cout
                    << "PV reverse start timeout.\n\n";

                return false;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(20));
        }

        // ----------------------------------------------------
        // 2. 2초 동안 역방향 운전
        // ----------------------------------------------------
        std::cout
            << "  Running reverse for 2 seconds...\n";

        std::this_thread::sleep_for(
            std::chrono::milliseconds(2000));

        // ----------------------------------------------------
        // 3. 감속 정지 + Servo 토크 유지
        // ----------------------------------------------------
        const int stopResult =
            DaoEngine_ServoVelocity(
                servoIndex,
                0,
                1000,
                1000);

        std::cout
            << "  Stop request  : "
            << stopResult
            << '\n';

        if (stopResult == 0)
        {
            std::cout
                << "Velocity zero request rejected.\n\n";

            return false;
        }

        std::cout
            << "PV reverse test completed.\n"
            << "Deceleration stop + Servo torque hold applied.\n\n";

        return true;
    }

    bool ServoOn(
        int servoIndex)
    {
        DaoServoRuntimeInfo runtime{};

        if (!ReadServoRuntime(
            servoIndex,
            runtime) ||
            runtime.hasValidInputData == 0)
        {
            std::cout
                << "Servo runtime is not ready.\n\n";

            return false;
        }

        int currentPosition =
            runtime.latestInput.actualPosition;

        // Switch On Disabled -> Ready to Switch On
        if (!SendServoCommand(
            servoIndex,
            0x0006,
            currentPosition))
        {
            std::cout
                << "Shutdown command failed.\n\n";

            return false;
        }

        if (!WaitForServoState(
            servoIndex,
            0x0021,
            2000,
            runtime))
        {
            std::cout
                << "Ready to Switch On timeout.\n\n";

            return false;
        }

        currentPosition =
            runtime.latestInput.actualPosition;

        // Ready to Switch On -> Switched On
        if (!SendServoCommand(
            servoIndex,
            0x0007,
            currentPosition))
        {
            std::cout
                << "Switch On command failed.\n\n";

            return false;
        }

        if (!WaitForServoState(
            servoIndex,
            0x0023,
            2000,
            runtime))
        {
            std::cout
                << "Switched On timeout.\n\n";

            return false;
        }

        currentPosition =
            runtime.latestInput.actualPosition;

        // Switched On -> Operation Enabled
        if (!SendServoCommand(
            servoIndex,
            0x000F,
            currentPosition))
        {
            std::cout
                << "Enable Operation command failed.\n\n";

            return false;
        }

        if (!WaitForServoState(
            servoIndex,
            0x0027,
            2000,
            runtime))
        {
            std::cout
                << "Operation Enabled timeout.\n\n";

            return false;
        }

        std::cout
            << "Servo ON success.\n";

        std::cout
            << "  Statusword      : 0x"
            << std::hex
            << std::uppercase
            << runtime.latestInput.statusWord
            << std::dec
            << std::nouppercase
            << '\n';

        std::cout
            << "  Actual position : "
            << runtime.latestInput.actualPosition
            << "\n\n";

        return true;
    }

    bool ServoOff(
        int servoIndex)
    {
        DaoServoRuntimeInfo runtime{};

        if (!ReadServoRuntime(
            servoIndex,
            runtime))
        {
            std::cout
                << "Servo runtime read failed.\n\n";

            return false;
        }

        const int currentPosition =
            runtime.latestInput.actualPosition;

        if (!SendServoCommand(
            servoIndex,
            0x0006,
            currentPosition))
        {
            std::cout
                << "Servo OFF command failed.\n\n";

            return false;
        }

        if (!WaitForServoState(
            servoIndex,
            0x0021,
            2000,
            runtime))
        {
            std::cout
                << "Servo OFF state timeout.\n\n";

            return false;
        }

        std::cout
            << "Servo OFF success.\n";

        std::cout
            << "  Statusword : 0x"
            << std::hex
            << std::uppercase
            << runtime.latestInput.statusWord
            << std::dec
            << std::nouppercase
            << "\n\n";

        return true;
    }

    bool SetHomingModeAndServoOn(
        int servoIndex)
    {
        std::cout
            << "\nHoming Mode Change Test\n";

        // ----------------------------------------------------
        // 1. 통신이 실행 중인 상태에서 Servo OFF
        // ----------------------------------------------------
        if (!ServoOff(
            servoIndex))
        {
            std::cout
                << "Homing mode change failed: "
                << "Servo OFF failed.\n\n";

            return false;
        }

        // Servo OFF 명령이 PDO로 전달될 시간을 확보합니다.
        std::this_thread::sleep_for(
            std::chrono::milliseconds(100));

        // ----------------------------------------------------
        // 2. SDO와 PDO 통신 충돌 방지를 위해 통신 정지
        // ----------------------------------------------------
        DaoEngine_StopCommunication();

        const int stoppedResult =
            DaoEngine_IsCommunicationRunning();

        std::cout
            << "  Communication after stop : "
            << stoppedResult
            << '\n';

        // ----------------------------------------------------
        // 3. Homing Mode 설정
        //
        // 0x6060 = 6
        // ----------------------------------------------------
        const int modeResult =
            DaoEngine_SetServoOperationMode(
                servoIndex,
                6);

        std::cout
            << "  Homing mode result       : "
            << modeResult
            << '\n';

        if (modeResult == 0)
        {
            std::cout
                << "Homing mode SDO setting failed.\n\n";

            // 통신은 다시 살려놓습니다.
            DaoEngine_StartCommunication();

            return false;
        }

        // ----------------------------------------------------
        // 4. 2ms 통신 재시작
        // ----------------------------------------------------
        const int restartResult =
            DaoEngine_StartCommunication();

        std::cout
            << "  Communication restart    : "
            << restartResult
            << '\n';

        if (restartResult == 0)
        {
            std::cout
                << "Communication restart failed.\n\n";

            return false;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(200));

        // ----------------------------------------------------
        // 5. Homing Mode 상태에서 Servo ON
        //
        // 아직 Homing Start 0x001F는 보내지 않습니다.
        // ----------------------------------------------------
        const bool servoOnResult =
            ServoOn(
                servoIndex);

        std::cout
            << "  Servo ON after mode      : "
            << (servoOnResult ? 1 : 0)
            << "\n\n";

        return servoOnResult;
    }

    bool StartHomingAndWait(
        int servoIndex)
    {
        std::cout
            << "\nHoming Start Test\n";

        if (DaoEngine_IsCommunicationRunning() == 0)
        {
            std::cout
                << "Homing failed: communication is stopped.\n\n";

            return false;
        }

        DaoServoRuntimeInfo runtime{};

        if (DaoEngine_GetServoRuntimeInfo(
            servoIndex,
            &runtime) == 0 ||
            runtime.hasValidInputData == 0)
        {
            std::cout
                << "Homing failed: servo runtime is not ready.\n\n";

            return false;
        }

        const unsigned short cia402State =
            static_cast<unsigned short>(
                runtime.latestInput.statusWord &
                0x006F);

        if (cia402State != 0x0027)
        {
            std::cout
                << "Homing failed: servo is not Operation Enabled.\n";

            std::cout
                << "  CiA402 state : 0x"
                << std::hex
                << std::uppercase
                << cia402State
                << std::dec
                << std::nouppercase
                << "\n\n";

            return false;
        }

        
        DaoServoOutputPdo homingCommand =
            runtime.outputCommand;

        homingCommand.controlWord =
            0x001F;

        homingCommand.targetPosition =
            runtime.latestInput.actualPosition;

        homingCommand.touchProbeFunction =
            0;

        homingCommand.digitalOutputs =
            0;

        if (DaoEngine_SetServoOutputCommand(
            servoIndex,
            &homingCommand) == 0)
        {
            std::cout
                << "Homing failed: start command write failed.\n\n";

            return false;
        }

        constexpr int HOMING_TIMEOUT_MS =
            60000;

        const auto startTime =
            std::chrono::steady_clock::now();

        int printCounter = 0;

        while (true)
        {
            DaoServoRuntimeInfo currentRuntime{};

            if (DaoEngine_GetServoRuntimeInfo(
                servoIndex,
                &currentRuntime) == 0)
            {
                std::cout
                    << "Homing failed: runtime read failed.\n\n";

                break;
            }

            const unsigned short statusWord =
                currentRuntime.latestInput.statusWord;

            const bool fault =
                (statusWord & 0x0008) != 0;

            const bool targetReached =
                (statusWord & 0x0400) != 0;

            const bool homingAttained =
                (statusWord & 0x1000) != 0;

            const bool homingError =
                (statusWord & 0x2000) != 0;

            if ((printCounter++ % 20) == 0)
            {
                std::cout
                    << "  Homing"
                    << " SW=0x"
                    << std::hex
                    << std::uppercase
                    << statusWord
                    << std::dec
                    << std::nouppercase
                    << " Pos="
                    << currentRuntime.latestInput.actualPosition
                    << " TargetReached="
                    << (targetReached ? 1 : 0)
                    << " Attained="
                    << (homingAttained ? 1 : 0)
                    << " Error="
                    << (homingError ? 1 : 0)
                    << '\n';
            }

            if (fault)
            {
                std::cout
                    << "Homing failed: Servo Fault.\n\n";

                break;
            }

            if (homingError)
            {
                std::cout
                    << "Homing failed: Homing Error bit ON.\n\n";

                break;
            }

            if (homingAttained)
            {
                std::cout
                    << "Homing success.\n";

                std::cout
                    << "  Statusword : 0x"
                    << std::hex
                    << std::uppercase
                    << statusWord
                    << std::dec
                    << std::nouppercase
                    << '\n';

                std::cout
                    << "  Position   : "
                    << currentRuntime.latestInput.actualPosition
                    << "\n\n";

                DaoServoOutputPdo finishCommand =
                    currentRuntime.outputCommand;

                finishCommand.controlWord =
                    0x000F;

                finishCommand.targetPosition =
                    currentRuntime.latestInput.actualPosition;

                finishCommand.touchProbeFunction =
                    0;

                finishCommand.digitalOutputs =
                    0;

                DaoEngine_SetServoOutputCommand(
                    servoIndex,
                    &finishCommand);

                return true;
            }

            const auto now =
                std::chrono::steady_clock::now();

            const auto elapsedMs =
                std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    now - startTime)
                .count();

            if (elapsedMs >= HOMING_TIMEOUT_MS)
            {
                std::cout
                    << "Homing timeout.\n\n";

                break;
            }

            // LS L7NH는 호밍 완료까지 0x001F를 유지합니다.
            DaoEngine_SetServoOutputCommand(
                servoIndex,
                &homingCommand);

            std::this_thread::sleep_for(
                std::chrono::milliseconds(50));
        }

        // 실패 또는 Timeout 시 Servo ON 기본 상태로 복귀
        DaoServoRuntimeInfo finalRuntime{};

        DaoEngine_GetServoRuntimeInfo(
            servoIndex,
            &finalRuntime);

        DaoServoOutputPdo cancelCommand =
            finalRuntime.outputCommand;

        cancelCommand.controlWord =
            0x000F;

        cancelCommand.targetPosition =
            finalRuntime.latestInput.actualPosition;

        cancelCommand.touchProbeFunction =
            0;

        cancelCommand.digitalOutputs =
            0;

        DaoEngine_SetServoOutputCommand(
            servoIndex,
            &cancelCommand);

        return false;
    }

    void PrintBasicAdcAndIoStatus(
        int adcPhysicalSlaveIndex)
    {
        DaoIoRuntimeInfo ioRuntime{};
        DaoAdcRuntimeInfo adcRuntime{};

        const int ioResult =
            DaoEngine_GetIoRuntimeInfo(
                IO_INDEX,
                &ioRuntime);

        const int adcResult =
            adcPhysicalSlaveIndex > 0
            ? DaoEngine_GetAdcRuntimeInfo(
                0,
                &adcRuntime)
            : 0;

        std::cout
            << "\nADC / IO Basic Status\n";

        std::cout
            << "  IO runtime result : "
            << ioResult
            << '\n';

        if (ioResult == 1)
        {
            std::cout
                << "    Input          : 0x"
                << std::hex
                << std::uppercase
                << ioRuntime.latestInput
                << std::dec
                << std::nouppercase
                << '\n';

            std::cout
                << "    Output command : 0x"
                << std::hex
                << std::uppercase
                << ioRuntime.outputCommand
                << std::dec
                << std::nouppercase
                << '\n';

            std::cout
                << "    WKC            : "
                << ioRuntime.lastWkc
                << " / "
                << ioRuntime.expectedWkc
                << '\n';
        }

        std::cout
            << "  ADC runtime result: "
            << adcResult
            << '\n';

        if (adcResult == 1)
        {
            std::cout
                << "    Counter        : "
                << adcRuntime.latestData.testCounter
                << '\n';

            std::cout
                << "    Raw0           : "
                << adcRuntime.latestData.adcRaw0
                << '\n';

            std::cout
                << "    Raw1           : "
                << adcRuntime.latestData.adcRaw1
                << '\n';

            std::cout
                << "    Raw2           : "
                << adcRuntime.latestData.adcRaw2
                << '\n';

            std::cout
                << "    Raw3           : "
                << adcRuntime.latestData.adcRaw3
                << '\n';

            std::cout
                << "    LowLevelFiltered: "
                << std::fixed
                << std::setprecision(1)
                << adcRuntime.lowLevelFiltered
                << std::defaultfloat
                << '\n';

            std::cout
                << "    WKC            : "
                << adcRuntime.lastWkc
                << " / "
                << adcRuntime.expectedWkc
                << '\n';

            std::cout
                << "    Bad WKC frames : "
                << adcRuntime.badWkcFrameCount
                << '\n';
        }

        std::cout << '\n';
    }

    bool TestAdcZero(
        int adcPhysicalSlaveIndex)
    {
        std::cout
            << "\nADC Zero Test\n";

        if (adcPhysicalSlaveIndex <= 0)
        {
            std::cout
                << "ADC device not found.\n\n";

            return false;
        }

        // ----------------------------------------------------
        // 1. Zero 실행 전 현재값 확인
        // ----------------------------------------------------
        DaoAdcRuntimeInfo beforeRuntime{};

        if (DaoEngine_GetDaoAdcRuntimeInfo(
            adcPhysicalSlaveIndex,
            &beforeRuntime) == 0)
        {
            std::cout
                << "ADC runtime read failed.\n\n";

            return false;
        }

        std::cout
            << "  Before Zero\n"
            << "    LowLevelFiltered : "
            << std::fixed
            << std::setprecision(1)
            << beforeRuntime.lowLevelFiltered
            << '\n';

        // ----------------------------------------------------
        // 2. 논리 ADC 0번 Zero 실행
        // ----------------------------------------------------
        const int zeroResult =
            DaoEngine_SetAdcZero(0);

        std::cout
            << "  Zero request       : "
            << zeroResult
            << '\n';

        if (zeroResult == 0)
        {
            std::cout
                << "ADC Zero request failed.\n\n";

            return false;
        }

        // 새 ADC Sample이 몇 번 처리될 시간을 줍니다.
        // ----------------------------------------------------
        // Calibration Stable Capture 완료 대기
        // ----------------------------------------------------
        while (true)
        {
            DaoAdcRuntimeInfo progressInfo{};

            if (DaoEngine_GetDaoAdcRuntimeInfo(
                adcPhysicalSlaveIndex,
                &progressInfo) == 0)
            {
                std::cout
                    << "ADC runtime read failed during Calibration.\n\n";

                return false;
            }

            if (progressInfo.stableCaptureActive == 0)
            {
                break;
            }

            std::cout
                << "\r  Capture progress    : "
                << progressInfo.stableCaptureCollectedCount
                << " / "
                << progressInfo.stableCaptureSampleCount
                << std::flush;

            std::this_thread::sleep_for(
                std::chrono::milliseconds(100));
        }

        std::cout
            << "\r  Capture progress    : completed"
            << "                    \n";

        // ----------------------------------------------------
        // 3. Zero 이후 값 확인
        // ----------------------------------------------------
        DaoAdcRuntimeInfo afterRuntime{};

        if (DaoEngine_GetDaoAdcRuntimeInfo(
            adcPhysicalSlaveIndex,
            &afterRuntime) == 0)
        {
            std::cout
                << "ADC runtime read failed after Zero.\n\n";

            return false;
        }

        std::cout
            << "  After Zero\n"
            << "    LowLevelFiltered : "
            << afterRuntime.lowLevelFiltered
            << '\n';

        std::cout
            << "    ZeroedValue      : "
            << afterRuntime.zeroedValue
            << std::defaultfloat
            << "\n\n";

        std::cout
            << "    CalibratedValue  : "
            << std::fixed
            << std::setprecision(1)
            << afterRuntime.calibratedValue
            << std::defaultfloat
            << '\n';

        std::cout
            << "    FilteredValue   : "
            << std::fixed
            << std::setprecision(1)
            << afterRuntime.filteredValue
            << std::defaultfloat
            << '\n';


        std::cout
            << "\n  Filter monitor\n";

        for (int i = 0; i < 10; ++i)
        {
            DaoAdcRuntimeInfo monitorInfo{};

            if (DaoEngine_GetDaoAdcRuntimeInfo(
                adcPhysicalSlaveIndex,
                &monitorInfo) == 0)
            {
                std::cout
                    << "ADC monitor read failed.\n";

                return false;
            }

            std::cout
                << "    "
                << (i + 1)
                << "  Calibrated: "
                << std::fixed
                << std::setprecision(1)
                << monitorInfo.calibratedValue
                << "   Filtered: "
                << monitorInfo.filteredValue
                << '\n';

            std::this_thread::sleep_for(
                std::chrono::milliseconds(100));
        }

        std::cout
            << std::defaultfloat
            << '\n';

        return true;
    }

    bool TestAdcCalibration(
        int adcPhysicalSlaveIndex)
    {
        std::cout
            << "\nADC Calibration Test\n";

        if (adcPhysicalSlaveIndex <= 0)
        {
            std::cout
                << "ADC device not found.\n\n";

            return false;
        }

        // ----------------------------------------------------
        // Power Line Filter
        //
        // Calibration / Zero / No-load 확인 전체를
        // 60Hz Notch Filter ON 상태에서 진행합니다.
        // ----------------------------------------------------
        if (DaoEngine_SetAdcPowerLineFilterMode(
            0,
			DAO_ADC_POWER_FILTER_60_120HZ) == 0) // 0 = 60Hz Notch Filter ON
        {
            std::cout
                << "60Hz Power Line Filter setting failed.\n\n";

            return false;
        }

        // Notch 내부 상태가 안정될 시간을 약간 줍니다.
        std::this_thread::sleep_for(
            std::chrono::milliseconds(500));

        std::cout
            << "  Power Line Filter : 60Hz + 120Hz ON\n";

        // ----------------------------------------------------
        // 1. 무부하 Zero 실행
        // ----------------------------------------------------
        if (DaoEngine_SetAdcZero(0) == 0)
        {
            std::cout
                << "ADC Zero failed.\n\n";

            return false;
        }

        // Zero Stable Capture 완료 대기
        while (true)
        {
            DaoAdcRuntimeInfo zeroProgress{};

            if (DaoEngine_GetDaoAdcRuntimeInfo(
                adcPhysicalSlaveIndex,
                &zeroProgress) == 0)
            {
                std::cout
                    << "ADC runtime read failed during Zero.\n\n";

                return false;
            }

            if (zeroProgress.stableCaptureActive == 0)
            {
                break;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(100));
        }

        std::cout
            << "  Zero completed.\n";

        // ----------------------------------------------------
        // 2. 1kg 기준 분동 적용
        // ----------------------------------------------------
        std::cout
            << "  Apply 1kg reference load, then press ENTER...\n";

        std::cin.ignore(
            std::numeric_limits<std::streamsize>::max(),
            '\n');

        std::cin.get();

        // ----------------------------------------------------
        // 3. 1kg = 1000.0 Calibration
        // ----------------------------------------------------
        constexpr double REFERENCE_VALUE = 1000.0;

        const int calibrationResult =
            DaoEngine_SetAdcCalibration(
                0,
                REFERENCE_VALUE);

        std::cout
            << "  Calibration request : "
            << calibrationResult
            << '\n';

        if (calibrationResult == 0)
        {
            std::cout
                << "ADC Calibration failed.\n\n";

            return false;
        }

        // Calibration Stable Capture 완료 대기
        while (true)
        {
            DaoAdcRuntimeInfo progressInfo{};

            if (DaoEngine_GetDaoAdcRuntimeInfo(
                adcPhysicalSlaveIndex,
                &progressInfo) == 0)
            {
                std::cout
                    << "ADC runtime read failed during Calibration.\n\n";

                return false;
            }

            if (progressInfo.stableCaptureActive == 0)
            {
                break;
            }

            std::cout
                << "\r  Capture progress    : "
                << progressInfo.stableCaptureCollectedCount
                << " / "
                << progressInfo.stableCaptureSampleCount
                << std::flush;

            std::this_thread::sleep_for(
                std::chrono::milliseconds(100));
        }

        std::cout
            << "\r  Capture progress    : completed"
            << "                    \n";

        // ----------------------------------------------------
        // 4. Calibration 결과 확인
        // ----------------------------------------------------
        DaoAdcRuntimeInfo calibratedInfo{};

        if (DaoEngine_GetDaoAdcRuntimeInfo(
            adcPhysicalSlaveIndex,
            &calibratedInfo) == 0)
        {
            std::cout
                << "ADC runtime read failed.\n\n";

            return false;
        }

        std::cout
            << std::fixed
            << std::setprecision(3);

        std::cout
            << "  Loaded value\n"
            << "    CalibratedValue : "
            << calibratedInfo.calibratedValue
            << '\n'
            << "    FilteredValue   : "
            << calibratedInfo.filteredValue
            << '\n'
            << "    ReferenceValue  : "
            << REFERENCE_VALUE
            << "\n\n";

        // ----------------------------------------------------
        // 5. 분동 제거
        //
        // 여기서는 Zero를 다시 실행하지 않습니다.
        // 기존 Zero Offset과 Calibration Scale을 그대로 유지한 채
        // 실제 무부하 복귀값을 확인합니다.
        // ----------------------------------------------------
        std::cout
            << "  Remove 1kg reference load, then press ENTER...\n";

        std::cin.get();

        // 분동을 제거한 직후 기계적인 흔들림이 조금 가라앉도록
        // 테스트 표시 전에 잠깐 기다립니다.
        std::this_thread::sleep_for(
            std::chrono::milliseconds(500));

        // ----------------------------------------------------
        // 6. 무부하 안정화 대기
        //
        // 분동을 제거하면서 로드셀/지그를 건드린 영향이
        // 사라질 시간을 충분히 줍니다.
        // ----------------------------------------------------
        std::cout
            << "\n  Waiting 2 seconds for no-load stabilization...\n";

        std::this_thread::sleep_for(
            std::chrono::milliseconds(2000));


        // ----------------------------------------------------
        // 7. 무부하 상태 5초 연속 취득
        //
        // 100ms 간격 x 50개 = 약 5초
        //
        // 측정 중에는 Console 출력을 하지 않습니다.
        // 50개를 모두 취득한 뒤 한꺼번에 출력합니다.
        // ----------------------------------------------------
        constexpr int MONITOR_SAMPLE_COUNT = 250;

        double calibratedSamples[MONITOR_SAMPLE_COUNT]{};
        double filteredSamples[MONITOR_SAMPLE_COUNT]{};

        for (int i = 0; i < MONITOR_SAMPLE_COUNT; ++i)
        {
            DaoAdcRuntimeInfo monitorInfo{};

            if (DaoEngine_GetDaoAdcRuntimeInfo(
                adcPhysicalSlaveIndex,
                &monitorInfo) == 0)
            {
                std::cout
                    << "ADC monitor read failed.\n\n";

                return false;
            }

            calibratedSamples[i] =
                monitorInfo.calibratedValue;

            filteredSamples[i] =
                monitorInfo.filteredValue;

            std::this_thread::sleep_for(
                std::chrono::milliseconds(20));
        }


        // ----------------------------------------------------
        // 8. 취득 완료 후 결과를 한꺼번에 출력
        // ----------------------------------------------------
        std::cout
            << "\n  No-load monitor - 5 seconds\n";

        for (int i = 0; i < MONITOR_SAMPLE_COUNT; ++i)
        {
            std::cout
                << "    "
                << (i + 1)
                << "  Calibrated: "
                << std::fixed
                << std::setprecision(3)
                << calibratedSamples[i]
                << "   Filtered: "
                << filteredSamples[i]
                << '\n';
        }

       

        std::cout
            << std::defaultfloat
            << '\n';

        return true;
    }


    bool SaveAdcDiagnosticCaptureCsv(
        int logicalAdcIndex,
        const char* fileName)
    {
        int captureActive = 0;
        unsigned int capturedSampleCount = 0;
        unsigned int targetSampleCount = 0;

        if (DaoEngine_GetAdcDiagnosticCaptureInfo(
            logicalAdcIndex,
            &captureActive,
            &capturedSampleCount,
            &targetSampleCount) == 0)
        {
            std::cout
                << "Diagnostic capture info read failed.\n";

            return false;
        }

        if (captureActive != 0)
        {
            std::cout
                << "Diagnostic capture is still running.\n";

            return false;
        }

        if (capturedSampleCount == 0)
        {
            std::cout
                << "No diagnostic samples available.\n";

            return false;
        }

        std::ofstream file(fileName);

        if (!file.is_open())
        {
            std::cout
                << "CSV file open failed.\n";

            return false;
        }

        // ----------------------------------------------------
        // CSV Header
        // ----------------------------------------------------
        file
            << "SampleIndex,"
            << "Raw,"
            << "LowLevelFiltered,"
            << "PowerLineFiltered,"
            << "ZeroedValue,"
            << "CalibratedValue,"
            << "MedianFilteredValue,"
            << "FilteredValue\n";

        // ----------------------------------------------------
        // Sample Data
        // ----------------------------------------------------
        file
            << std::fixed
            << std::setprecision(6);

        for (unsigned int i = 0;
            i < capturedSampleCount;
            ++i)
        {
            DaoAdcDiagnosticSample sample{};

            if (DaoEngine_GetAdcDiagnosticSample(
                logicalAdcIndex,
                i,
                &sample) == 0)
            {
                file.close();

                std::cout
                    << "Diagnostic sample read failed at index "
                    << i
                    << ".\n";

                return false;
            }

            file
                << sample.sampleIndex
                << ','
                << sample.rawValue
                << ','
                << sample.lowLevelFiltered
                << ','
                << sample.powerLineFiltered
                << ','
                << sample.zeroedValue
                << ','
                << sample.calibratedValue
                << ','
                << sample.medianFilteredValue
                << ','
                << sample.filteredValue
                << '\n';
        }

        file.close();

        std::cout
            << "Diagnostic CSV saved.\n"
            << "  File    : "
            << fileName
            << '\n'
            << "  Samples : "
            << capturedSampleCount
            << " / "
            << targetSampleCount
            << "\n\n";

        return true;
    }


    bool TestAdcDiagnosticCapture(
        int adcPhysicalSlaveIndex)
    {
        std::cout
            << "\nADC Diagnostic Capture Test\n";

        if (adcPhysicalSlaveIndex <= 0)
        {
            std::cout
                << "ADC device not found.\n\n";

            return false;
        }

        // ----------------------------------------------------
        // 1. 60Hz Power Line Filter ON
        // ----------------------------------------------------
        if (DaoEngine_SetAdcPowerLineFilterMode(
            0,
            2) == 0) // 2 = 60Hz
        {
            std::cout
                << "60Hz Power Line Filter setting failed.\n\n";

            return false;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(500));

        // ----------------------------------------------------
        // 2. 10초 Diagnostic Capture 시작
        //
        // 약 2000 sample/sec 기준
        // 10초 = 20000 sample
        //
        // 실제 종료는 시간이 아니라 Sample 개수로 판단합니다.
        // ----------------------------------------------------
        constexpr unsigned int TARGET_SAMPLE_COUNT =
            60000;

        if (DaoEngine_StartAdcDiagnosticCapture(
            0,
            TARGET_SAMPLE_COUNT) == 0)
        {
            std::cout
                << "Diagnostic capture start failed.\n\n";

            return false;
        }

        std::cout
            << "  Diagnostic capture started.\n"
            << "  Target samples : "
            << TARGET_SAMPLE_COUNT
            << '\n';

        // ----------------------------------------------------
        // 3. Capture 완료 대기
        // ----------------------------------------------------
        while (true)
        {
            int captureActive = 0;
            unsigned int capturedSampleCount = 0;
            unsigned int targetSampleCount = 0;

            if (DaoEngine_GetAdcDiagnosticCaptureInfo(
                0,
                &captureActive,
                &capturedSampleCount,
                &targetSampleCount) == 0)
            {
                std::cout
                    << "Diagnostic capture info read failed.\n\n";

                return false;
            }

            if (captureActive == 0)
            {
                std::cout
                    << "  Capture completed.\n"
                    << "  Captured samples : "
                    << capturedSampleCount
                    << " / "
                    << targetSampleCount
                    << '\n';

                break;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(100));
        }

        // ----------------------------------------------------
        // 4. CSV 저장
        // ----------------------------------------------------
        const char* fileName =
            "adc_diagnostic_30s.csv";

        if (!SaveAdcDiagnosticCaptureCsv(
            0,
            fileName))
        {
            return false;
        }

        return true;
    }


    bool TestAdcRingBuffer(
        int adcPhysicalSlaveIndex)
    {
        std::cout
            << "\nADC Ring Buffer Test\n";

        if (adcPhysicalSlaveIndex <= 0)
        {
            std::cout
                << "ADC device not found.\n\n";

            return false;
        }

        // ----------------------------------------------------
        // Ring Buffer 초기화
        // ----------------------------------------------------
        if (DaoEngine_ClearAdcRingBuffer(0) == 0)
        {
            std::cout
                << "Ring buffer clear failed.\n\n";

            return false;
        }

        std::cout
            << "  Ring buffer cleared.\n";


        // ----------------------------------------------------
        // 1. 테스트 전에 현재 Buffer 상태 확인
        // ----------------------------------------------------
        unsigned int sampleCount = 0;
        unsigned long long overflowCount = 0;

        if (DaoEngine_GetAdcRingBufferInfo(
            0,
            &sampleCount,
            &overflowCount) == 0)
        {
            std::cout
                << "Ring buffer info read failed.\n\n";

            return false;
        }

        std::cout
            << "  Before wait\n"
            << "    Sample count   : "
            << sampleCount
            << '\n'
            << "    Overflow count : "
            << overflowCount
            << '\n';

        // ----------------------------------------------------
        // 2. 약 20ms 대기
        //
        // 약 2000 sample/sec 기준이면
        // 약 40 Sample 정도 추가되는 것이 정상입니다.
        // ----------------------------------------------------
        std::this_thread::sleep_for(
            std::chrono::milliseconds(20));

        // ----------------------------------------------------
        // 3. 다시 Buffer 상태 확인
        // ----------------------------------------------------
        if (DaoEngine_GetAdcRingBufferInfo(
            0,
            &sampleCount,
            &overflowCount) == 0)
        {
            std::cout
                << "Ring buffer info read failed after wait.\n\n";

            return false;
        }

        std::cout
            << "  After 20ms wait\n"
            << "    Sample count   : "
            << sampleCount
            << '\n'
            << "    Overflow count : "
            << overflowCount
            << '\n';

        // ----------------------------------------------------
        // 4. 최대 100 Sample Batch Read
        // ----------------------------------------------------
        constexpr unsigned int MAX_READ_COUNT = 100;

        DaoAdcBufferedSample samples[
            MAX_READ_COUNT]{};

            unsigned int readSampleCount = 0;

            if (DaoEngine_ReadAdcRingBuffer(
                0,
                samples,
                MAX_READ_COUNT,
                &readSampleCount) == 0)
            {
                std::cout
                    << "Ring buffer batch read failed.\n\n";

                return false;
            }

            std::cout
                << "  Batch read count : "
                << readSampleCount
                << '\n';

            // ----------------------------------------------------
            // 5. 처음 몇 개 Sample만 출력
            // ----------------------------------------------------
            const unsigned int printCount =
                (readSampleCount < 10)
                ? readSampleCount
                : 10;

            for (unsigned int i = 0;
                i < printCount;
                ++i)
            {
                std::cout
                    << "    "
                    << i
                    << "  Index: "
                    << samples[i].sampleIndex
                    << "  Value: "
                    << std::fixed
                    << std::setprecision(3)
                    << samples[i].filteredValue
                    << '\n';
            }

            std::cout
                << std::defaultfloat
                << '\n';

            return true;
    }


    bool TestAdcLongTermDrift(
        int adcPhysicalSlaveIndex)
    {
        std::cout
            << "\nADC Long-Term Drift Test\n";

        if (adcPhysicalSlaveIndex <= 0)
        {
            std::cout
                << "ADC device not found.\n\n";

            return false;
        }

        // ----------------------------------------------------
        // 60Hz + 120Hz Power Line Filter 사용
        // ----------------------------------------------------
        if (DaoEngine_SetAdcPowerLineFilterMode(
            0,
            2) == 0) // 2 = 60Hz mode
        {
            std::cout
                << "Power Line Filter setting failed.\n\n";

            return false;
        }

        const char* fileName =
            "adc_drift_30min.csv";

        std::ofstream file(fileName);

        if (!file.is_open())
        {
            std::cout
                << "CSV file open failed.\n\n";

            return false;
        }

        // ----------------------------------------------------
        // CSV Header
        // ----------------------------------------------------
        file
            << "SampleIndex,"
            << "TimeSec,"
            << "LowLevelFiltered,"
            << "PowerLineFiltered,"
            << "CalibratedValue,"
            << "FilteredValue\n";

        file
            << std::fixed
            << std::setprecision(6);

        // ----------------------------------------------------
        // 30분 / 0.5초 = 3600 Sample
        // ----------------------------------------------------
        constexpr int SAMPLE_COUNT = 3600;

        constexpr auto SAMPLE_PERIOD =
            std::chrono::milliseconds(500);

        const auto startTime =
            std::chrono::steady_clock::now();

        auto nextSampleTime =
            startTime;

        std::cout
            << "  Duration        : 30 minutes\n"
            << "  Sample interval : 0.5 sec\n"
            << "  Samples         : "
            << SAMPLE_COUNT
            << '\n'
            << "  File            : "
            << fileName
            << "\n\n";

        // ----------------------------------------------------
        // 장기 Drift 측정
        // ----------------------------------------------------
        for (int i = 0;
            i < SAMPLE_COUNT;
            ++i)
        {
            nextSampleTime +=
                SAMPLE_PERIOD;

            DaoAdcRuntimeInfo runtimeInfo{};

            if (DaoEngine_GetDaoAdcRuntimeInfo(
                adcPhysicalSlaveIndex,
                &runtimeInfo) == 0)
            {
                file.close();

                std::cout
                    << "ADC runtime read failed.\n\n";

                return false;
            }

            const auto now =
                std::chrono::steady_clock::now();

            const double elapsedSec =
                std::chrono::duration<double>(
                    now - startTime)
                .count();

            file
                << i
                << ','
                << elapsedSec
                << ','
                << runtimeInfo.lowLevelFiltered
                << ','
                << runtimeInfo.powerLineFiltered
                << ','
                << runtimeInfo.calibratedValue
                << ','
                << runtimeInfo.filteredValue
                << '\n';

            // 10초마다 파일 내용을 실제 디스크에 반영
            if ((i % 20) == 0)
            {
                file.flush();
            }

            std::this_thread::sleep_until(
                nextSampleTime);
        }

        file.close();

        std::cout
            << "\nLong-term drift test completed.\n"
            << "  File : "
            << fileName
            << "\n\n";

        return true;
    }

    bool TestAdcPowerLineFilter(
        int adcPhysicalSlaveIndex)
    {
        std::cout
            << "\nADC Power Line Filter Test\n";

        if (adcPhysicalSlaveIndex <= 0)
        {
            std::cout
                << "ADC device not found.\n\n";

            return false;
        }

        // ----------------------------------------------------
        // 1. 60Hz Notch Filter ON
        // ----------------------------------------------------
        const int setResult =
            DaoEngine_SetAdcPowerLineFilterMode(
                0,
                2); // 2 = 60Hz

        std::cout
            << "  60Hz filter request : "
            << setResult
            << '\n';

        if (setResult == 0)
        {
            std::cout
                << "60Hz filter setting failed.\n\n";

            return false;
        }

        // Notch 내부 상태 안정화
        std::this_thread::sleep_for(
            std::chrono::milliseconds(500));

        // ----------------------------------------------------
        // 2. Notch 전 / 후 값 직접 비교
        //
        // 60Hz 주기 약 16.67ms이므로
        // 5ms 간격으로 여러 위상을 확인합니다.
        // ----------------------------------------------------
        std::cout
            << "\n  60Hz Notch Monitor\n";

        for (int i = 0; i < 40; ++i)
        {
            DaoAdcRuntimeInfo runtimeInfo{};

            if (DaoEngine_GetDaoAdcRuntimeInfo(
                adcPhysicalSlaveIndex,
                &runtimeInfo) == 0)
            {
                std::cout
                    << "ADC runtime read failed.\n\n";

                return false;
            }

            const double difference =
                runtimeInfo.powerLineFiltered -
                runtimeInfo.lowLevelFiltered;

            std::cout
                << "    "
                << (i + 1)
                << "  Before: "
                << std::fixed
                << std::setprecision(1)
                << runtimeInfo.lowLevelFiltered
                << "   After: "
                << runtimeInfo.powerLineFiltered
                << "   Diff: "
                << difference
                << '\n';

            std::this_thread::sleep_for(
                std::chrono::milliseconds(5));
        }

        std::cout
            << std::defaultfloat
            << '\n';

        return true;
    }
    void PrintDeviceList(
        int slaveCount)
    {
        for (int listIndex = 0;
            listIndex < slaveCount;
            ++listIndex)
        {
            DaoSlaveInfo info{};

            if (DaoEngine_GetSlaveInfo(
                listIndex,
                &info) == 0)
            {
                continue;
            }

            std::cout
                << "Slave["
                << listIndex
                << "] "
                << info.name
                << " | Physical "
                << info.physicalSlaveIndex
                << " | Vendor 0x"
                << std::hex
                << std::uppercase
                << info.vendorId
                << " | Product 0x"
                << info.productCode
                << std::dec
                << std::nouppercase
                << '\n';
        }

        std::cout << '\n';
    }

    bool StartEtherCAT(
        int adapterIndex,
        int& outAdcPhysicalSlaveIndex)
    {
        if (DaoEngine_OpenAdapter(
            adapterIndex) == 0)
        {
            std::cout
                << "Adapter open failed.\n";

            return false;
        }

        const int slaveCount =
            DaoEngine_ScanSlaves();

        if (slaveCount <= 0)
        {
            std::cout
                << "No EtherCAT slave found.\n";

            return false;
        }

        std::cout
            << "\nDetected slave count: "
            << slaveCount
            << "\n\n";

        PrintDeviceList(
            slaveCount);

        if (DaoEngine_RequestAllSlavesPreOp() == 0)
        {
            std::cout
                << "PRE-OP transition failed.\n";

            return false;
        }

        if (DaoEngine_MapProcessData() == 0)
        {
            std::cout
                << "PDO mapping failed.\n";

            return false;
        }

        const int servoCount =
            DaoEngine_GetLogicalDeviceCount(
                DAO_DEVICE_SERVO);

        const int adcCount =
            DaoEngine_GetLogicalDeviceCount(
                DAO_DEVICE_ADC);

        const int ioCount =
            DaoEngine_GetLogicalDeviceCount(
                DAO_DEVICE_IO);
        
        const int encoderCount =
            DaoEngine_GetLogicalDeviceCount(
                DAO_DEVICE_ENCODER);

        std::cout
            << "Logical devices\n"
            << "  Servo   : "
            << servoCount
            << '\n'
            << "  ADC     : "
            << adcCount
            << '\n'
            << "  IO      : "
            << ioCount
            << '\n'
            << "  Encoder : "
            << encoderCount
            << "\n\n";

       

        if (adcCount > 0)
        {
            DaoLogicalDeviceInfo adcInfo{};

            if (DaoEngine_GetLogicalDeviceInfo(
                DAO_DEVICE_ADC,
                0,
                &adcInfo) == 1)
            {
                outAdcPhysicalSlaveIndex =
                    adcInfo.physicalSlaveIndex;
            }
        }

        if (DaoEngine_RequestAllSlavesSafeOp() == 0)
        {
            std::cout
                << "SAFE-OP transition failed.\n";

            return false;
        }

        if (DaoEngine_RequestAllSlavesOperational() == 0)
        {
            std::cout
                << "OP transition failed.\n";

            return false;
        }

        if (DaoEngine_StartCommunication() == 0)
        {
            std::cout
                << "Communication start failed.\n";

            return false;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(200));

        return true;
    }

    void SafeShutdown()
    {
        if (DaoEngine_IsCommunicationRunning() == 1)
        {
            ServoOff(
                SERVO_INDEX);

            DaoEngine_SetIoOutputCommand(
                IO_INDEX,
                0);

            std::this_thread::sleep_for(
                std::chrono::milliseconds(100));

            DaoEngine_StopCommunication();
        }

        DaoEngine_CloseAdapter();
        DaoEngine_Shutdown();
    }

    void PrintMenu()
    {
        std::cout
            << "========================================\n"
            << " DAO EtherCAT Servo Test\n"
            << "========================================\n"
            << " 1. Servo status\n"
            << " 2. Servo ON\n"
            << " 3. Servo OFF\n"
            << " 4. ADC / IO basic status\n"
            << " 5. Homing mode + Servo ON test\n"
            << " 6. Start homing and wait\n"
            << " 7. Async Servo ON test\n"
            << " 8. Async Servo OFF test\n"
            << " 9. Home request only test\n"
            << " 10. MoveAbs prepare test\n"
            << " 11. PV Mode test (Velocity 0)\n"
            << " 12. PV Forward / Torque Hold Stop Test\n"
            << " 13. PV Reverse / Torque Hold Stop Test\n"
            << " 14. PV Forward / ServoStop Test\n"
            << " 15. ADC Zero Test\n"
            << " 16. ADC Calibration Test\n"

            << " 17. ADC 60Hz Filter Test\n"

            << " 18. ADC Diagnostic Capture 10s\n"
            << " 19. ADC Long-Term Drift 30min\n"

            << " 20. ADC Ring Buffer Test\n"
            << " 21. Encoder status\n"
            << " 0. Exit\n"
            << "----------------------------------------\n"
            << "Select: ";
    }
}

int main()
{
    std::cout
        << "DAO EtherCAT Servo Test\n";

    std::cout
        << "Version: "
        << DaoEngine_GetVersion()
        << "\n\n";

    if (DaoEngine_Initialize() == 0)
    {
        std::cout
            << "Engine initialize failed.\n";

        return 1;
    }

    const int adapterCount =
        DaoEngine_GetAdapterCount();

    std::cout
        << "Adapter count: "
        << adapterCount
        << "\n\n";

    for (int adapterIndex = 0;
        adapterIndex < adapterCount;
        ++adapterIndex)
    {
        DaoAdapterInfo adapterInfo{};

        if (DaoEngine_GetAdapterInfo(
            adapterIndex,
            &adapterInfo) == 1)
        {
            std::cout
                << "["
                << adapterIndex
                << "] "
                << adapterInfo.description
                << '\n';
        }
    }

    std::cout
        << "\nSelect EtherCAT adapter index: ";

    int selectedAdapterIndex =
        -1;

    std::cin >>
        selectedAdapterIndex;

    int adcPhysicalSlaveIndex =
        -1;

    if (!StartEtherCAT(
        selectedAdapterIndex,
        adcPhysicalSlaveIndex))
    {
        SafeShutdown();

        return 1;
    }

    std::cout
        << "\nEtherCAT communication started.\n";

    PrintServoRuntime(
        SERVO_INDEX);

    bool exitRequested =
        false;

    while (!exitRequested)
    {
        PrintMenu();

        int menu =
            -1;

        std::cin >>
            menu;

        std::cout << '\n';

        switch (menu)
        {

        case 0:
            exitRequested =
                true;
            break;

        case 1:
            PrintServoRuntime(
                SERVO_INDEX);
            break;

        case 2:
            ServoOn(
                SERVO_INDEX);
            break;

        case 3:
            ServoOff(
                SERVO_INDEX);
            break;

        case 4:
            PrintBasicAdcAndIoStatus(
                adcPhysicalSlaveIndex);
            break;

        case 5:
            SetHomingModeAndServoOn(
                SERVO_INDEX);
            break;

        case 6:
            StartHomingAndWait(
                SERVO_INDEX);
            break;

        case 7:
             TestAsyncServoOn(
                SERVO_INDEX);
            break;

        case 8:
            TestAsyncServoOff(
                SERVO_INDEX);
            break;

        case 9:
            TestHomeRequestOnly(
                SERVO_INDEX);
            break;

        

        case 10:
            TestMoveAbsPrepareOnly(
                SERVO_INDEX);
            break;

        case 11:
            TestVelocityModeOnly(
                SERVO_INDEX);
            break;

        case 12:
            TestVelocityForwardAndStop(
                SERVO_INDEX);
            break;

        case 13:
            TestVelocityReverseAndStop(
                SERVO_INDEX);
            break;

        case 14:
            TestVelocityForwardAndServoStop(
                SERVO_INDEX);
            break;

        case 15:
            TestAdcZero(
                adcPhysicalSlaveIndex);
            break;

        case 16:
            TestAdcCalibration(
                adcPhysicalSlaveIndex);
            break;

        case 17:
            TestAdcPowerLineFilter(
                adcPhysicalSlaveIndex);
            break;

        case 18:
            TestAdcDiagnosticCapture(
                adcPhysicalSlaveIndex);
            break;

        case 19:
            TestAdcLongTermDrift(
                adcPhysicalSlaveIndex);
            break;

        case 20:
            TestAdcRingBuffer(
                adcPhysicalSlaveIndex);
            break;
        
        case 21:
        {
            DaoEncoderRuntimeInfo encoderInfo{};

            const int result =
            DaoEngine_GetEncoderRuntimeInfo(
                0, &encoderInfo);

            std::cout
            << "\nEncoder Runtime\n"
            << "  Read result       : "
            << result
            << '\n'
            << "  Physical Slave    : "
            << encoderInfo.physicalSlaveIndex
            << '\n'
            << "  Configured        : "
            << encoderInfo.configured
            << '\n'
            << "  Communication     : "
            << encoderInfo.communicationRunning
            << '\n'
            << "  Valid input       : "
            << encoderInfo.hasValidInputData
            << '\n'
            << "  CH1 Raw Count     : "
            << encoderInfo.presentCounterCh1
            << '\n'
            << "  CH2 Raw Count     : "
            << encoderInfo.presentCounterCh2
            << '\n'
            << "  CH1 Pulse Rate    : "
            << encoderInfo.pulseRateCh1
            << '\n'
            << "  CH2 Pulse Rate    : "
            << encoderInfo.pulseRateCh2
            << '\n'
            << "  Counter Command   : 0x"
            << std::hex
            << static_cast<int>(
                encoderInfo.counterCommand)
            << std::dec
            << '\n'
            << "  Last / Expected WKC: "
            << encoderInfo.lastWkc
            << " / "
            << encoderInfo.expectedWkc
            << "\n\n";

            break;
        }

        default:
            std::cout
                << "Invalid menu.\n\n";
            break;
        }
    }

    SafeShutdown();

    std::cout
        << "Engine shutdown completed.\n";

    return 0;
}