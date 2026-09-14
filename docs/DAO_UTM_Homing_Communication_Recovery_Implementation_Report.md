# DAO UTM Homing / Communication Recovery Implementation Report

- 상태: **구현 중단 (pre-implementation gate)**
- 날짜: 2026-09-14
- Hardware: 미시험
- 소스 변경: 없음

## 1. 중단 사유

구현 전 작업 트리와 호출 경로를 재검토한 결과, 사용자 지시의 명시적 중단 조건 두 가지가 확인되었다.

### 1.1 Homed gating이 기존 Absolute Move 동작을 광범위하게 변경

현재 다음 경로는 UTM-level Homed 상태 없이 동작한다.

- `DaoUtm_MoveAbsolute()` → `UtmEngineCore::MoveAbsolute()` → `UtmMotionController::Prepare()`
- Absolute target은 기존 의미대로 Test coordinate이며 Position Zero offset을 더해 Servo absolute target으로 변환된다.
- `UtmSequencer`의 `UTM_SEQUENCE_STEP_MOVE_ABSOLUTE`도 같은 경로를 사용한다.
- `app/utm_test/main.cpp`의 수동 Absolute Move 및 sample Sequence도 Machine Home 선행 절차 없이 구성돼 있다.
- UI의 Manual Absolute Move에도 Home 선행 workflow가 없다.

따라서 startup 기본값을 `NOT_HOMED`로 두고 기존 Absolute Move에 즉시 Homed requirement를 적용하면, 기존 사용자/API/Sequence의 모든 Absolute Move가 Homing 전 거부된다. 이는 API의 좌표 의미는 유지하더라도 기존 승인/성공 동작을 광범위하게 바꾸는 변경이다.

사용자 지시의 “Homed gating이 기존 Absolute Move behavior를 광범위하게 깨는 경우 중단” 조건에 해당하므로 임의 compatibility mode를 만들지 않았다.

### 1.2 Sequence/Calibration Motion ownership이 설계 기준보다 복잡하며 작업 중 변경과 겹침

조사 시작 시 작업 트리는 이미 대규모 미커밋 변경을 포함하고 있었다. 특히 다음 사용자 변경이 본 작업의 핵심 파일과 직접 겹친다.

- `utm/src/UtmEngineCore.cpp`: Compliance Auto Calibration Motion, safety/stop 처리 추가
- `utm/include/DaoUtm.Types.h`, `utm/include/DaoUtm.Engine.h`, `utm/src/DaoUtm.Engine.cpp`: additive calibration API/runtime 추가
- `ui/src/MainWindow.cpp`, `ui/src/UtmUiController.cpp` 및 headers: calibration owner/UI 추가
- `CMakeLists.txt`: 새 test/library targets 추가

현재 control loop의 Motion-active 판정에는 general motion, Jog, Sequence뿐 아니라 새 `UtmComplianceCalibrationController`의 velocity request, force-stop request, return absolute action 및 safety fault 전이가 함께 관여한다. Communication incident 분류와 Homing admission/abort를 구현하려면 이 미완료 변경의 ownership semantics를 확정하거나 동시에 수정해야 한다.

이는 “Sequence/Calibration ownership 충돌이 설계보다 복잡한 경우 중단” 조건에 해당하며, 사용자 변경을 덮어쓰거나 임의로 통합하지 않았다.

## 2. 영향

- PHASE A와 PHASE B 모두 소스 구현을 시작하지 않았다.
- Basic recovery runtime API, recovery 단계 강화, UTM interruption policy, UI notification은 미구현이다.
- UTM Homing API/runtime/state machine 및 Homed gating도 미구현이다.
- 기존 RuntimeV1~V6, public ABI, Force/ADC/Calibration/Compliance/Protocol/PDO/SOEM에는 변경이 없다.
- Build/CTest는 새 구현이 없으므로 실행하지 않았다. 기존 dirty worktree의 상태를 본 작업 결과로 평가하지 않았다.

## 3. 진행 대안

### 대안 A — 권고: 현재 Auto Calibration 작업을 기준선으로 먼저 확정

1. 현재 미커밋 Compliance/Auto Calibration 변경을 별도 검토·테스트·커밋하여 안정된 기준선을 만든다.
2. Motion ownership을 하나의 고정 크기 snapshot으로 정의한다: Jog, general position/velocity, force/hold, Sequence motion/pending action, calibration velocity/return motion, output arbiter state, pending mailbox.
3. PHASE A를 이 기준선 위에서 작은 커밋 단위로 구현하고 전체 회귀 테스트한다.
4. Absolute Move gating 정책을 별도 승인한 후 PHASE B를 진행한다.

장점: 사용자 작업 보존과 회귀 원인 분리가 가장 명확하다. 단점: Homing/Recovery 구현 시작이 늦어진다.

### 대안 B — 기능 범위 분할: PHASE A만 먼저 승인

현재 변경을 보존한 채 PHASE A만 구현하되, Auto Calibration을 명시적인 Motion-active owner로 포함하는 통합 규칙을 사용자가 승인한다. PHASE B와 Absolute Move gating은 보류한다.

장점: 현장 UI STOP 문제를 먼저 해결할 수 있다. 단점: 미완료 calibration 변경과 같은 파일을 수정하므로 merge/review 위험이 높고, PHASE A 안정성 판정에 calibration 테스트 완료가 필요하다.

### 대안 C — Homing을 도입하되 기존 Absolute Move gating은 후속 전환

UTM Home API/runtime/UI와 Homed 상태만 additive하게 구현하고, 기존 `DaoUtm_MoveAbsolute()`는 당분간 Homed 여부를 diagnostics/warning으로만 노출한다. 새 Sequence 시작도 기존대로 유지한다. 하드웨어 검증과 migration 공지 후 별도 승인에서 gating을 활성화한다.

장점: 기존 동작 회귀 없이 Homing workflow를 먼저 검증할 수 있다. 단점: 전환 전에는 Machine coordinate가 유효하지 않아도 Absolute Move가 계속 허용되므로 최종 안전 목표를 즉시 달성하지 못한다.

### 대안 D — 즉시 강제 gating (명시적 breaking behavior 승인 필요)

startup/reconnect 후 Homing 전 모든 Absolute Move 및 Absolute step을 포함한 Sequence start를 거부한다. 기존 API 의미는 Test coordinate로 유지하지만 admission behavior 변경을 공식 breaking operational change로 승인하고 UI/운영 절차/테스트를 함께 갱신한다.

장점: 가장 즉각적이고 보수적인 좌표 안전 정책이다. 단점: 기존 workflow가 즉시 중단되므로 본 작업의 현재 승인 범위를 넘는다.

## 4. 필요한 결정

1. 대안 A 또는 B 중 PHASE A 통합 기준선 선택
2. Absolute Move 정책에 대해 대안 C(단계적 전환) 또는 D(즉시 강제)를 명시적으로 승인
3. Auto Calibration의 communication interruption 시 결과를 `ABORTED`와 `FAULTED` 중 어느 것으로 표시할지 확정
4. Sequence가 non-motion step을 실행 중일 때 incident를 idle로 볼지, 전체 Sequence ownership을 active interruption으로 볼지 확정
5. 현재 dirty changes가 하나의 승인된 기준선인지, 별도 작업 중 변경인지 확인

이 결정 전에는 안전 관련 코드를 임의로 수정하지 않는다.

## 5. ABI 및 테스트 상태

- RuntimeV1~V6 layout: 변경 없음
- 기존 public ABI: 변경 없음
- 새 public symbol: 없음
- Debug build: 미실행 (구현 미착수)
- Debug CTest: 미실행
- Release build/CTest: 미실행
- Hardware: 미시험

