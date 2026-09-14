# DAO UTM Machine Compliance Auto Calibration 작업완료 보고서

작성일: 2026-09-07

## 1. 변경 파일 목록

이번 작업에서 직접 추가 또는 수정한 파일은 15개다.

1. `CMakeLists.txt`
2. `compliance/include/UtmComplianceAutoCalibration.h`
3. `compliance/src/UtmComplianceAutoCalibration.cpp`
4. `compliance/include/UtmComplianceCompensation.h`
5. `app/compliance_auto_calibration_test/main.cpp`
6. `app/compliance_profile_test/main.cpp`
7. `ui/include/MachineProfile.h`
8. `ui/src/MachineProfile.cpp`
9. `ui/include/UtmUiController.h`
10. `ui/src/UtmUiController.cpp`
11. `ui/include/MainWindow.h`
12. `ui/src/MainWindow.cpp`
13. `protocol/include/DaoProtocolV1.h`
14. `docs/DAO_UTM_Compliance_Auto_Calibration_Architecture.md`
15. `docs/DAO_UTM_Compliance_Auto_Calibration_Report.md`

작업 시작 전부터 존재하던 다른 미커밋 파일과 build 산출물은 이 목록에 포함하지 않았다.

## 2. 기존 Compliance 구조 재사용

기존 `UtmComplianceCompensation`의 64-point 고정 배열, configure-time sorting, finite/duplicate validation, signed piecewise-linear interpolation, endpoint clamp, curve version, enabled 상태, 그리고 `raw - compensation` 공식을 그대로 유지했다.

자동교정 결과는 동일한 `CompliancePoint { forceN, deformationMm }`로 생성되므로 별도 fitting이나 다른 correction 공식을 도입하지 않았다. 기존 수동 CAPTURE ZERO, ADD/DELETE/CLEAR, APPLY/SAVE 편집기도 삭제하지 않았다.

Corrected displacement는 application analysis 경로에만 남아 있다. 이번 기능에서 Motion 또는 Safety 입력으로 사용하지 않는다. Extensometer도 변경하지 않았다.

## 3. Compression/Tension Curve 저장 구조

`MachineProfile`에 독립적인 두 curve를 추가했다.

```text
compressionCompliance:
  enabled
  version
  points

tensionCompliance:
  enabled
  version
  points
```

JSON에는 `machineCompliance.compression`과 `machineCompliance.tension`으로 저장한다. 각 방향은 enabled, version, points를 독립적으로 갖는다. 유효성과 force range는 기존 curve validator와 정렬된 endpoint로 결정된다.

현재 시험 Recipe에는 권위 있는 Compression/Tension mode가 없으므로 자동판별하지 않았다. `machineCompliance.activeMode`를 명시적으로 선택하며, 자동교정 curve 저장 시 해당 mode가 active mode가 된다. 향후 Recipe에 명시적 test/compliance mode를 추가하는 것이 권장된다.

## 4. Auto Calibration State Machine

새 `UtmComplianceAutoCalibration`은 Qt나 Engine 내부에 의존하지 않는 고정 메모리 상태기다.

상태:

- IDLE
- VALIDATING
- ZEROING_FORCE
- CAPTURING_REFERENCE
- PRECHECK_APPROACH
- PRECHECK_STABILIZING
- WAITING_FULL_START
- APPROACHING_TARGET
- FINE_APPROACH
- STABILIZING
- CAPTURING_POINT
- NEXT_POINT
- RELEASING_FORCE
- RETURNING
- COMPLETE_PENDING_SAVE
- ABORTED
- FAULTED

상태기는 긴 loop나 blocking wait를 하지 않는다. runtime snapshot을 받을 때 한 단계씩 갱신하고 기존 UTM public motion 요청을 하나씩 출력한다. Point와 target은 `std::array<...,64>`에 보관한다.

## 5. Pre-check 동작

Pre-check는 생략할 수 없다.

1. 기존 Force Zero 요청
2. Force Zero capture가 실제 시작된 것을 관찰
3. capture 종료와 valid 상태 확인
4. calibration start machine/test position 저장
5. 독립 compliance reference machine position 저장
6. 저속 Move To Force
7. force polarity 확인
8. machine position 방향 확인
9. 지정 travel 이후 minimum force rise 확인
10. pre-check target 도달 후 Stop
11. tolerance 내 stabilization
12. `WAITING_FULL_START`

Pre-check 성공 후 사용자가 별도의 START FULL CALIBRATION 버튼을 눌러야 본 교정이 시작된다.

Pre-check force는 설정값이 0이면 `min(5 N, maximumForce*0.05)`다. Pre-check maximum travel과 force-rise 조건은 별도 설정 구조를 가진다.

## 6. Maximum Force 보호

시작 validation은 다음 한계를 계산한다.

```text
allowedMaximum = min(loadcellCapacityN * 0.90,
                     enabled configured overload limit)
```

사용자 Maximum Calibration Force가 이를 초과하면 시작을 거부한다. 기존 overload가 disabled이면 loadcell 90% 제한만 적용한다. runtime에서는 measured-force magnitude가 allowed maximum을 넘거나 기존 UTM overload stop이 발생하면 즉시 Stop 요청과 FAULTED 전이를 수행한다.

Calibration guard는 기존 UTM Safety보다 상위에서 servo를 직접 제어하지 않는다. 기존 UTM overload 판단은 계속 2 ms 제어 경로에서 우선 수행된다.

## 7. Maximum Travel 보호

교정 시작 시 machine position을 `calibrationStartPositionMm`으로 저장한다. 모든 active phase에서 다음을 검사한다.

```text
abs(currentMachinePositionMm - calibrationStartPositionMm)
```

Maximum Calibration Travel을 넘으면 즉시 기존 UTM Stop을 요청하고 FAULTED가 된다. Pre-check에는 더 작은 별도 travel limit을 적용할 수 있다. 실패 후 return은 수행하지 않는다.

## 8. Force polarity 검증

새로운 sign convention은 만들지 않았다. 기존 UTM은 UP-positive logical force를 만들기 위해 `forceDirectionSign`을 사용한다. UI Controller는 선택한 UTM motion direction과 기존 `forceDirectionSign`으로 기대 raw measured-force sign을 계산한다.

- Compression: `expectedForceSign = -forceDirectionSign`
- Tension: `expectedForceSign = +forceDirectionSign`

Pre-check에서 force가 tolerance를 넘는 반대 부호로 증가하면 `ForcePolarityMismatch`로 중단한다. 실제 loadcell 장착 polarity는 반드시 hardware에서 검증해야 한다.

## 9. Force rise 검증

Pre-check에서 이동량이 `forceRiseTravelThresholdMm`에 도달했는데 시작 대비 force 증가가 `minimumForceRiseN`보다 작으면 fixture 미접촉 또는 잘못된 설치로 판단하여 중단한다.

이 threshold는 state-machine configuration에 있으며 공격적인 고정 hardware 상수로 숨기지 않았다. Profile 기본값은 향후 실제 장비 검증 후 조정해야 한다.

## 10. Target Force 접근 방식

Target은 0부터 Force Step으로 생성하며 나머지가 생겨도 Maximum Force를 마지막에 정확히 포함한다. 64 points를 초과하는 구성은 reject한다.

Target별 속도 선택:

- Maximum의 70% 미만: Approach Speed
- 70% 이상 90% 미만: Calibration Speed
- 90% 이상: Fine Speed

각 접근은 기존 `DaoUtm_MoveToForce`를 사용한다. Calibration 코드에서 Basic Engine servo API 또는 PDO/raw velocity를 호출하지 않는다.

## 11. Stabilization / Average 방식

Target 도달 시 기존 UTM Stop을 요청하고 stabilization을 시작한다. Force가 signed target의 tolerance 밖으로 나가면 timer와 sample sum/count를 모두 reset한다.

설정 시간 동안 유효한 sample을 유지하면 다음 평균을 저장한다.

```text
capturedForceN = forceSum / sampleCount
capturedPositionMm = positionSum / sampleCount
deformationMm = capturedPositionMm - complianceReferencePositionMm
```

Force/deformation에 `abs()`를 적용하지 않는다. Application controller의 현재 runtime poll 주기에서 sample을 수집하며, Safety와 실제 Motion 제어는 기존 2 ms UTM 경로가 계속 담당한다.

## 12. 정상 Force Release / Return 방식

마지막 point가 성공한 정상 경로에서만 release를 수행한다.

1. Fine Speed로 loading 반대 방향의 기존 Move Velocity 요청
2. measured force가 near-zero tolerance에 들어오면 Stop
3. 저장한 calibration start test position으로 기존 Move Absolute 요청
4. motion complete 시 COMPLETE_PENDING_SAVE

Return은 machine position을 억지로 맞추기 위해 하중을 다시 가하지 않는다. Return 중 Safety/fault가 발생하면 즉시 중단한다.

## 13. Abort/Fault 동작

다음은 Stop 후 ABORTED 또는 FAULTED로 전이한다.

- User Abort
- E-STOP
- External STOP
- Upper/Lower Limit
- Servo Fault
- Communication invalid/recovering
- 기존 Overload
- calibration hard-force guard
- Motion failure
- Maximum Travel
- Force polarity mismatch
- Position direction mismatch
- Insufficient force rise
- Force Zero 실패/timeout

Abort/Fault에서는 auto return과 auto restart가 없다. Communication recovery 후에도 session은 재개되지 않는다. Calibration 코드는 latch를 ACK/reset하지 않는다.

## 14. 기존 Active Curve 보호 방식

State-machine points는 session 내부 고정 배열에만 저장된다. Start, pre-check, 각 point capture, abort, 정상 completion은 `MachineProfile`이나 active evaluator를 변경하지 않는다.

정상 completion 후에도 결과는 Pending이다. DISCARD는 pending만 제거한다. SAVE 또는 SAVE & ENABLE에서 candidate profile을 만들고 validate/atomic save가 성공한 후에만 active profile/evaluator를 교체한다. 저장 실패 시 기존 curve를 유지한다.

## 15. Profile backward compatibility

기존 schemaVersion 1을 유지했다. 기존 `complianceCompensation` object도 계속 load/save한다.

이전 profile에 `machineCompliance`가 없으면:

- legacy single curve는 기존 필드에 그대로 유지
- Compression/Tension points는 empty
- Compression/Tension enabled는 false
- legacy 데이터를 양 방향으로 복제하거나 자동 활성화하지 않음
- auto-calibration 기본 maximum/step/tolerance는 기존 capacity/overload 범위에서 보수적으로 초기화

새 방향 curve의 version은 해당 방향 SAVE 성공 시에만 증가한다. 반대 방향 version과 points는 변경하지 않는다.

## 16. Main/Calibration UI 변경

Main header에는 작은 상태만 추가했다.

```text
COMPLIANCE C: ON/OFF / T: ON/OFF
```

Main에서 curve 편집 또는 calibration 시작은 제공하지 않는다.

Calibration 영역에는 다음을 추가했다.

- loadcell capacity read-only 표시
- maximum force, force step
- approach/calibration/fine/return speed
- maximum travel, tolerance, stabilization time
- START COMPRESSION PRE-CHECK
- START TENSION PRE-CHECK
- START FULL CALIBRATION
- 항상 보이는 ABORT CALIBRATION
- DISCARD, SAVE CURVE, SAVE & ENABLE
- stage, target/current force, reference/current position, deformation, captured points, travel, allowed maximum, abort reason 표시

시작 dialog는 fixture/direction/loadcell/jig/moving-area 확인을 표시하며 CANCEL을 default/escape button으로 둔다. 기존 manual curve editor는 그대로 유지했다.

## 17. Protocol 영향

Protocol V1 frame, Message ID, `LIVE_DATA` byte layout, raw/compliance/corrected fields는 변경하지 않았다.

Future metadata를 위한 additive C++ 구조만 준비했다.

- compliance mode
- compression curve version
- tension curve version
- active curve version
- enabled

이를 V1 wire payload에 직렬화하지 않았으므로 Windows V1 client compatibility는 유지된다. 실제 Protocol↔UTM production binding은 기존과 같이 후속 작업이다.

## 18. Basic Engine ABI 변경 여부

변경 없음.

- `engine/` diff 없음
- export된 Basic Engine symbol: 66개
- 새 `DaoEngine_*` API 없음

## 19. UTM Engine ABI 변경 여부

변경 없음.

- `utm/` diff 없음
- export된 UTM Engine symbol: 54개
- 새 `DaoUtm_*` API 없음

자동교정은 기존 public API만 사용하는 application-level 기능이다.

## 20. Motion/Safety/Servo/ADC 변경 여부

- 기존 Motion 의미: 변경 없음
- 기존 Safety 의미/우선순위/latch: 변경 없음
- Servo driver/PDO: 변경 없음
- ADC DSP/filter/zero 알고리즘: 변경 없음
- EtherCAT Master/recovery: 변경 없음
- Sequencer: 변경 없음

Calibration state-machine은 Safety를 우회하거나 reset하지 않으며 corrected displacement를 control에 사용하지 않는다.

## 21. Clean Release Build 결과

명령:

```text
cmake --fresh -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j2
```

결과: 성공.

Qt UI, Basic/UTM shared libraries, master, compliance, protocol, mock 및 persistence test targets가 모두 Release로 build됐다.

## 22. CTest 전체 결과

명령:

```text
ctest --test-dir build-release --output-on-failure
```

결과: 8/8 passed, 0 failed.

일반 filesystem sandbox에서는 Protocol loopback bind/connect가 제한되어 해당 test가 connect 단계에서 실패했다. 실제 hardware 권한을 사용하지 않고 local loopback TCP만 허용한 실행에서 전체 8개가 통과했다.

## 23. 새로 추가된 Test 목록

### dao_compliance_auto_calibration_offline

- Compression/Tension target generation
- non-divisible final maximum inclusion
- loadcell 90% limit
- overload limit precedence
- Force Zero command/capture sequencing
- force/position polarity failure
- insufficient force rise
- maximum travel
- overload, E-STOP, limit, External STOP, Servo Fault, communication recovery abort
- User Abort
- pre-check pass and explicit full-start
- staged target commands
- stabilization average
- signed deformation capture
- normal release/return/complete pending
- active curve preservation on abort
- raw/corrected formula and extensometer non-application

### dao_compliance_directional_profile

- Compression/Tension independent points/enabled/version persistence
- restart/reload
- legacy single-curve backward compatibility
- no legacy duplication/activation
- pending state version preservation
- version increment on actual save
- opposite direction preservation

기존 compliance, protocol, force-unit/profile, setup persistence, production UI startup, communication policy tests도 함께 통과했다.

## 24. 실제 EtherCAT Hardware 미실행 확인

실제 EtherCAT hardware를 실행하지 않았다.

- `dao_utm_ui --hardware` 미실행
- adapter open/scan/OP 미실행
- Servo ON 미실행
- Jog/Motion 실제 명령 미실행
- sudo hardware test 미실행

수행한 것은 compile, pure offline state-machine/profile tests, Qt offscreen startup tests, local TCP mock test뿐이다.

## 25. 실제 UTM 조립 후 필수 검증

1. Compression/Tension servo direction과 machine-position sign
2. `forceDirectionSign`에 따른 raw force polarity
3. 저속 pre-check target 도달과 false polarity trip 방지
4. fixture 미접촉 force-rise guard threshold
5. loadcell 90% 및 기존 overload의 실제 stop latency/overshoot
6. maximum travel stop distance
7. approach/calibration/fine speed별 target overshoot
8. stabilization time, noise, creep, hysteresis와 평균 repeatability
9. Force Zero capture 시작/완료/실패/timeout
10. E-STOP, External STOP, 상·하 Limit, Servo Fault를 모든 phase에서 투입
11. EtherCAT TRANSIENT/RECOVERING/FAULT 및 cable disconnect
12. fault 후 auto restart와 auto return이 발생하지 않는지 확인
13. 정상 release 중 force가 안전하게 감소하는지 확인
14. 정상 return target의 test/machine coordinate 일치
15. abort 후 manual recovery 가능 여부
16. pending 결과가 active curve를 변경하지 않는지 확인
17. SAVE/SAVE & ENABLE 후 방향별 version 및 QSaveFile restart
18. Compression/Tension curve의 signed interpolation과 clamp
19. corrected displacement가 Motion/Safety/Sequencer에 들어가지 않는지 실기 telemetry 확인
20. Extensometer 값 불변
21. UI Controller 40 ms 상태 갱신과 기존 2 ms Safety/Motion timing의 독립성
22. 장시간 반복 calibration 중 drift와 thermal effect

## 결론

Machine Compliance Auto Calibration은 기존 UTM Motion/Safety/Public API를 재사용하는 application-level 안전 상태기로 구현됐다. 방향별 curve, mandatory pre-check와 두 번째 사용자 승인, force/travel/polarity/rise guard, signed stable averaging, 정상 경로 전용 release/return, pending-save/atomic commit 정책을 갖춘다. Software build와 offline tests는 완료됐지만, 실제 자동 motion 활성화 전에는 위 hardware polarity·Safety·overshoot·jitter 검증이 필수다.
