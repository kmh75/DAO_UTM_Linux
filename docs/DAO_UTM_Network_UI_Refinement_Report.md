# DAO UTM Network UI Refinement Report

Date: 2026-09-14

## 1. Changed Files

- `ui/src/MainWindow.cpp`
- `ui/include/MainWindow.h`
- `ui/src/NetworkService.cpp`
- `ui/include/NetworkService.h`
- `ui/include/ApplicationShutdownCoordinator.h`
- `app/network_system_test/main.cpp`
- `app/setup_persistence_test/main.cpp`
- `docs/DAO_UTM_Network_UI_Refinement_Report.md`

The existing NetworkService/backend API meaning, shutdown ordering, power-off backend, and machine-control behavior were retained.

## 2. Main Exit UI Location

The explicit Exit controls now appear in a compact control strip at the bottom-right of the Main page, below the main graph and jog controls:

- `Power off PC after exit` checkbox, default OFF and not persisted
- `EXIT` button

The Setup page no longer contains application exit or system power-off controls. The window close button still requests app-only orderly shutdown and never reads the power-off checkbox.

## 3. Status Bar Handling

The prior bottom message label is now hosted by a real `QStatusBar`. Normal command/profile information, network operation summaries, rejected exit reasons, and `Closing application...` are shown there. Safety-critical confirmation dialogs in the calibration workflow were not changed.

Network failures use a concise status-bar summary such as `Network configuration failed.` The original backend/nmcli message remains visible in the relevant Ethernet or Wi-Fi status area for engineering diagnosis.

## 4. Network UI Structure

Setup retains one compact `Network Settings` section split into two familiar panels:

- Ethernet: Adapter, Status, IP Mode, conditional manual fields, Apply, Refresh
- Wi-Fi: Adapter, Status, Available Networks, Password, radio/scan/connect actions

This avoids a new UI framework or service redesign while clearly separating the two network types.

## 5. DHCP / Manual UI Behavior

`Automatic (DHCP)` is the default selection. IP Address, Subnet Prefix, Gateway, and DNS controls are hidden and disabled in this mode. Selecting `Manual` makes the same controls visible and enabled. Apply continues to call the existing DHCP or static NetworkService operation, including validation and rollback behavior.

## 6. Wi-Fi UI Behavior

Wi-Fi presents adapter state, connection/profile name, IP address, scanned SSID/signal/security rows, and a masked password field. Scan and connection controls are disabled when the adapter is unavailable. Connect is disabled while connected, and Disconnect is enabled only while connected. No-adapter state reads `No Wi-Fi adapter available.`

## 7. EtherCAT NIC Protection

The configured EtherCAT adapter remains filtered by `NetworkService::IsUserNetworkAdapter()` before populating either user adapter selector. It receives no DHCP, Manual, Apply, Wi-Fi, or disconnect control. Setup only shows informational text in the form `EtherCAT Interface: enp6s0 (Managed by UTM)`.

Read-only host output confirmed `enp6s0` is currently `ethernet:unmanaged`; it is not exposed as a configurable adapter.

## 8. nmcli `--separator` Error

Cause: the installed `nmcli 1.54.3` supports `--escape` but does not advertise or accept `--separator`. Both device enumeration and Wi-Fi scanning previously requested the unsupported option.

Fix: commands now use supported terse output with `-t --escape yes`. A small parser splits unescaped colons and decodes escaped colons/backslashes, so SSIDs and connection names containing delimiters remain intact. `QProcess` still receives a fixed program plus argument list; `/bin/sh -c` was not introduced, credentials are not logged, and password arguments are not copied to diagnostics.

Actual read-only host enumeration succeeded and returned:

- `enp5s0:ethernet:connected` with IPv4 `192.168.0.24/24`
- `enp6s0:ethernet:unmanaged` (protected EtherCAT interface)
- loopback, which is filtered from the DAO Network Settings UI

## 9. English-only UI

The Korean Exit checkbox, button, note, and active-operation rejection text were replaced with English. A source scan found no Korean literal in `ui/`. OS/nmcli-provided profile names and raw errors remain unmodified as required.

## 10. Tests

Existing tests were retained. Added assertions cover:

- Exit controls are children of Main and not Setup
- power-off checkbox default OFF
- real MainWindow status bar presence
- DHCP manual fields disabled and Manual fields enabled
- escaped terse device and Wi-Fi parsing
- EtherCAT exclusion during adapter enumeration
- absence of `--separator` and presence of supported `--escape`

Existing shutdown policy tests continue to cover app-only exit, optional power-off ordering, active motion/test rejection, cleanup failure, and duplicate shutdown prevention. MainWindow's close event continues to call the app-only path (`false`) independently of the checkbox.

## 11. Debug / Release Results

- Debug configure/build: PASS
- Debug CTest: 9/9 PASS
- Release configure/build: PASS
- Release CTest: 9/9 PASS

The protocol integration test requires local loopback socket access. It failed only inside the restricted sandbox and passed in both configurations when run with normal host permissions.

## 12. Actual PC Manual Checks

Completed safely:

- `nmcli 1.54.3` version/help inspection
- read-only device status enumeration
- read-only IPv4/gateway/DNS query for `enp5s0`

Recommended operator checks:

- Confirm the Setup page panels fit the target 10.1-inch display and scroll naturally.
- Confirm Wi-Fi radio OFF/ON button states on hardware with a Wi-Fi adapter installed.
- Select DHCP/Manual and confirm the conditional fields are visually clear.
- Exercise explicit Exit once inactive, first with power-off unchecked.
- Exercise optional power-off only during an approved maintenance window with systemd-logind permission configured.

## 13. Known Limitations

- The development PC exposed no Wi-Fi adapter in the read-only enumeration, so live scan/connect presentation was not exercised against hardware.
- NetworkManager and authorization errors remain OS-locale text in the detailed panel by policy.
- No DHCP/static profile, Wi-Fi radio/connection, or power state was changed during automated verification.

