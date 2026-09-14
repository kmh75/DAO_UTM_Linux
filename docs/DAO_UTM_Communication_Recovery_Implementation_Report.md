# DAO UTM Communication Recovery Implementation Report

- 상태: 구현 완료 (PHASE A only)
- 기준일: 2026-09-14
- Homing: 미구현
- Hardware: 미시험

## 1. Baseline 검증 결과

현재 dirty worktree를 보존한 상태에서 먼저 기준선을 검증했다.

- Debug configure/build: PASS
- 최초 sandbox CTest: 7/8 PASS, Protocol V1 localhost connect 실패
- 원인: sandbox의 local socket 제한 (`ss` netlink도 operation not permitted)
- host 권한 재검증: CTest 8/8 PASS

따라서 Auto Calibration/Compliance 변경은 현재 제공된 deterministic tests 기준 정상 기준선으로 확인한 후 Recovery 구현을 시작했다.

## 2. 기존 dirty changes 분류

구현 전 변경은 다음 범주였다.

- Compliance compensation library/profile/UI
- Compliance Auto Calibration controller/API/runtime/UI/tests
- Protocol V1 library/server/mock integration test
- Force unit/setup persistence/production UI test 보강
- 관련 CMake targets 및 문서
- 생성된 `build`, `build-release` 산출물

이 변경을 reset/revert하지 않았다. 본 Recovery 변경은 같은 `UtmEngineCore`, public headers, UI에 일부 겹치므로 working tree만으로 Commit A/B를 자동 분리하는 것은 위험하다.

## 3. 실제 Recovery 변경 파일

- Basic API/core/master: `engine/include/DaoEtherCAT.Engine.h`, `engine/include/DaoEngineCore.h`, `engine/src/DaoEngineCore.cpp`, `engine/src/DaoEtherCAT.Engine.cpp`, `master/include/DaoEtherCATMaster.h`, `master/src/DaoEtherCATMaster.cpp`
- UTM policy/core: `utm/src/UtmMotionOwnershipPolicy.h`, `utm/src/UtmInputCollector.*`, `utm/src/UtmSafetyMonitor.cpp`, `utm/src/UtmEngineCore.*`, `utm/src/UtmMotionOutputArbiter.*`, `utm/src/UtmCommandMailbox.*`, `utm/src/UtmSequencer.*`, `utm/src/UtmRuntimeStore.*`
- Additive UTM API: `utm/include/DaoUtm.Types.h`, `utm/include/DaoUtm.Engine.h`, `utm/src/DaoUtm.Engine.cpp`
- Auto Calibration interruption: `utm/src/UtmComplianceCalibrationController.cpp`
- UI diagnostics: `ui/include/UtmUiController.h`, `ui/src/UtmUiController.cpp`, `ui/src/MainWindow.cpp`
- Tests: `app/communication_policy_test/main.cpp`, `app/compliance_auto_calibration_test/main.cpp`

## 4. Motion Ownership 정의

고정 크기 `UtmMotionOwnershipSnapshot`과 allocation-free pure classifier `UtmMotionOwnershipPolicy::IsActive()`를 추가했다.

다음 owner를 active로 분류한다.

- Jog request/active
- general motion active/stopping
- Sequence running (WAIT_TIME/WAIT_INPUT을 포함해 step 종류 무관)
- Sequence pending/waiting action/start request
- Auto Calibration active/velocity/return/force-stop
- output arbiter position/velocity active
- MotionController pending/active
- command mailbox 및 현재 accepted Start/Jog command

따라서 target velocity가 0이어도 active position command, Sequence ownership 또는 pending Motion이면 active이다.

## 5. Basic recovery runtime API

기존 구조를 변경하지 않고 다음 additive API를 추가했다.

```cpp
int DaoEngine_GetCommunicationRecoveryRuntimeV1(
    DaoCommunicationRecoveryRuntimeV1* runtimeInfo);
```

고정 크기 POD snapshot은 다음을 포함한다.

- state/stage, incident generation, active/success/failure
- attempt, elapsed/max duration
- current/expected/min WKC, consecutive good/bad, maximum bad
- failed Slave index/state/AL status
- total/recovered/failed incidents, last incident monotonic timestamp

Communication thread는 atomic numeric fields만 publish한다. Getter는 allocation, file I/O 또는 communication-thread mutex를 추가하지 않는다.

UTM에는 독립 additive API를 추가했다.

```cpp
int DaoUtm_GetCommunicationRuntimeV1(
    UtmCommunicationRuntimeInfoV1* runtime);
```

RuntimeV1~V6 layout은 변경하지 않았다.

## 6. Recovery 단계 강화

기존 timing을 유지했다.

- bad 5 cycle: recovery 시작
- retry 25 bad cycles, 약 50 ms
- total deadline 300 ms
- good WKC 3 cycle: recovered
- per-Slave SOEM timeout 2,000 μs

단계 publish 및 확인을 다음처럼 명확히 했다.

1. READ_STATE
2. SAFE_OP+ERROR ACK 및 statecheck
3. lost/NONE이면 recover, 그 외 reconfig
4. SAFE_OP statecheck
5. OP request
6. Slave별 OP statecheck
7. process loop expected WKC 확인
8. GOOD stabilization
9. RECOVERED 또는 deadline FAILED

Slave loop는 `1..slaveCount` 일반 순회이며 특정 index/CNT02 분기가 없다. result, state 및 AL status는 numeric diagnostics로 남긴다.

## 7. UTM Communication 정책

UTM InputCollector는 더 이상 별도 bad-cycle Recovery state machine 결과로 실제 recovery를 추측하지 않는다. Basic V1 runtime의 state를 UTM safety state로 mapping하며 UTM freshness와 required input 검사는 유지한다.

Basic incident generation이 바뀌는 순간 ownership snapshot을 고정하고 새 Motion admission을 inhibit한다. command epoch 증가, queued Motion invalidation 및 Jog request clear를 수행한다.

복구 후 다음이 확인돼야 admission inhibit가 해제된다.

- Basic HEALTHY / recovery inactive
- UTM communication valid
- Servo communication valid
- Servo fault/STO 없음
- Servo operation state를 읽을 수 있음

## 8. Idle incident 처리

ownership inactive이면 RECOVERING 자체를 StopReason으로 만들지 않는다.

- queued stale Motion 폐기 및 신규 Motion 일시 거부
- StopLatch 생성 없음
- `forceMotionStop` 없음
- `DaoEngine_ServoStop` 불필요 호출 없음
- Machine READY 유지, ACK 불필요
- 성공 후 health/fresh Servo 조건 확인 뒤 admission 자동 재개
- Engineer diagnostics/count에는 사건 기록

기존 `previousTargetVelocity=0` idle 사례는 active position/pending owner도 없으면 이 경로를 사용한다.

## 9. Active incident 처리

ownership active이면 다음을 수행한다.

- `motionInterrupted` latch
- command epoch 증가 및 queued/pending continuation 차단
- Jog clear/inhibit
- 기존 Safety StopLatch를 Communication reason으로 사용
- general Motion abort, Sequence safety abort, Calibration fault
- STOPPED 유지 및 ACK 전 READY 복귀 금지
- 이전 Motion 자동 resume 금지

통신 단절 중 발행된 STOP 성공은 물리 정지 확인으로 취급하지 않는다. Basic recovery가 성공한 뒤 output arbiter의 stop guard를 re-arm하여 fresh STOP alignment를 한 번 더 요청한다. `safeAlignmentResult=1`은 요청, `2`는 arbiter stop-complete 관찰을 뜻하며 실제 물리 정지 safety claim은 아니다.

## 10. Sequence incident 처리

Sequence `sequenceRunning` 자체를 active ownership으로 분류한다. 현재 step이 WAIT_TIME/WAIT_INPUT 같은 non-motion step이어도 Communication incident에서 StopLatch 및 `AbortFromSafety()` 경로로 들어가며 자동 continuation하지 않는다. Sequence ABI와 step enum은 변경하지 않았다.

## 11. Auto Calibration incident 처리

Auto Calibration active 상태는 active ownership이다. `communicationRecovering` 발생 시 `UTM_COMPLIANCE_FAULT_COMMUNICATION_INTERRUPTED`를 사용해 FAULTED로 종료한다. recovery 실패/communication invalid는 기존 `UTM_COMPLIANCE_FAULT_COMMUNICATION`을 사용한다. 정상 workflow와 compliance formula는 변경하지 않았다.

## 12. ServoStop 발행 조건 변화

- 이전: RECOVERING이면 Motion 유무와 관계없이 Safety evaluation → latch → unconditional force-stop
- 현재 idle: RECOVERING만으로 safety request를 만들지 않으므로 ServoStop 미발행
- 현재 active: interruption stop path 유지
- recovery failed: communication invalid이므로 idle이어도 Communication Fault latch
- active recovery 성공: 통신 복구 후 fresh stop alignment 재요청

## 13. User warning/fault 정책

- idle 단발 recovery 성공: Main alarm/STOP/ACK 없음
- active recovery 성공: `MOTION INTERRUPTED`, STOPPED, ACK 필요
- recovery failure: Communication Fault/Fault
- rolling 1 hour recovery incident 3회 이상: `EtherCAT ⚠ UNSTABLE`

Warning constants는 `UtmCommunicationWarningPolicy`에 있으며 UI magic number가 아니다. Warning은 Safety Stop 조건으로 사용하지 않는다. 32-entry fixed timestamp buffer를 사용한다.

## 14. Engineer Diagnostics

UI polling side에서 additive UTM runtime을 읽고 문자열을 formatting한다. Engineer panel에 다음을 표시한다.

- state/stage/attempt
- generation 및 active/interrupted classification
- total/recovered/failed, rolling count/warning
- current/max duration
- WKC current/expected/min, bad/max
- failed Slave/state/AL
- incident command/source
- Sequence running/step, Calibration active
- post-recovery Servo state/alignment result

Control/communication loop에서는 문자열 formatting/file I/O를 추가하지 않았다.

## 15. Tests

`dao_communication_policy_fault_injection`에 다음 deterministic assertions를 추가/변경했다.

- idle recovery: no StopLatch, READY 유지
- active recovery: Communication StopLatch, STOPPED, no auto resume
- target velocity와 무관한 active position classification
- Sequence running/non-motion ownership classification
- Calibration ownership 및 pending Motion classification
- rolling 1 hour 2회 no warning / 3회 warning / window expiry

`dao_compliance_auto_calibration_engine`에는 recovering incident가 FAULTED 및 `COMMUNICATION_INTERRUPTED` reason인지 확인하는 assertion을 추가했다.

Basic SOEM recovery와 실제 ServoStop PDO 전달/Slave index 독립성은 hardware 없이 직접 fault injection할 seam이 없어 unit test에서 물리적으로 실행하지 못했다. Slave loop에 특정 index 상수는 없으며 하드웨어 시험 항목으로 남긴다.

## 16. 최종 Build/CTest

### Debug

- Configure: PASS
- Build `-j2`: PASS
- CTest: **8/8 PASS**

### Release

- Configure: PASS
- Build `-j2`: PASS
- CTest: **8/8 PASS**

Protocol localhost test는 sandbox 제한을 피하기 위해 승인된 host 권한으로 실행했다.

## 17. ABI compatibility

- RuntimeV1~V6 layout/field 변경 없음
- 기존 public function 의미 변경 없음
- `DaoUtm_MoveAbsolute` Test-coordinate 의미 유지
- 기존 exported symbols 확인: `DaoEngine_ServoHome`, `DaoUtm_MoveAbsolute`, `DaoUtm_GetRuntimeV6` 유지
- 새 exported symbols: `DaoEngine_GetCommunicationRecoveryRuntimeV1`, `DaoUtm_GetCommunicationRuntimeV1`
- Homing API/runtime/UI/gating 및 Sequence HOME 미추가

## 18. Git commit 분리 가능 여부

논리적으로 다음 두 커밋을 권고한다.

- Commit A: 기존 Auto Calibration/Compliance/Protocol 기준선
- Commit B: 본 보고서 3절의 Recovery 변경

하지만 두 작업이 `CMakeLists.txt`, `UtmEngineCore`, UTM public headers, UI 및 calibration test에서 겹친다. 현재 working tree에서 자동 staging/commit하면 사용자 변경을 잘못 분리할 위험이 있어 commit 또는 history 조작을 수행하지 않았다. 수동 interactive staging 또는 기준선 Commit A를 먼저 확정한 뒤 Recovery patch를 분리하는 방법이 안전하다. Push는 수행하지 않았다.

## 19. Hardware verification 필요사항

1. idle WKC 12→10→recovery에서 ServoStop 로그와 Main STOP이 실제로 사라지는지
2. Jog/position/force/Sequence WAIT/Calibration 각각에서 interruption, no-resume, ACK 확인
3. recovery 후 fresh STOP alignment가 Drive에 도달하고 실제 정지 상태가 확인되는지
4. SAFE_OP ACK/recover/reconfig/OP statecheck의 Slave별 timing 및 2 ms cycle jitter
5. L7NH EtherCAT watchdog/communication error 동작과 stale setpoint 재실행 여부
6. CNT02/IO 위치 및 cable/noise/ground 조건에서 failed Slave/state/AL diagnostics 정확성
7. 24~48시간 rolling warning과 counter/max duration 검증

## 20. Known limitations

- Hardware/SOEM fault injection은 수행하지 않았다.
- `safeAlignmentResult=2`는 software command completion 관찰이지 물리 정지 인증이 아니다.
- rolling window는 process lifetime의 최근 32 incidents만 보존하며 persistence하지 않는다.
- Basic runtime의 atomic fields는 lock-free numeric snapshot이므로 서로 다른 cycle의 값이 섞일 수 있다. Incident generation을 일관성 기준으로 사용하며 safety decision은 conservative state/input 조건을 함께 본다.
- failed Slave diagnostics는 마지막 비-OP Slave를 나타내며 모든 Slave history는 기존 log/ring에 의존한다.
- Homing, Homed, Absolute Move gating은 의도적으로 구현하지 않았다.
