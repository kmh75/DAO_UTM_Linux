# DAO UTM Protocol V1 Production Monitoring Integration Report

## 1. 결과와 변경 파일

기준 커밋은 `caf29ae`이다. Protocol V1 wire format과 UTM RuntimeV1~V6는 변경하지 않고 production `dao_utm_ui`에 monitoring-only TCP server를 연결했다.

- `CMakeLists.txt`: service library/test target 추가, production UI의 `dao_protocol` 간접 link
- `protocol/include/DaoProtocolV1.h`: reader/writer data race가 없는 service-layer snapshot mailbox
- `protocol/include/DaoProtocolServer.h`, `protocol/src/DaoProtocolServer.cpp`: 실제 listen 진단, timeout, fail-closed, single-client, shutdown/send-failure 정책
- `ui/include/UtmProtocolService.h`, `ui/src/UtmProtocolService.cpp`: RuntimeV6/compliance adapter와 service lifecycle
- `ui/include/UtmUiController.h`, `ui/src/UtmUiController.cpp`: hardware runtime publish 및 shutdown 순서
- `app/dao_utm_ui/main.cpp`: `--hardware`에서 monitoring service enable
- `app/protocol_test_client/main.cpp`: fail-closed 기대값 반영
- `app/protocol_service_test/main.cpp`: deterministic production service tests
- 이 문서

## 2. ProtocolService 구조

`UtmProtocolService`는 UI widget과 분리된 application/service layer 객체이다. 공개 동작은 `Start()`, `Stop()`, `IsRunning()`, `IsListening()`, `IsClientConnected()`, `Port()`, `LastError()`, `PublishCurrentRuntime()`이다.

데이터 경로는 다음과 같다.

`UTM RuntimeStore → DaoUtm_GetRuntimeV6 → UtmUiController 40 ms poll → UtmProtocolService fixed snapshot → ProtocolServer worker → TCP 45550`

production UI는 `dao_utm_protocol_service`를 link하고, 이 library가 `dao_protocol`과 기존 compliance library를 link한다. 기존 protocol test target도 유지했다.

## 3. Production lifecycle과 listen 상태

V0.1은 offline mock 노출을 피하기 위해 explicit `--hardware` 모드에서만 server를 enable한다. Server start는 EtherCAT connect와 독립적이므로 hardware mode에서 EtherCAT 연결 성공 전에도 monitoring endpoint와 진단을 사용할 수 있다. 기본 설정은 빈 bind address(`INADDR_ANY`, `0.0.0.0`)와 TCP port `45550`이다. `ProtocolServerConfig`에서 향후 IPv4 monitoring NIC 주소와 port를 지정할 수 있다.

`Start()`가 listener socket 생성, bind, listen을 호출한 뒤에만 `true`를 반환한다. 따라서 worker 생성 성공만을 뜻하지 않는다. `Listening()`, 실제 bound port(테스트의 ephemeral port 포함), `ServerError::{Socket, InvalidBindAddress, Bind, Listen, Thread}`를 조회할 수 있다. Bind 충돌은 `Start()==false`, `LastError()==Bind`로 동기 보고된다.

## 4. Runtime/STATUS/LIVE mapping

STATUS와 LIVE는 한 번의 `UtmRuntimeInfoV6` getter 결과 및 그 결과로 계산한 compliance runtime으로 하나의 `MonitoringSnapshot`을 만든다. 두 message가 서로 다른 runtime 시점에서 조립되지 않는다.

### MACHINE_STATUS (24 bytes)

| Protocol field | Production source |
|---|---|
| `testId` | `sequence.recordingSessionId` |
| `machineState` | `runtime...runtime.machineState` |
| `sequenceState` | `sequence.sequenceState` |
| `currentStep` | `sequence.currentStepIndex` |
| `testRunning` | `sequence.sequenceRunning != 0` |

`recordingSessionId`는 V0.1의 process-local 임시 ID이며 영속 Test ID가 아니다.

### LIVE_DATA (76 bytes)

| Protocol field | Production source/meaning |
|---|---|
| `timestampUs` | `publishedTimestampNs / 1000`; TCP send 시간이 아님 |
| `testId` | 임시 `recordingSessionId` |
| `forceN` | canonical `UtmInputSnapshot.forceN`; display smoothing 값이 아님 |
| `rawDisplacementMm` | `UtmGeneralMotionRuntimeInfo.testPositionMm`; `machinePositionMm`가 아님 |
| `complianceCompensationMm` | 기존 directional/profile `complianceRuntime()` 결과 |
| `correctedDisplacementMm` | 동일 compliance 결과의 corrected displacement |
| `extensometerMm` | calibrated `input.encoderPosition` |
| state/step/running | STATUS와 동일 snapshot의 실제 runtime enum/value |

`publishedTimestampNs != 0`, initialized, control loop running 조건을 만족하기 전에는 snapshot 자체를 publish하지 않는다. 따라서 startup 직후 HELLO_ACK는 가능하지만 가짜 0 STATUS/LIVE는 전송하지 않는다. LIVE scheduler는 약 100 ms마다 최신 snapshot만 확인한다. 지연 후 deadline은 `now + 100 ms`로 재설정하여 catch-up burst를 만들지 않고, 이전에 보낸 source timestamp와 같으면 재전송하지 않는다.

## 5. sampleFlags

기존 `TestSampleFlag`만 사용한다.

- `0x01 ComplianceEnabled`: 현재 compliance enabled
- `0x02 ComplianceOutOfRange`: enabled이지만 calibration range 밖
- `0x04 ForceValid`: engine force-valid이며 finite
- `0x08 DisplacementValid`: test position zero와 servo position이 valid이며 finite
- `0x10 ExtensometerValid`: encoder present/valid이며 calibrated position이 finite

새 bit는 추가하지 않았다. Invalid encoder 값은 field와 별개로 `0x10`이 clear되어 구분된다.

## 6. Snapshot race 해결

기존 two-slot mailbox는 reader가 copy하는 동안 writer가 같은 slot으로 두 번 돌아와 덮을 수 있었다. 이를 service-layer의 짧은 mutex 보호 fixed-size copy로 교체하고 STATUS/LIVE/validity를 하나의 bundle로 publish한다. Lock 구간에는 socket send, serialization, compliance 계산이 없다.

이 mailbox writer는 40 ms application poll이고 protocol worker가 reader이다. 2 ms control loop는 기존 RuntimeStore publish만 수행하며 protocol mutex, socket/file I/O, allocation이 추가되지 않았다. 동시 writer/reader invariant stress test를 추가했다.

## 7. Command policy와 session isolation

Handler가 없는 known control command는 더 이상 자동 ACK되지 않고 `COMMAND_NACK / UnsupportedMessage`가 된다. Production service도 아래 여섯 command handler를 명시적으로 `UnsupportedMessage`로 reject한다.

- `LOAD_RECIPE`, `COMMIT_RECIPE`, `START_TEST`
- `STOP_TEST`, `ACK_RESET`, `GET_TEST_DATA`

HELLO와 HEARTBEAT만 정상 처리한다. Production handler는 UTM command API reference를 갖지 않으므로 monitoring command/disconnect/timeout 경로에서 `StopSequence`, `AcknowledgeStop`, engine disconnect 또는 Motion을 호출할 수 없다.

## 8. Heartbeat, timeout, send failure, single client

- HELLO timeout: 5 seconds
- Handshake 이후 inbound client idle timeout: 10 seconds
- Windows heartbeat 권고: 2 seconds
- LIVE target cadence: fresh source data가 있을 때 약 10 Hz

Timeout은 해당 client socket만 닫고 listener는 유지한다. Nonblocking send의 partial send는 계속 전송하지만 `EINTR` 이외의 error 및 `EAGAIN/EWOULDBLOCK`은 session failure로 처리해 client를 닫는다. HELLO_ACK, HEARTBEAT, STATUS, LIVE, event에 동일 정책을 쓴다. 느린 client를 위한 backpressure queue는 만들지 않았다.

V0.1은 single active client이다. Active session 중 listener도 함께 poll하며 두 번째 connection을 accept한 즉시 shutdown/close한다. 첫 session 종료 후 listener는 다음 client를 받는다.

## 9. Linux app shutdown

`UtmUiController::orderlyShutdown()`과 destructor는 protocol timer publication을 멈추고 service `Stop()`을 먼저 호출한다. `Stop()`은 running/listening을 clear하고 listener와 client socket을 shutdown/close한 뒤 worker를 join한다. 그 다음 기존 `DaoUtm_Disconnect()` 또는 기존 destructor cleanup이 실행된다. 연결된 Windows client가 있어도 poll/recv 대기 때문에 app shutdown이 hang하지 않는다.

## 10. Tests and validation

`dao_protocol_production_service`가 다음을 결정적으로 검증한다.

- start/stop, actual listening, bind failure, duplicate/repeated start/stop
- HELLO success/timeout, HEARTBEAT, idle timeout
- 여섯 monitoring-only command NACK와 handler-missing fail-closed
- actual STATUS/LIVE adapter field mapping
- STATUS 24 bytes, LIVE 76 bytes, big-endian regression
- mutex snapshot concurrent invariant
- initial invalid runtime withholding
- second-client immediate close
- RST/send failure 후 listener/reconnect
- stale source timestamp 단일 송신(무한 replay 없음), scheduler no-catch-up 구현
- disconnect path의 UTM command call count 0 (server/service는 engine reference 없음)

Debug:

```text
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 10 (2.0 s)
```

Release:

```text
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j2
ctest --test-dir build-release --output-on-failure
100% tests passed, 0 tests failed out of 10 (2.0 s)
```

Production executable localhost read-only test:

```text
dao_utm_ui --hardware --no-auto-connect --smoke-test
LISTEN 0 1 0.0.0.0:45550 0.0.0.0:*
HELLO_ACK: 44 41 55 54 00 01 00 02 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 2a
```

EtherCAT connect와 hardware Motion은 수행하지 않았다.

## 11. ABI/wire compatibility

`utm/include`, UTM RuntimeV1~V6 definitions, engine sources, exported version scripts는 변경하지 않았다. 따라서 RuntimeV1~V6 layout와 public engine ABI 의미는 기준 커밋과 동일하다. Protocol header는 24 bytes이고 message IDs, header layout, STATUS 24-byte payload, LIVE 76-byte payload 및 big-endian encoding도 변경하지 않았다. 추가된 server config/diagnostics/service API는 내부 static/application layer의 additive change이다.

## 12. 실제 LAN 시험 절차

Linux monitoring IP는 hardcode하지 않는다. Windows와 연결된 monitoring NIC에서 확인한다.

```bash
ip -br -4 addr
ip route get <WINDOWS_IP>
ss -ltnp '( sport = :45550 )'
```

기대 listen은 `0.0.0.0:45550`이다. Host firewall이 활성화되어 있으면 monitoring subnet/interface에 한정해 TCP 45550 inbound를 허용해야 한다. 예를 들어 UFW 사용 환경에서는 현장 subnet을 확인한 뒤 `sudo ufw allow from <MONITORING_SUBNET/CIDR> to any port 45550 proto tcp`를 사용한다. 배포 환경의 nftables/firewalld 정책이 있으면 해당 관리 체계를 사용하며 무조건 firewall을 disable하지 않는다.

Windows 연결 확인:

```powershell
Test-NetConnection -ComputerName <LINUX_MONITORING_IP> -Port 45550
```

Windows client 최초 frame은 payload 없는 24-byte HELLO이다. 예시에서 requestId는 42이다.

```text
44 41 55 54  00 01  00 01  00 00 00 00  00 00 00 00
00 00 00 00  00 00 00 2A
```

서버는 HELLO_ACK(type `0x0002`, 같은 requestId)를 먼저 보내고, valid production runtime이 준비되어 있으면 MACHINE_STATUS(type `0x000A`, payload 24)를 보낸다. 이후 fresh source snapshot이 있을 때 LIVE_DATA(type `0x000B`, payload 76)를 약 10 Hz로 보낸다. Windows는 2초마다 HEARTBEAT를 보내야 하며 10초 동안 inbound frame이 없으면 server가 session을 닫는다.

## 13. Known limitations / Windows V0.1 확정 정보

- Monitoring은 `--hardware`에서만 자동 enable된다. Offline simulation은 전송하지 않는다.
- Listen은 IPv4 `INADDR_ANY`; config boundary는 특정 IPv4/NIC bind를 지원하지만 production CLI/UI 설정은 아직 없다.
- 한 번에 client 한 대만 허용한다. 두 번째 client는 즉시 close된다.
- Persistent Test ID가 없어 V0.1은 process-local `recordingSessionId`를 사용한다.
- Source timestamp가 정지하면 동일 LIVE를 반복 송신하지 않는다. Windows는 timestamp를 source freshness 기준으로 사용해야 한다.
- Recipe/control/result/recorder/security/discovery/Protocol V2는 이번 범위에 없다.
- Linux UI에 별도 status widget은 추가하지 않았다. 현재 상태는 startup log와 service diagnostics API로 확인한다.
- Windows V0.1은 network byte order, 24-byte header, port 45550, HELLO-first, 5초 HELLO timeout, 2초 heartbeat, 10초 idle timeout, STATUS 24 bytes, LIVE 76 bytes, 최대 10 Hz fresh-only, control NACK, single-client 정책을 구현 기준으로 삼는다.

Git commit 후보는 하나이다: `Integrate Protocol V1 monitoring server into production UTM UI`. Push는 수행하지 않았다.
