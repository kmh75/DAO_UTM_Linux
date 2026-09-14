# DAO UTM Linux ↔ Windows Monitoring Communication 현황 감사 및 구현 설계

- 감사 기준일: 2026-09-07
- 기준: 저장소의 현재 실제 코드 우선, 기존 문서는 보조 자료
- 작업 성격: 구현 현황 감사 및 후속 Architecture 제안
- 이번 작업: 신규 보고서 작성만 수행
- 코드 변경·리팩터링·format 변경·build/test·network/hardware 실행·commit·push: 수행하지 않음

## 1. Executive Summary

현재 Protocol V1은 **독립적인 protocol library와 C++ mock integration test 수준**이다. Fixed binary framing, stream decoder, Linux TCP listener, HELLO/ACK, heartbeat echo, status/live payload codec, generic command callback, event queue API, test-data chunk codec가 존재한다. 그러나 production `dao_utm_ui`는 `dao_protocol`을 링크하지 않고 `ProtocolServer`를 생성·시작하지 않는다. UTM runtime을 protocol snapshot으로 변환하는 adapter, protocol command를 UTM API/Sequencer에 연결하는 service, 실제 event publisher, local recorder/result store, GET_TEST_DATA service는 구현되지 않았다.

따라서 현재 상태는 다음과 같이 요약된다.

- Protocol codec/server foundation: IMPLEMENTED, OFFLINE/MOCK TESTED
- Production protocol integration: NOT IMPLEMENTED
- Live UTM publication: NOT IMPLEMENTED; mock values만 시험됨
- Remote recipe/command binding: NOT IMPLEMENTED; generic callback boundary만 존재
- Local recording: NOT IMPLEMENTED; Sequencer runtime flag/session counter만 존재
- Test data transfer: PARTIAL; opaque chunk codec만 존재
- Windows application: NOT IMPLEMENTED; C++ console mock만 존재
- SnapshotMailbox: 실제 C++ data race 가능, production 연결 전에 수정 REQUIRED

Windows UI/analysis skeleton 개발은 지금 시작할 수 있지만 실제 protocol contract를 고정하거나 production command/data 기능을 완료됐다고 전제해서는 안 된다. Linux 쪽 snapshot 안전화, production adapter, local recorder/result store, recipe codec/binding, event publisher, resumable data transfer 순으로 먼저 구현하는 것이 가장 작은 위험의 경로다.

## 2. 감사 범위와 확인된 구성

감사한 주요 경로:

- `protocol/include/DaoProtocolV1.h`
- `protocol/src/DaoProtocolV1.cpp`
- `protocol/include/DaoProtocolServer.h`
- `protocol/src/DaoProtocolServer.cpp`
- `app/protocol_test_client/main.cpp`
- `utm/include/DaoUtm.Types.h`
- `utm/include/DaoUtm.Engine.h`
- `utm/src/UtmEngineCore.cpp`
- `utm/src/UtmSequencer.cpp`
- `utm/src/UtmRuntimeStore.*`
- `ui/src/UtmUiController.cpp`
- `ui/src/MachineProfile.cpp`
- `app/dao_utm_ui/main.cpp`
- `CMakeLists.txt`

`dao_protocol`은 별도 static library이고 `dao_protocol_test_client`만 링크한다. Production `dao_utm_ui`의 link libraries는 `dao_utm_engine`, `dao_utm_compliance`, Qt Core/Gui/Widgets이며 `dao_protocol`이 없다. 저장소 전체에서 mock client 이외에 `ProtocolServer` 생성, `Start()`, `PublishLiveData()`, `PublishStatus()`, `SetCommandHandler()` 호출자는 없다.

## 3. Protocol V1 구현 현황 매트릭스

판정 정의:

- IMPLEMENTED: 해당 기능의 실질 동작 코드 존재
- PARTIAL: ID/codec/boundary 일부만 존재
- NOT IMPLEMENTED: 실제 기능 경로 없음
- CODE ONLY: 코드 존재, 실행 검증 근거 없음
- OFFLINE / MOCK TESTED: local C++ mock/loopback test에 포함
- PRODUCTION CONNECTED: production application과 실제 runtime에 연결
- REAL NETWORK TESTED: 별도 장비/실망 테스트 근거 존재
- UNKNOWN: 코드에서 시험 근거 확인 불가

| 항목 | 구현 상태 | 시험 상태 | 코드 기준 설명 |
|---|---|---|---|
| TCP Server | IMPLEMENTED | OFFLINE / MOCK TESTED | POSIX socket/poll 기반 `ProtocolServer` |
| Linux listener | IMPLEMENTED | OFFLINE / MOCK TESTED | `INADDR_ANY`, configurable port, backlog 1 |
| Windows client 대응 구조 | PARTIAL | OFFLINE / MOCK TESTED | C# client 없음; endian/framing은 재구현 가능 |
| Port | IMPLEMENTED | OFFLINE / MOCK TESTED | default 45550, mock는 45551 |
| Client 수 | IMPLEMENTED | OFFLINE / MOCK TESTED | 한 번에 1 client; disconnect 후 다음 client accept |
| HELLO | IMPLEMENTED | OFFLINE / MOCK TESTED | version 확인 및 handshake gate |
| HELLO_ACK | IMPLEMENTED | OFFLINE / MOCK TESTED | HELLO 성공 시 송신 |
| HEARTBEAT | IMPLEMENTED | OFFLINE / MOCK TESTED | request echo response; timeout/liveness policy 없음 |
| MACHINE_STATUS | PARTIAL | OFFLINE / MOCK TESTED | codec와 HELLO 직후 송신 있음; production UTM publisher 없음 |
| LIVE_DATA | PARTIAL | OFFLINE / MOCK TESTED | codec와 10 Hz scheduler 있음; production UTM publisher 없음 |
| LOAD_RECIPE | PARTIAL | CODE ONLY | enum/known dispatch와 generic callback뿐; payload/binding 없음 |
| COMMIT_RECIPE | PARTIAL | OFFLINE / MOCK TESTED | mock ACK 경로만; Sequencer binding 없음 |
| START_TEST | PARTIAL | OFFLINE / MOCK TESTED | mock NACK callback만; UTM start binding 없음 |
| STOP_TEST | PARTIAL | CODE ONLY | callback dispatch만 존재 |
| ACK_RESET | PARTIAL | CODE ONLY | callback dispatch만 존재 |
| COMMAND_ACK | IMPLEMENTED | OFFLINE / MOCK TESTED | generic response와 requestId 보존 |
| COMMAND_NACK | IMPLEMENTED | OFFLINE / MOCK TESTED | generic reason 2-byte payload와 requestId 보존 |
| TEST_STARTED | PARTIAL | CODE ONLY | enum/event queue type만 존재; source adapter/payload 없음 |
| STEP_CHANGED | PARTIAL | CODE ONLY | 동일 |
| TEST_COMPLETE | PARTIAL | CODE ONLY | 동일 |
| TEST_ABORTED | PARTIAL | CODE ONLY | 동일 |
| FAULT_EVENT | PARTIAL | CODE ONLY | 동일 |
| GET_TEST_DATA | PARTIAL | CODE ONLY | known command/callback만 존재; lookup/service 없음 |
| TEST_DATA_BEGIN | PARTIAL | CODE ONLY | enum만 존재; metadata codec 없음 |
| TEST_DATA_CHUNK | PARTIAL | OFFLINE / MOCK TESTED | opaque bytes chunk codec만 roundtrip 시험 |
| TEST_DATA_END | PARTIAL | CODE ONLY | enum만 존재; integrity summary codec 없음 |

어떤 항목도 PRODUCTION CONNECTED 또는 REAL NETWORK TESTED로 판정할 코드 근거가 없다.

## 4. TCP Server와 Frame 구조

### 4.1 역할과 thread

- Linux: TCP Server
- Windows 목표: TCP Client
- default port: 45550
- listen address: `INADDR_ANY`
- listener: `SOCK_STREAM | SOCK_NONBLOCK`
- accepted client: `accept4(..., SOCK_NONBLOCK)`
- client concurrency: 1
- Server는 자체 `std::thread`에서 accept/poll/recv/send/live scheduler/event drain을 실행한다.
- listener poll은 50 ms, connected client poll은 10 ms다.

현재 `Start()`는 thread 생성 성공을 반환할 뿐 bind/listen 완료를 호출자에게 동기적으로 보고하지 않는다. bind 실패 시 worker가 `running_=false`로 바꾸지만 structured startup error는 없다.

### 4.2 Header

Header는 24 bytes이며 native struct를 전송하지 않는다.

| Offset | Size | Field | Encoding |
|---:|---:|---|---|
| 0 | 4 | Magic | `0x44415554`, `DAUT` |
| 4 | 2 | Protocol Version | V1 |
| 6 | 2 | Message Type | unsigned integer |
| 8 | 4 | Flags | V1 reserved |
| 12 | 4 | Payload Length | maximum 1 MiB |
| 16 | 4 | Sequence Number | connection-local outbound counter |
| 20 | 4 | Request ID | command correlation |

모든 integer와 IEEE-754 double bit pattern은 big-endian으로 encode/decode한다. LIVE bool도 native bool이 아니라 32-bit 0/1로 encode된다.

### 4.3 Stream handling

- Split packet: internal byte vector에 누적 후 full header/payload가 될 때까지 보류
- Coalesced packet: 한 `Feed()`에서 여러 frame 방출
- Invalid magic/length: decoder를 malformed로 고정하고 server가 connection 종료
- Maximum payload: 1,048,576 bytes
- Unknown request type: HELLO 이후 generic `COMMAND_NACK/UnsupportedMessage`
- Version mismatch: HELLO에서 NACK
- Reconnect: client loop 종료 후 listener accept loop로 복귀; mailbox/server object 상태는 유지

제약:

- decoder buffer와 frame/payload가 `std::vector`를 사용하므로 network thread allocation이 발생한다. Control thread는 아니므로 허용 가능하나 memory-pressure limit와 per-connection buffering policy가 필요하다.
- malformed frame은 explicit NACK 없이 connection을 닫는다. 공격적/손상 client에 합리적이지만 diagnostic counter/log가 없다.
- nonblocking client socket에서 `SendFrame()`은 EAGAIN/PARTIAL backpressure queue를 관리하지 않는다. EAGAIN이면 false로 connection을 끊는다.
- outbound priority가 없다. bulk transfer가 추가되면 event/live starvation을 막아야 한다.
- sequence number 수신 검증, gap/duplicate 정책은 없다.
- HELLO 이후 frame별 version 일관성을 재검사하지 않는다.

### 4.4 C# 재구현 적합성

기본 framing은 C#에 적합하다. `BinaryPrimitives.Read/Write*BigEndian`, `BitConverter.Int64BitsToDouble`, `System.IO.Pipelines` 또는 `NetworkStream` 누적 parser로 그대로 구현할 수 있다. Header 크기와 fixed live/status payload도 명확하다.

고정 전 준비할 사항:

- 각 payload의 byte offset 표와 reserved field 규정
- invalid enum/bool/non-finite double validation
- HELLO capability/minor-version payload
- command-specific ACK/NACK detail schema
- framing/bulk transfer golden-vector 파일
- maximum recipe/result sizes와 string encoding(UTF-8) 규칙

## 5. LIVE_DATA 실제 상태

실제 encoded payload는 76 bytes다.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | timestampUs |
| 8 | 8 | testId |
| 16 | 8 | forceN |
| 24 | 8 | rawDisplacementMm |
| 32 | 8 | complianceCompensationMm |
| 40 | 8 | correctedDisplacementMm |
| 48 | 8 | extensometerMm |
| 56 | 4 | machineState |
| 60 | 4 | sequenceState |
| 64 | 4 | currentStep |
| 68 | 4 | testRunning, 0/1 |
| 72 | 4 | sampleFlags |

Server scheduler는 HELLO 완료 후 `steady_clock` 기준 매 100 ms, 즉 10 Hz로 `live_.Read()` 값을 encode/send한다.

구현 층별 판정:

- A. Protocol codec에 field 존재: YES
- B. 실제 UTM runtime에서 publish: NO
- C. Mock data 전달: YES
- D. Production UI/Application에 Server 연결: NO

Mock client가 `LiveData`를 직접 만들어 `PublishLiveData()`하고 roundtrip/10 Hz를 확인한다. 실제 `UtmRuntimeInfoV6`, compliance runtime, calibration/encoder 값을 `LiveData`로 변환하는 production adapter는 없다. 현재 production에서는 LIVE_DATA TCP 송신 자체가 일어나지 않는다.

## 6. Production Integration

원하는 흐름:

```text
UTM Runtime → Protocol Snapshot Adapter → ProtocolServer → TCP Client
```

현재 실제 흐름:

```text
Mock constants → ProtocolServer → local C++ mock client
```

`dao_utm_ui`는 `dao_protocol`을 링크하지 않으며 Server lifecycle, configured port, service start/stop, UTM snapshot publish, command handler registration이 없다. 따라서 Protocol Production Integration은 **NOT IMPLEMENTED**다.

권고 최소 변경은 UI widget에 Server를 직접 넣는 것이 아니라 `UtmProtocolService`를 Application/Service 계층에 추가하는 것이다. 이 service가 UTM public runtime을 읽고 snapshot을 만들며 command를 UTM API로 변환하고, Qt UI는 service status만 표시한다. 장기적으로 Linux control daemon이 engine/protocol/recorder를 소유하고 HMI가 client가 되는 구조가 더 강하지만, 현 단계 최소 변경은 production application 내부의 독립 service object다.

## 7. Command Flow 감사

`ProtocolServer::HandleFrame()`은 HELLO 이후 LOAD_RECIPE, COMMIT_RECIPE, START_TEST, STOP_TEST, ACK_RESET, GET_TEST_DATA를 `CommandHandler` callback으로 넘긴다. callback 결과 `NackReason::None`이면 ACK, 아니면 NACK를 보낸다.

존재하지 않는 부분:

- command별 payload codec/validation
- caller/client/session authorization
- stateful recipe staging
- UTM runtime state validation adapter
- `DaoUtm_LoadSequence`, `ValidateSequence`, `CommitSequence`, `StartSequence`, `StopSequence`, `AcknowledgeStop` 호출
- actual acceptance와 command completion의 구분
- structured NACK detail과 current state
- idempotency/replay 방지

`UTM_COMMAND_SOURCE_REMOTE`는 이미 enum과 Jog/motion source validation에 존재한다. 그러나 `DaoUtm_StartSequence()`에는 source parameter가 없고 remote protocol command와 source가 연결되어 있지 않다. START_TEST의 최종 binding은 committed sequence, machine READY, stop latch clear, no active calibration/motion/jog 등을 UTM API가 재검증한 뒤 수행해야 한다. ACK는 “요청이 engine에 수락됨”을 의미하고 TEST_STARTED event는 실제 runtime start 관측 후 별도로 보내야 한다.

권고 흐름:

```text
Frame decode
→ command payload decode/finite/range validation
→ connection role/authorization validation
→ Protocol Service state validation
→ UTM public API call
→ immediate COMMAND_ACK/NACK(requestId)
→ runtime transition observation
→ TEST_STARTED/FAULT_EVENT 등 event
```

## 8. Recipe와 Sequencer

Linux Sequencer 자체는 다음 구조를 구현한다.

```text
UtmSequenceDefinition pending
→ Validate
→ Commit copies pending to active
→ RequestStart
→ immutable active sequence execution
```

시험 중 `Load()`는 reject되고 Engine `SubmitMotion()`도 running Sequencer에서 non-Sequencer source motion을 reject한다.

Protocol 측 감사:

| 항목 | 상태 |
|---|---|
| Recipe binary payload | NOT IMPLEMENTED |
| Sequence Step codec | NOT IMPLEMENTED |
| `UtmSequenceDefinition` 변환 | NOT IMPLEMENTED |
| Validation API binding | NOT IMPLEMENTED |
| Commit API binding | NOT IMPLEMENTED |
| Active test 중 protocol edit 차단 | NOT IMPLEMENTED at protocol layer; Sequencer core는 Load reject |
| Recipe ID/version/hash | NOT IMPLEMENTED |

권고 V1 recipe payload:

- recipeId: UUID 또는 128-bit stable ID
- revision: uint32/uint64
- canonical definition hash: SHA-256
- stepCount
- explicit per-step fields, native `UtmSequenceStep` memory 전송 금지
- units/schema version
- optional UTF-8 name/operator notes with bounded length

LOAD_RECIPE는 decode→temporary definition→`DaoUtm_LoadSequence`→`ValidateSequence` 결과를 pending receipt와 함께 반환해야 한다. COMMIT_RECIPE에는 recipeId/revision/hash를 넣어 stale pending commit을 방지한다. Active test snapshot에는 committed recipe 전체 또는 hash+immutable stored copy를 남긴다.

## 9. 시험 상태와 Event

Event enum과 `QueueEvent(type,payload)` API는 있으나 UTM runtime에서 event를 만들어 Server로 전달하는 코드가 없다. payload codec도 없다. 따라서 TEST_STARTED, STEP_CHANGED, TEST_COMPLETE, TEST_ABORTED, FAULT_EVENT는 모두 PARTIAL/CODE ONLY다.

권고 adapter:

```text
2 ms UTM Engine
  → fixed runtime publication + bounded event record
Application Protocol Service
  → runtime transition detector/event drain
Protocol TCP thread
  → priority outbound queues/socket send
```

가능한 가장 작은 변경은 Service가 `UtmRuntimeInfoV6`의 이전/현재 snapshot을 비교해 다음 edge를 생성하는 것이다.

- `recordingActive 0→1` 또는 sequenceRunning 0→1: TEST_STARTED
- currentStepIndex 변경: STEP_CHANGED
- sequenceComplete: TEST_COMPLETE
- sequenceAborted: TEST_ABORTED
- stop latch sequence 증가: FAULT_EVENT

단 event loss 없는 제품 동작을 위해서는 Engine이 monotonic event sequence 또는 bounded event ring을 제공하는 편이 좋다. 단순 polling transition detector는 service 지연 중 여러 step이 지나가면 event를 잃을 수 있다. Control thread는 socket send나 protocol mutex를 잡아서는 안 된다.

## 10. Local Recording 감사

Sequencer runtime에는 다음만 있다.

- `recordingSessionId`: sequence start마다 증가하는 process-local counter
- `recordingActive`: start에서 1, complete/abort에서 0
- `recordingLastEvent`: start=1, complete=2, abort=3

이는 **recording hook/state marker**이며 실제 recorder가 아니다.

현재 없는 것:

- 지속 가능한 globally unique Test ID
- 2 ms sample collection
- sample ring/buffer
- recording thread
- result directory/catalog
- CSV 또는 binary raw file
- metadata snapshot
- complete/abort atomic finalize
- fsync/checkpoint/journal
- power-loss recovery

`app/engine_test`의 ADC diagnostic CSV는 시험 recorder가 아니라 개발 diagnostic utility다. `UtmAnalysisSample`은 desired sample POD를 정의하지만 producer/queue/writer가 없다. Local Recording 상태는 **NOT IMPLEMENTED**다.

## 11. 권장 Linux Recording Architecture

```text
UTM Control Thread, 2 ms
  └─ canonical fixed sample 작성
       ↓ bounded preallocated SPSC ring
Recording Thread
  ├─ binary sample blocks append
  ├─ periodic checkpoint/hash
  └─ metadata/event journal
       ↓ atomic finalize
Local Result Store / Index
```

Control thread sample:

- timestampUs
- testId/session generation
- forceN
- rawDisplacementMm
- complianceCompensationMm
- correctedDisplacementMm
- extensometerMm
- sequenceStep
- sampleFlags
- 권장 additive: raw IO input/output state, machineState, motion/sequence state, event marker, source freshness/validity

원본 `rawDisplacementMm`와 measured force는 절대 버리지 않는다. Corrected 값과 active curve identity를 함께 저장한다. Queue overflow는 silent drop이 아니라 sample-gap flag, dropped count, event와 final metadata에 남겨야 한다. Recorder failure는 제품 정책상 시험 지속/중단 여부를 명시해야 하며 기본 권고는 operator-visible fault와 local buffer grace period다. Safety logic 자체는 recorder I/O에 의존하지 않는다.

## 12. Test Result File Format 비교와 추천

| 형식 | 장점 | 단점 |
|---|---|---|
| A. Binary samples + JSON metadata | 빠른 append, 작은 크기, transfer/backup 쉬움, schema 명시 가능, raw 보존 | custom reader 필요, crash recovery/index를 설계해야 함 |
| B. SQLite | transaction/crash recovery, query/index/metadata 통합, Windows/AI 도구 풍부 | write tuning 필요, 단일 DB corruption/blast radius, bulk network transfer와 file lifecycle 복잡 |
| C. CSV + JSON | 사람이 읽기 쉽고 Windows 호환 최고 | 용량/속도/정밀도/파싱 비용, partial write, schema/type 안정성 약함 |
| D. HDF5/Parquet 등 | 대규모 분석/columnar 효율 | embedded dependency/복구/Windows 배포 복잡, append와 단일 시험 이동성 tradeoff |

추천: **시험별 append-only binary sample file + JSON metadata/manifest**다.

권장 단위:

```text
results/<testId>/
  manifest.pending.json
  samples.bin.partial
  events.bin.partial
  manifest.json          # finalize 시 atomic rename/commit
  samples.bin
  events.bin
```

- binary file은 file header, schema version, canonical unit IDs, fixed record size, little 또는 big endian marker를 가진다.
- 일정 sample block마다 block index, first/last timestamp, sample count, CRC32C를 둔다.
- final manifest에 전체 SHA-256, sample count, dropped samples, completion state를 둔다.
- periodic fdatasync/checkpoint로 power-loss 범위를 제한한다.
- startup recovery가 `.partial` blocks를 CRC 기준으로 마지막 valid block까지 복구한다.
- Windows는 원본 binary를 보관하고 필요 시 CSV를 export한다.
- SQLite는 여러 시험의 searchable catalog/index에 선택적으로 사용하되 원본 sample의 유일 저장소로 두지 않는 것이 좋다.

이 구조는 파일 단위 이동, 재분석, AI ingestion, interrupted transfer에 유리하다.

## 13. GET_TEST_DATA 현황과 설계

현재 구현:

- GET_TEST_DATA message ID와 callback dispatch: 존재
- TEST_DATA_BEGIN/END IDs: 존재
- TEST_DATA_CHUNK payload: `testId:uint64`, `chunkIndex:uint32`, `length:uint32`, opaque bytes
- chunk encode/decode roundtrip mock: 존재

미구현:

- actual result lookup/catalog
- metadata/manifest codec
- sample record/chunk codec
- configured chunk size
- BEGIN total length/count/schema/hash
- END final count/hash/status
- requestId와 transferId lifecycle
- checksum 검증
- retransmit/duplicate handling
- interruption/reconnect
- partial resume/range request
- outbound backpressure/fair scheduling

따라서 실제 저장 데이터 전송은 불가능하다.

권고:

```text
GET_TEST_DATA(testId, artifact, offset/chunkIndex, optional knownHash)
→ TEST_DATA_BEGIN(transferId, totalBytes, chunkSize, fileHash, metadata)
→ TEST_DATA_CHUNK(transferId, chunkIndex, offset, bytes, CRC32C)
→ TEST_DATA_END(transferId, chunks, bytes, SHA-256)
```

Reconnect 후 같은 immutable result hash에 대해 last verified chunk+1부터 resume한다. Protocol thread가 result file을 직접 blocking read하기보다는 Result Service/async reader가 bounded chunks를 준비하고, event/command ACK가 bulk traffic보다 높은 우선순위를 갖게 한다. 권장 chunk는 초기 64 KiB이며 실측 후 조정한다.

## 14. Disconnect/Reconnect 정책

현재 standalone server는 client disconnect 시 내부 status/live mailbox를 지우지 않고 다음 client를 accept한다. Mock는 testId=77/running=true snapshot이 reconnect 후 다시 전송됨을 확인한다. 이는 정책의 작은 proof일 뿐 실제 sequence/recording과 연결되어 있지 않다.

Production에서 필요한 동작:

- connection loss가 UTM stop condition에 연결되지 않음
- Linux Engine/Safety/Sequencer/Recorder는 Protocol Service와 독립 lifetime
- reconnect HELLO 후 current MACHINE_STATUS 즉시 송신
- last Test ID, current step, running/completed/aborted state 제공
- event cursor 또는 missed-event summary 제공
- completed result catalog에서 GET_TEST_DATA 재요청 가능

현재 production Server와 recorder가 없으므로 전체 정책은 NOT IMPLEMENTED다. Architecture상 충분히 구현 가능하다.

## 15. Heartbeat 정책

현재 HEARTBEAT는 client request에 같은 requestId로 echo하는 것뿐이다. 주기, timeout, last-seen, connection health runtime이 없다.

권고 기본값:

- Windows send interval: 2 s
- Linux reply: 즉시 network thread에서 echo + optional monotonic timestamp
- Windows dead threshold: 3 missed replies 또는 6 s
- Linux idle-client threshold: 10 s 후 socket close
- TCP keepalive: 장시간 half-open 보조 수단으로 활성화, application heartbeat 대체 아님
- heartbeat 상태와 machine/test state를 완전히 분리

Monitoring heartbeat loss는 Motion/Safety stop 조건이 아니다. 향후 remote-control lease가 필요하면 monitoring connection과 별도의 short-lived authorization lease를 두며, lease loss 정책도 시험 중 자동 stop으로 암묵 연결하지 말고 명시적 customer policy로 둔다.

## 16. SnapshotMailbox Data Race 감사

현재 구현은 두 slot과 atomic published index를 사용한다.

Writer:

```text
next = (published + 1) & 1
slots[next] = value          # non-atomic struct copy
published.store(next, release)
```

Reader:

```text
slots[published.load(acquire)] # non-atomic struct copy
```

Release/acquire는 index가 가리키는 직전 write의 visibility는 보장하지만 reader가 해당 slot copy를 끝냈다는 ownership을 writer에게 알려주지 않는다. 다음 두 번의 publish가 빠르게 일어나면 writer가 reader가 아직 읽는 slot을 다시 overwrite할 수 있다. doubles/struct fields의 concurrent read/write는 C++ data race이며 undefined behavior다. torn/internally inconsistent LIVE_DATA나 MACHINE_STATUS가 발생할 수 있다.

현재 writer/reader:

- Mock에서 main/test writer와 Server network reader
- Production 예상에서 runtime adapter writer와 Protocol network reader
- publish rate가 2 ms라면 overwrite 가능성이 더 커짐

추천: **bounded SPSC latest-value triple buffer with explicit per-slot reader ownership/generation**, 또는 검증된 sequence-lock protocol이다. 표준 C++에서 non-atomic payload seqlock도 엄밀히 data race가 될 수 있으므로 payload access 자체의 memory-model 안전성을 확보해야 한다.

가장 현실적인 최소안:

- Control thread는 preallocated SPSC ring에 fixed `ProtocolRuntimeSample`을 try-push하며 절대 기다리지 않음
- Protocol Service thread가 ring을 drain해 자신의 plain latest snapshot을 소유
- TCP thread와 Service가 동일 thread라면 mailbox 자체가 불필요
- 별도 TCP thread가 필요하면 Service→TCP에는 짧은 mutex-protected snapshot을 사용하되 Control thread는 그 mutex를 절대 잡지 않음

이 구조가 2 ms path에 가장 안전하다. 단순 mutex를 Control publisher가 잡는 방식은 권고하지 않는다. SnapshotMailbox 문제는 production 연결 전에 반드시 수정해야 한다.

## 17. Event Queue 감사

현재 `events_`는 `std::vector<Frame>`이고 `eventMutex_`로 보호된다. `QueueEvent()`는 mutex를 잡고 vector에 push하므로 lock contention과 dynamic allocation이 가능하다. 현재 저장소에는 production caller가 없고 mock도 event queue를 실질 검증하지 않는다.

향후 2 ms UTM thread가 직접 `QueueEvent()`를 호출하면 안 된다.

추천:

- Engine/Control → Protocol Service: fixed-capacity SPSC event ring, fixed POD event, nonblocking try-push
- Protocol Service → TCP outbound: service/network thread 소유 deque 또는 priority queues
- overflow: dropped-event counter와 mandatory resync flag
- FAULT/complete/abort는 high priority, live는 replaceable, bulk chunks는 low priority

현재 mutex event queue는 network/service thread 내부에서만 호출한다면 유지 가능하다. Control thread boundary로 사용하려면 교체가 REQUIRED다.

## 18. 권장 Linux Thread Architecture

| Thread | 읽기 | 쓰기 | 전달 방식 |
|---|---|---|---|
| EtherCAT/UTM Control, 2 ms | PDO/input, command mailboxes | Servo/IO output, runtime sample, event records | preallocated runtime store/SPSC rings |
| Recording Thread | sample/event SPSC rings | `.partial` binary files/checkpoints | bounded queues; disk ownership 단독 |
| Protocol Service/TCP Thread | latest runtime, event ring, command frames, result chunks | socket, protocol state, UTM API request mailboxes | framed TCP, bounded outbound priority queues |
| Qt UI Thread | public runtime/service status | local operator requests/profile save | signals/slots and UTM public API; no control ownership |
| Result Service Thread, optional | finalized files/catalog | chunk buffers/hash/recovery | async bounded work queue |

Control thread 금지 항목:

- socket/send/recv
- file/disk I/O
- JSON/string formatting
- blocking wait
- UI calls
- unbounded/long mutex
- dynamic allocation 가능 경로
- compression/hash of bulk data

Protocol command는 direct Servo command가 아니라 existing UTM public API/mailbox를 사용한다. Linux 시험 lifetime은 UI/Windows connection lifetime과 분리한다.

## 19. 권장 Windows Monitoring Architecture

```text
DAO UTM Windows Application
├─ Protocol Client
│  ├─ framing/parser
│  ├─ heartbeat/reconnect
│  └─ command correlation
├─ Connection / Session Service
│  ├─ capabilities/version
│  ├─ monitoring vs control permission
│  └─ state resync
├─ Live Runtime Store
│  └─ immutable observable snapshots
├─ Recipe Service
│  ├─ edit/validate locally
│  ├─ load/commit workflow
│  └─ recipe ID/version/hash
├─ Test Data Repository
│  ├─ resumable download
│  ├─ integrity verification
│  └─ local cache/catalog
├─ Analysis Engine
├─ Report/CSV/PDF Engine
├─ UI
└─ AI-ready service adapters
```

UI가 socket, reconnect, file format을 직접 다루지 않도록 service interfaces를 둔다. Client가 연결을 잃어도 stale live snapshot을 명확히 표시하고 Linux test를 stop시키지 않는다.

## 20. AI-ready Boundary

AI는 Servo/Motion/Safety command authority를 가져서는 안 된다. 다음 교체 가능한 boundary를 권고한다.

- `ITestStandardService`: ASTM/ISO/KS/customer standard knowledge와 인간 검토용 recipe suggestion
- `ITestAnalysisService`: deterministic calculation API, versioned algorithm, reproducible outputs
- `IAiAssistantService`: 자연어 intent→standard/analysis service 호출과 결과 설명
- `IReportService`: approved data/analysis→CSV/PDF/report template

AI의 출력은 suggestion/draft이며 START/STOP/ACK_RESET 같은 protocol command endpoint에 직접 연결하지 않는다. AI가 제안한 recipe는 일반 recipe validation과 operator commit을 반드시 거친다.

지금부터 보존할 데이터:

- canonical raw samples와 validity flags
- exact timestamps/sample gaps
- immutable recipe/sequence snapshot
- calibration/compliance identities와 point lists/hash
- software/protocol/schema/analysis versions
- machine/config/operator context
- event/fault timeline
- unit definitions
- algorithm parameters와 report template version
- derived 값이 아니라 재계산 가능한 원본

## 21. 권장 Test Metadata

한 시험 결과에 최소 다음을 저장한다.

- Test ID: reboot 후에도 unique한 UUID/ULID
- Machine ID/serial
- Start/end wall-clock UTC와 monotonic basis
- completion/abort/fault status 및 reason mask
- Recipe ID, revision, SHA-256
- 전체 committed Sequence Definition
- operator/batch/specimen/customer identifiers
- Machine Profile name, revision/hash
- Load Cell model/serial/capacity N
- force calibration scale/zero policy/date/identity
- extensometer model/serial/scale/calibration identity
- Compliance mode/enabled
- compression/tension curve version와 hash/point list
- actual active curve version/hash
- sample nominal rate와 actual timing statistics
- dropped/gap/corrupt sample counts
- raw/canonical unit schema
- Linux software/build version
- Basic/UTM Engine versions
- Protocol version/features
- adapter/device topology identity
- safety/limit/protection configuration snapshot
- event timeline
- recording schema/file hashes/finalization status
- analysis algorithm/report versions는 Windows 파생 metadata로 별도 append

## 22. Security와 Network

현재 V1에는 authentication, TLS/encryption, role authorization, remote-command enable policy가 없다. Server는 `INADDR_ANY`로 listen하고 HELLO version만 확인한다. LAN에 접근 가능한 client는 command callback에 도달할 수 있다.

폐쇄형 장비 LAN의 현실적 최소 권고:

- machine VLAN/전용 NIC와 firewall allowlist
- default bind address configurable; 필요 시 control NIC에만 bind
- monitoring-only가 기본 role
- Remote command는 local HMI setting/key-switch와 session authorization 둘 다 필요
- HELLO에 client ID, requested role, nonce와 pre-shared-key HMAC 옵션
- audit log: connection, role grant, command request/ACK/NACK
- one active control lease, 여러 monitoring client는 향후 별도 확장
- recipe/data length/range/UTF-8/enum validation

TLS는 폐쇄형 V1 첫 단계의 필수 조건은 아니지만 routed/customer network로 노출되면 TLS 또는 OS-level VPN이 필요하다. Monitoring permission과 remote command permission은 반드시 분리한다.

## 23. Version Compatibility

현재는 header version exact `1`만 HELLO에서 수락한다. Fixed payload는 additive field를 붙이면 기존 decoder의 exact-size 검사 때문에 호환되지 않는다.

권고:

- Header major protocol version은 현재 `1` 유지
- HELLO payload에 `major`, `minor`, `minimumCompatibleMinor`, feature bitset, max frame/chunk size 추가
- V1.x는 새 message type 또는 새 payload schema ID로 확장
- 기존 fixed V1 LIVE_DATA 76-byte layout은 변경하지 않음
- 추가 live fields는 `LIVE_DATA_V2` 신규 message 또는 negotiated extension frame
- Unknown event/status message: ignore + metric
- Unknown request: NACK UnsupportedMessage
- Unknown mandatory feature: HELLO reject
- Unknown optional payload field가 필요하면 TLV payload를 신규 message에만 사용
- V2는 framing/semantic breaking change가 있을 때만 major bump
- golden vectors와 Linux/C# compatibility test를 release gate로 사용

## 24. 구현 우선순위

| 우선순위 | 항목 | WHY | DEPENDENCY | RISK |
|---:|---|---|---|---|
| 1 | Snapshot boundary 안정화 | torn live/status와 UB 제거 | fixed POD 정의 | HIGH |
| 2 | Test ID/metadata/schema 확정 | recorder, event, transfer, Windows 모델의 공통 기준 | product identity policy | HIGH |
| 3 | Local Recorder + SPSC ring | Linux가 원본 데이터 주체라는 핵심 요구 | sample schema/Test ID | HIGH |
| 4 | Result Store/recovery/finalize | GET_TEST_DATA와 reconnect의 실제 source | recorder | HIGH |
| 5 | Production Protocol Adapter/lifecycle | actual UTM status와 Server 연결 | snapshot 안전화 | HIGH |
| 6 | Recipe/Sequence codec와 binding | Windows recipe workflow 구현 | recipe ID/schema, adapter | HIGH |
| 7 | Event publisher/ring | 즉시 상태 변화와 missed-event 복구 | event schema/Test ID | MEDIUM |
| 8 | GET_TEST_DATA/resume/integrity | completed result 전달 | result store, bulk scheduler | HIGH |
| 9 | Heartbeat/backpressure/security roles | product network robustness | server lifecycle | MEDIUM |
| 10 | Windows client core | stable contract 대상으로 개발 | golden vectors/features | MEDIUM |
| 11 | Windows analysis/report/AI services | 원본/metadata 기반 기능 | repository/schema | MEDIUM |

Production adapter보다 recorder/schema를 앞에 둔 이유는 protocol을 먼저 실제 연결하면 testId와 data contract를 다시 바꿀 가능성이 크기 때문이다. 다만 Windows framing/client skeleton은 1~4와 병렬로 개발할 수 있다.

## 25. Hardware 유무별 구현 가능 범위

### Hardware 없이 가능

- SnapshotMailbox/SPSC boundary 수정과 concurrency test
- production Protocol Service lifecycle과 simulated UTM adapter
- recipe/step codec, validation/binding mock
- local recorder, result format, crash/recovery/fault injection
- Test ID/catalog/metadata
- event ring/publisher simulation
- GET_TEST_DATA resume/checksum/backpressure
- C# client skeleton과 golden-vector interoperability
- reconnect/session/status state machine
- Windows repository/analysis/report service interfaces
- security role/feature negotiation

### Hardware가 있어야 의미 있는 검증

- 실제 시험 중 cable/network loss와 Linux sequence 지속
- 2 ms control + recording + TCP bulk transfer jitter
- full sample rate 장시간 recording과 storage latency
- 실제 EtherCAT recovery와 event ordering
- Servo/ADC/encoder live mapping 정확성
- load-cell/compliance raw/corrected 값의 실제 동시성
- power loss/UPS/filesystem behavior
- 다시간 시험, disk-full, thermal/storage throttling

Hardware 없이 architecture와 대부분의 software를 구현할 수 있지만 PRODUCTION VALIDATED 판정은 실제 UTM 검증 전까지 금지한다.

## 26. 현재 Simulation Gate

`dao_utm_ui`는 `--hardware`가 명시된 경우에만 EtherCAT hardware mode를 요청하고, 그 외에는 offline/display-only 경로를 사용한다. Auto Calibration UI도 offline demo에서 실제 calibration command를 거부하고 hardware-disabled Engine simulation test를 별도로 사용하도록 되어 있다.

Protocol은 production UI에 연결되지 않아 별도의 production network enable gate가 아직 없다. 향후 `--protocol-server`/profile setting과 bind interface/role policy를 명시적으로 두는 것이 좋다.

권고: 현재 hardware opt-in gate를 유지한다. 실제 UTM 조립과 commissioning 전 제거하지 않는다. Protocol production adapter 역시 기본 disabled로 추가하고 mock/simulated runtime에서 먼저 검증한다.

## 27. 현재 Protocol의 주요 제품 위험 7개

| 등급 | 위험 | 제품 영향 |
|---|---|---|
| HIGH | Production adapter 자체가 없음 | 실제 UTM monitoring/command/data 기능이 동작하지 않음 |
| HIGH | Local recorder/result store 없음 | 원본 시험 데이터 손실, GET_TEST_DATA 불가 |
| HIGH | SnapshotMailbox C++ data race | torn status/live, undefined behavior |
| HIGH | Recipe/command payload·binding·authorization 없음 | 잘못된 명령 수락 또는 제품 기능 미완성 |
| HIGH | Nonblocking send backpressure/priority/resume 없음 | 느린 client에서 disconnect, event/data 손실 |
| MEDIUM | Event queue가 mutex+dynamic vector이고 source adapter 없음 | control 연결 시 jitter 또는 event loss 가능 |
| MEDIUM | 인증/role 분리와 heartbeat timeout 없음 | LAN client의 원격 명령 위험, half-open 식별 지연 |

## 28. 최종 권장 Architecture Diagram

```text
┌──────────────────────────── Linux Control PC ────────────────────────────┐
│                                                                          │
│  EtherCAT Devices                                                        │
│  Servo / ADC / IO / Encoder                                              │
│          │ PDO                                                           │
│          ▼                                                               │
│  ┌───────────────────────────────┐                                       │
│  │ EtherCAT + UTM Control 2 ms   │                                       │
│  │ Motion / Force / Safety       │                                       │
│  │ StopLatch / Sequencer         │                                       │
│  │ Raw Coordinate                │                                       │
│  └──────────┬──────────────┬─────┘                                       │
│             │ fixed sample │ fixed event/runtime                         │
│             ▼              ▼                                             │
│  ┌─────────────────┐   ┌────────────────────┐                            │
│  │ Compliance      │   │ Bounded SPSC       │                            │
│  │ Analysis values │   │ runtime/event edge │                            │
│  └────────┬────────┘   └──────────┬─────────┘                            │
│           │ raw+corrected         │                                      │
│           ▼                       ▼                                      │
│  ┌──────────────────┐   ┌────────────────────────┐                       │
│  │ Recording Thread │   │ Protocol Service/TCP   │                       │
│  │ Binary + Events  │   │ Adapter/Validation     │◄── UTM Public API     │
│  └────────┬─────────┘   └───────────┬────────────┘                       │
│           ▼                         │ framed TCP                          │
│  ┌──────────────────┐               │                                    │
│  │ Local Result     │───────────────┤ async result chunks                │
│  │ Store + Catalog  │               │                                    │
│  └──────────────────┘               │                                    │
│                                     │                                    │
│  ┌──────────────────┐               │                                    │
│  │ Qt Local HMI     │── operator ───┤ service/public API                 │
│  └──────────────────┘               │                                    │
└─────────────────────────────────────┼────────────────────────────────────┘
                                      │ LAN, monitoring not control loop
                                      ▼
┌──────────────────────────── Windows PC ──────────────────────────────────┐
│  Protocol Client → Connection/Session → Live Runtime Store               │
│         │                    │                                            │
│         ├→ Recipe Service ───┘ load/validate/commit/start                 │
│         └→ Test Data Repository ← resumable result transfer              │
│                               │                                          │
│                               ▼                                          │
│                      Analysis Engine                                     │
│                         │       │                                        │
│                         ▼       ▼                                        │
│                    Graph/UI   CSV/PDF Report                             │
│                               │                                          │
│  ITestStandardService / ITestAnalysisService / IAiAssistantService       │
│  AI: suggestion/analysis/report draft only; no Servo/Safety authority    │
└──────────────────────────────────────────────────────────────────────────┘
```

## 29. 최종 질문 답변

1. **현재 Protocol V1은 어디까지 구현되어 있는가?**  Fixed header/codec, stream fragmentation/coalescing, standalone single-client TCP server, HELLO/heartbeat, generic ACK/NACK callback, 10 Hz mock live/status, event queue API, opaque test chunk codec까지다. Production adapter와 실제 command/data service는 없다.

2. **Production UTM application에 TCP Server가 연결되어 있는가?**  아니다. `dao_utm_ui`는 `dao_protocol`을 링크하거나 Server를 생성하지 않는다.

3. **LIVE_DATA는 실제 UTM 값인가, mock만 존재하는가?**  Payload field와 scheduler는 구현됐지만 현재 송신 시험은 mock 값뿐이다. 실제 UTM runtime publisher는 없다.

4. **LOAD_RECIPE/COMMIT_RECIPE/START_TEST가 실제 Sequencer와 연결되어 있는가?**  아니다. message ID와 generic callback boundary만 존재한다.

5. **Local Recording은 실제 구현되어 있는가?**  아니다. Sequencer session counter/active/event flag는 hook일 뿐 sample/file recorder가 아니다.

6. **GET_TEST_DATA는 실제 저장 데이터 전송까지 가능한가?**  아니다. Opaque chunk codec만 있고 result lookup, metadata, sample codec, transfer lifecycle이 없다.

7. **Windows 연결이 끊겨도 시험을 계속할 현재 구조인가?**  UTM Engine/Sequencer 자체는 protocol과 독립이라 원칙에 맞지만 production protocol/recorder가 연결되지 않아 end-to-end 구현 완료 상태는 아니다. Standalone mock server는 snapshot을 유지하고 reconnect한다.

8. **Windows Monitoring 개발을 지금 시작해도 되는가?**  Framing/parser, connection service, mock monitoring, UI skeleton은 시작 가능하다. Recipe/result/live production contract를 완료된 것으로 고정하면 안 된다.

9. **시작 전에 Linux에서 먼저 구현할 것은 무엇인가?**  Snapshot boundary 수정, Test ID/metadata schema, local recorder/result store, production protocol adapter, recipe codec/binding, event publisher, GET_TEST_DATA 순이다.

10. **SnapshotMailbox race는 실제 수정해야 하는가?**  그렇다. 실제 C++ data race 가능성이 있으므로 production publisher 연결 전에 반드시 수정해야 한다.

11. **Simulation gate는 유지해야 하는가?**  그렇다. Hardware commissioning 전까지 `--hardware` opt-in과 Auto Calibration offline restriction을 유지하고 Protocol production enable도 별도 gate로 추가하는 것이 좋다.

12. **AI 기능을 위해 지금 준비할 데이터는 무엇인가?**  Canonical raw samples, timestamps/gaps, immutable recipe/sequence, machine/profile/calibration/compliance snapshots와 hashes, event/fault timeline, validity flags, software/schema/protocol versions, deterministic analysis provenance를 저장해야 한다.

## 30. 최종 판정

Protocol V1 foundation은 불필요하게 폐기하거나 전면 재설계할 필요가 없다. Header/codec/message IDs/single-client server는 기반으로 유지할 수 있다. 다만 이를 production-ready로 부르기 전에 snapshot race를 제거하고, Application/Service adapter, recorder/result store, recipe/event/data-transfer의 실제 binding을 추가해야 한다. Linux control loop와 socket/disk/JSON 경계를 분리하는 것이 모든 후속 구현의 최우선 원칙이다.
