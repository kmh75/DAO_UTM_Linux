# DAO UTM Network Settings / System Shutdown Implementation Report

## 1. 환경 조사 결과

- Ubuntu 환경에 NetworkManager와 `/usr/bin/nmcli` 1.54.3이 설치되어 있고, NetworkManager 상태는 `connected:full`이었다.
- 조사 시 일반 LAN은 `enp5s0`(connected), EtherCAT 전용 `enp6s0`은 NetworkManager에서 unmanaged 상태였다.
- Qt 6.10.2의 Core, Widgets, DBus 모듈을 사용할 수 있다. 반면 libnm 개발 패키지는 pkg-config에서 발견되지 않았다.
- systemd-logind의 `CanPowerOff` 결과는 `challenge`였다. 즉 활성 desktop session의 PolicyKit agent 또는 제품용 제한 정책이 필요할 수 있다.
- 기존 UI 종료는 `MainWindow::closeEvent()`에서 motion stop을 요청한 뒤 창을 닫는 구조였다. `UtmUiController`의 disconnect/destructor 경로는 `DaoUtm_Shutdown()` 및 `DaoUtm_Disconnect()`를 호출하며, 엔진 shutdown은 control thread join, Servo/communication cleanup을 동기적으로 완료한다.
- 생산 UI에는 ProtocolServer가 연결되어 있지 않으며 별도 recorder worker도 없다. 현재 recording 상태는 sequence runtime을 통해 판정한다.

## 2. NetworkManager 접근 방식 선택 및 이유

`QProcess + nmcli` backend를 선택했다. libnm 개발 의존성이 없고, 새 대형 의존성을 추가하지 않으면서 NetworkManager profile을 표준 방식으로 관리할 수 있기 때문이다.

- shell과 `/bin/sh -c`를 사용하지 않는다.
- 실행 파일과 argument list를 분리한다.
- exit code, stdout/stderr, 시작/완료 timeout을 확인한다.
- EtherCAT/control loop에서는 호출하지 않으며 Setup UI 경로에서만 실행한다.

## 3. NetworkService 구조

`NetworkService`와 주입 가능한 `NetworkBackend`를 추가했다. 실제 backend는 `NmcliNetworkBackend`이며 다음 기능을 제공한다.

- adapter/status/IP/gateway/DNS 조회 및 refresh
- wired DHCP/static 적용
- Wi-Fi enable/disable, scan, connect, disconnect
- IPv4/prefix/gateway/DNS validation
- backend 오류와 timeout의 구조화된 반환

Backend 분리로 unit test가 host network를 변경하지 않고 fake 기반으로 동작한다.

## 4. EtherCAT NIC 보호 방식

현재 MachineProfile/Setup에서 선택된 EtherCAT adapter 이름을 NetworkService에 전달하고 일반 adapter 목록 및 모든 변경 요청에서 제외한다. 이름을 특정 NIC로 하드코딩하지 않았다.

추가로 loopback, non-Ethernet/Wi-Fi type 및 docker, bridge, veth, tun/tap, WireGuard 계열 interface를 일반 사용자 목록에서 제외한다. 실제 장비에서는 EtherCAT NIC를 NetworkManager unmanaged로 유지해야 한다.

## 5. Wired DHCP 구현

선택한 기존 NetworkManager connection profile에 `ipv4.method auto`를 적용하고 수동 address/gateway/DNS 값을 비운 뒤 profile을 활성화한다. EtherCAT adapter 및 유효하지 않은 adapter 요청은 거부한다.

## 6. Wired Static 구현

IPv4 address, prefix(0~32), 선택 gateway, 쉼표 구분 DNS를 검증한 후 기존 profile에 manual 설정을 적용한다. 적용 전에 현재 IPv4 profile 속성을 snapshot하며 activation 실패 시 이전 값을 복원하고 다시 활성화를 시도한다. 새 connection profile을 임의로 생성하지 않는다.

## 7. Wi-Fi 구현

Wi-Fi 장치가 있을 때 enable/disable, rescan, SSID/signal/security 표시, connect/disconnect를 제공한다. 장치가 없으면 정상적인 `No Wi-Fi adapter` 상태로 표시한다. 실제 연결 정보는 NetworkManager profile에 맡기며 application config에는 저장하지 않는다.

## 8. Password handling

- password 입력은 masked widget이다.
- connect 요청 직후 UI 입력을 지운다.
- application log, diagnostics, runtime dump 및 MachineProfile에 저장하지 않는다.
- shell 문자열을 만들지 않는다.

현재 nmcli 방식 특성상 연결 수행 중 password가 잠시 process argument로 노출될 수 있다. 이를 제거하려면 후속 버전에서 NetworkManager secret-agent D-Bus 또는 nmcli password-file/stdin 지원 backend를 검토해야 한다.

## 9. Network UI

기존 Setup page에 Network Settings를 추가했다.

- Wired: adapter, 상태, DHCP/static, IP/prefix/gateway/DNS, Apply
- Wi-Fi: adapter, enabled 상태, 현재 SSID/IP, scan 결과, masked password, Connect/Disconnect
- status label 중심 오류 표시와 작업 중 버튼 비활성화
- 일반 LAN 변경이 monitoring connection을 끊을 수 있다는 안내

Internet 직접 ping은 구현하지 않았다. 이번 버전은 link/IP/gateway/DNS 상태까지만 표시한다.

## 10. ShutdownCoordinator 구조

UI와 분리된 `ApplicationShutdownCoordinator` 및 `SystemPowerService`를 추가했다. Coordinator는 injectable cleanup/power callback을 사용하며 다음을 보장한다.

1. runtime activity precheck
2. 중복 exit 요청 차단
3. 신규 UI polling/command admission 정지
4. 기존 UTM orderly disconnect/shutdown 수행
5. cleanup 성공 검증
6. 선택된 경우에만 OS poweroff 요청

## 11. 정상 application cleanup 순서

현재 생산 구조에서 cleanup은 UI polling 중지 후 기존 `DaoUtm_Disconnect()` 경로를 재사용한다. 이 경로는 UTM shutdown, control thread join, Servo/Engine/EtherCAT close를 수행한다. 완료 후 `DaoUtm_IsRunning()==0` 및 `DaoUtm_IsInitialized()==0`을 확인한다. 실패하면 UI polling을 복구하고 application 종료 및 OS poweroff를 모두 중단한다.

종료 precheck는 Jog, move/stopping, force/hold-force, sequence, auto calibration, recording, critical finalize와 향후 homing 확장 필드를 포함한다. active이면 사용자가 먼저 동작/시험을 중지하도록 종료를 거부하며 임의 강제 중단하지 않는다.

## 12. OS poweroff 방식

Qt DBus로 systemd-logind `org.freedesktop.login1.Manager.PowerOff(true)`를 요청한다. sudo, 비밀번호 저장, root UI 실행을 사용하지 않는다. PowerOff는 UTM cleanup 성공 후에만 호출된다.

## 13. 권한 / PolicyKit / logind 처리

현재 PC는 poweroff 권한이 `challenge`이므로 desktop PolicyKit agent가 인증을 요구할 수 있다. 제품 배포 시에는 DAO UTM 전용 사용자 또는 그룹에 대해 `org.freedesktop.login1.power-off` action만 허용하는 최소 범위 PolicyKit rule을 설치 절차로 제공해야 한다. NetworkManager 변경 권한 역시 활성 local session의 NetworkManager PolicyKit 정책으로 부여하며 application 전체를 root로 실행하지 않는다. 이번 구현은 system policy를 자동 변경하지 않았다.

## 14. Window X 처리

Exit button과 Window close가 동일 Coordinator를 통과한다. Exit checkbox는 기본 OFF이고 저장하지 않는다. Window X는 항상 application-only 경로를 사용하므로 PC poweroff로 이어지지 않는다. 기존처럼 close 시 임의 stop을 발행하지 않고, active runtime이면 동일 precheck로 종료를 거부한다.

## 15. Test 내용

`dao_network_system_policy` deterministic test를 추가했다.

- EtherCAT, loopback, virtual adapter filtering
- DHCP/static 및 invalid IPv4/prefix validation
- special-character Wi-Fi password의 argument 전달과 shell 미사용 확인
- NetworkManager unavailable 처리
- READY app-only 종료
- cleanup 완료 후 poweroff 순서
- Jog/move/sequence/auto-calibration active 종료 거부
- cleanup 실패 시 poweroff 미호출
- 반복 exit 요청의 중복 실행 방지

Setup persistence test는 network/shutdown widgets를 OS 전용 non-persistent field로 명시하도록 갱신했다. MachineProfile schema는 변경하지 않았다.

## 16. Debug CTest 결과

- Configure/build: PASS
- CTest: **9/9 PASS**

## 17. Release CTest 결과

- Configure/build: PASS
- CTest: **9/9 PASS**

## 18. 기존 ABI 영향

UTM RuntimeV1~V6, Basic/UTM public ABI, Protocol V1 및 MachineProfile schema 변경은 없다. 새 코드는 UI/application 내부 service와 test target의 additive 변경이다. EtherCAT Recovery, Homing, Motion, Force, ADC/calibration, Compliance, Auto Calibration 정상 workflow, Sequencer, PDO/SOEM은 변경하지 않았다.

## 19. 실제 PC에서 수동 확인해야 할 항목

- 장비별 EtherCAT adapter 선택값과 NetworkManager unmanaged 상태
- 제품 사용자에 대한 NetworkManager/PolicyKit 최소 권한
- 유선 DHCP/static 적용, 실패 시 profile rollback
- Wi-Fi hardware별 scan/connect/disconnect와 secured network
- LAN 변경 시 Windows Monitoring 연결 영향
- logind poweroff 인증 prompt 또는 제품용 제한 PolicyKit rule
- UTM hardware 연결 상태에서 Exit 및 Window X cleanup 완료
- 실제 shutdown 요청은 별도 사용자 승인 후 수행

## 20. Known limitations

- nmcli 호출은 bounded synchronous QProcess이므로 Setup UI가 작업 중 잠시 응답하지 않을 수 있으나 control loop에는 영향을 주지 않는다.
- Internet availability의 별도 외부 검사는 보류했다.
- 기존 NetworkManager profile이 없는 wired adapter에는 새 profile을 만들지 않는다.
- profile rollback은 best-effort이며 실제 장비/NetworkManager 버전별 검증이 필요하다.
- Wi-Fi password는 저장/로그하지 않지만 nmcli 실행 중 process argv에 일시 노출될 수 있다.
- 현재 생산 UI에 별도 ProtocolServer/recorder worker가 추가되면 Coordinator의 critical-finalize와 worker join 항목을 확장해야 한다.
- 실제 network 변경과 실제 system poweroff는 자동 또는 수동으로 실행하지 않았다.
