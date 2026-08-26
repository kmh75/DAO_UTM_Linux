# DAO EtherCAT Engine API Guide

**Project:** DAO EtherCAT Engine  
**Engine Version:** 0.1.0  
**Platform:** Windows x64  
**EtherCAT Master:** SOEM  
**Document Purpose:** Engine 사용 및 유지보수용 개발자 문서

---

# 1. 문서 목적

이 문서는 `DAO EtherCAT Engine` DLL의 구조와 공개 API 사용 방법을 설명한다.

문서의 주요 목적은 다음과 같다.

- DAO EtherCAT Engine의 전체 구조 이해
- 프로그램 시작 및 종료 호출 순서 이해
- 각 공개 API의 용도 이해
- 각 함수의 입력 인자 이해
- 함수 반환값의 의미 이해
- Logical Index와 Physical Slave Index 구분
- Servo 제어 함수 사용 방법 이해
- EtherCAT IO 사용 방법 이해
- ADC Zero / Calibration / Filter 사용 방법 이해
- ADC Ring Buffer를 이용한 고속 데이터 취득 방법 이해
- Runtime 상태 Polling 방법 이해
- 일반 UI용 API와 개발/진단용 API 구분
- 향후 새로운 장비 UI 개발 시 동일 Engine을 재사용할 수 있도록 기준 제공

본 Engine은 특정 시험기 또는 특정 장비의 시험 목적을 포함하지 않는다.

Engine은 다음과 같은 **하드웨어 통신과 기본 제어 기능**만 담당한다.

- EtherCAT Adapter 검색
- EtherCAT Slave 검색
- Slave 종류 판별
- PDO Mapping
- EtherCAT 상태 전환
- Process Data 통신
- Servo 명령 전달
- Servo 상태 취득
- ADC 원시 데이터 취득
- ADC 기본 Signal Processing
- ADC Ring Buffer
- EtherCAT IO 입출력
- 통신 상태 및 오류 상태 제공

다음과 같은 **장비 목적에 따른 기능은 상위 UI 프로그램에서 구현한다.**

- 자동 시험 시퀀스
- 장비별 안전 인터록 판단
- Servo 축의 장비상 의미
- IO Bit의 장비상 의미
- 센서 단위 변환
- 시험 데이터 저장
- 그래프 표시
- 시험 결과 계산
- PASS / FAIL 판정
- 장비별 설정 관리

---

# 2. Engine 기본 구조

DAO EtherCAT Engine은 다음과 같은 구조로 구성된다.

```text
Application / UI
        ↓
DaoEtherCAT.Engine.dll
        ↓
DaoEngineCore
        ↓
DaoEtherCATMaster
        ↓
SOEM
        ↓
EtherCAT Slave
```

---

## 2.1 Application / UI

Application 또는 UI는 실제 장비 프로그램이다.

예:

- Servo 위치제어 장비
- Servo + IO 자동화 장비
- Servo + ADC 측정 장비
- 시험 데이터 저장 장비
- UTM 또는 기타 시험기 UI

UI의 주요 역할은 다음과 같다.

```text
사용자 명령
시험 시퀀스
장비 상태 표시
Servo 현재 위치 표시
ADC 현재값 표시
IO 상태 표시
데이터 저장
그래프
결과 계산
인터록 판단
```

UI는 SOEM을 직접 사용하지 않는다.

UI에서는 원칙적으로 다음 형태의 공개 DLL 함수만 호출한다.

```cpp
DaoEngine_...
```

---

## 2.2 DaoEtherCAT.Engine.dll

외부 프로그램에서 사용하는 공개 DLL Interface이다.

공개 함수와 공개 구조체는 다음 파일에 정의된다.

```text
DaoEtherCAT.Engine.h
```

외부 C/C++ 프로그램 또는 C# Wrapper는 이 공개 Interface만 사용한다.

---

## 2.3 DaoEngineCore

`DaoEngineCore`는 공개 DLL API와 실제 EtherCAT Master 사이의 중간 계층이다.

주요 역할:

- Engine 초기화
- Adapter 목록 관리
- Slave 검색 결과 관리
- 장치 종류 분류
- Logical Device List 생성
- Logical Index → Physical Slave Index 변환
- Master 계층 함수 호출

Logical Device는 다음과 같이 분리된다.

```text
Servo[]
ADC[]
IO[]
Unknown[]
```

---

## 2.4 DaoEtherCATMaster

실제 SOEM Context와 EtherCAT 통신을 관리한다.

주요 역할:

- Adapter Open / Close
- Slave Scan
- Servo PDO Mapping
- 전체 Process Data Mapping
- PRE-OP / SAFE-OP / OP 상태 전환
- Expected WKC 계산
- 2ms EtherCAT Communication Thread
- Servo PDO 송수신
- Servo Command State Machine
- IO PDO 송수신
- ADC PDO 송수신
- ADC Signal Processing
- ADC Ring Buffer
- ADC Diagnostic Capture

---

# 3. 지원 장치

현재 Engine에서 사용하는 장치 종류는 다음과 같다.

```cpp
enum DaoDeviceType
{
    DAO_DEVICE_UNKNOWN = 0,
    DAO_DEVICE_SERVO   = 1,
    DAO_DEVICE_ADC     = 2,
    DAO_DEVICE_IO      = 3
};
```

---

## 3.1 Servo

현재 지원 Servo:

```text
LS Mecapion L7NH EtherCAT Servo Drive
```

장치 판별 기준:

```text
Vendor ID
Product Code
Revision
```

Servo는 논리적으로 다음과 같이 관리한다.

```text
Servo[0]
Servo[1]
Servo[2]
...
```

---

## 3.2 ADC

현재 지원 ADC:

```text
DAO EtherCAT ADC
```

현재 PDO Input은 다음 24 Byte 구조를 사용한다.

```text
Test Counter  4 byte
ADC Raw 0     4 byte
ADC Raw 1     4 byte
ADC Raw 2     4 byte
ADC Raw 3     4 byte
Status        4 byte

Total         24 byte
```

주의:

`adcRaw0 ~ adcRaw3`은 서로 다른 ADC Channel 4개가 아니다.

현재 구조에서는 **하나의 ADC 측정 Stream에 대한 연속 4 Sample**이다.

---

## 3.3 EtherCAT IO

현재 지원:

```text
FASTECH Ezi-IO EtherCAT IN8OUT8
FASTECH Ezi-IO EtherCAT IN16OUT16
```

IO는 논리적으로:

```text
IO[0]
IO[1]
...
```

형태로 관리한다.

---

# 4. 선택적 Slave 구성

Engine은 Servo, ADC, IO가 모두 존재해야 동작하는 구조가 아니다.

연결된 장치만 검색하여 Logical Device List를 생성한다.

따라서 다음과 같은 구성이 가능하다.

```text
ADC only

Servo only

IO only

Servo + IO

Servo + ADC

ADC + IO

Servo + IO + ADC
```

예:

```text
Servo : 1
ADC   : 0
IO    : 1
```

위 구성은 정상적인 Servo + IO 장비로 사용할 수 있다.

없는 장치는 Logical Device Count가 `0`이다.

없는 Logical Index를 호출하면 해당 장치 API는 실패를 반환한다.

---

# 5. Logical Index와 Physical Slave Index

Engine에서는 두 종류의 Index가 존재한다.

---

## 5.1 Physical Slave Index

SOEM에서 사용하는 실제 EtherCAT Slave 번호이다.

특징:

```text
1부터 시작
EtherCAT 실제 연결 순서
Slave 배치 순서에 따라 변경 가능
```

예:

```text
Physical Slave 1
Physical Slave 2
Physical Slave 3
```

---

## 5.2 Logical Index

DAO Engine이 장치 종류별로 생성하는 번호이다.

특징:

```text
0부터 시작
장치 종류별 독립 번호
```

예:

```text
Servo[0]
Servo[1]

ADC[0]

IO[0]
IO[1]
```

예를 들어 실제 EtherCAT 순서가:

```text
Physical Slave 1 = ADC
Physical Slave 2 = Servo
Physical Slave 3 = IO
```

라면 Logical Device는:

```text
ADC[0]   → Physical Slave 1
Servo[0] → Physical Slave 2
IO[0]    → Physical Slave 3
```

가 된다.

---

## 5.3 기본 사용 원칙

일반 UI에서는 가능하면 Logical Index를 사용한다.

예:

```cpp
DaoEngine_ServoOn(0);
DaoEngine_SetAdcZero(0);
DaoEngine_SetIoOutputCommand(0, value);
```

일부 초기 개발용/진단용 API는 아직 Physical Slave Index를 직접 사용한다.

따라서 각 함수 설명의 Index 종류를 반드시 확인해야 한다.

---

# 6. EtherCAT 주기 통신

현재 Engine Communication Thread 주기:

```text
2 ms
500 Hz
```

Communication Thread의 기본 동작:

```text
Servo Output PDO 준비
IO Output PDO 준비
ADC Output PDO 준비

        ↓

ecx_send_processdata()

        ↓

ecx_receive_processdata()

        ↓

Servo Input Runtime 갱신
IO Input Runtime 갱신
ADC Input Runtime 갱신

        ↓

Servo Command State Machine 처리

        ↓

ADC Signal Processing
```

UI는 2ms마다 Engine API를 호출할 필요가 없다.

권장 UI Runtime Polling:

```text
20 ~ 50 ms
```

즉:

```text
20 ms = 50 Hz
50 ms = 20 Hz
```

정도면 사용자 화면 표시에는 충분하다.

---

# 7. 프로그램 기본 시작 순서

일반적인 Application 시작 순서는 다음과 같다.

```text
1. DaoEngine_Initialize()

2. DaoEngine_GetAdapterCount()

3. DaoEngine_GetAdapterInfo()

4. DaoEngine_OpenAdapter()

5. DaoEngine_ScanSlaves()

6. DaoEngine_GetLogicalDeviceCount()

7. DaoEngine_RequestAllSlavesPreOp()

8. DaoEngine_MapProcessData()

9. DaoEngine_RequestAllSlavesSafeOp()

10. DaoEngine_RequestAllSlavesOperational()

11. DaoEngine_StartCommunication()
```

---

## 7.1 기본 예

```cpp
if (DaoEngine_Initialize() == 0)
{
    // 실패 처리
}

if (DaoEngine_OpenAdapter(adapterIndex) == 0)
{
    // 실패 처리
}

int slaveCount =
    DaoEngine_ScanSlaves();

if (slaveCount <= 0)
{
    // Slave 없음
}

DaoEngine_RequestAllSlavesPreOp();

DaoEngine_MapProcessData();

DaoEngine_RequestAllSlavesSafeOp();

DaoEngine_RequestAllSlavesOperational();

DaoEngine_StartCommunication();
```

---

# 8. 프로그램 종료 순서

권장 종료 순서:

```text
Servo 동작 중이면 안전 정지
        ↓
IO Output 필요 시 0
        ↓
DaoEngine_StopCommunication()
        ↓
DaoEngine_CloseAdapter()
        ↓
DaoEngine_Shutdown()
```

예:

```cpp
DaoEngine_StopCommunication();

DaoEngine_CloseAdapter();

DaoEngine_Shutdown();
```

---

# 9. 공통 Engine API

---

## 9.1 DaoEngine_GetVersion

### 함수

```cpp
const char* DaoEngine_GetVersion();
```

### 목적

현재 DAO EtherCAT Engine 버전 문자열을 반환한다.

### 인자

없음.

### 반환

문자열 Pointer.

현재:

```text
DAO EtherCAT Engine 0.1.0
```

### 사용 시점

프로그램 시작 시 버전 표시 또는 로그 기록.

---

## 9.2 DaoEngine_Initialize

### 함수

```cpp
int DaoEngine_Initialize();
```

### 목적

Engine 내부를 초기화하고 EtherCAT Adapter 목록을 검색한다.

### 반환

```text
1 = 성공
0 = 실패
```

### 호출 위치

Application 시작 시 한 번 호출.

### 주의

중복 초기화하지 않는다.

---

## 9.3 DaoEngine_Shutdown

### 함수

```cpp
void DaoEngine_Shutdown();
```

### 목적

Engine을 종료하고 내부 Resource를 정리한다.

### 반환

없음.

### 권장 호출

프로그램 종료 시 마지막에 호출.

---

## 9.4 DaoEngine_IsInitialized

### 함수

```cpp
int DaoEngine_IsInitialized();
```

### 반환

```text
1 = 초기화 상태
0 = 초기화되지 않음
```

---

# 10. Adapter 관련 API

---

## 10.1 DaoEngine_GetAdapterCount

```cpp
int DaoEngine_GetAdapterCount();
```

### 목적

현재 PC에서 검색된 Network Adapter 개수를 반환한다.

### 반환

```text
0 이상 = Adapter 개수
```

---

## 10.2 DaoEngine_GetAdapterInfo

```cpp
int DaoEngine_GetAdapterInfo(
    int adapterIndex,
    DaoAdapterInfo* adapterInfo);
```

### 목적

지정 Adapter의 이름과 설명을 읽는다.

### adapterIndex

0부터 시작.

### adapterInfo

결과 구조체 Pointer.

```cpp
struct DaoAdapterInfo
{
    char name[512];
    char description[512];
};
```

### 반환

```text
1 = 성공
0 = 실패
```

---

## 10.3 DaoEngine_OpenAdapter

```cpp
int DaoEngine_OpenAdapter(
    int adapterIndex);
```

### 목적

선택한 Network Adapter를 EtherCAT Master용으로 연다.

### 반환

```text
1 = 성공
0 = 실패
```

### 선행조건

```text
DaoEngine_Initialize() 성공
유효한 Adapter Index
```

---

## 10.4 DaoEngine_CloseAdapter

```cpp
void DaoEngine_CloseAdapter();
```

현재 열린 EtherCAT Adapter를 닫는다.

---

## 10.5 DaoEngine_IsAdapterOpen

```cpp
int DaoEngine_IsAdapterOpen();
```

### 반환

```text
1 = Adapter Open 상태
0 = 닫힘
```

---

# 11. Slave 검색 API

---

## 11.1 DaoEngine_ScanSlaves

```cpp
int DaoEngine_ScanSlaves();
```

### 목적

현재 EtherCAT Network의 Slave를 검색한다.

검색 후 Logical Device List도 생성된다.

### 반환

```text
0 = Slave 없음 또는 검색 실패
1 이상 = 검색된 Slave 개수
```

### 중요

이 함수 호출 후 다음 장치 목록이 생성된다.

```text
Servo[]
ADC[]
IO[]
Unknown[]
```

---

## 11.2 DaoEngine_GetSlaveCount

```cpp
int DaoEngine_GetSlaveCount();
```

마지막 Scan 결과의 전체 물리 Slave 개수를 반환한다.

---

## 11.3 DaoEngine_GetSlaveInfo

```cpp
int DaoEngine_GetSlaveInfo(
    int slaveListIndex,
    DaoSlaveInfo* slaveInfo);
```

### 목적

검색된 물리 Slave의 정보를 읽는다.

### slaveListIndex

```text
0부터 시작
```

주의:

이 값은 Physical Slave Index와 다르다.

```text
slaveListIndex 0
→ Physical Slave Index 1
```

### 주요 출력

```text
Physical Slave Index
Name
Vendor ID
Product Code
Revision
State
AL Status Code
```

---

# 12. Logical Device API

---

## 12.1 DaoEngine_GetLogicalDeviceCount

```cpp
int DaoEngine_GetLogicalDeviceCount(
    int deviceType);
```

### deviceType

```text
DAO_DEVICE_SERVO
DAO_DEVICE_ADC
DAO_DEVICE_IO
DAO_DEVICE_UNKNOWN
```

### 반환

지정 장치 종류의 개수.

예:

```cpp
int servoCount =
    DaoEngine_GetLogicalDeviceCount(
        DAO_DEVICE_SERVO);
```

---

## 12.2 DaoEngine_GetLogicalDeviceInfo

```cpp
int DaoEngine_GetLogicalDeviceInfo(
    int deviceType,
    int logicalIndex,
    DaoLogicalDeviceInfo* deviceInfo);
```

### 목적

Logical Device와 Physical Slave의 관계를 확인한다.

### 반환

```text
1 = 성공
0 = 실패
```

### 출력

```text
deviceType
logicalIndex
physicalSlaveIndex
name
vendorId
productCode
revision
```

---

# 13. EtherCAT 상태 전환 API

---

## 13.1 DaoEngine_RequestAllSlavesPreOp

```cpp
int DaoEngine_RequestAllSlavesPreOp();
```

모든 검색된 Slave를 PRE-OP 상태로 전환한다.

### 반환

```text
1 = 성공
0 = 실패
```

---

## 13.2 DaoEngine_MapProcessData

```cpp
int DaoEngine_MapProcessData();
```

### 목적

모든 Slave의 PDO를 SOEM IO Map에 배치한다.

Servo가 존재하면 LS L7NH용 PDO Mapping도 수행한다.

### 반환

```text
1 = 성공
0 = 실패
```

### 중요

OP 진입 전에 반드시 필요하다.

---

## 13.3 DaoEngine_RequestAllSlavesSafeOp

```cpp
int DaoEngine_RequestAllSlavesSafeOp();
```

전체 Slave를 SAFE-OP로 전환한다.

---

## 13.4 DaoEngine_RequestAllSlavesOperational

```cpp
int DaoEngine_RequestAllSlavesOperational();
```

### 목적

전체 Slave를 OP 상태로 전환한다.

OP 요청 전 Process Data Priming을 수행한다.

### 반환

```text
1 = 전체 OP 성공
0 = 실패
```

---

## 13.5 DaoEngine_RequestAllSlavesInit

```cpp
int DaoEngine_RequestAllSlavesInit();
```

전체 Slave를 INIT 상태로 전환한다.

일반 Application에서 직접 사용할 일은 많지 않다.

---

# 14. Communication Thread API

---

## 14.1 DaoEngine_StartCommunication

```cpp
int DaoEngine_StartCommunication();
```

### 목적

2ms EtherCAT 순환통신 Thread를 시작한다.

### 선행조건

```text
Adapter Open
Slave 검색 완료
PDO Mapping 완료
Slave OP 상태
```

### 반환

```text
1 = 통신 Thread 시작 성공
0 = 실패
```

---

## 14.2 DaoEngine_StopCommunication

```cpp
void DaoEngine_StopCommunication();
```

Communication Thread를 안전하게 종료한다.

---

## 14.3 DaoEngine_IsCommunicationRunning

```cpp
int DaoEngine_IsCommunicationRunning();
```

### 반환

```text
1 = 통신 중
0 = 정지
```

---

# 15. Process Data 상태 API

---

## 15.1 DaoEngine_GetProcessDataMapInfo

```cpp
int DaoEngine_GetProcessDataMapInfo(
    DaoProcessDataMapInfo* mapInfo);
```

### 출력

```text
mappedBytes
outputWkc
inputWkc
expectedWkc
```

### 목적

PDO Mapping과 Expected WKC 상태 확인.

주로 개발 및 진단에서 사용한다.

---

## 15.2 DaoEngine_GetSlavePdoInfo

```cpp
int DaoEngine_GetSlavePdoInfo(
    int slaveListIndex,
    DaoSlavePdoInfo* pdoInfo);
```

지정 Slave의 Input / Output PDO Byte 수를 반환한다.

---

# 16. Servo Runtime API

---

## 16.1 DaoEngine_GetServoRuntimeInfo

```cpp
int DaoEngine_GetServoRuntimeInfo(
    int logicalServoIndex,
    DaoServoRuntimeInfo* runtimeInfo);
```

### 분류

일반 UI용 핵심 API.

### 목적

지정 Servo의 최신 Runtime 상태를 읽는다.

### Index

```text
Logical Servo Index
0부터 시작
```

### 반환

```text
1 = 정상 Runtime 취득
0 = 실패
```

### 주요 정보

```text
configured
communicationRunning
hasValidInputData

cia402State
fault
operationEnabled
targetReached

commandId
commandType
commandState
commandStep
commandResult

homed

lastWkc
expectedWkc

actualPosition
positionError
statusWord
operationModeDisplay
digitalInputs
```

### 권장 Polling

```text
20 ~ 50 ms
```

### UI 사용 예

```text
현재 Servo 위치 표시
Servo ON/OFF 상태 표시
Fault 표시
Home 완료 여부 표시
Move 명령 완료 확인
```

---

# 17. Servo 명령 상태

Servo 명령은 대부분 비동기 형태이다.

즉 함수가 `1`을 반환했다고 해서 실제 Servo 동작이 완료된 것이 아니다.

---

## 17.1 Command State

```text
0 = IDLE
1 = ACCEPTED
2 = RUNNING
3 = COMPLETED
4 = STOPPED
5 = ERROR
6 = TIMEOUT
```

일반적으로:

```text
명령 API 호출
      ↓
반환 1
      ↓
ACCEPTED
      ↓
RUNNING
      ↓
COMPLETED
```

순서로 진행한다.

UI는 `DaoEngine_GetServoRuntimeInfo()`를 Polling하여 완료를 확인한다.

---

# 18. Servo ON

## DaoEngine_ServoOn

```cpp
int DaoEngine_ServoOn(
    int logicalServoIndex);
```

### 목적

지정 Servo를 CiA402 Operation Enabled 상태까지 전환한다.

### 인자

`logicalServoIndex`

```text
논리 Servo 번호
0부터 시작
```

### 반환

```text
1 = 명령 접수 성공
0 = 명령 접수 실패
```

### 중요

`1`은 Servo ON 완료가 아니다.

완료는 Runtime에서 확인한다.

권장 조건:

```text
commandState == COMPLETED
operationEnabled == 1
cia402State == 0x0027
```

### 실패 가능 조건

```text
통신 정지
유효하지 않은 Servo Index
Servo Runtime 미구성
Input PDO 없음
Fault 상태
다른 명령 진행 중
```

---

# 19. Servo OFF

## DaoEngine_ServoOff

```cpp
int DaoEngine_ServoOff(
    int logicalServoIndex);
```

### 목적

Servo를 Engine에서 정의한 OFF 상태인 Ready To Switch On 상태로 내린다.

### 반환

```text
1 = 명령 접수
0 = 실패
```

### 완료 확인

```text
commandState == COMPLETED
cia402State == 0x0021
```

---

# 20. Servo Home

## DaoEngine_ServoHome

```cpp
int DaoEngine_ServoHome(
    int logicalServoIndex,
    unsigned int timeoutMs);
```

### 목적

Servo Homing Sequence를 비동기로 수행한다.

Engine 내부에서:

```text
Homing 준비
→ Homing Mode 요청
→ Servo ON
→ Homing Start
→ Homing Running
→ Homing 완료
→ Profile Position Mode 복귀
```

순서로 처리한다.

### timeoutMs

Homing 최대 허용 시간.

단위:

```text
ms
```

예:

```cpp
DaoEngine_ServoHome(
    0,
    60000);
```

위 값은 최대 60초이다.

### 반환

```text
1 = 명령 접수 성공
0 = 명령 접수 실패
```

### 완료 확인

```text
commandState == COMPLETED
homed == 1
operationModeDisplay == 1
```

### 오류

```text
commandState == ERROR
또는
commandState == TIMEOUT
```

---

# 21. Servo 절대 위치 이동

## DaoEngine_ServoMoveAbsolute

```cpp
int DaoEngine_ServoMoveAbsolute(
    int logicalServoIndex,
    int targetPosition,
    unsigned int profileVelocity,
    unsigned int profileAcceleration,
    unsigned int profileDeceleration,
    unsigned int timeoutMs);
```

### 목적

Servo를 지정된 절대 위치로 이동한다.

### logicalServoIndex

논리 Servo 번호.

### targetPosition

Servo 절대 목표 위치.

현재 Engine은 LS Servo의 User Unit 값을 그대로 사용한다.

### profileVelocity

Profile Position 이동속도.

0은 허용되지 않는다.

### profileAcceleration

가속도.

현재 구현에서는 0 입력 시 Engine 기본값을 적용한다.

기본:

```text
1000
```

### profileDeceleration

감속도.

0 입력 시 기본값:

```text
1000
```

### timeoutMs

이동 Timeout.

`0` 입력 시 Engine이 현재 위치, 목표 위치, 속도를 기준으로 자동 계산한다.

자동 Timeout은 기본적으로:

```text
예상 이동시간 × 2 + 2초 여유
최소 3초
```

형태로 계산한다.

### 반환

```text
1 = 이동명령 접수
0 = 실패
```

### 중요

반환 1은 이동완료가 아니다.

### 완료 확인

```text
commandState == COMPLETED
commandResult == 1
```

### Timeout

```text
commandState == TIMEOUT
```

---

# 22. Servo Velocity 운전

## DaoEngine_ServoVelocity

```cpp
int DaoEngine_ServoVelocity(
    int logicalServoIndex,
    int targetVelocity,
    unsigned int acceleration,
    unsigned int deceleration);
```

### 목적

Servo를 Profile Velocity Mode로 운전한다.

### targetVelocity

```text
양수 = 정방향
음수 = 역방향
0    = 감속 정지 + Servo 토크 유지
```

### acceleration / deceleration

0 입력 시 Engine 기본값 1000 적용.

### 특징

이미 Velocity Mode가 Running 상태이면 새로운 `targetVelocity`를 넣어 속도를 변경할 수 있다.

따라서 Jog에도 사용할 수 있다.

### 반환

```text
1 = 명령 접수 또는 속도 갱신 성공
0 = 실패
```

---

# 23. Servo Jog

---

## 23.1 DaoEngine_ServoJogPositive

```cpp
int DaoEngine_ServoJogPositive(
    int logicalServoIndex,
    int speed,
    unsigned int acceleration,
    unsigned int deceleration);
```

정방향 Jog.

내부적으로 Velocity 명령을 사용한다.

`speed`는 양수로 입력한다.

---

## 23.2 DaoEngine_ServoJogNegative

```cpp
int DaoEngine_ServoJogNegative(
    int logicalServoIndex,
    int speed,
    unsigned int acceleration,
    unsigned int deceleration);
```

역방향 Jog.

입력 `speed`는 양수로 사용하고 Engine에서 음수 Velocity로 변환한다.

---

# 24. Servo Stop

## DaoEngine_ServoStop

```cpp
int DaoEngine_ServoStop(
    int logicalServoIndex);
```

### 목적

현재 Servo 운전을 정지한다.

Servo OFF와 다르다.

### 동작 개념

```text
Servo ON 유지
Torque 유지
운동만 정지
```

Profile Velocity Mode에서는 Target Velocity = 0 방식.

Profile Position Mode에서는 CiA402 Halt를 사용한다.

### 반환

```text
1 = Stop 요청 접수
0 = 실패
```

### 완료

Runtime:

```text
commandState == STOPPED
```

---

# 25. Low-Level Servo Output

## DaoEngine_SetServoOutputCommand

```cpp
int DaoEngine_SetServoOutputCommand(
    int logicalServoIndex,
    const DaoServoOutputPdo* command);
```

### 분류

Low-Level / 개발용.

### 목적

Servo Output PDO 전체 값을 직접 설정한다.

### 일반 UI 권장

일반적인 Servo 동작에는 사용하지 않는 것을 권장한다.

대신:

```text
ServoOn
ServoOff
ServoHome
ServoMoveAbsolute
ServoVelocity
ServoStop
```

등의 상위 API를 사용한다.

---

# 26. Servo Operation Mode 직접 설정

## DaoEngine_SetServoOperationMode

```cpp
int DaoEngine_SetServoOperationMode(
    int logicalServoIndex,
    signed char mode);
```

### 지원 Mode

```text
1 = Profile Position
3 = Profile Velocity
6 = Homing
```

### 중요

이 함수는 SDO를 사용한다.

따라서 Communication Thread가 실행 중이면 호출하지 않는다.

일반 UI에서는 Servo 비동기 명령 State Machine이 Mode 전환을 처리하므로 직접 호출할 필요가 거의 없다.

---

# 27. IO Runtime

## DaoEngine_GetIoRuntimeInfo

```cpp
int DaoEngine_GetIoRuntimeInfo(
    int logicalIoIndex,
    DaoIoRuntimeInfo* runtimeInfo);
```

### 목적

FASTECH EtherCAT IO의 최신 Input / Output 상태를 조회한다.

### logicalIoIndex

0부터 시작.

### 반환

```text
1 = 성공
0 = 실패
```

### 주요 출력

```text
configured
inputBytes
outputBytes

communicationRunning
hasValidInputData

lastWkc
expectedWkc

outputCommand
latestInput
```

### 권장 Polling

```text
20 ~ 50 ms
```

---

# 28. IO Output

## DaoEngine_SetIoOutputCommand

```cpp
int DaoEngine_SetIoOutputCommand(
    int logicalIoIndex,
    unsigned short outputValue);
```

### 목적

지정 IO Slave의 Output Bit 값을 설정한다.

### IN8OUT8

하위 8 Bit 사용.

```text
Bit 0 ~ Bit 7
```

### IN16OUT16

16 Bit 전체 사용.

```text
Bit 0 ~ Bit 15
```

### 예

Output 0번 ON:

```cpp
DaoEngine_SetIoOutputCommand(
    0,
    0x0001);
```

Output 전체 OFF:

```cpp
DaoEngine_SetIoOutputCommand(
    0,
    0x0000);
```

### 반환

```text
1 = 명령값 저장 성공
0 = 실패
```

실제 EtherCAT Output PDO 반영은 Communication Thread에서 수행한다.

---

# 29. ADC Runtime

## DaoEngine_GetDaoAdcRuntimeInfo

```cpp
int DaoEngine_GetDaoAdcRuntimeInfo(
    int physicalSlaveIndex,
    DaoAdcRuntimeInfo* runtimeInfo);
```

### 중요

현재 이 함수는 다른 일반 ADC API와 달리:

```text
Physical Slave Index
```

를 사용한다.

일반 Logical ADC Index가 아니다.

향후 API 정리 시 Logical Index 방식으로 통일을 검토할 수 있다.

### 목적

ADC의 가장 최근 Runtime 상태를 읽는다.

### 주요 출력

```text
communicationRunning
hasValidData

lastWkc
expectedWkc

latestData

lowLevelFiltered
powerLineFiltered
zeroedValue
calibratedValue
filteredValue

stableCaptureActive
stableCaptureType
stableCaptureCollectedCount
stableCaptureSampleCount
```

### UI 현재값 표시

일반적으로:

```text
filteredValue
```

를 사용한다.

### Polling

```text
20 ~ 50 ms
```

권장.

---

# 30. ADC Signal Processing 구조

현재 ADC 처리 순서:

```text
Raw
 ↓
Low-Level LPF
 ↓
Power Line Notch
 ↓
Zero
 ↓
Calibration
 ↓
Median 3
 ↓
User N Moving Average
 ↓
Filtered Value
 ↓
Ring Buffer
```

---

## 30.1 Low-Level Filter

현재 1차 Low-Pass Filter 사용.

현재 Alpha:

```text
0.1
```

이 Filter는 Engine 내부 기본 처리이므로 일반 UI에서 직접 설정하지 않는다.

---

## 30.2 Power Line Filter

지원 Mode:

```text
0 = OFF
1 = 50 Hz
2 = 60 Hz
3 = 120 Hz
4 = 50 + 60 Hz
5 = 60 + 120 Hz
```

한국 환경에서는 일반적으로:

```text
5 = 60 + 120 Hz
```

사용을 고려할 수 있다.

단 최종 선택은 장비 환경과 측정 신호 특성에 따라 결정한다.

---

## 30.3 Median Filter

3 Sample Median Filter를 사용한다.

목적:

```text
일시적으로 한 Sample만 크게 튀는 Spike 억제
```

---

## 30.4 User N Moving Average

사용자가 N값을 선택할 수 있다.

지원 범위:

```text
1 ~ 64
```

기본:

```text
16
```

N이 커질수록 표시값은 안정되지만 응답속도가 늦어진다.

---

# 31. ADC Power Line Filter 설정

## DaoEngine_SetAdcPowerLineFilterMode

```cpp
int DaoEngine_SetAdcPowerLineFilterMode(
    int logicalAdcIndex,
    int mode);
```

### Index

Logical ADC Index.

### mode

```text
0 = OFF
1 = 50Hz
2 = 60Hz
3 = 120Hz
4 = 50Hz + 60Hz
5 = 60Hz + 120Hz
```

### 반환

```text
1 = 성공
0 = 실패
```

### 권장 사용 시점

Calibration 또는 측정 설정 화면.

시험 중 매 Sample마다 호출하는 함수가 아니다.

---

# 32. ADC N Filter 설정

## DaoEngine_SetAdcFilterN

```cpp
int DaoEngine_SetAdcFilterN(
    int logicalAdcIndex,
    unsigned int filterN);
```

### 목적

최종 Moving Average N값을 설정한다.

### 범위

```text
1 ~ 64
```

### 기본

```text
16
```

### 반환

```text
1 = 성공
0 = 실패
```

### 중요

N값 변경 시 기존 Moving Average History는 초기화된다.

---

# 33. ADC Zero

## DaoEngine_SetAdcZero

```cpp
int DaoEngine_SetAdcZero(
    int logicalAdcIndex);
```

### 목적

ADC의 무부하 기준을 0으로 설정한다.

### 동작

Zero 요청 시 현재 한 Sample을 즉시 Zero Offset으로 사용하지 않는다.

현재 Engine은 유효 ADC Sample:

```text
600 samples
```

를 평균하여 Zero Offset을 결정한다.

현재 명목 2000 sample/sec 기준:

```text
약 0.3초
```

### 반환

```text
1 = Zero 작업 시작 성공
0 = 실패
```

### 중요

반환 `1`은 Zero 완료가 아니다.

### 완료 확인

`DaoEngine_GetDaoAdcRuntimeInfo()`에서:

```text
stableCaptureActive == 0
```

확인.

Zero 진행 중에는:

```text
stableCaptureCollectedCount
stableCaptureSampleCount
```

를 이용하여 진행상황을 표시할 수 있다.

---

# 34. ADC Calibration

## DaoEngine_SetAdcCalibration

```cpp
int DaoEngine_SetAdcCalibration(
    int logicalAdcIndex,
    double referenceValue);
```

### 목적

현재 기준하중을 이용하여 ADC Scale을 계산한다.

### 선행조건

```text
ADC 정상 데이터 수신
Zero 완료
referenceValue != 0
```

### 현재 동작

Calibration 요청 후:

```text
2000 Sample 안정화 대기
+
4000 Sample 평균
```

을 수행한다.

2000 SPS 기준:

```text
안정화 약 1초
측정 평균 약 2초
```

### referenceValue

사용자가 원하는 물리 단위의 기준값.

예:

1kg 분동을 gram 단위로 사용한다면:

```cpp
referenceValue = 1000.0;
```

이후 ADC 값도 gram 단위로 표시된다.

### 반환

```text
1 = Calibration 작업 시작
0 = 실패
```

### 완료 확인

Runtime:

```text
stableCaptureActive == 0
```

---

# 35. ADC Zero / Calibration 권장 순서

일반적인 초기 설정:

```text
Power Line Filter 설정
        ↓
Filter N 설정
        ↓
무부하 상태
        ↓
SetAdcZero()
        ↓
stableCaptureActive == 0 대기
        ↓
기준 분동 인가
        ↓
SetAdcCalibration(referenceValue)
        ↓
stableCaptureActive == 0 대기
        ↓
기준 분동 제거
        ↓
실제 값 확인
```

---

# 36. ADC Ring Buffer

ADC 고속 데이터 저장을 위해 Engine 내부에 Ring Buffer를 사용한다.

현재 Buffer Size:

```text
8192 samples
```

ADC 명목 처리속도 약:

```text
2000 samples/sec
```

따라서 Buffer는 약:

```text
4.1초
```

분량을 보관할 수 있다.

---

## 36.1 Ring Buffer 저장값

현재 첫 번째 구현에서는 각 Sample마다:

```text
sampleIndex
filteredValue
```

를 저장한다.

---

## 36.2 Buffer Overflow

UI 또는 저장 Thread가 너무 오랫동안 읽지 않으면 Buffer가 가득 찬다.

Buffer가 가득 찬 경우:

```text
가장 오래된 Sample 제거
새 Sample 저장
overflowCount 증가
```

형태로 동작한다.

---

# 37. DaoEngine_GetAdcRingBufferInfo

```cpp
int DaoEngine_GetAdcRingBufferInfo(
    int logicalAdcIndex,
    unsigned int* sampleCount,
    unsigned long long* overflowCount);
```

### 목적

현재 Ring Buffer에 쌓여있는 Sample 개수와 Overflow 횟수를 읽는다.

### 출력

`sampleCount`

현재 읽을 수 있는 Sample 수.

`overflowCount`

Buffer Full로 인해 오래된 Sample이 덮어쓰기 된 횟수.

### 반환

```text
1 = 성공
0 = 실패
```

---

# 38. DaoEngine_ReadAdcRingBuffer

```cpp
int DaoEngine_ReadAdcRingBuffer(
    int logicalAdcIndex,
    DaoAdcBufferedSample* samples,
    unsigned int maxSampleCount,
    unsigned int* readSampleCount);
```

### 목적

Ring Buffer의 오래된 Sample부터 Batch 방식으로 읽는다.

### 중요

이 함수는 **소비형 Read**이다.

읽은 데이터는 Ring Buffer에서 제거된다.

즉:

```text
Read
→ Tail 이동
→ ringBufferCount 감소
```

한다.

### maxSampleCount

한 번에 최대 몇 Sample을 읽을지 지정.

예:

```text
256
512
```

등.

### readSampleCount

실제로 읽은 Sample 개수.

### 반환

```text
1 = 정상
0 = 실패
```

Buffer가 비어있어 읽은 Sample이 0개인 경우도 정상 호출일 수 있다.

---

# 39. DaoEngine_ClearAdcRingBuffer

```cpp
int DaoEngine_ClearAdcRingBuffer(
    int logicalAdcIndex);
```

### 목적

ADC Ring Buffer를 초기화한다.

초기화되는 값:

```text
Head
Tail
Count
Sample Index
Overflow Count
```

### 권장 사용 시점

새 시험 시작 직전.

예:

```text
Zero / Calibration 완료
        ↓
Ring Buffer Clear
        ↓
시험 시작
```

---

# 40. ADC UI 데이터 저장 권장 구조

UI Thread에서 2000 SPS 데이터를 직접 파일로 하나씩 쓰지 않는다.

권장 구조:

```text
Engine 2ms Communication
        ↓
ADC Ring Buffer
        ↓
UI 또는 Data Acquisition Timer
20~50ms
        ↓
Batch Read
        ↓
Application Memory Queue
        ↓
별도 File Writer
        ↓
CSV / Binary / DB
```

---

## 40.1 UI 표시와 저장 분리

화면 Indicator:

```text
20 ~ 50ms Runtime Polling
```

그래프:

필요에 따라 Down Sampling.

실제 데이터 저장:

Ring Buffer Batch를 이용한다.

---

## 40.2 예

2000 SPS에서 20ms마다 Ring Buffer를 읽으면 대략:

```text
40 samples
```

50ms마다 읽으면:

```text
약 100 samples
```

정도가 쌓인다.

현재 Engine의 Batch Read 구조에서는 이 정도 양은 충분히 작다.

---

# 41. ADC Diagnostic Capture

Diagnostic Capture는 일반 시험 데이터 저장용 기능이 아니다.

주 용도:

```text
ADC Noise 분석
Filter 성능 분석
개발 검증
서비스 진단
```

---

## 41.1 DaoEngine_StartAdcDiagnosticCapture

```cpp
int DaoEngine_StartAdcDiagnosticCapture(
    int logicalAdcIndex,
    unsigned int targetSampleCount);
```

지정 개수만큼 ADC 처리 단계별 Sample을 Memory에 저장한다.

---

## 41.2 DaoEngine_GetAdcDiagnosticCaptureInfo

```cpp
int DaoEngine_GetAdcDiagnosticCaptureInfo(
    int logicalAdcIndex,
    int* captureActive,
    unsigned int* capturedSampleCount,
    unsigned int* targetSampleCount);
```

Capture 진행상황 확인.

---

## 41.3 DaoEngine_GetAdcDiagnosticSample

```cpp
int DaoEngine_GetAdcDiagnosticSample(
    int logicalAdcIndex,
    unsigned int sampleIndex,
    DaoAdcDiagnosticSample* sample);
```

### 반환 데이터

```text
Raw
LowLevelFiltered
PowerLineFiltered
ZeroedValue
CalibratedValue
MedianFilteredValue
FilteredValue
```

Noise 분석 등에 사용한다.

---

# 42. ADC Low-Level / Diagnostic API

다음 함수들은 일반 장비 UI에서 직접 사용할 필요가 거의 없다.

```text
DaoEngine_ValidateDaoAdcPdo

DaoEngine_RequestDaoAdcSafeOp

DaoEngine_ExchangeDaoAdcProcessDataOnce

DaoEngine_PrimeDaoAdcProcessData

DaoEngine_RequestDaoAdcOperational

DaoEngine_ReadDaoAdcOnce
```

이 함수들은 DAO ADC 개발 초기 단계의 상태전환, PDO 검증, 단발 통신 검증 등을 위해 만들어진 Low-Level API이다.

일반 Application에서는 전체 Slave 공통 API를 사용한다.

예:

```text
RequestAllSlavesPreOp
MapProcessData
RequestAllSlavesSafeOp
RequestAllSlavesOperational
StartCommunication
```

---

# 43. WKC

WKC는 EtherCAT Working Counter이다.

Engine은 PDO Mapping 후 Expected WKC를 계산한다.

Runtime에서:

```text
lastWkc
expectedWkc
```

를 제공한다.

정상 통신 예:

```text
Last / Expected WKC = 6 / 6
```

ADC + Servo + IO 조합에서는 전체 PDO 구조에 따라 다른 Expected WKC가 생성된다.

Engine에서는 기본적으로:

```text
actualWkc >= expectedWkc
```

일 때 정상 Frame으로 판단한다.

---

# 44. 일반 UI Polling 원칙

UI는 다음 값을 약 20~50ms 주기로 읽는 것이 좋다.

```text
Servo Runtime
IO Runtime
ADC Runtime
```

### 예

```text
UI Timer = 20ms

Servo[0] 현재 위치
Servo[0] 상태

IO[0] Input

ADC[0] 최신 표시값
```

EtherCAT Communication Thread는 이와 별개로 2ms 주기를 계속 유지한다.

---

# 45. UI에서 반복 호출하면 안 되는 함수

다음과 같은 명령 함수는 UI Timer에서 반복 호출하지 않는다.

```text
ServoOn
ServoOff
ServoHome
ServoMoveAbsolute
ServoStop
SetAdcZero
SetAdcCalibration
```

이 함수들은 사용자 명령 또는 시험 Sequence에서 한 번 호출한다.

그 이후 상태는 Runtime Polling으로 확인한다.

---

# 46. Servo + IO 장비 기본 사용 흐름

ADC가 없는 일반 하드웨어 제어 장비 예:

```text
Initialize
 ↓
OpenAdapter
 ↓
ScanSlaves
 ↓
Servo Count 확인
IO Count 확인
 ↓
PRE-OP
 ↓
MapProcessData
 ↓
SAFE-OP
 ↓
OP
 ↓
StartCommunication
 ↓
Servo Runtime Polling
IO Runtime Polling
 ↓
사용자 Servo 명령
IO Output
 ↓
StopCommunication
 ↓
CloseAdapter
 ↓
Shutdown
```

이 구조에서는 고속 ADC 저장이 없으므로 UI 부하는 상대적으로 작다.

---

# 47. Servo + IO + ADC 장비 기본 흐름

측정 기능이 포함된 장비:

```text
Engine 시작
 ↓
Servo / IO / ADC 검색
 ↓
OP
 ↓
Communication 시작
 ↓
ADC Filter 설정
 ↓
ADC Zero
 ↓
ADC Calibration
 ↓
Ring Buffer Clear
 ↓
시험 시작
 ↓
Servo 운전
IO 제어
ADC Batch Read
 ↓
데이터 저장
 ↓
시험 종료
 ↓
Servo Stop
 ↓
저장 종료
```

---

# 48. ADC 시험 시작 권장 흐름

```text
1. Communication 정상 확인

2. ADC Runtime hasValidData 확인

3. Power Line Filter 설정

4. Filter N 설정

5. Zero

6. Zero 완료 대기

7. 필요 시 Calibration

8. Calibration 완료 대기

9. Ring Buffer Clear

10. 시험 시작

11. Ring Buffer Batch Read

12. 저장

13. 시험 종료
```

---

# 49. Servo 명령 사용 예

절대 위치 이동:

```cpp
int result =
    DaoEngine_ServoMoveAbsolute(
        0,
        10000,
        1000,
        1000,
        1000,
        0);

if (result == 1)
{
    // 명령 접수 성공
}
```

이후 UI Timer에서:

```cpp
DaoServoRuntimeInfo runtime{};

DaoEngine_GetServoRuntimeInfo(
    0,
    &runtime);

if (runtime.commandState == 3)
{
    // 이동 완료
}
```

---

# 50. ADC Ring Buffer 사용 예

```cpp
constexpr unsigned int MAX_READ = 256;

DaoAdcBufferedSample samples[MAX_READ]{};

unsigned int readCount = 0;

if (DaoEngine_ReadAdcRingBuffer(
    0,
    samples,
    MAX_READ,
    &readCount) == 1)
{
    for (unsigned int i = 0;
         i < readCount;
         ++i)
    {
        // samples[i].sampleIndex
        // samples[i].filteredValue
    }
}
```

---

# 51. 반환값 기본 원칙

대부분 공개 API는:

```text
1 = 성공
0 = 실패
```

형태를 사용한다.

하지만 명령 함수에서는 의미에 주의한다.

예:

```cpp
DaoEngine_ServoMoveAbsolute(...)
```

가 `1`을 반환한 것은:

```text
명령이 정상적으로 접수되었다.
```

는 뜻이다.

```text
Servo가 목표 위치에 도착했다.
```

는 뜻이 아니다.

실제 완료는 Runtime의 `commandState`를 확인해야 한다.

---

# 52. 함수 분류

## 52.1 일반 Application용

```text
DaoEngine_Initialize
DaoEngine_Shutdown

DaoEngine_GetAdapterCount
DaoEngine_GetAdapterInfo
DaoEngine_OpenAdapter
DaoEngine_CloseAdapter

DaoEngine_ScanSlaves
DaoEngine_GetLogicalDeviceCount
DaoEngine_GetLogicalDeviceInfo

DaoEngine_RequestAllSlavesPreOp
DaoEngine_MapProcessData
DaoEngine_RequestAllSlavesSafeOp
DaoEngine_RequestAllSlavesOperational

DaoEngine_StartCommunication
DaoEngine_StopCommunication
DaoEngine_IsCommunicationRunning

DaoEngine_GetServoRuntimeInfo
DaoEngine_ServoOn
DaoEngine_ServoOff
DaoEngine_ServoHome
DaoEngine_ServoMoveAbsolute
DaoEngine_ServoVelocity
DaoEngine_ServoJogPositive
DaoEngine_ServoJogNegative
DaoEngine_ServoStop

DaoEngine_GetIoRuntimeInfo
DaoEngine_SetIoOutputCommand

DaoEngine_GetDaoAdcRuntimeInfo
DaoEngine_SetAdcPowerLineFilterMode
DaoEngine_SetAdcFilterN
DaoEngine_SetAdcZero
DaoEngine_SetAdcCalibration

DaoEngine_GetAdcRingBufferInfo
DaoEngine_ReadAdcRingBuffer
DaoEngine_ClearAdcRingBuffer
```

---

## 52.2 개발 / Diagnostic용

```text
DaoEngine_GetProcessDataMapInfo
DaoEngine_GetSlavePdoInfo

DaoEngine_ValidateDaoAdcPdo
DaoEngine_RequestDaoAdcSafeOp
DaoEngine_ExchangeDaoAdcProcessDataOnce
DaoEngine_PrimeDaoAdcProcessData
DaoEngine_RequestDaoAdcOperational
DaoEngine_ReadDaoAdcOnce

DaoEngine_StartAdcDiagnosticCapture
DaoEngine_GetAdcDiagnosticCaptureInfo
DaoEngine_GetAdcDiagnosticSample
```

---

## 52.3 Low-Level Servo용

```text
DaoEngine_SetServoOutputCommand
DaoEngine_SetServoOperationMode
```

일반 UI에서는 가능한 한 상위 Servo API를 사용한다.

---

# 53. Engine 설계 원칙

DAO EtherCAT Engine의 중요한 설계 원칙은 다음과 같다.

```text
Engine은 장비 목적을 모른다.
```

Engine은:

```text
Servo를 움직이고
ADC 값을 전달하고
IO를 읽고 쓰고
통신 상태를 제공한다.
```

상위 UI는:

```text
왜 Servo를 움직이는지
어떤 위치로 가야 하는지
어떤 ADC가 어떤 센서인지
IO Bit가 어떤 의미인지
시험을 언제 종료할지
무엇을 저장할지
```

를 결정한다.

이 역할 분리를 유지해야 하나의 Engine DLL을 여러 종류의 장비에서 재사용할 수 있다.

---

# 54. 현재 확인된 실제 구성

현재 개발 과정에서 확인된 주요 구성:

```text
ADC only
→ 정상 Slave Scan
→ Logical ADC 1
→ OP
→ Communication 시작 정상
```

그리고:

```text
Servo + FASTECH IO
→ Servo 1
→ ADC 0
→ IO 1
→ OP
→ Communication 정상
→ Servo Runtime 정상
→ IO Runtime 정상
→ WKC 정상
```

따라서 Engine은 특정 Slave 종류가 없더라도 현재 검색된 지원 장치로 동작하는 구조로 사용한다.

---

# 55. 현재 ADC 처리속도

EtherCAT Communication:

```text
2ms
500 frames/sec
```

한 ADC Frame:

```text
4 sequential samples
```

따라서 현재 명목 ADC 처리속도:

```text
500 × 4
=
약 2000 samples/sec
```

이다.

---

# 56. PC 및 UI 성능 관련 기본 원칙

Servo + IO만 사용하는 장비에서는:

```text
위치 표시
Servo 상태
IO 표시
```

정도가 주 작업이므로 일반적인 PC에서도 부담이 크지 않을 것으로 예상한다.

다수 ADC와 실시간 그래프, 고속 저장이 포함되는 장비는 Application 부하가 증가할 수 있다.

따라서 최종 PC 권장 사양은 Engine 완성 후 Reference UI에서 실제 측정하여 결정한다.

향후 검증 항목:

```text
CPU 사용률
Memory 사용량
UI 반응성
ADC 데이터 손실
Ring Buffer Overflow
CSV 저장속도
장시간 운전 안정성
```

---

# 57. Reference UI 개발 시 권장 구조

```text
Main UI Thread
    ├─ Servo 상태 표시
    ├─ ADC 최신값 표시
    ├─ IO 상태 표시
    └─ 사용자 입력

Runtime Polling
    └─ 약 20~50ms

ADC Data Acquisition
    └─ Ring Buffer Batch Read

File Writer
    └─ 별도 Worker / Task
```

파일 저장을 Main UI Thread에서 직접 장시간 수행하지 않는 것을 권장한다.

---

# 58. 향후 문서 유지관리 규칙

공개 API가 추가 또는 변경될 경우 본 문서도 같이 수정한다.

특히 다음 변경은 반드시 문서에 반영한다.

```text
새 Public API 추가
함수 인자 변경
Return 값 변경
Logical / Physical Index 변경
Servo Command State 추가
ADC Filter 변경
Ring Buffer 구조 변경
지원 Slave 추가
PDO Mapping 변경
통신 주기 변경
```

---

# 59. 현재 주의사항 및 향후 정리 대상

현재 개발 버전에서 향후 확인 또는 정리할 항목:

1. `DaoEngine_GetDaoAdcRuntimeInfo()`는 현재 Physical Slave Index를 사용하고 있으므로 다른 ADC User API의 Logical Index 방식과 통일 여부 검토.

2. Public Header 내부의 일부 개발 초기 주석은 Encoding 또는 내용 정리가 필요함.

3. Servo Public PDO 구조의 `#pragma pack` 구성을 최종 정리할 필요가 있음.

4. 개발/Diagnostic API와 일반 User API를 향후 Header에서 구역별로 명확히 구분하는 것을 권장.

5. Power Line Filter Mode는 향후 Public Enum으로 공개하면 Magic Number 사용을 줄일 수 있음.

6. Ring Buffer Clear와 Zero / Calibration의 관계는 Application 정책에 따라 명확히 유지한다. 현재 Zero 또는 Calibration이 자동으로 Ring Buffer를 Clear하지 않으므로 새 시험 전 UI가 명시적으로 Clear하는 방식을 권장한다.

7. 장치별 기능 추가 시 Engine에 시험 목적을 넣지 않고 장치 자체 기능만 추가하는 원칙을 유지한다.

---

# 60. 요약

DAO EtherCAT Engine의 일반 Application 사용 흐름은 다음과 같다.

```text
Initialize
 ↓
Adapter Search
 ↓
Open Adapter
 ↓
Scan Slaves
 ↓
Logical Device 확인
 ↓
PRE-OP
 ↓
PDO Mapping
 ↓
SAFE-OP
 ↓
OP
 ↓
Start Communication
 ↓
Runtime Polling
 ↓
Servo / ADC / IO 사용
 ↓
Stop Communication
 ↓
Close Adapter
 ↓
Shutdown
```

Servo 명령은:

```text
Command 호출
→ 명령 접수
→ Runtime Polling
→ COMPLETED / ERROR / TIMEOUT 확인
```

ADC 일반 사용은:

```text
Power Filter
→ N Filter
→ Zero
→ Calibration
→ Ring Buffer Clear
→ 시험
→ Batch Read
→ 데이터 저장
```

방식으로 사용한다.

DAO EtherCAT Engine은 하드웨어 통신과 기본 장치 기능만 제공하며, 실제 시험 목적과 장비 시퀀스는 상위 Application에서 구현한다.

---

# End of Document