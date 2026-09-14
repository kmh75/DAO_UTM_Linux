# DAO UTM Homing / Communication Recovery Design Review

- 문서 상태: 1단계 현황 조사 및 설계안
- 조사 기준: 2026-09-14 현재 작업 트리
- 구현 상태: **미구현** (본 문서 외 소스, ABI, 동작 변경 없음)
- 안전 전제: 본 설계는 소프트웨어 분석 결과이며, 실제 Homing method/parameter와 EtherCAT watchdog 동작은 LS ELECTRIC L7NH 설정 및 실기 검증으로 확정해야 한다.

## 1. 결론 요약

1. Basic EtherCAT Engine은 이미 `DaoEngine_ServoHome(logicalServoIndex, timeoutMs)`, Homing command state machine, Mode 6, Controlword start, Statusword 완료/오류 비트 및 `DaoServoRuntimeInfo::homed`를 제공한다. 특정 Homing method/parameter 번호를 Application이 설정하지는 않는다.
2. UTM Engine에는 Homing API, 상태 머신, Homed runtime/gating이 없다. 현재 Absolute Move의 입력은 **Test Position 좌표**이며 Position Zero offset을 더해 Servo absolute position으로 변환된다. 따라서 이름과 달리 현재 Absolute Move는 Machine Home 유효성을 요구하지 않는다.
3. 현재 Communication Recovery는 Basic Engine과 UTM에 각각 별도 5-bad/3-good/300 ms 상태 판정이 있다. Basic은 Slave 재구성 및 OP 요청을 수행하고, UTM은 이를 직접 조회하지 않고 자체 WKC 유효성으로 추론한다.
4. UTM은 Motion 유무와 관계없이 `RECOVERING`을 stop 사유로 평가한다. Stop latch가 남고 output arbiter가 최초 `forceStop`에서 무조건 ServoStop을 발행하므로 `previousTargetVelocity=0`에서도 STOP PDO와 사용자 `STOPPED` 표시가 발생한다.
5. 개선 시 통신 사건과 안전 Motion interruption을 분리해야 한다. 비활동 중 성공한 일시 복구는 diagnostics only로 끝내고, Motion 중 사건은 자동 재개를 금지하고 interruption latch 및 복구 후 Servo 상태 정렬/사용자 ACK를 요구하는 방향이 안전하다.

## 2. 조사 범위와 주요 코드 근거

| 영역 | 현재 근거 |
|---|---|
| Basic Homing public API/runtime | `engine/include/DaoEtherCAT.Engine.h`, `engine/src/DaoEngineCore.cpp`, `engine/src/DaoEtherCAT.Engine.cpp` |
| Servo Homing/PDO 처리 | `master/include/DaoEtherCATMaster.h`, `master/src/DaoEtherCATMaster.cpp` |
| 좌표/Move | `utm/src/UtmCoordinateController.*`, `utm/src/UtmMotionController.*`, `utm/src/UtmMotionOutputArbiter.*` |
| UTM 통신 정책 | `utm/src/UtmCommunicationPolicy.h`, `utm/src/UtmInputCollector.cpp` |
| 안전 latch/상태 | `utm/src/UtmSafetyMonitor.cpp`, `utm/src/UtmStateMachine.cpp`, `utm/src/UtmEngineCore.cpp` |
| UI 표시 | `ui/src/MainWindow.cpp`, `ui/src/UtmUiController.cpp` |
| Sequence | `utm/src/UtmSequencer.*`, `utm/include/DaoUtm.Types.h` |

작업 트리는 조사 시작 시 이미 다수의 수정/미추적 파일을 포함하고 있었다. 본 단계에서는 그것들을 변경하지 않았으며 현재 보이는 코드 기준으로 분석했다.

---

## 3. Homing 현황

### 3.1 Basic EtherCAT Engine의 기존 지원

지원이 존재한다.

- Public API: `DaoEngine_ServoHome(int logicalServoIndex, unsigned int timeoutMs)`
- 내부 진입: `DaoEngineCore::ServoHome()` → `DaoEtherCATMaster::RequestServoHome()`
- 명령 종류: `DAO_SERVO_CMD_HOMING` / 내부 `DAO_SERVO_COMMAND_HOMING`
- 단계: PREPARE → MODE_REQUEST → SERVO_ON → START → RUNNING → FINISH → RESTORE_MODE
- 시작 전 검사: Engine/master open, communication running, 유효 input, Servo fault 없음, STO inactive, 다른 command 비활동, timeout nonzero
- Mode/PDO: output `operationMode=6`, display가 6인지 확인, CiA 402 enable 전이 후 Controlword `0x001F`로 Homing start
- 완료 판정: Statusword bit 12 Homing Attained, bit 13 Homing Error, Target Reached를 사용한다. 기존 attained 상태의 오인 방지를 위해 attained가 한번 low였는지도 요구한다.
- 종료: Mode 1(Profile Position) 복원 확인 후 `homed=true`, command completed
- 실패: Servo fault/STO/Homing error/timeout에서 command error 또는 timeout 및 `homed=false`
- 런타임: `DaoServoRuntimeInfo::homed`, command type/state/step/result가 기존 Basic public 구조에 이미 있다.
- 별도로 HOME digital input (`0x60FD` bit 2)도 decode하지만, Application이 Homing method를 고르거나 이 입력으로 자체 원점 탐색을 수행하지 않는다.

Basic의 구현은 Drive에 설정된 Homing method를 실행하는 형태다. 코드에는 Homing method object/parameter 번호 설정이 없으므로 사용자가 Drive에서 설정할 “부동원점” 계열과 충돌하지 않는 방향이다. 다만 실제 L7NH에서 Mode 6, Statusword bit 의미, start edge 요구, 완료 후 actual position 값이 선택 method에서 어떻게 동작하는지는 장비 매뉴얼/실기로 확인해야 한다.

현재 구현상 주의점도 있다.

- Homing timeout은 2 ms frame 수로 환산되므로 통신 주기 전제에 결합돼 있다.
- RUNNING 단계의 위치 변화 감시(`>5` unit)는 기록을 갱신하지만 `noMoveFrames`가 실제 실패 판정에 사용되지 않는다.
- limit, E-STOP, external stop은 Basic Homing 요청 자체의 입력 조건이 아니다. UTM orchestration에서 차단/중단해야 한다.
- `homed`는 Homing 요청/일부 Homing 실패에서 clear되지만, 일반 통신 상실·재구성·전원 재기동 후 좌표 신뢰 상실을 포괄하는 명시적 invalidation 정책은 확인되지 않았다. UTM이 Basic `homed`를 그대로 안전 보증으로 사용해서는 안 된다.

### 3.2 UTM Engine의 현재 상태

UTM public API와 `UtmEngineCore`에는 Home 요청 API나 Homing orchestration state machine이 없다. `UtmInputSnapshot`에는 `servoHomeInput`이 수집되지만 Machine Home 완료 상태로 연결되지 않는다. Runtime V1~V6에도 Machine Homed 상태가 없고 Motion/Sequence admission에도 Homed 검사 없이 Absolute Move가 허용된다.

### 3.3 현재 좌표 흐름

현재 변환은 아래와 같다.

```text
Servo input PDO actualPosition (integer servo units)
  └─ / servoUnitsPerMm
       = machinePositionMm
          └─ - testZeroOffsetMm
               = testPositionMm
```

- `machinePositionMm = actualPosition / servoUnitsPerMm`
- `testPositionMm = machinePositionMm - testZeroOffsetMm`
- Position Zero 실행: `testZeroOffsetMm = current machinePositionMm`, `positionZeroValid=true`
- Position Zero clear: offset 0, valid=false

즉 현재 코드에서 “Machine Position”은 Drive가 보고하는 actual position을 scale한 값이다. 정식 Machine Home을 거쳐 유효성이 보장된 좌표라는 뜻은 아니다. `servoDirectionSign`은 velocity 방향 변환에 사용되지만 위 position 변환에는 적용되지 않는 점도 장비 좌표 방향 정의 시 재검토 대상이다.

### 3.4 현재 Absolute Move의 정확한 좌표

`DaoUtm_MoveAbsolute(targetPositionMm, ...)`의 target은 `UtmEngineCore::MoveAbsolute()` 및 `UtmMotionController`에서 **target Test Position mm**로 취급된다.

```text
requested test target mm
  + testZeroOffsetMm
  = machine target mm
  × servoUnitsPerMm
  = Servo Profile Position absolute target (rounded integer)
```

따라서 Position Zero가 설정되면 같은 Absolute Move 입력값도 다른 Servo absolute target이 된다. Position Zero가 invalid여도 offset이 0일 뿐 Absolute Move는 차단되지 않는다. Incremental Move는 현재 Test Position에 증분을 더한 뒤 같은 변환을 사용하므로 수학적으로 Servo 현재 위치 + 증분이 된다.

### 3.5 새 Homed 상태 필요성 및 좌표 정의

별도의 UTM-level Machine Homed 유효성이 필요하다. Position Zero와 분리된 다음 모델을 권고한다.

- `machinePositionMm`: Homing 성공 후 Drive actual position을 scale한 기계 좌표. 선택한 Drive method가 완료 위치를 어떤 actual position으로 만드는지(예: 0 또는 configured home offset)가 정의의 기준이다.
- `machinePositionValid` 또는 `homed`: UTM이 현재 세션에서 성공한 Homing 결과와 지속적인 좌표 신뢰를 확인한 상태.
- `testZeroOffsetMm`: operator가 Machine Position 위에 얹는 시험 좌표 offset. Homing 성공이 이를 자동으로 0으로 만들거나 덮어쓰면 안 된다.
- `testPositionMm = machinePositionMm - testZeroOffsetMm`: 기존 의미 유지.

권고 기본값은 프로세스 시작/Basic reconnect/Servo power cycle 추정/position discontinuity 또는 Drive 좌표 신뢰 상실/관련 Servo fault/통신복구 중 Servo 재구성 시 `homed=false`이다. 단, 어떤 사건에서 Drive absolute coordinate가 실제 보존되는지는 L7NH 실기 결과에 따라 더 좁힐 수 있다. 보수적 invalidation이 안전 우선 기본이다.

기존 `DaoUtm_MoveAbsolute`의 의미를 갑자기 Machine coordinate로 변경하면 동작 호환성이 깨진다. 기존 API는 Test Absolute 의미로 유지하되 Homed gating을 적용할지 migration 정책을 명시하고, 필요하면 별도 `MoveMachineAbsolute`를 additive API로 둔다. 가장 안전한 목표 정책은 모든 absolute position command(기존 Test Absolute 포함)를 `homed && machinePositionValid`일 때만 허용하는 것이나, 기존 현장 workflow 영향 때문에 compatibility/configuration 기간을 거쳐야 한다.

### 3.6 제안 Homing 상태 머신

```text
UNHOMED
  → PRECHECK
  → REQUESTED
  → PREPARING/MODE_CHANGE
  → HOMING_RUNNING
  → VERIFYING
  → HOMED

어느 진행 상태에서든:
  user abort / limit / E-STOP / Servo fault / communication incident
  → ABORTING_SAFE_ALIGNMENT → FAILED 또는 INTERRUPTED → UNHOMED
```

PRECHECK 조건:

- UTM READY, stop latch 없음, communication stable, Servo input fresh
- Jog/general motion/force motion/calibration/Sequence/Remote Motion 모두 inactive
- Servo fault/STO/E-STOP/external stop/해당 방향 limit 없음
- 유효 timeout 및 단일 owner 획득
- Position Zero는 보존하되 UI에 Homing 후 Test 좌표가 달라질 수 있음을 명확히 표시

동작 원칙:

- 시작 시 command epoch를 올리고 새 Motion 명령을 거부한다.
- Basic `DaoEngine_ServoHome` 한 번만 요청하고 command id/type/state/step/result를 관찰한다.
- Homing 중 Jog, Sequence start, UI/Remote motion, Position Zero 변경을 거부한다.
- 통신 사건 시 기존 Homing을 자동 resume하지 않고 interrupted로 끝낸다.
- 완료 후 Mode 1, operation enabled/expected safe state, fault 없음, fresh actual position, stable WKC, Basic `homed`를 함께 확인한 뒤 UTM Homed를 set한다.
- 실패/timeout/abort 후 무조건 목표값 자동 재개 금지. 통신이 유효할 때만 안전 stop/disable 정렬을 수행하며, 통신 단절 중 STOP PDO 전송 성공을 안전 근거로 기록하지 않는다.

Limit의 어느 방향이 Homing method상 정상 탐색인지 Application이 추측하면 안 된다. Drive 설정과 검증된 machine profile의 허용 방향/limit policy가 확정되기 전에는 limit active를 실패 처리하는 보수적 정책을 권고한다.

### 3.7 Homing API/Runtime 제안

Basic public ABI는 이미 충분하므로 우선 변경하지 않는다. UTM에는 additive versioning을 사용한다.

- 명령: `DaoUtm_RequestMachineHome(timeoutMs, source)`와 `DaoUtm_AbortMachineHome(source)` 제안
- 조회: 새 `UtmHomingRuntimeInfo` (state, homed, machinePositionValid, command/result, elapsed, failure reason, interrupted, Basic command snapshot)
- Runtime: 기존 V6 구조 수정 금지. `UtmRuntimeInfoV7 { UtmRuntimeInfoV6 runtime; UtmHomingRuntimeInfo homing; UtmCommunicationRuntimeInfo communication; }`처럼 tail-extension을 제안하거나 독립 getter V1을 둔다.
- 초기 구현에서는 method number/Drive parameter를 API에 넣지 않는다. Application은 Drive 설정을 변경하지 않는다.
- control loop에서 고정 크기 POD/atomic snapshot을 사용하고 allocation, blocking SDO, UI mutex를 추가하지 않는다.

### 3.8 UI 제안

- Main UI: `HOMED / NOT HOMED / HOMING / HOME FAILED`를 Position Zero 상태와 별도 표시
- Home action: READY 및 precheck 충족 시에만 활성화, 시작 전 “장비 이동 가능” 확인 절차 제공
- Absolute Move: Homed가 필수인 정책 적용 시 비활성화하고 정확한 이유 표시
- Position Zero: 기존 버튼/기능 유지; “Test Zero”로 명확히 명명하고 Machine Home과 시각적으로 분리
- Homing 상세 단계, Basic command result, sensor/limit/Servo state는 Engineer Diagnostics에 표시
- Homing interruption/failure는 사용자 alarm 대상. 성공은 정상 상태 복귀로 표시하되 event history에는 남긴다.

### 3.9 Sequence HOME 명령 의견

1차 구현에는 Sequence HOME 명령을 넣지 않는 것을 권고한다. Homing은 장비 준비/복구 절차이며 시험 Sequence 중 자동 실행되면 예상치 못한 장거리 이동과 좌표 재정의 위험이 있다. 우선 operator-controlled Machine Setup 동작으로 구현하고, Sequence는 시작 validation에서 `requiresHomed`를 검사한다.

추후 생산 요구가 명확해질 때에만 새 Sequence ABI version에 HOME step을 additive하게 추가한다. 그 경우 step은 반드시 sequence 첫 준비 구간, 명시적 timeout, precheck, non-resumable, failure abort 조건을 가지며 Protocol V1의 기존 의미는 변경하지 않는다.

---

## 4. 현재 EtherCAT Communication/Recovery 분석

### 4.1 Basic Engine 상태와 복구

500 Hz communication loop에서 `actualWkc >= expectedWkc`를 good으로 본다.

- bad 1회: NORMAL → TRANSIENT 로그 및 diagnostic ring dump
- bad 3회: TRANSIENT → DEGRADED
- bad 5회(약 10 ms): RECOVERING 진입, 즉시 soft recovery
- recovery 중 매 25 bad cycle(약 50 ms)마다 retry
- 최초 recovery 시작 후 300 ms timeout이면 communication thread stop 요청
- good WKC 3회 연속(약 6 ms): RECOVERED
- Slave recovery call timeout: Slave당 2,000 μs

`AttemptSoftRecovery()`의 현재 단계:

1. `ecx_readstate()`
2. 각 Slave 검사; OP이고 lost가 아니면 skip
3. SAFE_OP+ERROR이면 SAFE_OP+ACK write
4. lost/NONE이면 `ecx_recover_slave`, 아니면 `ecx_reconfig_slave`
5. 성공 반환 시 `islost=false`, state를 OP로 지정하고 `ecx_writestate`
6. 이후 loop에서 전체 process WKC good 3회를 성공 근거로 사용

장점은 모든 Slave를 일반적으로 순회하며 특정 slave=3/4를 하드코딩하지 않는다는 점이다. 현재 slave 위치 교환/CNT02 의심은 diagnostics 정보로만 다뤄야 한다.

현재 부족하거나 불명확한 점:

- OP request 후 `ecx_statecheck` 등으로 각 Slave가 실제 OP에 도달했는지 단계별 검증하지 않는다.
- reconfig/recover 반환과 OP write 반환은 로그만 남고 실패 사유별 누적/결과 상태가 없다.
- ACK 후 state 재확인, SAFE_OP 도달 확인, 모든 Slave state 확인이 명시적 단계로 분리돼 있지 않다.
- 성공은 aggregate expected WKC 3회로 판정하지만 Slave별 fresh input/update 및 expected WKC 구성의 정상성은 별도로 확인하지 않는다.
- retry는 300 ms/약 50 ms 주기로 유한하지만 Slave 수 × blocking recovery 호출 시간이 cycle budget을 초과할 수 있다.
- Basic recovery 상태/incident 결과가 public runtime으로 UTM에 전달되지 않는다. UTM은 독립된 유사 정책으로 추론한다.
- Basic과 UTM 양쪽의 상수는 현재 우연히 5 bad/3 good/300 ms로 같지만 단일 정책 소스가 아니므로 향후 drift 위험이 있다.

### 4.2 UTM Communication 상태 머신

`UtmInputCollector`는 required Servo/ADC/IO 각각의 `validInput`을 AND한 `cycleDataValid`와 Basic running을 `UtmCommunicationPolicy`에 넣는다. Optional encoder는 UTM 정책의 AND 조건에 포함되지 않는다.

- 1~2 invalid cycle: TRANSIENT
- 3~4: DEGRADED
- 5 이상: RECOVERING
- RECOVERING에서 good 3 cycle: NORMAL + recovered
- RECOVERING 시작 300 ms 경과: FAULT
- Basic running=false: 즉시 FAULT
- `stopRequired = RECOVERING || FAULT`, `hardFault = FAULT`

이 정책은 Basic의 실제 `communicationRecovering_`, recovery attempt/stage/result를 읽지 않는다. 즉 UTM `RECOVERING`은 “Basic recovery가 수행 중임”의 직접 관찰값이 아니라 동일 WKC 현상의 독립 추정값이다.

### 4.3 stopRequired 및 ServoStop 경로

정확한 경로는 다음과 같다.

```text
5 consecutive invalid cycles
→ UtmCommunicationPolicy::RECOVERING, stopRequired=true
→ UtmEngineCore passes communicationRecovering=true
→ UtmSafetyMonitor::Evaluate adds UTM_STOP_COMMUNICATION_FAULT
   (motionActive 조건 밖이므로 idle에도 추가)
→ UtmStopLatch::Update permanently latches stop
→ UtmStateMachine sees latched communication stop
   - communicationValid가 아직 true이면 STOPPED
   - hard fault로 invalid이면 FAULT
→ forceMotionStop = evaluation.requested || stop.latched || ...
→ UtmMotionOutputArbiter::Update(forceStop=true)
→ first forceStop calls IssueStop("force-stop") unconditionally
→ DaoEngine_ServoStop(), even if prior target velocity is zero
→ MainWindow maps machine state to STOPPED and alarm property=true
```

따라서 로그의 `previousTargetVelocity=0`은 버그 원인이 아니라 당시 arbiter 기록값이다. `forceStop` 분기는 active output/zero velocity 여부를 검사하지 않고 최초 호출에서 ServoStop을 발행한다. Recovery가 11 ms에 성공해 evaluation이 사라지고 `stopRequired=0`이 되어도 `UtmStopLatch`는 ACK 전까지 남는다. `UtmStateMachine`도 stop latch 때문에 STOPPED를 유지하고 Main UI는 `UTM_MACHINE_STOPPED`를 alarm으로 렌더링한다. 사용자의 Reset/ACK 후 latch가 clear되고 READY로 돌아가는 관측과 일치한다.

또한 통신이 실제 단절된 동안 `DaoEngine_ServoStop`의 API 반환값은 명령 등록/호출 결과일 뿐 Drive가 물리적으로 정지했다는 증거가 아니다. 이 반환을 safety confirmation으로 사용하면 안 된다.

---

## 5. 개선 Recovery 설계

### 5.1 상태/책임 분리

다음 세 축을 분리한다.

1. **Communication health:** NORMAL, TRANSIENT, DEGRADED, RECOVERING, STABILIZING, HEALTHY, FAILED
2. **Motion incident:** NONE, INTERRUPTED_UNCONFIRMED, ALIGNING_SAFE_STATE, ACK_REQUIRED
3. **User presentation:** NONE, UNSTABLE_WARNING, COMMUNICATION_FAULT

Main machine state나 STOP 표시가 단순히 RECOVERING과 동일해서는 안 된다. 반대로 UI에 표시하지 않는 transient도 Engineer log/statistics에는 반드시 기록한다.

Basic Engine이 실제 recovery execution의 단일 owner가 되고 UTM은 versioned runtime snapshot을 통해 stage/result/incident generation을 관찰하는 구조가 이상적이다. Basic ABI 변경을 피해야 하는 첫 구현에서는 기존 ABI를 수정하지 말고 additive diagnostics getter 또는 새 versioned struct를 검토한다. UTM의 safety classification은 자신이 관찰한 fresh input과 Basic incident generation을 함께 사용한다.

### 5.2 제안 Recovery 상태 머신

```text
HEALTHY
  bad WKC → TRANSIENT → DEGRADED
  threshold → INCIDENT_CLASSIFY + RECOVERING

RECOVERING (finite deadline/retry budget)
  READ_STATE
  → ACK_SAFEOP_ERROR (해당 시)
  → RECOVER_OR_RECONFIG
  → VERIFY_SAFE_OP / REQUEST_OP
  → VERIFY_EACH_SLAVE_OP
  → VERIFY_EXPECTED_WKC
  → STABILIZING (연속 good + fresh inputs)
  → RECOVERED

deadline/retry exhausted 또는 critical Servo state 불명
  → FAILED
```

권고 검증 항목:

- incident 시작 시 expected/current/min WKC와 모든 Slave state snapshot 고정 저장
- Slave별 READ_STATE 결과 및 AL status code 기록
- SAFE_OP+ERROR ACK 후 state 재확인
- lost는 recover, 존재하나 비-OP는 reconfig; 결과별 제한된 retry
- reconfiguration 후 SAFE_OP/OP 도달을 bounded `statecheck`로 확인
- 모든 required Slave가 OP이고 lost=false인지 확인
- aggregate WKC가 현재 topology의 expected WKC 이상인지 확인
- Servo/ADC/IO required input update가 새로 증가하고 fresh인지 확인
- good WKC 안정화 연속 sample 후에만 RECOVERED publish

초기 설계값(실기 튜닝 전 제안): 기존 현장 검증값을 기준선으로 **총 recovery deadline 300 ms, retry 간격 약 50 ms, 최대 5~6 attempts, Slave operation timeout 2 ms의 현행값을 우선 보존**한다. 다만 “시간”과 “횟수”를 모두 제한해 scheduler 지연에도 무한 retry가 없게 한다. 각 단계의 최종값은 SOEM call latency, Slave 수, L7NH/CNT02 worst-case 재진입 시간 실측 후 승인한다. 안전 중요 값이므로 본 문서에서 임의 확대하지 않는다.

### 5.3 Motion inactive 정책

incident 직전의 원자적 snapshot으로 아래가 모두 참일 때 inactive로 분류한다.

- output arbiter가 position/velocity active가 아님
- general Move/force motion inactive 및 stopping 아님
- Jog request/active 없음
- Sequence가 motion step을 실행 중이지 않고 pending motion action 없음
- calibration motion 없음
- Remote/UI command mailbox에 accepted/pending Motion 없음
- Servo output target velocity=0이며 active position command 없음

정책:

- 새 Motion admission을 즉시 inhibit하고 command epoch로 stale pending command를 폐기
- 불필요한 ServoStop PDO는 발행하지 않음
- bounded recovery 수행
- 성공 후 Servo fault/STO/CiA state/actual position freshness를 확인
- Motion interruption latch와 STOPPED latch를 만들지 않고 READY 자동 복귀
- 사용자 ACK 없음, Main UI alarm 없음, Engineer incident log/count만 증가
- 복구 중 좌표 신뢰가 상실되었다면 READY라도 `homed=false`로 Absolute Move는 금지

“target velocity=0” 하나만으로 inactive로 분류하면 position move/sequence pending을 놓치므로 위 복합 snapshot이 필요하다.

### 5.4 Motion active 정책

incident 당시 어떤 Motion ownership/command라도 있으면 다음을 원자적으로 수행한다.

- `motionInterrupted=true`, interruption generation/reason/command id/source/sequence step latch
- command epoch 증가, Jog 및 모든 pending/continuation 폐기
- Sequence와 calibration은 non-resumable abort; 이전 target velocity/position 재사용 금지
- 통신 단절 중 STOP PDO 성공을 주장하지 않음. Drive watchdog이 1차 안전 동작을 수행해야 한다.
- Recovery 후 fresh Servo Statusword/CiA state/fault/STO/actual velocity(현재 PDO 제공 여부 확인 필요)를 확인
- 통신이 유효해진 뒤 필요하면 새 명령으로 STOP 또는 Servo Disable/safe state alignment 수행하고 결과를 관찰
- 안전 상태가 확인되기 전 READY 금지
- 정렬 성공 후에도 ACK_REQUIRED/STOPPED 유지; 사용자가 상황을 확인하고 ACK해야 READY
- 정렬 실패, Servo state 불명, recovery 실패는 COMMUNICATION FAULT/FAULT 유지
- 어떤 경우에도 이전 Motion/Sequence 자동 resume 금지

실제 정지 확인 기준은 현재 `target velocity=0`이나 STOP command state만으로 충분하지 않다. L7NH actual velocity PDO availability, Statusword, position 안정성 및 Drive watchdog 결과를 토대로 별도 승인된 기준이 필요하다.

### 5.5 사용자 알림 정책

| 사건 | Main UI | ACK | Engineer |
|---|---|---:|---|
| 단발 transient/degraded 후 recovery 불필요 | 없음 | 없음 | WKC event 기록 |
| idle 중 bounded recovery 성공 | 없음(선택적으로 짧은 non-alarm status) | 없음 | incident/recovery 상세 |
| Motion 중 recovery 성공 | “Motion interrupted; inspect and acknowledge” STOPPED | 필요 | command/Slave/recovery/safe alignment 상세 |
| recovery 실패/Servo 상태 불명 | Communication Fault alarm | 필요 + 원인 해소 | 전체 상세 |
| 반복 recovery 성공 | Communication Unstable warning | 일반적으로 없음 | 빈도/Slave/기간 상세 |

반복 불안정의 초기 제안값은 **rolling 1 hour 내 recovery incident 3회 이상** warning, 또는 **연속 recovery failure 1회 즉시 fault**이다. 2회는 Engineer에 선행 표시하고 3회부터 사용자 warning을 권고한다. 이는 확정 하드코딩값이 아니며 1~2일 추가 장시간 테스트의 정상 background rate와 cable/ground 변경 결과로 승인해야 한다. Warning은 safety latch와 별개이며 Motion 자동 중단 조건으로 암묵 사용하지 않는다.

### 5.6 Engineer Diagnostics 추가 항목

고정 크기 누적 counter/snapshot/ring buffer로 다음을 제공한다.

- total communication incidents
- recovered incident count / recovery failure count
- rolling-window incident count 및 window 길이
- first/last incident monotonic timestamp, wall-clock 표시는 UI에서 변환
- last failed/recovered physical slave와 장치 identity/name
- last/maximum recovery duration, attempt count, final stage/result
- consecutive recovery failures
- Slave별 last EtherCAT state, AL status code, lost flag, reconfig/recover/OP 결과
- minimum/current/expected WKC, incident-start WKC, consecutive/max bad WKC
- good stabilization count, required input freshness/update counts
- receive timeout count, late-cycle/max-cycle 지표
- incident 당시 motion classification, command id/type/source, sequence step, target velocity/position
- post-recovery Servo CiA/status/fault/STO/operation mode 및 safe-alignment 결과
- Machine Homed invalidation 여부/사유

Control loop에서는 counter와 preallocated entry만 갱신하고 문자열 formatting, 파일 I/O, dynamic allocation은 비실시간 consumer/UI에서 수행한다.

### 5.7 Servo Drive watchdog 실기 검증

Application 변경과 별도로 다음을 LS L7NH 매뉴얼 및 안전한 무부하/저속 장비 시험으로 확인해야 한다. 이번 단계에서는 parameter를 변경하지 않는다.

- EtherCAT process-data watchdog timeout의 실제 설정값 및 enable 여부
- watchdog 발생 시 Drive 동작: coast, quick stop, controlled stop, torque off 등
- 통신 단절 중 active velocity/position 명령이 얼마나 유지되는지
- 통신 복귀 시 이전 setpoint가 재실행되는지, enable 상태가 보존되는지
- watchdog/communication error가 Statusword, error code/history, AL status에 어떻게 나타나는지
- SAFE_OP/OP 재진입 후 Servo enable, mode, target, actual velocity/position 상태
- Drive 전원 cycle/reconfig/recover 후 absolute position 및 Homing attained 신뢰성
- E-STOP/STO/limit가 Homing 및 recovery 도중 우선 동작하는지
- cable disconnect, link flap, CNT02/IO 위치 교환, encoder cable/noise/ground 조건별 failed Slave 식별 정확성

최종 safety claim은 PC가 STOP PDO를 보냈다는 사실이 아니라 Drive watchdog과 독립 안전 회로가 검증된 동작을 수행하고, 복구 후 Application이 실제 상태를 재확인했다는 근거로 구성해야 한다.

---

## 6. 예상 수정 범위 (승인 후 구현 단계)

아래는 예상이며 이번 단계에서 수정하지 않았다.

| 파일/영역 | 예상 변경 | ABI 영향 |
|---|---|---|
| `utm/src/UtmSafetyMonitor.*` | recovery와 motion interruption 조건 분리 | 내부 |
| `utm/src/UtmCommunicationPolicy.h` | 사건 결과/통계/유한 상태 명확화 | 내부 |
| `utm/src/UtmInputCollector.*` | Basic recovery snapshot 및 Slave/freshness 수집 | 내부 |
| `utm/src/UtmEngineCore.*` | incident classification, command invalidation, Homing orchestration | 내부 |
| `utm/src/UtmStateMachine.*` | idle recovery auto-ready 및 active interruption ACK 정책 | 내부 |
| `utm/src/UtmMotionOutputArbiter.*` | inactive 무출력 stop, post-recovery alignment | 내부 |
| `utm/src/UtmMotionController.*`, `UtmCoordinateController.*` | Homed gating/invalidation 및 좌표 validity | 내부 |
| `utm/include/DaoUtm.Types.h`, `DaoUtm.Engine.h`, `utm/src/DaoUtm.Engine.cpp` | additive V7 또는 독립 V1 runtime/API | **새 ABI symbol/struct만 추가**, 기존 불변 |
| `master/src/DaoEtherCATMaster.cpp`, `master/include/...` | 단계 검증/통계 snapshot 강화 | 가급적 내부 |
| `engine/include/DaoEtherCAT.Engine.h`, wrapper/core | additive recovery diagnostics getter가 필요할 경우 | **새 symbol만 추가**, 기존 불변 |
| `ui/src/MainWindow.cpp`, `UtmUiController.*` | Homed UI, alarm/diagnostics 분리 | 내부 UI |
| `utm/src/UtmSequencer.*`, Types | 현재는 Homed validation만; HOME step은 후속 별도 승인 | HOME 추가 시 새 Sequence ABI 필요 |
| `app/communication_policy_test`, UTM/engine test | 정책/state/invalidation 테스트 | 없음 |

변경 금지 범위인 RuntimeV6, 기존 Force/ADC/calibration/compliance/Protocol V1/MachineProfile persistence/PDO mapping/SOEM은 그대로 둔다. 새 persistence는 우선 만들지 않는다. Homed를 저장해 재기동 후 복원하는 것은 좌표 신뢰를 과대평가하므로 금지하는 방향이 기본이다.

### 6.1 Public ABI 및 version 결론

- Basic Homing에는 기존 public API/ABI가 이미 있으므로 변경 불필요.
- UTM Homing과 communication diagnostics 노출에는 additive API가 필요하다.
- 기존 Runtime V6 필드 추가/재배열은 금지. Runtime V7 tail extension 또는 독립 `GetHomingRuntimeV1`/`GetCommunicationRuntimeV1` 중 하나를 사용한다.
- Sequence HOME는 기존 Sequence definition ABI에 enum만 끼워 넣지 말고, 필요성이 승인되면 V2 definition/API로 추가한다.
- Protocol V1은 변경하지 않는다. Remote 노출은 별도 protocol version에서 보안/ownership/ACK semantics까지 설계한다.

---

## 7. 회귀 및 안전 위험

### 7.1 주요 회귀 위험

- 기존 Absolute Move 사용자가 Position Zero만 설정하고 운전하던 workflow가 Homed gating으로 거부될 수 있음
- idle/active 오분류 시 필요한 stop latch가 누락되거나 불필요한 STOPPED가 재발할 수 있음
- Basic/UTM recovery 상태 timing 불일치 및 thread snapshot race
- post-recovery stale command/setpoint가 재발행될 위험
- Sequence/calibration/force motion owner 정리 누락
- Homing 완료 후 Drive actual position jump로 Test Position/그래프/stop-condition 값이 불연속이 될 위험
- Position 방향에 `servoDirectionSign`이 미적용된 기존 의미를 섣불리 바꾸면 좌표 반전 회귀
- recovery call이 2 ms control/communication cycle을 block하여 jitter를 키울 위험
- Homed를 통신 복구 후 잘못 유지하거나 너무 자주 무효화하는 양쪽 위험
- UI warning과 실제 safety latch를 한 필드로 재사용할 경우 운영자 혼동

### 7.2 안전 주의사항

- Software STOP 전송은 통신 단절 시 정지 보증이 아니다.
- Motion active 판정은 target velocity 하나로 하면 안 된다.
- recovery 성공은 Motion continuation 허가가 아니다.
- Homing method/offset/direction/limit behavior를 Application에서 추측하거나 변경하지 않는다.
- Homing 중 physical travel envelope와 fixture/load 상태를 사전 확인해야 한다.
- Position Zero는 Machine Home이 아니며 Homed validity를 만들지 않는다.
- actual position의 scale/방향/overflow/불연속을 검증하기 전 Machine Position을 안전 limit 기준으로 승격하지 않는다.
- 모든 timeout/retry/watchdog 값은 하드웨어 evidence와 change control 승인을 거친다.

---

## 8. 구현 순서 제안

1. 요구/용어 동결: Machine/Test 좌표, Homed invalidation matrix, active Motion 정의, 사용자 ACK 정책 승인
2. 하드웨어 baseline: L7NH Homing 설정과 watchdog/communication error 동작을 문서화하고 안전 시험
3. 테스트 가능한 순수 policy부터 작성: incident classification, retry/deadline, notification, Homed invalidation
4. Basic recovery diagnostics를 additive snapshot으로 노출하고 Slave별 verify 단계 강화
5. UTM idle/active recovery 분리 및 stale command invalidation 구현; 자동 resume 금지 테스트
6. UTM Homing orchestration과 additive runtime/API 구현; 기존 Position Zero 유지
7. UI Main/Engineer 분리 및 Homing controls 추가
8. Sequence는 Homed precondition validation만 먼저 적용; HOME step은 별도 승인 후 수행
9. simulation/fault injection → bench → 무부하 저속 → 제한된 load → 장시간 실기 순으로 승격

각 단계는 독립 review/rollback point를 두고 Homing과 recovery 변경을 한 번에 현장 투입하지 않는 것을 권고한다.

---

## 9. 테스트 계획

### 9.1 Unit test

- 좌표: servo↔machine↔test 변환, Position Zero 보존, rounding/overflow/NaN, Homing 후 actual position 변화
- Absolute: Homed false/true admission, 기존 Test target + offset 변환 호환성
- Homing: 모든 정상 단계, stale attained bit, mode timeout, Homing error, Servo fault/STO/limit/E-STOP, abort, communication interruption, competing owner 거부
- invalidation: startup/reconnect/reconfig/power-loss/fault별 Homed matrix
- communication: bad 1/3/5, good stabilization, retry count/deadline, failure, rolling window warning
- incident classification: idle, velocity 0 but position active, pending sequence, force/calibration, Jog release race, Remote mailbox pending
- latch: idle recovery 성공 시 no stop/no ACK; active recovery 시 interrupted latch/ACK; failure 시 fault
- arbiter: idle recovery에서 ServoStop 미발행; active 복구 후 stale target 미재발행
- 통계: saturation/rollover, ring wrap, max duration/min WKC/last slave

### 9.2 Simulation / fault injection

- Slave별 WKC drop, no-frame, intermittent 1~4 cycles, 5+ cycles, 300 ms 초과
- SAFE_OP+ERROR, NONE/lost, reconfig failure, recover failure, OP request failure, one Slave non-OP with aggregate variations
- Basic recovery와 UTM consumer의 scheduling phase 차이
- recovery 직전/도중 command 제출, Sequence step transition, Jog reversal, force hold correction
- success 후 old target velocity/position가 절대 출력되지 않는지 trace assertion
- UI에서 단발 성공은 alarm 없음, 반복은 warning, active interruption/failure만 STOP/FAULT인지 검증
- Runtime V1~V6 binary/layout regression 및 기존 tests 전부 실행

### 9.3 Hardware test

사전 조건: 무부하 또는 안전 fixture, 저속/제한 travel, E-STOP/STO 독립 확인, 관찰자 배치, Drive parameter backup.

1. Drive에 사용자가 설정한 Homing method로 Basic Engine 단독 Homing 단계/Mode/Status/actual position 확인
2. UTM Homing 정상/실패/timeout/limit/E-STOP/STO/Servo fault 및 Position Zero 보존 확인
3. 전원 재기동/Servo reset/link recovery 후 Homed invalidation 확인
4. 완전 idle에서 각 Slave link disturbance: recovery 성공, ServoStop 미발행, Main STOP 없음, READY 복귀 확인
5. target velocity 0이지만 position command/Sequence pending인 경계 조건이 active로 분류되는지 확인
6. 저속 Jog/velocity/position/Sequence 중 link disturbance: Drive watchdog 물리 동작, no auto-resume, interrupted latch, post-recovery state alignment와 ACK 확인
7. recovery 실패/timeout 및 반복 incident warning 검증
8. Servo/ADC/IO/CNT02 각각에 동일 일반 recovery 적용 확인; physical index 하드코딩 부재 확인
9. Encoder cable/noise/ground 변경 전후 24~48시간 통계 비교: incident count, failed Slave, min WKC, max duration
10. 전체 기존 Force/ADC/filter/calibration/compliance/sequencer/Protocol/UI 장시간 회귀 시험

합격 기준에는 “Recovery 로그가 성공”뿐 아니라 물리 Motion 관찰, Drive fault history, Slave별 OP/fresh data, stale setpoint 부재, UI/ACK 정확성을 포함해야 한다.

---

## 10. 설계 승인 전 결정 필요 항목

- 기존 `MoveAbsolute`에 Homed requirement를 즉시 적용할지, 단계적 compatibility mode를 둘지
- Homing 성공 시 기존 Test Zero offset을 보존할지(권고: 보존) 또는 operator 재설정을 요구할지
- 어떤 communication/reconfig/power event가 L7NH absolute coordinate 신뢰를 실제로 잃게 하는지
- post-recovery “Servo safely stopped”의 측정 가능한 판정 기준
- rolling warning threshold(초기 제안: 1시간/3회)와 warning clear 정책
- recovery 300 ms/attempt/Slave timeout의 장비별 실측 승인값
- Homing을 Setup 전용으로 둘지, 장래 Sequence V2 HOME 요구가 실제 존재하는지

이 항목들이 승인되기 전에는 안전 관련 구현을 시작하지 않는다.
