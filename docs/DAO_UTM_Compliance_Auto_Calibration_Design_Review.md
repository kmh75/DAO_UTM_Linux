# DAO UTM Linux — Machine Compliance Auto Calibration 설계 검토

- 검토일: 2026-09-07
- 단계: 구현 전 설계 검토
- 결론: 조건부 구현 가능
- 권고 구조: C — UTM Engine 소유 상태기계 + Application/UI/Protocol 관리 계층
- 본 검토에서 코드·리팩터링·빌드·하드웨어 실행·커밋·푸시: 수행하지 않음

## 1. Executive Summary

Machine Compliance Auto Calibration은 현재 DAO UTM 구조에서 안전하게 구현할 수 있다. 다만 UI의 40 ms polling timer가 여러 `DaoUtm_*` 명령을 순차 호출하는 방식으로 실제 장비를 운전해서는 안 된다. Calibration의 motion ownership, 2 ms interlock, force/position 동시 sample capture, abort/fault 처리와 release sequencing은 UTM Engine control thread가 소유해야 한다. UI와 향후 Windows Protocol은 설정 제출, 두 단계 사용자 승인, 진행 표시, pending curve의 discard/save/enable만 담당해야 한다.

현재 작업 트리에는 `UtmComplianceAutoCalibration`과 방향별 profile/UI의 미커밋 초안이 존재한다. 이 초안은 offline 논리 실험에는 유용하지만 production engine에 통합되지 않았으며, UI timer 의존, 일반 UI command source 사용, 원자적 capture 부재, calibration 전용 motion ownership 부재 때문에 실제 hardware-ready 구현으로 판정할 수 없다. 본 설계는 그 초안을 채택하는 승인이 아니라 실제 코드 감사에 따른 목표 구조다.

HIGH RISK 항목은 7개다.

1. Force/position polarity가 설치별로 검증되지 않음
2. UI polling 기반 orchestration의 지연·종료·disconnect 위험
3. 기존 `HOLD_FORCE`의 설정이 calibration 요청별 속도와 일치하지 않음
4. 기존 overload stop이 controlled stop이며 calibration upper guard와 독립 여유가 필요함
5. force release 중 fixture 분리 또는 반대방향 하중 전환 위험
6. force와 position의 동일-cycle 평균 capture API가 없음
7. source ownership이 UI/REMOTE/Sequencer와 calibration 사이에서 명시적으로 분리되지 않음

## 2. 실제 코드 감사 결과

### 2.1 Displacement와 coordinate

`UtmInputCollector`가 Servo actual position을 `UtmInputSnapshot::servoActualPosition`에 수집한다. `UtmMotionController::UpdatePosition()`은 이를 `UtmCoordinateController`에 전달해 다음 두 값을 만든다.

```text
machinePositionMm = servoPosition / servoUnitsPerMm
testPositionMm    = machinePositionMm - testZeroOffsetMm
```

`servoDirectionSign`은 속도 명령을 Servo UU/s로 바꿀 때 적용되며, `machinePositionMm` 자체에는 적용되지 않는다. 따라서 UP/DOWN enum, Servo velocity sign, 증가/감소하는 machine position의 관계를 실제 장비에서 확인해야 한다.

Auto Calibration reference와 deformation에는 `machinePositionMm`을 사용하도록 권고한다. Position Zero가 operator에 의해 달라져도 calibration 결과가 변하지 않으며 공식은 다음과 같다.

```text
deformationMm = averagedMachinePositionMm - referenceMachinePositionMm
```

`testPositionMm`은 start-position return target에만 기존 raw coordinate로 사용 가능하다. Corrected displacement는 어떤 motion 판단에도 사용하지 않는다.

### 2.2 Force convention과 measuredForceN

`measuredForceN`의 실제 원천은 `UtmInputSnapshot::forceN`이다. Force control은 다음 directional force를 계산한다.

```text
directionSign = UP ? +1 : -1
directionalForce = snapshot.forceN * forceDirectionSign * directionSign
error = positiveTargetForceN - directionalForce
```

Public `DaoUtm_MoveToForce`/`DaoUtm_HoldForce`의 `targetForceN`은 양의 magnitude이고 motion direction과 `forceDirectionSign`이 polarity를 결정한다. 반면 저장 curve point는 실제 `snapshot.forceN`과 signed deformation을 그대로 저장해야 한다. Target 판단용 directional magnitude와 저장용 signed canonical force를 혼동하면 안 된다.

### 2.3 MotionController와 output arbiter

`UtmMotionController`는 pending/active motion을 하나만 허용하며 UI, REMOTE, SEQUENCER, INTERNAL source를 인정한다. `UtmMotionOutputArbiter`는 velocity/position output, stop, direction reversal 시 stop-complete 및 한 cycle 간격을 관리한다. Auto Calibration은 이 경로를 우회하거나 Basic Engine에 직접 Servo 명령을 보내면 안 된다.

현재 source enum에는 CALIBRATION이 없다. INTERNAL을 재사용하면 다른 내부 동작과 ownership 구분이 사라지고, UI를 사용하면 UI 명령과 자동 작업을 구별할 수 없다. Additive `UTM_COMMAND_SOURCE_CALIBRATION`과 calibration-active arbitration이 필요하다.

### 2.4 MOVE_TO_FORCE 재사용성

`MOVE_TO_FORCE`는 target 방향 하중이 처음 도달하거나 초과하면 motion을 stop하고 완료한다. max travel, timeout, force validity, Servo/communication readiness와 directional limit은 기존 경로가 처리한다.

재사용 가능한 부분:

- 안전한 velocity 변환과 output arbitration
- directional-force 계산 convention
- target crossing 감지
- max travel/timeout
- limit/Servo/communication interlock
- stop-complete 확인

제한:

- tolerance band에서 안정화하지 않는다.
- 단일 crossing 후 정지하므로 settling 후 force가 tolerance 밖으로 빠질 수 있다.
- calibration별 70/90% 속도 정책은 각 호출 속도로 일부 표현 가능하지만 target 인근 세밀한 correction은 없다.
- force rise rate, jump, wrong polarity/position direction 검사가 없다.

따라서 pre-check와 각 target의 coarse approach에는 재사용할 수 있으나 point stabilization/capture 전체를 맡길 수 없다.

### 2.5 HOLD_FORCE 재사용성

`HOLD_FORCE`는 tolerance 내부에서 velocity를 0으로 하고 hold timer를 누적하며, 벗어나면 approach/medium/fine/reverse speed로 보정한다. direction reversal도 output arbiter를 통해 stop 후 안전하게 수행한다.

재사용 가능한 부분:

- tolerance 유지
- error band별 감속
- overshoot 시 low-speed reverse correction
- hold time과 max travel/timeout

제한:

- public `SubmitMotion()`은 HOLD 속도·가감속도를 전역 `UtmForceControlConfig`로 덮어쓴다.
- 전역 force config는 control loop 실행 중 변경할 수 없다.
- calibration 요청의 Approach/Calibration/Fine/Return 속도와 독립적으로 맞출 수 없다.
- hold 완료 시 평균 force/position을 반환하지 않는다.
- 안정 구간 중 force jump/rise derivative 검사가 없다.

권고: 기존 force-control 로직을 공유하는 engine-internal calibration regulator를 사용하되, calibration snapshot의 요청별 speed bands/tolerance를 받도록 한다. 외부에서 `HOLD_FORCE`를 반복 호출해 흉내 내는 방식은 권고하지 않는다.

### 2.6 StopMotion, Safety와 StopLatch

`DaoUtm_StopMotion`은 `UtmMotionController::RequestStop()`을 통해 controlled stop을 요청한다. Safety는 매 2 ms cycle에서 E-STOP, Servo Fault, External STOP, overload, communication, Servo readiness, 방향성 limit, user stop을 평가한다. 새 stop이 latch되면 command epoch 증가, queued motion 무효화, jog clear, active motion stop, running sequence abort가 수행된다.

StopLatch는 원인이 제거되고 E-STOP/External STOP/Servo Fault/communication이 정상이어야 acknowledge할 수 있다. Calibration fault 후 자동 acknowledge나 자동 restart를 해서는 안 된다.

### 2.7 Sequencer

Sequencer는 pending → validate → commit → immutable active definition을 제공하고 motion/zero/stop action을 control thread에서 실행한다. recording session hook도 가진다. 그러나 calibration은 각 target의 동일-cycle 평균, pre-check 승인 barrier, pending curve commit이라는 전용 의미가 있으므로 일반 recipe step들의 조합으로 구현하지 않는 편이 안전하다.

재사용할 것은 action ownership, start/stop/fault pattern과 immutable snapshot 개념이다. Sequencer와 Auto Calibration은 동시에 실행할 수 없도록 상호 배타적으로 중재한다.

### 2.8 Compliance, Profile, UI, Protocol

`UtmComplianceCompensation`은 최대 64개의 signed `(forceN, deformationMm)`을 configuration 때 정렬·검증하고 runtime에서 piecewise-linear interpolation과 endpoint clamp를 수행한다. 이 lookup은 그대로 재사용한다.

Profile은 `QSaveFile` atomic persistence를 제공한다. 기존 single curve를 유지하면서 새 directional curve를 독립 필드로 추가할 수 있다. UI는 40 ms precise timer로 RuntimeV6를 polling한다. 이 timer는 표시와 operator action에는 적합하지만 safety-critical calibration orchestration에는 적합하지 않다.

Protocol V1은 현재 live data와 command callback 기반이다. 향후 calibration progress/event/command를 additive message로 확장할 수 있으나 V1 LIVE_DATA layout은 바꾸지 않는 것이 좋다.

## 3. 권고 Architecture

### A. Application/UI Service 상태기계

장점:

- Engine ABI 변경 없이 빠르게 구성 가능
- UI 개발과 offline simulation이 쉬움
- profile/save workflow에 직접 접근 가능

단점:

- 현재 40 ms polling 간격만큼 wrong polarity/jump/max travel 판단이 늦다.
- UI hang/close/disconnect가 orchestration lifetime을 깨뜨린다.
- force/position snapshot이 서로 다른 호출 시점일 수 있다.
- command source ownership과 타 UI/REMOTE command 배제가 불완전하다.
- 향후 Windows client가 UI와 다른 동작 lifetime을 만들 수 있다.

판정: 실제 hardware motion 소유 계층으로 부적합.

### B. 전부 UTM Engine 내부

장점:

- 2 ms safety/interlock/capture
- 원자적인 input snapshot 사용
- motion arbitration과 stop latch 직접 통합
- UI/Windows 연결과 무관하게 안전 상태 유지
- deterministic offline simulation 가능

단점:

- profile JSON/QSaveFile과 UI workflow까지 Engine에 넣으면 계층이 오염된다.
- curve 승인/저장 같은 operator policy가 control engine에 들어간다.
- additive ABI가 필요하다.

판정: runtime에는 적합하지만 persistence/UI까지 포함하는 순수 B는 부적합.

### C. 혼합 구조 — 추천

UTM Engine이 다음을 소유한다.

- immutable calibration request snapshot
- calibration-exclusive command source
- 2 ms state machine
- pre-check/full-run motion
- 모든 safety/fault/abort 처리
- force/position 동시 sample accumulation
- pending result 고정 배열
- release/return sequencing
- progress runtime publication

Application/UI Service가 다음을 소유한다.

- 설정 편집과 validation feedback
- 위험 확인 dialog
- START PRE-CHECK와 START FULL CALIBRATION의 별도 명시적 명령
- progress 표시
- pending curve VIEW/DISCARD/SAVE/SAVE & ENABLE
- MachineProfile와 QSaveFile
- 기존 manual editor

Protocol Service는 UI와 동일한 additive UTM public API를 호출하고 runtime/event만 중계한다. Windows 연결 손실은 engine calibration을 자동 restart시키지 않으며, 정책은 보수적으로 “remote-started calibration이면 connection loss 시 controlled abort, auto return 없음”을 권고한다. Local HMI 시작 calibration은 Windows monitoring loss와 무관하다.

## 4. Calibration Configuration

필수 사용자 노출 항목:

- Mode: Compression/Tension
- Load Cell Capacity: profile read-only
- Maximum Calibration Force [N]
- Force Step [N]
- Approach Speed [mm/min]
- Calibration Speed [mm/min]
- Fine Speed [mm/min]
- Return Speed [mm/min]
- Maximum Calibration Travel [mm]
- Stabilization Time [ms]
- Force Tolerance [N]
- Pre-check Force [N]

추가하되 Advanced 또는 내부 기본값으로 둘 항목:

- Pre-check Maximum Travel [mm] — 접촉 실패 보호, REQUIRED
- Motion/point timeout [ms] — 무한 이동 방지, REQUIRED
- Release Force Threshold [N] — zero noise보다 큰 안전 threshold, REQUIRED
- Release Maximum Travel [mm] — 반대방향 무한 release 방지, REQUIRED
- Minimum Force Rise [N] per monitoring window — REQUIRED
- Force Rise Travel Threshold [mm] — REQUIRED
- Maximum Force Jump [N/cycle 또는 N/window] — REQUIRED
- Overshoot Guard [N] — `min(allowedMaximum, target + margin)`, REQUIRED
- Minimum stable sample count — stabilization 시간이 같아도 유효 sample 부족 방지, REQUIRED

UI 복잡도를 줄이기 위해 마지막 7개는 load-cell capacity와 sample period에서 계산한 engineer defaults로 제공하고 Advanced에서만 표시한다.

Validation upper bound:

```text
allowedMaximumForceN = min(
    0.90 * loadcellCapacityN,
    configured overload limit,
    optional manufacturer calibration limit)
```

overload protection이 disabled여도 calibration은 자체 hard upper guard를 반드시 사용한다. 권장값 90%는 절대 보증값이 아니며 fixture/센서 제조사 허용치가 더 낮으면 그것을 우선한다.

## 5. Compression/Tension Curve 구조와 Backward Compatibility

권고 profile 구조:

```json
"machineCompliance": {
  "schemaVersion": 2,
  "compression": {
    "enabled": false,
    "version": 3,
    "points": [{"forceN": 0.0, "deformationMm": 0.0}]
  },
  "tension": {
    "enabled": false,
    "version": 7,
    "points": [{"forceN": 0.0, "deformationMm": 0.0}]
  },
  "autoCalibrationDefaults": {}
}
```

각 curve는 enabled/configured/version/point count/range를 독립 계산한다. `configured`와 force range는 points에서 유도하므로 JSON 중복 저장은 불필요하다. Runtime lookup은 measured signed force의 sign 또는 명시적인 analysis mode snapshot으로 curve를 선택한다. 시험 중 선택 규칙이 바뀌지 않도록 test start 때 active compliance selection/version을 snapshot해야 한다.

Backward compatibility:

- 기존 `complianceCompensation` single curve는 그대로 읽고 기존 기능에서만 유지한다.
- 새 compression/tension fields가 없으면 둘 다 disabled/empty/version 0이다.
- legacy single curve를 어느 방향에도 복제하거나 자동 enable하지 않는다.
- legacy curve가 존재하면 UI에 `LEGACY SINGLE CURVE — migration required`를 표시한다.
- 사용자가 manual editor에서 명시적으로 한 방향으로 import/save할 때만 새 directional curve가 생성된다.
- profile root schemaVersion을 깨지 않고 optional `machineCompliance.schemaVersion`을 두거나, loader가 additive object를 허용하는 현재 방식으로 확장한다.

Curve 저장은 active curve를 즉시 덮어쓰지 않는다. Engine pending result를 UI가 copy한 후 candidate profile validation과 QSaveFile commit이 성공해야 active application curve를 교체한다. 저장 실패나 discard/abort/fault에서는 기존 curve와 version이 그대로 유지된다.

## 6. Manual Curve Editor 재사용

현재 table, point validation, sorting, `UtmComplianceCompensation::ConfigureCurve`, QSaveFile save를 대부분 재사용할 수 있다. UI는 다음 두 영역으로 분리한다.

- AUTO CALIBRATION: 설정, pre-check, 진행, pending result 승인
- ADVANCED / MANUAL CURVE EDIT: mode 선택 후 해당 방향 curve 편집

Manual editor가 legacy/Compression/Tension 중 무엇을 편집하는지 명시해야 한다. 자동 결과는 manual table에 바로 commit하지 않고 read-only pending preview로 먼저 표시한다. SAVE 또는 SAVE & ENABLE 후에만 선택 방향의 editor/active curve를 갱신한다.

## 7. 제안 State Machine

| State | 진입 조건 | 수행 동작 | 완료 조건/다음 State | Abort/Fault |
|---|---|---|---|---|
| IDLE | calibration 없음 | 기존 curve 유지 | StartPrecheck → VALIDATING | 없음 |
| VALIDATING | 명시적 start 요청 | config/profile/runtime/interlock 검증, immutable request 생성 | 성공 → ZEROING_FORCE | 실패 → REJECTED/IDLE, motion 없음 |
| ZEROING_FORCE | READY, no motion | 기존 ADC zero action 요청 | stable capture 성공 → CAPTURING_REFERENCE | timeout/failure → FAULTED |
| CAPTURING_REFERENCE | force zero valid, axis 정지 | 동일-cycle force/machine position 여러 sample 평균, zero point 생성 | reference 확정 → PRECHECK_APPROACH | invalid sample → FAULTED |
| PRECHECK_APPROACH | reference 확보 | Fine Speed로 precheck target 접근; polarity/rise/jump/travel 매 cycle 감시 | target band 진입 → PRECHECK_STABILIZING | interlock 위반 → FAULTED |
| PRECHECK_STABILIZING | precheck target band | tolerance 연속 유지 및 평균; 필요 시 제한된 fine correction | 안정 성공 → STOPPING_PRECHECK | instability timeout → FAULTED |
| STOPPING_PRECHECK | pre-check 완료 | controlled stop, stop-complete 확인 | → PRECHECK_PASSED | stop failure → FAULTED |
| PRECHECK_PASSED | stop 완료 | `PRE-CHECK PASSED` publish | 내부적으로 WAITING_FULL_START | safety 발생 → FAULTED |
| WAITING_FULL_START | 사용자 승인 대기 | motion 금지, precheck 하중은 유지 또는 안전 hold 정책 적용 | ConfirmFull → PREPARING_FIRST_TARGET | Abort → ABORTED; safety → FAULTED |
| PREPARING_FIRST_TARGET | full 승인 | precheck 안정성 재확인, target list snapshot | → APPROACHING_TARGET | 실패 → FAULTED |
| APPROACHING_TARGET | 다음 target 존재 | target ratio에 맞는 approach/calibration speed | fine band → FINE_APPROACH | safety/rise/jump/travel/timeout → FAULTED |
| FINE_APPROACH | target 근접 | fine regulator, overshoot guard | tolerance 진입 → STABILIZING | 동일 |
| STABILIZING | tolerance 진입 | 연속 안정 시간과 최소 sample 수 충족, force/position 합산 | 성공 → CAPTURING_POINT | band 이탈 시 timer/accumulator reset; timeout → FAULTED |
| CAPTURING_POINT | 안정 완료 | 평균 force와 평균 machine position으로 signed point 작성 | → NEXT_POINT | non-finite/overflow → FAULTED |
| NEXT_POINT | point 저장 | 다음 target 결정 | 있으면 APPROACHING_TARGET, 없으면 RELEASING_FORCE | 없음 |
| RELEASING_FORCE | final point 확보 | 반대 방향 Fine/Release Speed, release travel/timeout/반대하중 감시 | release threshold → STOPPING_RELEASE | safety/threshold 초과 → FAULTED |
| STOPPING_RELEASE | 저하중 도달 | controlled stop 확인 | → RETURNING | fault → FAULTED |
| RETURNING | release 성공, latch 없음 | raw start test position으로 absolute return | position completion → COMPLETE_PENDING_SAVE | fault/abort → FAULTED/ABORTED, 재시도 없음 |
| COMPLETE_PENDING_SAVE | 정상 return 완료 | pending curve 고정, 기존 active 유지 | Save/Discard → IDLE | 저장 실패 시 이 상태 유지 |
| ABORTING | user abort | controlled stop 요청 | stop complete → ABORTED | safety 발생 → FAULTED |
| ABORTED | stop 완료 | pending 폐기, 기존 curve 유지 | 명시적 reset 후 IDLE | Auto return 금지 |
| FAULTED | safety/motion/data fault | safety action/stop latch 존중, pending 폐기 | operator 확인·ack 후 별도 reset | Auto restart/return 금지 |

`WAITING_CONFIRMATION`은 UI dialog 단계이므로 Engine에서는 motion이 전혀 시작되지 않은 VALIDATING 전 단계로 둔다. Pre-check 이후에는 명시적인 `ConfirmFullCalibration(sessionId)`만 다음 상태로 전이시킨다.

주의: PRECHECK_PASSED에서 하중을 유지한 채 무기한 사용자 입력을 기다리는 것도 위험하다. 권고 정책은 짧은 confirmation timeout을 두고, timeout 시 controlled release 후 “pre-check expired”로 종료하되 start position 자동 return 여부는 정상 precheck 종료 정책으로만 허용하는 것이다. 사용자가 요구한 “자동으로 full calibration 계속하지 않음”은 유지된다.

## 8. Safety Interlock

| Condition | Detection Source | Action | Calibration State | Auto Return Allowed? | Existing Curve Preserved? |
|---|---|---|---|---|---|
| E-STOP | `snapshot.emergency`, SafetyMonitor | Disable motion, latch, pending 폐기 | FAULTED | No | Yes |
| External STOP | `snapshot.externalStop`, SafetyMonitor | Controlled stop, latch | FAULTED | No | Yes |
| Servo Fault | `snapshot.servoFault`, SafetyMonitor | Disable motion, latch | FAULTED | No | Yes |
| Upper Limit | input + requested direction | 해당 방향 controlled stop/latch | FAULTED | No | Yes |
| Lower Limit | input + requested direction | 해당 방향 controlled stop/latch | FAULTED | No | Yes |
| Communication Recovering | InputCollector communication state | Disable motion immediately, latch | FAULTED | No | Yes |
| Communication Fault | communicationValid/SafetyMonitor | Disable motion, latch | FAULTED | No | Yes |
| Overload | existing `maxAllowedForceN` | Existing controlled stop + latch | FAULTED | No | Yes |
| Calibration Max Force | calibration hard guard | Controlled stop before overload, session fault | FAULTED | No | Yes |
| Calibration Max Travel | machinePosition-reference | Controlled stop, session fault | FAULTED | No | Yes |
| Wrong Force Polarity | directional force delta | Controlled stop immediately | FAULTED | No | Yes |
| Wrong Position Direction | signed machine-position delta | Controlled stop immediately | FAULTED | No | Yes |
| Insufficient Force Rise | force delta vs travel/time | Controlled stop, contact failure | FAULTED | No | Yes |
| Excessive Force Jump | per-cycle/window force derivative | Controlled stop; severe threshold may disable | FAULTED | No | Yes |
| Force invalid/stale | forceValid/source freshness | Disable/controlled stop per safety classification | FAULTED | No | Yes |
| User Abort | additive calibration abort API | Controlled stop, pending 폐기 | ABORTED | No | Yes |

Calibration upper guard는 기존 overload보다 낮게 설정되어야 한다. E-STOP/Servo/communication처럼 output 신뢰성이 없는 fault에서는 release나 return을 시도하지 않는다.

## 9. 100 N Compression Calibration 실제 시간 순서

조건:

- Load Cell Capacity: 100 N
- Maximum Calibration Force: 90 N
- Force Step: 10 N
- Pre-check Force: 5 N
- Compression direction과 expected polarity는 설치 검증값 사용

1. 사용자가 rigid reference fixture, jig 체결, load-cell rating, 이동 영역을 확인한다.
2. `START COMPRESSION CALIBRATION`을 누른다. 아직 Motor는 움직이지 않는다.
3. UI가 capacity 100 N, maximum 90 N, travel, speed, fixture 경고를 표시한다.
4. 사용자가 `START PRE-CHECK`를 명시적으로 누른다.
5. Engine이 machine READY, Servo ON/ready, required slaves operational, communication stable, force valid/calibrated, stop latch clear, E-STOP/External STOP/limit clear, 다른 motion/jog/sequence/calibration 없음 등을 한 snapshot에서 검증한다.
6. `min(90% capacity, overload limit, manufacturer limit)`가 90 N 이상인지 확인한다. 아니면 motion 없이 reject한다.
7. Engine은 target `[0,10,20,...,90]`과 설정을 immutable session snapshot으로 만든다.
8. 기존 force-zero action을 실행한다. ADC stable capture의 active→complete와 timeout을 확인한다.
9. Axis가 정지한 상태에서 유효한 force/machine-position sample을 짧게 평균해 reference position을 확정한다. 기존 Position Zero는 변경하지 않는다.
10. `(capturedForceN≈0, deformationMm=0)` pending point를 만든다.
11. Engine이 Compression 방향으로 Fine Speed의 5 N pre-check motion을 시작한다.
12. 매 2 ms cycle마다 force polarity, machine position 방향, force rise, force jump, pre-check travel, max force, 일반 Safety를 감시한다.
13. contact가 없어서 pre-check travel만 소비되거나 force가 증가하지 않으면 즉시 stop하고 FAULTED로 종료한다.
14. 5 N tolerance band에 들어오면 fine correction/hold를 수행하고 stabilization 시간 동안 연속 유효 sample을 평균한다.
15. Pre-check가 성공하면 velocity를 0으로 하고 Servo stop-complete를 확인한다.
16. UI에 `PRE-CHECK PASSED`를 표시한다. Engine은 full calibration을 자동 시작하지 않는다.
17. 사용자가 fixture, 실제 force sign과 이동 방향을 확인한다.
18. 사용자가 `START FULL CALIBRATION`을 누른다. session ID가 일치하고 safety가 여전히 정상일 때만 계속한다.
19. 첫 정식 target 10 N으로 접근한다. 10 N은 전체 maximum의 70% 미만이므로 configured Approach Speed를 사용할 수 있지만 실제 fine band에서는 Fine Speed로 전환한다.
20. 10 N ± tolerance에 안정적으로 머무는 동안 force와 machine position을 같은 2 ms snapshot에서 누적한다.
21. stabilization과 최소 sample 수를 만족하면 평균 force와 평균 position을 확정한다.
22. deformation=`average position-reference position`으로 signed 10 N point를 pending buffer에 저장한다.
23. 20, 30, 40, 50, 60 N에서 19~22 과정을 반복한다.
24. 70 N 부근부터 Calibration Speed 구간으로 전환한다. 경계는 target ratio뿐 아니라 instantaneous error band도 함께 사용한다.
25. 70 N과 80 N point를 각각 안정화·평균·capture한다.
26. 90% 구간인 90 N target은 Fine Speed로 접근한다. calibration hard upper guard와 overload guard는 계속 독립 감시한다.
27. 90 N point를 안정화하고 평균 capture한다. single instantaneous sample은 저장하지 않는다.
28. final point 후 곧바로 start position으로 복귀하지 않는다.
29. Engine은 Compression 반대 방향으로 Fine/Release Speed를 명령해 force를 감소시킨다.
30. release 동안에도 E-STOP, limit, communication, Servo fault, release travel/time, 반대 sign 하중을 감시한다.
31. force가 release threshold 이하로 안정되면 controlled stop을 요청하고 stop-complete를 확인한다.
32. latch/fault가 없고 release가 정상 완료된 경우에만 기존 raw `testPositionMm`의 calibration start target으로 `MOVE_ABSOLUTE` return을 수행한다.
33. absolute motion completion을 기존 Servo/runtime 경로로 확인한다.
34. Engine은 `COMPLETE_PENDING_SAVE`를 publish한다. 기존 active compression curve는 아직 바뀌지 않는다.
35. UI는 existing version, pending point count/range/max deformation을 보여준다.
36. 사용자가 DISCARD를 누르면 pending만 폐기한다.
37. SAVE CURVE를 누르면 compression curve candidate를 validation하고 QSaveFile로 저장하되 disabled 상태로 둔다.
38. SAVE & ENABLE을 누르면 같은 atomic save 성공 후 compression curve만 새 version으로 활성화한다. Tension 및 legacy curve는 변경하지 않는다.

어느 단계에서든 fault 또는 user abort가 발생하면 stop 후 자동 release/return을 수행하지 않고 기존 curve를 유지한다.

## 10. Public ABI 설계

### Basic Engine

변경 필요 없음. Servo PDO, ADC DSP/calibration, encoder, EtherCAT recovery는 기존 API로 충분하다. Auto Calibration이 Basic Engine을 직접 호출하면 계층과 safety ownership을 우회하므로 금지한다.

### UTM Engine

안전한 권고 구조에는 additive API가 필요하다. 기존 ABI를 변경하지 않고 새 POD types와 새 exported functions만 추가한다.

제안 함수:

```text
DaoUtm_StartCompliancePrecheck(const UtmComplianceAutoConfigV1*, uint64_t* sessionId)
DaoUtm_ConfirmComplianceFullCalibration(uint64_t sessionId)
DaoUtm_AbortComplianceCalibration(uint64_t sessionId)
DaoUtm_GetComplianceCalibrationRuntimeV1(UtmComplianceAutoRuntimeV1*)
DaoUtm_GetCompliancePendingPoints(uint64_t sessionId, ..., capacity, *count)
DaoUtm_DiscardCompliancePending(uint64_t sessionId)
```

SAVE/ENABLE은 profile 소유 Application 계층에 남긴다. Engine pending points read는 completed session에서만 허용하며 fixed capacity와 count negotiation을 사용한다. 기존 `UtmRuntimeInfoV6`를 수정하지 않고 `V7` additive wrapper를 만들거나 별도 runtime getter를 사용한다. 별도 getter가 기존 nesting 증가를 피하므로 우선 추천한다.

새 command source `UTM_COMMAND_SOURCE_CALIBRATION` 추가는 enum 값 append로 가능하지만 public POD enum 해석에 영향을 문서화해야 한다. 기존 함수 signature와 struct layout은 변경하지 않는다.

## 11. Command Arbitration과 Lifetime

- Calibration active/pending command 중 UI, REMOTE, Sequencer, Jog의 새 motion start를 reject한다.
- Stop/E-STOP는 source와 무관하게 항상 수락한다.
- Calibration state machine만 CALIBRATION source motion을 제출한다.
- Start 시 command epoch/session ID를 발급한다.
- stale confirm/abort/save 요청은 session ID mismatch로 reject한다.
- UI process/window close는 UTM Engine이 같은 process의 shared library이므로 application shutdown 전에 explicit abort와 stop-complete를 기다리는 shutdown contract가 필요하다.
- 별도 Linux service process로 engine ownership을 이동하는 장기 구조에서는 UI disconnect에도 calibration safety가 유지된다.
- Windows remote start는 local enable/key switch 또는 현장 HMI confirmation 없이는 허용하지 않는 것을 권고한다.

## 12. 위험하거나 모순되는 요구와 대안

### 12.1 UI 계층에서 기존 API만 연쇄 호출

Requested: 기존 `DaoUtm_*` 호출만으로 Auto Calibration 수행

Problem: UI 40 ms polling과 process lifetime이 safety-critical state owner가 됨

Reason: 2 ms snapshot 원자성, deterministic abort, exclusive arbitration을 보장하지 못함

Recommended Alternative: Engine-owned state machine과 additive UTM API

### 12.2 90% capacity를 보편적 안전 한계로 사용

Requested: 기본 권장 상한 약 90%

Problem: 90%가 모든 load cell/fixture의 안전 operating limit이라는 보장은 없음

Reason: overload setpoint, manufacturer calibration rating, fixture rating, dynamic overshoot가 다름

Recommended Alternative: 90%는 maximum candidate로만 사용하고 더 낮은 모든 한계를 우선

### 12.3 Pre-check passed 상태에서 하중 무기한 유지

Requested: 사용자 full-start 입력까지 자동 진행하지 않음

Problem: 하중을 유지한 채 operator를 무기한 기다리면 creep/thermal drift와 fixture 위험 증가

Reason: 현재 HOLD는 timeout이 있고 indefinite safe hold가 아님

Recommended Alternative: 짧은 confirmation timeout; 만료 시 저속 release 후 session 종료. Full calibration은 자동 시작하지 않음

### 12.4 Fault/Abort 시 즉시 Motion Stop

Requested: 모든 fault에서 즉시 Motion Stop

Problem: “즉시”의 stop 방식은 fault별로 다르며 controlled stop은 물리적으로 시간이 필요함

Reason: E-STOP/communication/Servo fault는 disable, External STOP/limit/overload는 기존 controlled stop

Recommended Alternative: 기존 Safety action severity를 그대로 사용하고 calibration은 즉시 새 출력 생성을 중단

### 12.5 Final force에서 바로 MOVE_ABSOLUTE return

Requested: release 후 return

Problem: 단순 반대 velocity release는 fixture 이탈 후에도 계속 움직이거나 반대방향 하중을 만들 수 있음

Reason: force threshold noise와 backlash/contact separation

Recommended Alternative: release force threshold + stable window + release max travel + timeout + opposite-force guard 후 stop-complete, 그 다음 absolute return

### 12.6 기존 HOLD_FORCE를 그대로 calibration hold로 사용

Requested: 기존 기능 최대 재사용

Problem: public HOLD는 global force config 속도를 사용하고 평균 point를 반환하지 않음

Reason: 요청별 calibration speed/tolerance와 atomic capture를 표현하지 못함

Recommended Alternative: force-control kernel과 output arbiter를 engine 내부에서 재사용하고 calibration state/capture를 별도로 둠

### 12.7 현재 미커밋 Auto Calibration 초안을 hardware에서 실행

Requested: 현재 상황을 기반으로 향후 구현

Problem: 초안은 UI timer가 state machine을 service하고 UI source motion을 호출함

Reason: production engine ownership, 2 ms guards, command-source exclusivity가 없음

Recommended Alternative: 초안을 test model/reference로만 사용하고 Engine integration 설계 승인 후 hardware-disabled 구현

## 13. 예상 변경 범위

| 영역/파일 | 분류 | 예상 변경 |
|---|---|---|
| Basic Engine (`engine/*`, `master/*`) | NOT REQUIRED | 변경 없음 |
| `utm/include/DaoUtm.Types.h` | REQUIRED | additive config/runtime/session/source types |
| `utm/include/DaoUtm.Engine.h` | REQUIRED | additive start/confirm/abort/runtime/pending API |
| `utm/src/DaoUtm.Engine.cpp` | REQUIRED | C ABI forwarding |
| `utm/src/UtmEngineCore.*` | REQUIRED | 2 ms state-machine integration, arbitration, publication |
| 신규 `utm/src/UtmComplianceCalibrationController.*` | REQUIRED | fixed-memory state machine/capture/guards |
| `UtmMotionController.*` | OPTIONAL | calibration ownership helper; 기존 algorithm 변경 금지 |
| `UtmSafetyMonitor.*` | OPTIONAL | 새 global stop reason 없이 session-local reason 사용 가능; hard calibration guard 통합 시 additive |
| `UtmRuntimeStore.*` | OPTIONAL | 별도 runtime store 또는 additive field publication |
| `UtmSequencer.*` | NOT REQUIRED | 동시 실행 reject만 EngineCore에서 수행 |
| `UtmCoordinateController.*` | NOT REQUIRED | raw machine/test 좌표 API 재사용 |
| `UtmComplianceCompensation.*` | REQUIRED | directional selection wrapper 또는 두 instance 관리; interpolation 변경 없음 |
| `MachineProfile.*` | REQUIRED | independent compression/tension/default config, legacy 보존 |
| `UtmUiController.*` | REQUIRED | 새 UTM API adapter와 pending save; motion orchestration 제거 |
| `MainWindow.*` | REQUIRED | Auto/Manual 분리, two-stage confirmation, progress/result UI |
| Protocol codec/server | OPTIONAL | remote monitoring/events; remote start policy 확정 후 additive message |
| CMake/tests | REQUIRED | state machine, profile, contamination, fault-injection tests |
| 문서 | REQUIRED | operator workflow, polarity commissioning, protocol/API guide |

## 14. Test Plan

### 14.1 Hardware 없이 가능한 시험

- Target generation: divisible/non-divisible maximum, 0과 final 포함, 64-point 제한
- Maximum force validation: NaN/zero/negative/step>max reject
- 90% capacity: 90 N accept, 초과 reject
- Overload precedence: profile overload가 90%보다 낮으면 낮은 값 적용
- Manufacturer limit precedence
- Pre-check pass와 full-start confirmation barrier
- Wrong force polarity 즉시 fault/stop command
- Wrong position direction
- Insufficient force rise by travel/time
- Excessive force jump
- Pre-check max travel와 full max travel
- Existing overload, E-STOP, External STOP, limit, Servo fault injection
- Communication RECOVERING와 FAULT injection
- Force invalid/stale sample
- User abort in every moving/stabilizing state
- Abort/fault 후 auto return command가 없는지 검증
- Stabilization band 이탈 시 timer와 accumulator reset
- Minimum valid sample count
- 동일 input snapshot의 averaged force/position point capture
- Compression signed curve
- Tension signed curve
- Direction별 independent enable/version/range
- Existing curve preservation on reject/abort/fault/discard/save failure
- Save와 version increment only after QSaveFile commit
- Legacy single-curve load; directional curves disabled/empty
- Start/confirm session ID replay reject
- Calibration 중 UI/REMOTE/Sequencer/Jog motion reject, Stop 허용
- Corrected displacement가 calibration/motion/limit/return target에 들어가지 않는 contamination test
- Extensometer unchanged
- 2 ms fixed-loop simulation timing과 no-allocation instrumentation
- Release threshold/stable window/max travel/timeout/opposite-force guard
- Protocol progress encode/decode와 reconnect status resync(확장 시)

### 14.2 실제 Hardware에서만 가능한 시험

- UP/DOWN, servoDirectionSign, machinePosition 증가 방향 commissioning
- Compression/Tension raw force polarity와 forceDirectionSign 검증
- 실제 5 N pre-check force rise와 contact travel threshold 튜닝
- Force jump threshold와 ADC noise/필터 지연 측정
- MOVE/HOLD overshoot 및 speed-band tuning
- 90%가 아닌 제조사 허용 maximum 확인
- Rigid fixture 강성, 정렬, slip, backlash, hysteresis
- Stabilization time과 creep/thermal drift
- Release 시 contact separation과 반대하중 방지
- Limit 근처 release/return behavior
- E-STOP/External STOP/Servo fault/통신 단절 실동작
- Dynamic overshoot가 overload/calibration guard보다 낮은지 확인
- 반복성, 방향별 재현성, warm-up 영향
- UI/Windows disconnect와 Linux service lifetime
- EtherCAT 2 ms jitter 및 cycle overrun

Hardware 시험은 먼저 저용량 dummy fixture 또는 제조사가 승인한 보호 fixture, 매우 낮은 force limit와 낮은 speed로 시작해야 한다.

## 15. 구현 순서 권고

1. Polarity commissioning 절차와 manufacturer limits를 문서로 확정한다.
2. Hardware-disabled Engine state machine과 additive ABI를 구현한다.
3. Deterministic simulator/fault-injection test를 통과시킨다.
4. UI orchestration 초안을 Engine API adapter로 교체한다.
5. Directional profile migration/save tests를 완료한다.
6. Protocol은 monitoring부터 추가하고 remote start는 local authorization 정책 뒤에 연다.
7. 실제 hardware에서 low-force pre-check만 검증한다.
8. 승인된 단계별 force envelope로 maximum을 점진적으로 높인다.

## 16. 핵심 질문 답변

1. **현재 구조에서 안전하게 구현 가능한가?**  가능하다. 단 UI timer가 아니라 UTM Engine 2 ms state machine이 motion과 capture를 소유해야 한다.

2. **가장 적합한 구현 계층은?**  C 혼합 구조다. Engine은 safety-critical runtime, Application은 UI/profile/save를 소유한다.

3. **MOVE_TO_FORCE/HOLD_FORCE 재사용 범위는?**  MotionController, output arbiter, directional force convention, speed-band regulator, max travel/timeout, stop-complete는 재사용한다. Public 함수만 조합하는 것은 부족하며 calibration별 regulator/capture가 필요하다.

4. **별도 Calibration State Machine이 필요한가?**  필요하다. Pre-check 승인 barrier, 평균 capture, release/return, pending-save lifecycle은 일반 motion이나 Sequencer만으로 안전하게 표현되지 않는다.

5. **Compression/Tension 분리 구조는?**  Profile과 runtime에 독립된 두 curve object를 두고 legacy single curve는 별도로 보존한다. 자동 복제/활성화는 하지 않는다.

6. **추가 Safety가 필요한가?**  필요하다. Calibration hard upper force, pre-check/release travel, point/release timeout, force-rise, force-jump, overshoot, opposite-force, minimum stable samples, exclusive source/session guards를 추가해야 한다.

7. **가장 안전한 release/return 순서는?**  Final capture → 저속 반대방향 force release → release threshold 안정 확인 → controlled stop 완료 → latch/fault 재확인 → raw start position absolute return이다. Fault/abort에서는 return하지 않는다.

8. **Basic Engine 변경이 필요한가?**  필요 없다.

9. **UTM Engine API 추가가 필요한가?**  권고 설계에서는 필요하다. 기존 ABI를 깨지 않는 additive start/confirm/abort/runtime/pending-result API가 필요하다.

10. **지금 바로 구현 가능한가, hardware polarity 검증이 먼저인가?**  Hardware-disabled 코드와 simulator test는 바로 구현할 수 있다. 실제 Motor를 움직이는 enablement와 force limit 확장은 compression/tension polarity 및 position direction commissioning 후에만 허용해야 한다.

## 17. 최종 판정

설계 승인 후 구현은 가능하지만 현재 UI-driven 미커밋 초안을 실제 장비에 연결해서는 안 된다. 먼저 Engine-owned calibration state machine, additive UTM ABI, exclusive arbitration, hard calibration guards와 deterministic tests를 완성해야 한다. 실제 hardware execution은 별도 commissioning gate를 통과한 뒤 low-force pre-check부터 단계적으로 허용한다.
