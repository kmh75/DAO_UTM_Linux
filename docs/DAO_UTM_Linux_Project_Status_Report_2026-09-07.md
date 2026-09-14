# DAO_UTM_Linux 프로젝트 업무인계 기술 보고서

## 감사 기준

- 대상: `/home/dao/Projects/DAO_UTM_Linux`
- 기준일: 2026-09-07
- 분석 기준: 현재 작업 트리의 실제 코드 우선
- Git HEAD: `d030a32`
- 제한 준수:
  - 코드/기존 문서 수정 없음
  - Refactor/format/commit/push 없음
  - EtherCAT Hardware 실행 없음
  - Build/CTest 재실행 없음
- 주의: 현재 작업 트리는 clean하지 않다. `compliance/`, `protocol/`, 관련 UI·CMake·테스트·문서 변경과 `build-release/`가 미커밋 상태다. 따라서 본 보고서는 HEAD만의 상태가 아니라 현재 파일시스템에 존재하는 구현 상태를 감사한 결과다.

---

## 1. Executive Summary

DAO_UTM_Linux는 다음 계층으로 구성된 UTM 제어 프로그램이다.

```text
SOEM
  ↓
DaoEtherCATMaster
  ↓
DaoEngineCore / libdao_ethercat.so
  ↓
UtmEngineCore / libdao_utm_engine.so
  ↓
UtmUiController / Qt6 Local HMI

Application-level analysis:
UtmComplianceCompensation

Application/service-level communication:
DaoProtocolServer
  ↓
future Windows Monitoring Client
```

핵심 판단은 다음과 같다.

- EtherCAT scan, PDO mapping, OP 진입, 2 ms cyclic communication, LS servo, DAO ADC, FASTECH IO/encoder 장치 경로가 구현되어 있다.
- 저장소 로그에는 4개 실제 slave, Expected/Actual WKC 12, Servo ON, Jog, slave recovery 성공 기록이 있다.
- UTM Engine에는 수동 Motion, Force Motion, Safety, Sequencer, Calibration API, profile persistence가 상당 부분 구현되어 있다.
- UI는 Basic Engine을 직접 호출하지 않고 `DaoUtm_*` API를 사용한다.
- Machine Compliance는 고정 크기 LUT, signed interpolation, clamp, UI 편집, profile 저장까지 구현되어 있다.
- Compliance는 제어 좌표에 들어가지 않는다. 현재 적용 범위는 UI/분석 데이터뿐이다.
- 자동으로 servo를 움직여 compliance curve를 생성하는 기능은 없다.
- Compression/Tension 별도 curve도 없으며 하나의 signed curve를 사용한다.
- Protocol V1의 framing, codec, TCP server, 10 Hz live scheduling, mock client는 구현돼 있다.
- 그러나 Protocol Server는 production UTM/UI에 연결되지 않았다. Recipe, command, event, recording, test-data transfer는 callback/codec 경계까지만 존재한다.
- 실제 local test sample recorder/file store는 아직 없다.
- Basic Engine 및 UTM Engine의 기존 Public ABI는 이번 미커밋 Compliance/Protocol 추가로 변경되지 않았다.

가장 중요한 기술 위험은 다음이다.

1. Protocol `SnapshotMailbox`의 double-buffer 구현에 C++ data race 가능성이 있다.
2. 2 ms UTM 제어 경로가 여러 `std::mutex`를 사용한다.
3. EtherCAT recovery가 cyclic thread 안에서 SOEM recovery/reconfiguration을 수행한다.
4. force/servo/coordinate sign 조합이 실제 인장·압축 장비에서 완전히 검증됐다는 근거가 없다.
5. Public Basic Engine API를 직접 사용하는 외부 코드가 UTM Safety 계층을 우회할 수 있다.

---

## 2. Current Architecture

### SOEM / EtherCAT transport

책임:

- NIC open/close
- slave 탐색
- PDO configuration/mapping
- EtherCAT state transition
- process-data 송수신
- slave recovery/reconfiguration

주요 위치:

- `master/`
- `third_party/soem/`

DAO 코드는 SOEM을 `add_subdirectory()`로 포함하고 `DaoEtherCATMaster` 내부에서 `ecx_*` API를 호출한다. SOEM 내부 알고리즘 자체를 수정하거나 재구현하지 않는다.

### Basic EtherCAT Engine

책임:

- EtherCAT Master 추상화
- logical device 분류
- servo/ADC/IO/encoder runtime
- ADC DSP
- C Public ABI 제공

주요 위치:

- `engine/`
- 출력: `libdao_ethercat.so`

### UTM Engine

책임:

- Machine lifecycle/state
- coordinate와 zero
- jog/position/velocity/force motion
- Safety 및 stop latch
- command-source arbitration
- Sequencer
- UTM C Public ABI

주요 위치:

- `utm/`
- 출력: `libdao_utm_engine.so`

### Application / Analysis

책임:

- Machine profile
- display conversion
- compliance correction
- UI와 Engine 연결

주요 위치:

- `ui/`
- `compliance/`

### Protocol service

책임:

- TCP framing
- V1 message codec
- live/status publishing
- request/response callback 경계
- mock-client 기반 offline 검증

주요 위치:

- `protocol/`

현재 production application에는 Protocol Server 인스턴스가 연결되어 있지 않다.

---

## 3. Project Directory / Module Map

| 경로 | 실제 역할 | 상태 |
|---|---|---|
| `master/` | SOEM 기반 EtherCAT Master | IMPLEMENTED |
| `engine/` | Basic Engine 및 C ABI | IMPLEMENTED |
| `utm/` | UTM machine/motion/safety/sequencer | IMPLEMENTED |
| `compliance/` | Compliance LUT 및 analysis sample | IMPLEMENTED, 미커밋 |
| `protocol/` | Protocol V1 codec/server/mock | PARTIAL, 미커밋 |
| `ui/` | Qt6 Local HMI/controller/profile | IMPLEMENTED |
| `app/` | console, hardware/manual test, offline tests | IMPLEMENTED |
| `config/` | 현재 유의미한 관리 파일 없음 | NOT IMPLEMENTED/EMPTY |
| `devices/` | 현재 장치 구현의 주 위치가 아님 | EMPTY |
| `tests/` | 현재 별도 테스트 파일 없음 | EMPTY |
| `docs/` | API, Compliance, Protocol 문서 | PARTIALLY OUTDATED |
| `third_party/soem/` | SOEM submodule | DEPENDENCY |
| `build-release/` | 기존 release build 산출물 | GENERATED/DIRTY |
| `CMakeLists.txt` | 전체 target/test 구성 | IMPLEMENTED, 미커밋 변경 |

---

## 4. EtherCAT Master

### 구현 기능

| 기능 | 구현 | 검증 |
|---|---|---|
| Adapter enumeration | IMPLEMENTED | CODE ONLY |
| Adapter open/close | IMPLEMENTED | REAL HARDWARE VERIFIED |
| Slave scan | IMPLEMENTED | REAL HARDWARE VERIFIED |
| PDO mapping | IMPLEMENTED | REAL HARDWARE VERIFIED |
| PRE-OP/SAFE-OP/OP/INIT | IMPLEMENTED | REAL HARDWARE VERIFIED |
| Expected/Actual WKC | IMPLEMENTED | REAL HARDWARE VERIFIED |
| Cyclic send/receive | IMPLEMENTED | REAL HARDWARE VERIFIED |
| Diagnostics ring/log | IMPLEMENTED | REAL HARDWARE VERIFIED |
| Slave ACK/reconfigure/recover | IMPLEMENTED | REAL HARDWARE VERIFIED |
| Hard communication stop | IMPLEMENTED | MOCK + LOG EVIDENCE |
| Long-duration stability | UNKNOWN | UNKNOWN |

### 주기와 thread

Master는 별도 communication thread에서 `sleep_until` 기반으로 약 2 ms cycle을 수행한다.

Cycle 안에서 다음이 실행된다.

1. servo output state 준비
2. PDO send
3. PDO receive
4. WKC 판정
5. input capture
6. ADC sample DSP
7. communication diagnostic 갱신
8. 필요 시 recovery

### Communication policy

코드상의 threshold:

| 조건 | 동작 |
|---|---|
| 첫 BAD WKC | `NORMAL → TRANSIENT` |
| 연속 3 BAD | `TRANSIENT → DEGRADED` |
| 연속 5 BAD | `DEGRADED → RECOVERING`, soft recovery 시작 |
| RECOVERING 중 | 25 bad-cycle 간격으로 recovery 재시도 |
| 연속 3 GOOD | `RECOVERED`, 이후 normal 복귀 |
| recovery 300 ms 초과 | communication hard-stop 요청 |
| slave recovery timeout | slave당 약 2000 µs |

Soft recovery는 다음을 수행한다.

- state reread
- SAFE-OP + ERROR ACK
- lost/none slave에 `ecx_recover_slave`
- 그 외 `ecx_reconfig_slave`
- OP 재요청

Motion은 communication recovery 후 자동 재개되지 않는다. UTM stop latch가 남으므로 ACK/reset이 필요하다.

### Master와 UTM의 이중 policy

Master 내부 recovery state와 별도로 UTM Engine에도 `UtmCommunicationPolicy`가 있다. UTM policy는 WKC/basic-running 정보를 다시 판정하여 Safety에 `communicationRecovering` 또는 `communicationValid=false`를 전달한다.

이는 구현상 동작하지만, recovery 상태 소유자가 두 군데라 threshold 또는 상태 전이가 나중에 어긋날 위험이 있다.

### 문서 차이

Document: 기존 API Guide는 Basic Engine 호출 중심이며 현재의 multi-stage recovery 정책을 충분히 설명하지 않는다.  
Code: Master와 UTM policy 양쪽에 3/5/3/300 ms 정책이 존재한다.  
Difference: 문서만으로 실제 stop/latch/recovery 동작을 판단할 수 없다.

---

## 5. Basic Engine

### 구조

- `DaoEtherCATMaster`: SOEM과 직접 접촉
- `DaoEngineCore`: device/runtime façade
- `DaoEtherCAT.Engine` C API: 외부 공개 ABI
- shared object: `libdao_ethercat.so`
- soname version: `1`
- 현재 library version: `1.0.1`
- version script: `DaoEngine_*`만 export

### 주요 Public ABI 범주

- lifecycle: initialize/shutdown/version
- adapter: enumerate/info/open/close
- scan/device: slave count/info, logical-device count/info
- EtherCAT state: PRE-OP/SAFE-OP/OP/INIT
- PDO/raw process data
- communication start/stop/status
- ADC runtime V1/V2/V3
- ADC zero/calibration/filter/diagnostics
- servo runtime/on/off/home/move/velocity/jog/stop
- IO read/write
- encoder runtime/reset/direction/scale/calibration

현재 Compliance와 Protocol은 이 ABI에 함수를 추가하지 않았다.

### Direct access 문제

Basic Engine은 servo와 raw PDO에 직접 접근할 수 있는 API를 공개한다. Qt UI에서는 이를 직접 사용하지 않지만, 별도 application이 `DaoEngine_*`를 호출하면 UTM Safety/command arbitration을 우회할 수 있다. 배포 구조에서 Basic Engine 직접 접근 주체를 제한해야 한다.

---

## 6. UTM Engine

### 주요 구성요소

| 구성 | 책임 |
|---|---|
| `UtmEngineCore` | lifecycle/control thread/통합 |
| `UtmInputCollector` | Basic Engine runtime 수집 |
| `UtmStateMachine` | machine state |
| `UtmSafetyMonitor` | safety 조건 평가 |
| `UtmStopLatch` | stop 원인 latch/ACK |
| `UtmCommandMailbox` | command source 전달 |
| `UtmJogController` | source별 jog 상태 |
| `UtmMotionController` | position/velocity/force motion |
| `UtmCoordinateController` | machine/test/servo 좌표 변환 |
| `UtmMotionOutputArbiter` | 최종 Basic Engine command |
| `UtmStopConditionMonitor` | force/break/travel/time 조건 |
| `UtmSequencer` | pending/active sequence 실행 |
| `UtmRuntimeStore` | public runtime snapshot |

### Machine state

- INITIALIZING
- READY
- MANUAL
- RUNNING
- STOPPING
- STOPPED
- EMERGENCY
- FAULT

### Command source

- UI
- EXTERNAL_GO
- DIGITAL_JOG
- INTERNAL
- REMOTE
- SEQUENCER

REMOTE source는 정의되어 있지만 Protocol Server와 실제 연결되지는 않았다.

### Runtime locking

Control path에서 `UtmSequencer`, `UtmMotionController`, `UtmJogController`, `UtmRuntimeStore`, `UtmCommandMailbox`의 `std::mutex`를 사용한다. 기능상 동기화는 되어 있지만 hard real-time 관점에서 bounded latency가 보장되지 않는다.

---

## 7. Hardware Device Support

### DAO ADC

- Vendor: `0x11C0`
- Product: `0xDA01`
- revision 제한: 없음
- PDO output: 4 bytes
- PDO input: 24 bytes
- input에는 test counter, 4개 raw `int32` sample, status가 포함됨
- cycle frame당 4 sample이며 코드상 유효 ADC sample rate는 약 2000 Hz
- zero, calibration, filters, diagnostics API 존재

상태: IMPLEMENTED  
검증: ADC slave scan/PDO/OP와 filter 적용 로그는 REAL HARDWARE VERIFIED. 실제 정밀 force calibration 성능은 UNKNOWN.

### LS ELECTRIC L7NH Servo

- Vendor: `0x7595`
- Product: `0x10001`
- Revision: `1`
- RxPDO: controlword, mode, target position, profile velocity/acc/dec, target velocity, touch probe, digital outputs
- TxPDO: statusword, actual mode/position, following error, touch probe, digital inputs
- `0x1601` / `0x1A01` PDO mapping
- asynchronous servo state-machine 존재

상태: IMPLEMENTED  
검증: detection, PDO, OP, Servo ON, Jog/Stop은 REAL HARDWARE VERIFIED. Absolute/Incremental/Force motion 실기 완료 여부는 UNKNOWN.

### FASTECH Ezi-IO IN8OUT8

- Vendor: `0x0FA00000`
- Product: `0x2021`
- Revision: `1`
- PDO: 1 byte output, 1 byte input
- IN16OUT16 `0x2023`도 추가 지원

상태: IMPLEMENTED  
검증: 장치 detection/PDO/OP는 REAL HARDWARE VERIFIED. 실제 모든 I/O bit 동작은 UNKNOWN.

### FASTECH CNT02 Encoder

- Vendor: `0x0FA00000`
- Product: `0x2301`
- PDO: output 13 bytes, input 56 bytes
- reset, direction, scale, calibration runtime 지원

상태: IMPLEMENTED  
검증: detection/PDO/OP는 REAL HARDWARE VERIFIED. 실제 displacement calibration 정확도는 UNKNOWN.

### Expected WKC 12

코드는 Expected WKC를 12로 hard-code하지 않는다.

```text
expectedWkc = outputsWKC * 2 + inputsWKC
```

현재 저장소 로그의 4-slave 구성에서 계산 결과가 12이고 Actual WKC도 정상 시 12로 기록되어 있다.

---

## 8. Motion

### UI → UTM → Basic Engine 원칙

`ui/`에서 `DaoEngine_*` 직접 호출은 발견되지 않았다.

```text
MainWindow
  → UtmUiController
  → DaoUtm_*
  → UtmEngineCore
  → UtmMotionController / JogController
  → UtmCoordinateController
  → UtmMotionOutputArbiter
  → DaoEngine_*
  → DaoEtherCATMaster
  → Servo PDO
```

### Motion별 경로

| Motion | 핵심 UTM 처리 | Basic Engine 경계 |
|---|---|---|
| Jog | source별 press/release, velocity 생성 | `DaoEngine_ServoVelocity` |
| Move Absolute | test target→machine target→servo UU | `DaoEngine_ServoMoveAbsolute` |
| Move Incremental | 현재 test position+increment | absolute command로 변환 |
| Move Velocity | 방향과 speed 변환 | `DaoEngine_ServoVelocity` |
| Move To Force | measured force 감시, velocity approach | `DaoEngine_ServoVelocity` |
| Hold Force | error band별 staged velocity | `DaoEngine_ServoVelocity/Stop` |
| Stop | command epoch 무효화/arbiter stop | `DaoEngine_ServoStop` |

### 단위

- UI/UTM speed: mm/min
- acceleration/deceleration profile: mm/s²
- servo velocity: `mm/min × unitsPerMm ÷ 60`
- servo acceleration/deceleration: `mm/s² × unitsPerMm`

Direction은 motion direction과 `servoDirectionSign`을 결합한다.

### 좌표 위험

Machine position 계산은 기본적으로 다음과 같다.

```text
machinePositionMm = servoActualPosition / servoUnitsPerMm
```

position feedback에 `servoDirectionSign`을 다시 적용하지 않는다. 이것이 의도된 drive polarity 구성인지 실제 장비에서 반드시 교차 검증해야 한다.

---

## 9. Force Motion

### 사용 Force

`MOVE_TO_FORCE`, `HOLD_FORCE`, overload, break 판정은 `snapshot.forceN`, 즉 ADC engineering force를 사용한다. Display average force는 사용하지 않는다.

```text
directionalForce =
    measuredForceN × forceDirectionSign × motionDirectionSign
```

### MOVE_TO_FORCE

- target force magnitude validation
- approach velocity command
- target crossing 시 완료
- timeout/max-travel/invalid-force 확인
- PID 없음
- 현재 구현은 별도 medium/fine staged approach가 아닌 단일 approach 중심

### HOLD_FORCE

실제 staged velocity 방식이다.

- APPROACH
- FINE_APPROACH 내부에서 medium/fine speed 선택
- HOLDING
- CORRECTING

행동:

- 오차가 tolerance 이내: servo stop, holding
- overshoot: reverse speed로 correcting
- 큰 오차: approach speed
- 중간 오차: medium speed
- 작은 오차: fine speed

PID의 P/I/D accumulator 또는 controller 구현은 발견되지 않았다.

상태: IMPLEMENTED  
검증: 코드 및 test harness 존재, 실제 하중 실기 검증은 UNKNOWN.

---

## 10. Safety

| 조건 | 판단 계층 | 중지 계층/정책 |
|---|---|---|
| Emergency Stop | UTM Input/Safety | 즉시 disable, EMERGENCY latch |
| Upper/Lower Limit | UTM Safety | directional motion stop/latch |
| External Stop | UTM Safety | controlled stop/latch |
| Servo Fault | Basic runtime→UTM Safety | disable, FAULT |
| EtherCAT Fault | Master/UTM policy→Safety | disable, FAULT |
| Communication Recovering | UTM policy→Safety | stop/latch |
| Overload | measured force→Safety | controlled stop/latch |
| User Stop | command/Safety | controlled stop |
| Motion Fault | MotionController | motion fail/stop; 독립 Safety enum은 아님 |
| Sequence Abort | Sequencer/Engine | active motion stop, recording-event abort |

### 우선순위

Safety reason은 평가 순서상 communication, emergency, servo, external, overload, limit, user stop 순서 영향을 받는다. 단 Emergency Stop은 후속 override로 primary reason이 되도록 처리되어 있다.

### Latch/reset

- stop 시 motion command epoch가 무효화된다.
- jog source가 clear된다.
- physical motion이 inhibit된다.
- active sequence가 abort된다.
- fault condition이 제거된 뒤 ACK/reset이 있어야 latch가 해제된다.
- EtherCAT recovery 성공만으로 이전 motion은 자동 재개되지 않는다.

### 확인되지 않은 부분

- 실제 E-STOP 배선 polarity
- 상·하 limit polarity와 장착 방향
- servo STO와 UTM stop reason의 최종 실기 동작
- overload trip accuracy

는 UNKNOWN이다.

---

## 11. Communication Recovery

실제 로그에는 slave 4가 SAFE-OP+ERROR로 진입한 뒤 다음 순서로 회복된 사례가 있다.

1. error ACK
2. reconfiguration
3. OP 요청
4. WKC 12 복구
5. 연속 GOOD 확인
6. motion resume disabled

약 12 ms 복구 로그가 확인된다.

주의점:

- SOEM `readstate/recover/reconfig`가 master cyclic thread에서 호출된다.
- recovery 중 cycle jitter가 증가할 수 있다.
- UTM Safety는 RECOVERING부터 motion을 중지한다.
- 복구 후 motion은 자동으로 살아나지 않는다.
- ACK/reset 정책은 유지된다.

---

## 12. ADC / Force Pipeline

실제 순서는 다음과 같다.

```text
Raw ADC
  → optional IIR
  → optional power-line notch
  → zero subtraction
  → calibration scale
  → optional 3-point median
  → moving average
  → engineering forceN
  → UTM runtime measuredForceN
  → separate display averaging
  → displayForceN
```

예상 순서와 달리 zero/calibration은 median/moving average보다 앞에 있다.

### 용도 분리

`measuredForceN`:

- MOVE_TO_FORCE
- HOLD_FORCE
- overload
- break/stop condition
- Sequencer/motion runtime
- 향후 recording sample의 원천

`displayForceN`:

- Qt UI 표시
- UI graph/display formatting

Display force가 control 판단에 사용되는 경로는 발견되지 않았다.

### ADC zero/calibration

Stable capture는 pre-zero/pre-calibration 단계의 filtered ADC 값을 평균하여 zero offset 또는 reference force 기반 scale을 산출한다.

### ADC recovery/diagnostic 한계

상위 Linux 코드가 알고 있는 내용:

- PDO test counter
- raw status
- WKC
- data freshness/stale time
- DSP diagnostic sample
- filter runtime

명확히 노출되지 않은 내용:

- STM32 내부 ADC stall state
- AD7190 missing conversion count의 의미 있는 필드
- firmware recovery state machine
- recovery attempt/result enum

따라서 firmware 내부 recovery 문서가 있더라도 현재 Public PDO/API까지 올라왔다고 판단할 수 없다.

---

## 13. Coordinate / Displacement

| 값 | 생성 위치 | 단위 | 용도 |
|---|---|---:|---|
| Servo Actual Position | Basic Engine servo PDO | UU/count | 원시 feedback |
| Machine Position | CoordinateController | mm | limit/travel/motion |
| Position Zero Offset | UTM runtime | mm | test 좌표 원점 |
| Test Position | machine-zero | mm | motion/표시 |
| Raw Test Displacement | 현재 test position | mm | compliance 입력 |
| Compensation | LUT(forceN) | mm | 분석 |
| Corrected Displacement | raw-compensation | mm | UI/protocol schema |
| Extensometer | encoder engineering value | mm | 직접 specimen displacement |

공식:

```text
compensationMm = LookupCompliance(measuredForceN)

correctedDisplacementMm =
    rawTestDisplacementMm - compensationMm
```

### Control contamination 검사

Corrected displacement는 다음에서 사용되지 않는다.

- Jog
- position/velocity target
- position completion
- max travel
- limit
- Safety
- force control
- Sequencer

따라서 현재 Compliance가 motion/safety를 오염시키는 HIGH RISK 경로는 없다. Extensometer에도 Compliance correction을 적용하지 않는다.

---

## 14. Calibration

| 기능 | UI | Runtime API | Validation | Profile 저장/복원 |
|---|---|---|---|---|
| Force Zero | 있음 | 있음 | stable capture | 저장 안 함 |
| Force Calibration | 있음 | 있음 | reference/scale 검증 | scale 저장/복원 |
| Position Zero | 있음 | 있음 | machine state 확인 | 저장 안 함 |
| Extensometer Zero | 있음 | encoder reset | 상태 확인 | 저장 안 함 |
| Extensometer Calibration | 있음 | scale calibration | reference 검증 | scale 저장/복원 |
| ADC Filter | 있음 | apply/readback | filter config 검증 | 저장/복원 |
| Display Unit | 있음 | UI 변환 | enum/range | 저장/복원 |
| Loadcell Capacity | 있음 | UI/profile | positive/finite | 저장/복원 |
| Compliance | 있음 | application class | LUT 검증 | 저장/복원 |

Force calibration과 Compliance는 별도 기능이다. Compliance는 ADC scale을 변경하지 않는다.

---

## 15. Profile / Persistence

### 저장 항목

- adapter/device configuration
- auto-servo setting
- servo units/mm
- servo direction
- force direction
- jog speed
- motion dynamics
- force-control speed/tolerance/error settings
- protection/max force/travel/time
- I/O mapping
- force calibration scale
- extensometer calibration scale
- ADC filter
- force display unit/decimals
- loadcell capacity
- fullscreen/display averaging
- compliance enabled/version/points

### 저장하지 않는 runtime 값

- Force Zero
- Position Zero
- Extensometer Zero
- compliance calibration temporary baseline
- current motion
- current sequence-running state
- fault/stop latch
- current EtherCAT recovery state

### 저장 경로

- profile directory: `QStandardPaths::AppConfigLocation/profiles`
- selected profile: `QSettings`
- atomic file replacement: `QSaveFile`
- profile 부재 시 `default_machine.json` 생성
- 기존 파일을 임의로 덮어쓰지 않음

### Schema/backward compatibility

- schema version은 현재 `1`
- compliance object가 없으면 disabled/empty
- 일부 예전 motion/filter 필드는 default 또는 legacy conversion 적용
- 지원하지 않는 schema version은 reject
- 이전 `lastTab` 값과 무관하게 startup tab은 Main

### Setup commit 흐름

```text
UI edit
  → temporary/candidate MachineProfile
  → unit conversion 및 validation
  → controller active profile 갱신
  → 명시적 SAVE/APPLY
  → QSaveFile atomic commit
```

`loadingProfile_`은 restore 중 signal에 의한 임의 profile 변경을 막는다. 일부 연동 widget은 `QSignalBlocker`도 사용한다.

---

## 16. Machine Compliance

주요 파일:

- `compliance/include/UtmComplianceCompensation.h`
- `compliance/src/UtmComplianceCompensation.cpp`
- `compliance/include/UtmAnalysisSample.h`

### 실제 구현

- 최대 64 point 고정 배열
- 최소 2 point
- finite validation
- configure 시 force 오름차순 정렬
- duplicate force reject
- signed force 보존
- signed deformation 보존
- piecewise-linear interpolation
- range 밖 endpoint clamp
- out-of-range flag
- enabled/configured 상태
- curve version
- runtime evaluation 시 heap allocation 없음

Endpoint 자체는 in-range이며, endpoint를 넘어가면 clamp와 함께 out-of-range가 된다.

### Profile

```json
"complianceCompensation": {
  "enabled": true,
  "version": 1,
  "points": [
    { "forceN": -100.0, "deformationMm": -0.014 },
    { "forceN": 0.0, "deformationMm": 0.0 },
    { "forceN": 100.0, "deformationMm": 0.015 }
  ]
}
```

### Restart/reconnect

- profile load 시 curve 복원 경로 존재
- hardware disconnect 자체는 application compliance curve를 지우지 않음
- temporary zero baseline은 저장하지 않음

### Raw 데이터 정책

`UtmAnalysisSample`에 다음 필드가 준비되어 있다.

- timestamp
- forceN
- rawDisplacementMm
- complianceCompensationMm
- correctedDisplacementMm
- extensometerMm
- sequenceStep
- sampleFlags

다만 실제 sample recorder가 아직 없으므로 원본이 local recording file에 항상 보존된다고 볼 수 없다. 현재는 구조 준비 단계다.

### Compression/Tension

현재는 하나의 공통 signed curve다. 별도 Compression Curve와 Tension Curve는 없다.

---

## 17. Compliance UI

Calibration tab에 다음 항목이 존재한다.

- Current Force
- Raw Test Displacement
- Current Compensation
- Corrected Displacement
- Enabled
- Force/Deformation table
- CAPTURE ZERO
- ADD POINT
- DELETE POINT
- CLEAR CURVE
- APPLY / SAVE COMPENSATION

### 버튼 동작

| UI | 실제 동작 |
|---|---|
| CAPTURE ZERO | 현재 raw test position을 별도 baseline으로 저장 |
| ADD POINT | 현재 force와 `raw-baseline`을 table에 추가 |
| DELETE POINT | 선택한 table row 삭제 |
| CLEAR CURVE | table을 비우고 Enabled를 해제; 즉시 저장하지 않음 |
| APPLY/SAVE | parse→validate→curve configure→profile version 갱신→QSaveFile 저장 |

CAPTURE ZERO는 기존 UTM Position Zero를 변경하지 않는다.

### 확인된 약점

Profile 객체를 controller에 직접 대입하는 일부 경로에서는 compliance runtime과 UI table이 자동 재구성되지 않을 가능성이 있다. 정식 `loadProfile()` 경로에서는 복원되지만, 모든 profile assignment 경로가 동일한 synchronization 함수를 쓰는 것은 아니다.

상태: IMPLEMENTED  
검증: MOCK / UNIT TESTED  
실제 하드웨어 compliance calibration: UNKNOWN

---

## 18. Sequencer

지원 step:

- ZERO_FORCE
- ZERO_POSITION
- ZERO_ENCODER
- MOVE_ABSOLUTE
- MOVE_INCREMENTAL
- MOVE_VELOCITY
- MOVE_TO_FORCE
- HOLD_FORCE
- WAIT_TIME
- WAIT_INPUT
- SET_OUTPUT
- PULSE_OUTPUT
- LOOP_START
- LOOP_END
- END

### 구조

```text
editable/pending definition
  → validation
  → commit
  → active snapshot
  → execution
```

Active snapshot은 실행 중 pending 편집과 분리된다. 검증에는 step type, parameter, loop pairing/depth, 최대 step 수 등이 포함된다.

### Recording hook

Sequencer runtime에는 다음 필드가 있다.

- recording session id
- recording active
- last event(start/complete/abort)

그러나 이는 hook/event 상태이며 실제 sample buffer나 result file writer는 아니다.

### External GO/STOP

- External GO rising edge:
  - committed sequence가 있으면 `RequestGoStart()`
  - 아니면 일반 START command
- External STOP:
  - InputCollector→Safety→controlled stop/latch

READY 상태에서 committed sequence를 External GO로 시작하는 코드가 존재한다.

---

## 19. Protocol V1

주요 파일:

- `protocol/include/DaoProtocolV1.h`
- `protocol/src/DaoProtocolCodec.cpp`
- `protocol/src/DaoProtocolServer.cpp`
- `app/protocol_test_client/main.cpp`

### Architecture

- Linux TCP server
- 향후 Windows TCP client
- 기본 port: `45550`
- protocol version: `1`
- active monitoring client: 한 번에 1개
- listener backlog: 1
- server 별도 `std::thread`
- nonblocking socket + poll
- maximum payload: 1 MiB

### Frame

24-byte fixed header:

| Offset/크기 | 필드 |
|---|---|
| 4 | Magic `0x44415554` (`DAUT`) |
| 2 | Protocol Version |
| 2 | Message Type |
| 4 | Flags |
| 4 | Payload Length |
| 4 | Sequence Number |
| 4 | Request ID |

모든 integer는 explicit big-endian codec을 사용한다. C/C++ struct memory를 그대로 전송하지 않는다. Double도 IEEE-754 bit representation을 명시적으로 byte encoding한다.

Stream decoder는 split packet과 한 recv에 합쳐진 여러 frame을 처리한다.

### Message ID

| ID | Message |
|---:|---|
| 1 | HELLO |
| 2 | HELLO_ACK |
| 3 | HEARTBEAT |
| 10 | MACHINE_STATUS |
| 11 | LIVE_DATA |
| 20 | LOAD_RECIPE |
| 21 | COMMIT_RECIPE |
| 22 | START_TEST |
| 23 | STOP_TEST |
| 24 | ACK_RESET |
| 30 | COMMAND_ACK |
| 31 | COMMAND_NACK |
| 40 | TEST_STARTED |
| 41 | STEP_CHANGED |
| 42 | TEST_COMPLETE |
| 43 | TEST_ABORTED |
| 44 | FAULT_EVENT |
| 50 | GET_TEST_DATA |
| 51 | TEST_DATA_BEGIN |
| 52 | TEST_DATA_CHUNK |
| 53 | TEST_DATA_END |

### Canonical units

- Force: N
- Position/displacement/extensometer: mm
- Speed: mm/min
- timestamp: unsigned microseconds
- display unit은 payload에 포함하지 않음

### LIVE_DATA

76-byte explicit payload:

- timestampUs
- testId
- forceN
- rawDisplacementMm
- complianceCompensationMm
- correctedDisplacementMm
- extensometerMm
- machineState
- sequenceState
- currentStep
- testRunning
- flags

주기: 100 ms, 즉 10 Hz.

### ACK/NACK

Header의 `requestId`를 ACK/NACK에 보존한다. Mock handler에서 requestId matching이 검증된다.

### Disconnect/reconnect

- client disconnect는 server의 외부 snapshot 상태를 지우지 않음
- test를 자동 중지하는 코드 없음
- reconnect 후 HELLO와 MACHINE_STATUS 재수신 가능
- production UTM test state와 server가 아직 연결되지 않았으므로 실제 running test 유지 검증은 mock state에 한정됨

### Recipe 연결 상태

`LOAD_RECIPE`, `COMMIT_RECIPE`, `START_TEST`는 generic callback handler까지만 구현되어 있다.

다음은 아직 없다.

- protocol recipe payload → `UtmSequenceDefinition` 변환
- `DaoUtm_ValidateSequence`
- pending load
- commit
- actual start
- UTM error → protocol NACK reason mapping

상태: PARTIAL.

### Test-data transfer

Codec에는 `TEST_DATA_CHUNK` roundtrip이 있다. 하지만 다음은 없다.

- local recording 파일
- GET 요청에 대한 result lookup
- BEGIN/CHUNK/END streaming producer
- reconnect 후 data resume

따라서 test-data transfer는 framing/codec 준비 수준이다.

### Control thread 분리

현재 UTM 2 ms thread 안에 socket, JSON, protocol serialization은 없다. Protocol server는 별도 thread다. 다만 production 연결 자체가 없으므로 end-to-end nonblocking integration이 완료된 것은 아니다.

### Protocol thread 위험

`SnapshotMailbox`는 두 slot과 atomic published index를 사용하지만 slot 본문은 non-atomic 구조체다. writer가 빠르게 두 slot을 순환하면 reader가 복사 중인 slot을 다시 덮어쓸 수 있어 C++ data race 가능성이 있다. HIGH RISK다.

Event queue는 mutex 기반이므로 향후 control thread에서 직접 `QueueEvent()`를 호출하면 control loop blocking 위험이 있다.

---

## 20. Qt UI

Tab은 실제로 다음 6개다.

1. Main
2. Manual
3. Sequence
4. Calibration
5. Diagnostics
6. Setup

Startup은 저장된 이전 tab과 무관하게 항상 Main tab이다.

### 주요 기능

- Main: machine 상태, force/displacement, graph
- Manual: servo, jog, absolute/incremental/velocity/force motion
- Sequence: step 편집, validation, commit, start/abort
- Calibration: force/extensometer/zero/filter/compliance
- Diagnostics: EtherCAT, WKC, device/runtime 상태
- Setup: devices, motion, force control, protection, I/O, units, profile
- UI runtime poll: 약 40 ms

### API 계층 준수

UI 소스에서 `DaoEngine_*` 직접 호출은 발견되지 않았다.

```text
UI → UtmUiController → DaoUtm_*
```

원칙이 지켜지고 있다.

---

## 21. Tests

CMake에 등록되는 CTest는 Qt6가 발견되는 정상 구성 기준 총 6개다.

| CTest | 검증 내용 |
|---|---|
| `dao_compliance_compensation` | LUT, signed points, duplicate, clamp, corrected, disabled, motion/extensometer 불변 |
| `dao_protocol_v1_offline_integration` | header, fragmentation, coalescing, invalid length, handshake, 10 Hz, ACK/NACK, reconnect, chunk |
| `dao_force_unit_profile_lifecycle` | force unit/profile lifecycle |
| `dao_setup_persistence_restart` | Setup 저장/재시작 |
| `dao_production_ui_startup_save_restart` | production UI startup/save/restart |
| `dao_communication_policy_fault_injection` | communication threshold/recovery/fault policy |

추가 executable이지만 CTest 등록이 아닌 것:

- `dao_engine_test`
- `dao_utm_test`
- `dao_utm_sequence_test`
- graph 관련 test executable

`dao_engine_test` 등 일부는 실제 hardware를 사용하는 interactive test이므로 이번 감사에서 실행하지 않았다.

현재 파일 정의상 CTest 수는 6개다. 기존 `build-release` 산출물과 이전 보고에는 6/6 passed 흔적이 있으나, 이번 분석에서는 재실행하지 않았으므로 이번 감사에서 재검증됨으로 표시하지 않는다.

### Coverage 공백

- 실제 Protocol↔UTM integration
- local recording/data retrieval
- production reconnect 중 running sequence
- actual 2 ms jitter under network load
- compliance hardware curve
- force polarity/hysteresis
- actual E-stop/limit/overload

---

## 22. Documentation Status

### DAO EtherCAT Engine API Guide

분류: PARTIALLY OUTDATED

Document: Engine 0.1.0/Windows x64 중심으로 기술한다.  
Code: 현재 Linux CMake shared library는 1.0.1이며 UTM, encoder, recovery, compliance, protocol 계층이 추가되어 있다.  
Difference: Basic API 참고 자료로는 유효하지만 현재 전체 architecture/기능 현황 문서로 사용할 수 없다.

### DAO_UTM_Compliance_Architecture.md

분류: PARTIALLY OUTDATED

Document: profile JSON 예제에 point 한 개도 제시하며 raw samples가 항상 보존된다고 설명한다.  
Code: nonempty curve는 최소 2 point가 필요하며, analysis sample 구조는 있지만 실제 recorder는 없다.  
Difference: LUT 및 UI 설계는 대체로 현재 코드와 일치하지만 recording 완료 상태를 과장한다.

### DAO_UTM_Protocol_V1.md

분류: PARTIALLY OUTDATED / 일부 PLAN ONLY

Document: Linux가 local recording의 주체이고 recipe/data retrieval이 기존 실행 흐름과 연결된 것으로 기술한다.  
Code: server/codec/mock는 존재하지만 UTM binding과 recorder/data streaming producer는 없다.  
Difference: wire protocol 설명은 대체로 CURRENT지만 production integration 설명은 목표 구조다.

---

## 23. Hardware Verification Status

| 대상 | 판정 | 근거 |
|---|---|---|
| EtherCAT adapter/open/scan | REAL HARDWARE VERIFIED | 저장소 EtherCAT 로그 |
| 4-slave PDO mapping | REAL HARDWARE VERIFIED | ADC/Servo/IO/Encoder PDO 로그 |
| SAFE-OP/OP | REAL HARDWARE VERIFIED | state `0x08`, startup complete |
| Expected/Actual WKC 12 | REAL HARDWARE VERIFIED | 정상 cycle 로그 |
| Servo ON | REAL HARDWARE VERIFIED | status `0x27` 기록 |
| Jog/Stop | REAL HARDWARE VERIFIED | ±1667 velocity 및 stop 기록 |
| Slave recovery/reconfigure | REAL HARDWARE VERIFIED | slave 4 recovery 로그 |
| ADC PDO/filter application | REAL HARDWARE VERIFIED | ADC runtime/filter 로그 |
| IO/Encoder device presence | REAL HARDWARE VERIFIED | scan/PDO/OP |
| IO 실제 입출력 전체 | UNKNOWN | 명시적 동작 증거 부족 |
| Encoder calibration/정확도 | UNKNOWN | 실기 결과 부족 |
| Absolute/Incremental move | UNKNOWN | code/test harness만 확인 |
| Force calibration 정확도 | UNKNOWN | 실기 결과 부족 |
| Move To Force | UNKNOWN | 구현만 확인 |
| Hold Force | UNKNOWN | 구현만 확인 |
| Sequencer 실제 장비 실행 | UNKNOWN | 구현/test 코드만 확인 |
| Compliance | SOFTWARE TESTED ONLY | offline unit/UI 경로 |
| Protocol | SOFTWARE TESTED ONLY | mock/local TCP |
| Long-term EtherCAT operation | UNKNOWN | 시간·jitter acceptance 근거 없음 |

---

## 24. Known Risks / Tech Debt

### HIGH

1. **Protocol SnapshotMailbox data race 가능성**  
   Atomic index만으로 구조체 slot 읽기/쓰기 전체가 동기화되지 않는다.

2. **2 ms UTM 경로의 mutex 사용**  
   Sequencer, MotionController, JogController, RuntimeStore, CommandMailbox가 mutex 기반이다. UI/API thread가 오래 점유하면 cycle 지연 가능성이 있다.

3. **Recovery 작업이 EtherCAT cyclic thread에서 수행됨**  
   `ecx_readstate`, `ecx_recover_slave`, `ecx_reconfig_slave`가 recovery 시 cycle latency를 늘릴 수 있다.

4. **Force/coordinate polarity 실기 검증 부족**  
   `servoDirectionSign`, `forceDirectionSign`, motion direction의 조합이 인장·압축 양방향에서 검증되지 않았다.

5. **Basic Engine 직접 접근 가능성**  
   UI는 안전하지만 다른 application이 `DaoEngine_Servo*`를 직접 호출하면 UTM Safety와 arbitration을 우회할 수 있다.

### MEDIUM

- Master와 UTM 양쪽에 communication policy/state가 존재한다.
- Compliance profile과 runtime/table synchronization 경로가 단일화되지 않았다.
- Protocol event queue가 mutex 기반이다.
- nonblocking socket send의 `EAGAIN`을 연결 실패로 취급하여 느린 client가 disconnect될 수 있다.
- invalid enum은 frame decoder가 아니라 server dispatch에서 처리한다.
- header corruption 후 decoder resynchronization 없이 malformed 상태가 유지된다.
- sequence recording flag가 실제 recorder처럼 오해될 수 있다.
- position feedback sign과 command sign 정책이 코드만으로 직관적이지 않다.

### LOW

- Profile 저장은 `QSaveFile`이라 corruption 위험은 낮다.
- Compliance는 control code와 분리되어 있어 오염 위험이 낮다.
- Windows disconnect가 motion stop을 유발하지 않는 정책은 의도와 일치한다.

### 가장 함부로 변경하면 안 되는 영역 5개

1. `DaoEtherCATMaster` cyclic communication/PDO/recovery  
   실제 4-slave OP와 WKC 12가 확인된 핵심 기반이다.

2. Basic Engine device PDO 구조와 exported C ABI  
   byte layout 또는 symbol 변경은 hardware와 기존 application을 동시에 깨뜨릴 수 있다.

3. UTM SafetyMonitor/StopLatch/state transition  
   E-stop, communication fault, overload, limit, ACK 정책이 결합돼 있다.

4. CoordinateController/MotionOutputArbiter sign 및 단위 변환  
   작은 변경도 실제 이동 방향, travel limit, position completion을 바꾼다.

5. ADC DSP/zero/calibration pipeline  
   filter 순서, zero 기준, calibration scale을 변경하면 force control과 overload가 함께 달라진다.

---

## 25. Hardware Validation Remaining

실제 1000 N/2000 N UTM 조립 후 최소 다음을 검증해야 한다.

### 방향과 좌표

- servo positive/negative direction
- machine coordinate 증가 방향
- force direction sign
- compression polarity
- tension polarity
- position zero와 restart 동작
- encoder/extensometer direction

### Safety

- E-STOP hardware chain
- upper/lower limit 각각의 방향성 차단
- external STOP
- servo fault
- EtherCAT cable disconnect
- overload threshold와 stop distance
- recovery 후 자동 motion 미재개
- ACK/reset 조건
- STO 입력/servo disable 확인

### Motion

- Jog 양방향
- Absolute
- Incremental
- Velocity
- Move To Force overshoot
- Hold Force 안정도와 hunting
- timeout/max travel
- motion completion tolerance
- sequence 전체 step
- External GO/STOP

### Timing/통신

- 2 ms 평균 주기
- worst-case jitter
- recovery 중 jitter
- 장시간 WKC 안정도
- protocol 10 Hz 부하 동시 실행
- Windows cable disconnect/reconnect
- slow client/backpressure
- malformed client traffic

### Force/Compliance

- force zero drift
- calibration scale linearity
- 1000 N/2000 N capacity별 overload
- compression/tension hysteresis
- jig/loadcell/machine compliance 반복성
- endpoint out-of-range 표시
- signed curve
- temperature/time drift
- rigid-reference 재설치 repeatability

### Recording/Data

- local sample rate
- raw/corrected 동시 보존
- power loss 중 data integrity
- abort 결과 확정
- reconnect 후 testId 조회
- full data chunk 전송
- interrupted transfer recovery
- Windows 분석 결과 재현성

---

## 26. Machine Compliance Auto Calibration Readiness

### 현재 존재 여부

**NOT IMPLEMENTED**

다음 기능은 없다.

- servo-driven automatic compliance calibration
- compression calibration state machine
- tension calibration state machine
- maximum calibration force
- calibration speed/fine speed workflow
- force-step generation
- stabilization detector
- automatic point capture
- automatic return
- calibration-specific safety abort state
- resumable calibration session

현재 기능은 manual zero capture, point capture/edit, curve validation/save뿐이다.

### 기존 기능 재사용 가능성

재사용 가능:

- `DaoUtm_MoveToForce`
- `DaoUtm_HoldForce`
- `DaoUtm_StopMotion`
- raw measured force
- raw test position
- UTM Safety
- stop latch
- max travel/timeout
- compliance curve validation/profile save

그대로 재사용하기 어려운 부분:

- 각 force 단계의 stabilization 판단
- automatic point capture
- compression/tension plan
- calibration-specific progress/state/error
- safe return-to-start
- partial curve 처리
- UI cancel/retry
- remote status/event

일반 Sequencer만으로는 도달 후 안정화하고 raw position을 원자적으로 capture하는 semantics가 부족하다. 별도의 calibration state machine이 적합하다.

### 최소 변경 모듈

권장 최소 범위:

- 새 compliance auto-calibration state-machine 파일
- `UtmUiController.h/.cpp`
- `MainWindow.h/.cpp`
- `MachineProfile.h/.cpp` — speed, maximum force, step, stabilization 조건을 저장한다면 필요
- `CMakeLists.txt`
- 새 offline state-machine/profile test

Protocol에서도 제어하려면 추가로 다음이 필요하다.

- protocol calibration message/payload 또는 기존 command extension
- production service binding
- progress/event codec/test

### Basic Engine 변경

필요 없음. 기존 servo/force/runtime 기능으로 충분하다.

### UTM Engine ABI 변경

필수는 아니다.

Application/service state machine이 기존 `DaoUtm_*` motion/runtime API를 사용하면 ABI 변경 없이 구현할 수 있다. 다만 다음이 필요하면 additive UTM API가 권장된다.

- 2 ms 경계에서 안정화와 capture를 원자적으로 처리
- calibration state를 public runtime으로 노출
- local/remote command arbitration에 calibration을 정식 source로 포함
- disconnect와 fault 상황에서 일관된 state 관리

기존 ABI를 깨뜨리는 변경은 필요 없다. 필요하더라도 V7 runtime 또는 신규 `DaoUtm_ComplianceCalibration*` 같은 additive 방식이어야 한다.

---

## 27. Recommended Next Development Step

가장 먼저 Protocol V1의 production integration보다도 공통 기반 위험을 작은 범위에서 정리하는 것이 좋다.

권장 순서:

1. `SnapshotMailbox`를 data-race 없는 bounded snapshot 방식으로 교체하고 TSAN/offline stress test 추가
2. Protocol service와 UTM의 명시적 adapter 계층 설계
3. 실제 local test recorder 구현
4. UTM immutable sequence recipe와 Protocol LOAD/COMMIT/START 연결
5. testId 기반 local result store 및 BEGIN/CHUNK/END 연결
6. 그 후 Machine Compliance Auto Calibration state machine 구현
7. 실제 장비에서 polarity/safety/force-control 검증 후 자동 calibration 활성화

Auto Calibration을 먼저 구현할 경우에도 force/coordinate polarity 검증 없이 실제 servo 자동 이동을 허용해서는 안 된다.

---

# 최종 상태표

## A. IMPLEMENTED & REAL HARDWARE VERIFIED

- EtherCAT NIC open 및 4-slave scan
- DAO ADC, LS L7NH, FASTECH IO, CNT02 detection
- PDO mapping
- SAFE-OP/OP 진입
- Expected/Actual WKC 12
- cyclic process-data communication
- Servo ON
- Jog positive/negative 및 Stop
- slave SAFE-OP error ACK/reconfigure/OP recovery
- recovery 후 motion automatic resume disabled
- ADC PDO/filter application
- IO/Encoder slave presence 및 OP

## B. IMPLEMENTED & SOFTWARE TESTED ONLY

- Machine profile validation/atomic save/restart
- force display unit/profile lifecycle
- Setup persistence
- UI Main-tab startup policy
- communication fault policy unit test
- Compliance fixed LUT
- signed interpolation
- duplicate rejection
- endpoint clamp/out-of-range
- raw-minus-compensation calculation
- Compliance manual UI and profile persistence
- Protocol header/codec
- TCP fragmentation/coalescing parser
- HELLO/version handling
- 10 Hz LIVE_DATA scheduling
- ACK/NACK requestId matching
- mock disconnect/reconnect status
- test-data chunk codec

## C. PARTIALLY IMPLEMENTED

- Protocol production service integration
- REMOTE command source binding
- Protocol Recipe→Sequencer binding
- Protocol event publication
- local test recording
- GET_TEST_DATA actual file/result transfer
- recording metadata
- compliance protocol population from real UTM runtime
- ADC firmware stall/recovery diagnostics exposure
- end-to-end Windows reconnect during real test
- compliance profile/UI synchronization의 일부 경로
- real-time lock/contention 검증
- Force Motion hardware validation
- Sequencer hardware validation
- IO/Encoder full functional validation

## D. NOT IMPLEMENTED

- Windows Monitoring UI/client
- Machine Compliance Auto Calibration
- automatic compression calibration
- automatic tension calibration
- separate Compression/Tension compliance curves
- automatic force-step/stabilization/capture/return
- production local sample/result file recorder
- test-data transfer resume
- report generation
- HOLD_FORCE PID
- TLS/Auth — V1 의도적 범위 제외

---

# 마지막 8개 질문에 대한 명확한 답

## 1. 현재 Machine Compliance 기능은 정확히 어디까지 구현되어 있는가?

최대 64-point signed piecewise-linear LUT, validation/sorting/duplicate reject, endpoint clamp와 out-of-range flag, `corrected=raw-compensation`, manual capture/edit UI, QSaveFile profile 저장/복원까지 구현되어 있다. Control에는 적용되지 않고 UI/analysis/protocol payload 구조에만 사용된다. 실제 recorder 및 production protocol publisher는 아직 연결되지 않았다.

## 2. 실제 Servo를 움직여 Compliance Curve를 자동 생성하는 Machine Compliance Auto Calibration은 현재 구현되어 있는가?

**아니다. NOT IMPLEMENTED이다.**

## 3. 현재 Compression / Tension Compliance Curve가 별도로 존재하는가?

**아니다.** 하나의 공통 signed force/deformation curve를 사용한다.

## 4. 현재 Motion/Safety 구조를 재사용하여 Auto Calibration을 구현할 수 있는가?

**가능하다.** Move To Force, Hold Force, Stop, measured force, raw test position, Safety, timeout/max travel을 재사용할 수 있다. 다만 단계 진행·안정화·자동 capture·return을 담당하는 별도 calibration state machine이 필요하다.

## 5. Auto Calibration 구현 시 변경해야 할 최소 모듈/파일은 무엇인가?

- 신규 compliance auto-calibration state machine
- `UtmUiController.h/.cpp`
- `MainWindow.h/.cpp`
- 설정을 영속화한다면 `MachineProfile.h/.cpp`
- `CMakeLists.txt`
- 관련 offline/unit tests

Remote 지원까지 포함하면 protocol service binding과 event/payload 파일도 필요하다.

## 6. Basic EtherCAT Engine Public ABI 변경이 필요한가?

**필요 없다.** 기존 servo, force runtime, stop API로 구현 가능하다.

## 7. UTM Engine Public ABI 변경이 필요한가?

**필수는 아니다.** Application-level state machine으로 구현하면 기존 ABI로 가능하다. 원자적 capture, public calibration runtime, remote arbitration을 정식 지원하려면 기존 ABI를 깨지 않는 additive API 추가가 권장된다.

## 8. 현재 프로젝트에서 다음 작업자가 가장 조심해야 할 코드 영역 5개는 무엇인가?

1. EtherCAT 2 ms cyclic/PDO/recovery
2. Basic Engine device layout와 exported ABI
3. UTM SafetyMonitor/StopLatch/state transition
4. Coordinate sign/unit conversion과 MotionOutputArbiter
5. ADC DSP/zero/calibration pipeline

이 다섯 영역은 실제 servo 방향, 하중 polarity, 안전 정지, WKC recovery, force-control 결과에 직접 영향을 준다.
