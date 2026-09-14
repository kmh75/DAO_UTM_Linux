# DAO UTM Linux — Machine Compliance Auto Calibration Implementation Report

## 1. Result

The approved mixed **C architecture** was implemented. `UtmEngineCore` owns a dedicated, fixed-capacity compliance-calibration controller and services it from the 2 ms control cycle with the same `UtmInputSnapshot` used by Motion and Safety. The UI only submits operator commands, polls progress, reviews pending points, and performs profile persistence.

No physical EtherCAT, Servo ON, motor movement, or load-cell loading was executed during this work.

## 2. Changed and new files

### Engine and public UTM API

- `utm/src/UtmComplianceCalibrationController.h` — new fixed-capacity controller, state, actions, guards, and pending result.
- `utm/src/UtmComplianceCalibrationController.cpp` — new deterministic 2 ms state-machine implementation.
- `utm/src/UtmEngineCore.h`, `utm/src/UtmEngineCore.cpp` — controller ownership, same-cycle input, output arbitration, StopLatch integration, and additive API implementation.
- `utm/include/DaoUtm.Types.h` — additive command source, calibration config/runtime/point/state/fault types, and calibration Stop reason. `UtmRuntimeInfoV6` layout was not changed.
- `utm/include/DaoUtm.Engine.h`, `utm/src/DaoUtm.Engine.cpp` — additive C API entry points.
- `utm/src/UtmMotionController.cpp` — recognizes the additive internal calibration source.
- `CMakeLists.txt` — builds the controller and its offline test.

### Application, profile, and UI

- `ui/include/MachineProfile.h`, `ui/src/MachineProfile.cpp` — independent Compression/Tension profiles and persisted auto-calibration settings.
- `ui/include/UtmUiController.h`, `ui/src/UtmUiController.cpp` — command/poll/pending-save application service; no motion orchestration timer.
- `ui/include/MainWindow.h`, `ui/src/MainWindow.cpp` — pre-check/full-start/abort/review/save UI and concise main-screen directional status.

### Tests

- `app/compliance_auto_calibration_test/main.cpp` — Engine-controller deterministic tests.
- `app/compliance_profile_test/main.cpp` — directional persistence, legacy compatibility, versioning, and independence tests.

## 3. Discarded A-architecture draft

The following UI-driven production approach was removed:

- `compliance/include/UtmComplianceAutoCalibration.h`
- `compliance/src/UtmComplianceAutoCalibration.cpp`
- `UtmUiController::serviceAutoCalibration()` and its 40 ms invocation from UI polling
- chained UI-source calls to `MoveToForce`, `MoveVelocity`, `MoveAbsolute`, and `StopMotion`
- UI-poll samples as calibration capture data

Target-generation concepts, state terminology, directional-profile concepts, the UI layout, and offline-test ideas were retained only where appropriate and moved to the Engine implementation.

## 4. Engine-owned architecture

`UtmEngineCore` exclusively owns `UtmComplianceCalibrationController`. Every controller update receives force and raw machine position originating from one control-cycle snapshot. The controller emits only fixed-size motion/action requests. Existing `UtmMotionController`, `UtmMotionOutputArbiter`, Safety evaluation, StopLatch, ADC zero capture, coordinate conversion, and absolute-motion execution remain the output path.

The controller update path contains no file I/O, JSON, Qt, socket calls, or dynamic containers. Targets and points are fixed arrays of 64 elements. Sorting occurs once when the completed pending result is finalized, outside acquisition states, and uses the fixed array.

## 5. Additive public API

- `DaoUtm_StartCompliancePrecheck`
- `DaoUtm_ConfirmComplianceFullCalibration`
- `DaoUtm_AbortComplianceCalibration`
- `DaoUtm_GetComplianceCalibrationRuntimeV1`
- `DaoUtm_GetCompliancePendingPoints`
- `DaoUtm_DiscardCompliancePending`

All session-changing operations validate `sessionId`. Runtime is provided by a separate V1 getter; the existing RuntimeV6 layout is unchanged.

## 6. Command ownership and arbitration

`UTM_COMMAND_SOURCE_CALIBRATION` was appended as a diagnostic/internal source. Public commands cannot impersonate it. While the controller owns motion:

- UI, REMOTE, SEQUENCER, and Jog start/motion requests are rejected.
- Sequence start is rejected, and calibration start is rejected while a Sequence or motion is active.
- STOP remains accepted and becomes Calibration Abort.
- E-STOP and all existing Safety actions retain higher priority.
- Controller-generated velocity and return commands alone use the internal calibration source.

Calibration-local guard faults additionally latch `UTM_STOP_CALIBRATION_FAULT`, invalidate queued motion, inhibit Jog, and require the existing ACK policy before subsequent operation.

## 7. State machine

The implemented path is:

`IDLE → ZEROING_FORCE → CAPTURING_REFERENCE → PRECHECK_APPROACH → PRECHECK_STABILIZING → STOPPING_PRECHECK → WAITING_FULL_START → APPROACHING_TARGET/FINE_APPROACH → STABILIZING → CAPTURING_POINT → NEXT_POINT → RELEASING_FORCE → STOPPING_RELEASE → RETURNING → COMPLETE_PENDING_SAVE`.

Terminal/exception states are `PRECHECK_EXPIRED`, `ABORTING`, `ABORTED`, and `FAULTED`. Full calibration cannot start without an explicit, current-session confirmation after successful pre-check.

## 8. Compression and Tension profiles

`machineCompliance.schemaVersion` is 2 and contains independent `compression` and `tension` objects, each with `enabled`, `version`, and `points`. Each curve is independently validated and restored. Stored points are canonical signed N/mm and are sorted by force before becoming pending.

The legacy `complianceCompensation` object remains readable and unchanged. It is never copied into either directional curve and never automatically enabled. Runtime display/analysis selection uses signed force plus the existing `forceDirectionSign`; near zero, the explicitly selected direction is retained.

## 9. Force and position capture

Target decisions use the existing directional force convention. Stored points use raw signed `measuredForceN` and signed raw-machine deformation:

`deformationMm = averagedMachinePositionMm - complianceReferenceMachinePositionMm`

Force and machine position are accumulated from the same controller update. Both stabilization duration and minimum stable sample count must pass. Leaving tolerance resets the timer and both accumulators. No `abs()` is applied to stored signs, and the existing Position Zero is not changed.

## 10. Limits and guards

The effective maximum is the minimum available value among 90% load-cell capacity, enabled overload limit, and optional manufacturer/calibration limit. The calibration hard-force guard remains active even when normal overload protection is disabled.

Implemented guards include hard force, full/pre-check/release travel, point/release/confirmation timeouts, wrong force polarity, wrong position direction, insufficient force rise, excessive force jump, overshoot, opposite force during release, invalid/stale force indication, minimum stable samples, E-STOP, External STOP, limits, Servo fault, communication recovery/fault, overload, StopLatch, and motion-output/action failure.

Safety conditions and local guard faults immediately inhibit new calibration output. A fault clears the pending acquisition and never performs release, return, or automatic restart. Existing active curves are not touched.

## 11. Pre-check and confirmation timeout

Pre-check performs force zero, stable zero-force/reference-position capture, low-force approach, polarity/direction/rise/jump/travel checks, stabilization, and complete stop. It then waits at `WAITING_FULL_START` for explicit confirmation.

Confirmation timeout never starts full calibration. If Safety remains normal, the controller performs only low-speed force release, verifies the release threshold/stable window and stop completion, then ends as `PRECHECK_EXPIRED`; it does not return automatically. Any Safety/Fault condition suppresses this release path.

## 12. Target generation and speed policy

Targets begin at zero, advance by Force Step, always include Maximum Force once, reject duplicate/overlapping tolerance bands, and reject more than 64 points. Approach speed selection uses target ratio plus instantaneous error: Approach below 70%, Calibration at/above 70%, Fine at/above 90%, and Fine near every target.

The controller does not call public `MOVE_TO_FORCE`/`HOLD_FORCE` from the UI. It reuses existing coordinate conversion and velocity-output arbitration internally while owning the motion source.

## 13. Normal release and return

Only a fully captured curve enters normal release:

`Final capture → low-speed reverse release → threshold stable window → controlled stop → stop complete and Safety/StopLatch recheck → raw start test-position absolute return → pending result`.

Release travel, timeout, opposite-force, force-jump, and all Safety guards remain active. Abort and Fault never auto-release or auto-return.

## 14. Pending result, save, and version ordering

Normal completion creates an isolated Engine pending result and does not modify active profile/runtime curves. UI choices are Discard, Save Curve, and Save & Enable.

For save, the application constructs a candidate profile with `oldVersion + 1`, validates it, and atomically writes it through `QSaveFile`. Only successful commit replaces the active in-memory profile/runtime and discards Engine pending data. Write failure leaves active version/curve unchanged and keeps pending data available for retry or discard. Saving one direction does not modify the other.

## 15. UI changes

Calibration UI provides Compression/Tension pre-check start, maximum/step forces, four speeds, full/pre-check travel, pre-check force, tolerance, stabilization, confirmation timeout, explicit Full Start, Abort, Discard, Save, and Save & Enable. It polls and displays state, target/current force, reference/current position, deformation, captured count, travel, effective force ceiling, and fault.

The existing manual curve editor remains available as the advanced/manual path. The Main screen shows only `COMPLIANCE C: ON/OFF / T: ON/OFF`.

## 16. Protocol impact

No Protocol V1 frame, message ID, payload, LIVE_DATA layout, server, or SnapshotMailbox code was changed for Auto Calibration. Remote Auto Calibration is not enabled. The existing offline Protocol V1 integration test remains passing, including its byte/framing behavior.

## 17. Basic Engine and existing control algorithms

- **Basic Engine public ABI:** unchanged.
- **Basic Engine source:** unchanged by this implementation.
- **UTM ABI:** additive functions/types only; no existing API removed and RuntimeV6 layout unchanged.
- **EtherCAT Master/PDO/SOEM:** unchanged.
- **Servo driver and ADC DSP:** unchanged.
- **Force Calibration and coordinate semantics:** unchanged.
- **Existing Motion/Safety algorithms:** not rewritten or weakened; the implementation adds ownership/guard integration and reuses their output paths.
- **Corrected displacement:** never used for calibration motion, Servo target, limit, travel, completion, Sequence, Jog, Move To Force, or Hold Force.
- **Extensometer:** not read or modified by Auto Calibration.

## 18. Offline tests added/covered

The deterministic controller/profile tests cover target generation, non-divisible final target, 64-point limit, 90% capacity, overload/manufacturer precedence, hard guard with overload disabled, start validation, pre-check barrier and timeout, stale session rejection, polarity/direction/rise/jump faults, pre-check/full travel, point timeout, stabilization reset, minimum stable samples, same-cycle signed averaging, Compression/Tension signs, sorted points, safety inputs, abort, no return after fault, normal release/stop/return order, opposite-force guard, pending isolation, directional persistence/independence, legacy non-migration, and save-version increment.

Existing tests additionally cover Compliance interpolation/clamp/disabled/raw-corrected behavior, profile restart/persistence, UI startup/save/restart, communication fault injection, force-unit boundaries, and Protocol V1 offline integration.

## 19. Build and CTest results

Commands:

```text
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j2
ctest --test-dir build-release --output-on-failure
```

Result: Clean Release build succeeded. CTest passed **8/8**, with **0 failures**. Tests were:

1. `dao_compliance_compensation`
2. `dao_compliance_auto_calibration_engine`
3. `dao_protocol_v1_offline_integration`
4. `dao_force_unit_profile_lifecycle`
5. `dao_compliance_directional_profile`
6. `dao_setup_persistence_restart`
7. `dao_production_ui_startup_save_restart`
8. `dao_communication_policy_fault_injection`

## 20. Hardware execution confirmation

Actual EtherCAT hardware was **not executed**. No `--hardware`, Servo ON, EtherCAT OP motion, motor movement, or physical load was requested or performed.

## 21. Mandatory commissioned-hardware verification

Before production enablement, perform a controlled commissioning procedure with a sacrificial/low-capacity setup and independent physical stop access:

1. Confirm raw Force polarity for Compression and Tension against `forceDirectionSign`.
2. Confirm machine-position direction for both commanded directions.
3. Confirm upper/lower limit mapping and escape behavior.
4. Verify E-STOP, External STOP, Servo fault, communication recovery/fault, and StopLatch ACK during every calibration phase.
5. Validate force-zero completion behavior and reference stability under real ADC noise.
6. Measure actual force rise per travel and tune rise/travel/jump/overshoot guards conservatively.
7. Verify Approach/Calibration/Fine speeds and deceleration do not overshoot the hard ceiling.
8. Verify disabled normal overload still leaves the calibration hard guard effective.
9. Verify pre-check timeout release, normal final release, stop completion, and raw-position return.
10. Inject Abort and Fault during approach, stabilization, release, stop, and return; confirm no automatic return after Abort/Fault.
11. Verify fixture rigidity, backlash, hysteresis, thermal drift, repeatability, and separate Compression/Tension curves.
12. Confirm saved curves against independent measurements and report reconstruction before enabling compensation.

## 22. Remaining risks

- Force polarity, position direction, real deceleration distance, fixture stiffness, and stable-force behavior cannot be proven without commissioned hardware.
- Guard defaults are conservative software defaults, not certified values for a specific machine/load cell; commissioning must set and lock approved values.
- During stabilization the controller commands a controlled stop and requires force to remain inside tolerance. A real machine that relaxes outside tolerance will reset acquisition and eventually time out; it will not silently capture an unstable point.
- Calibration output uses the existing control-thread synchronization style with a small fixed critical section. No heap/file/network work occurs there, but cycle jitter must still be measured on the production PC under worst-case UI polling.
- Remote Auto Calibration remains intentionally unavailable pending a separate protocol/safety authorization design.
