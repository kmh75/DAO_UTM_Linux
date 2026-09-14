# Machine Compliance Auto Calibration — Architecture and Safety

## Purpose and boundary

Machine Compliance Compensation removes measured elastic deformation of the loadcell mounting, jig, and relevant machine structure from analysis displacement. It is not force calibration, extensometer calibration, specimen-strain calculation, or a control-coordinate transform.

Canonical units are N, mm, mm/min, mm/s², and monotonic microseconds. Display-unit conversion remains at the UI boundary.

```text
compensationMm = LookupCompliance(measuredForceN)
correctedDisplacementMm = rawTestDisplacementMm - compensationMm
```

Raw displacement is retained. Corrected displacement is limited to display, recording preparation, analysis, and monitoring. Jog, all position and force motion, servo target, position completion, travel, limit, Safety, and Sequencer continue to use raw machine/test coordinates. Extensometer displacement is never compensated.

## Directional curves and compatibility

Profiles contain independent Compression and Tension curves. Each has enabled, version, points, validity through curve validation, and an implicit range from sorted endpoints. Points retain signed canonical force and deformation.

The former `complianceCompensation` object remains readable and writable. When `machineCompliance` is absent, the legacy curve is retained only in legacy fields. It is not copied to or enabled for either direction.

The selected curve is explicit `machineCompliance.activeMode`. Automatic direction inference was not added because the current recipe has no authoritative test mode.

The evaluator retains its fixed maximum of 64 points, minimum of two configured points, finite and duplicate validation, configure-time sorting, allocation-free linear lookup, and endpoint clamp without extrapolation or sign-destroying `abs()`.

## Auto Calibration architecture

`UtmComplianceAutoCalibration` is an application-level, fixed-memory state machine. It never accesses PDOs or Basic Engine servo APIs. It emits requests that `UtmUiController` translates only to existing public UTM operations:

- `DaoUtm_ZeroForce`
- `DaoUtm_MoveToForce`
- `DaoUtm_MoveVelocity`
- `DaoUtm_MoveAbsolute`
- `DaoUtm_StopMotion`

Basic Engine, UTM Engine, servo driver, ADC DSP, EtherCAT Master, existing Motion semantics, and existing Safety semantics are unchanged.

## State machine

```text
IDLE
 → VALIDATING
 → ZEROING_FORCE
 → CAPTURING_REFERENCE
 → PRECHECK_APPROACH
 → PRECHECK_STABILIZING
 → WAITING_FULL_START
 → APPROACHING_TARGET / FINE_APPROACH
 → STABILIZING
 → CAPTURING_POINT
 → NEXT_POINT (repeat)
 → RELEASING_FORCE
 → RETURNING
 → COMPLETE_PENDING_SAVE

Operator cancellation → ABORTED
Safety/communication/motion failure → FAULTED
```

Motion is not run synchronously in a UI callback. The controller polls UTM runtime and advances the state machine. Runtime data and points are fixed-size; update does no file, JSON, socket, or UI work.

## Start interlocks and confirmation

Start requires valid communication, no recovery, required devices operational, servo enabled, READY machine, no active motion/sequence/session, no stop latch, no E-STOP/External STOP/limit/servo fault, and valid force input/calibration. Capacity, speeds, step, tolerance, travel, and timing must also validate.

The UI warning requires intentional confirmation and gives CANCEL default focus. Full calibration does not begin after that click: a successful low-load pre-check enters a second explicit `START FULL CALIBRATION` confirmation state.

## Maximum force

```text
allowedMaximum = min(loadcellCapacityN * 0.90, enabledOverloadLimitN)
```

The requested maximum must not exceed the result. Existing UTM overload Safety remains authoritative. A calibration guard also aborts when force magnitude exceeds the allowed maximum. It requests Stop, enters FAULTED, preserves active curves, and prohibits automatic return/restart.

## Pre-check

The explicit pre-check target defaults, when zero, to `min(5 N, maximumForce*0.05)`.

1. request existing Force Zero
2. observe capture start and successful completion
3. store machine and test start positions
4. store an independent compliance reference position
5. issue low-speed Move To Force
6. validate measured-force polarity
7. validate machine-position direction
8. monitor travel against minimum force rise
9. stop and stabilize
10. wait for explicit full-start confirmation

The existing UP-positive logical-force convention is reused. Expected raw measured-force sign is derived from `forceDirectionSign` and selected UTM direction. Hardware polarity remains a required real-machine validation.

## Target generation and approach

Targets begin at zero, advance by Force Step, and always include Maximum Force exactly. Duplicate endpoints are not emitted and more than 64 points is rejected.

- below 70% of maximum: Approach Speed
- 70% to below 90%: Calibration Speed
- 90% and above: Fine Speed

All target motion uses existing Move To Force. No raw servo command is introduced.

## Stabilization and point acquisition

Arrival requests Stop. Samples are accepted only inside the signed target tolerance. Leaving tolerance resets accumulated samples and the stabilization timer.

```text
capturedForceN = sum(forceN) / sampleCount
capturedPositionMm = sum(machinePositionMm) / sampleCount
deformationMm = capturedPositionMm - complianceReferencePositionMm
```

Force and deformation signs are retained.

## Travel and force-rise guards

The state machine continuously checks `abs(currentMachinePositionMm - calibrationStartPositionMm)` against maximum travel. Pre-check can use a smaller travel limit. It can require Minimum Force Rise after a configured Force Rise Travel Threshold. These are configuration values, not aggressive hidden constants.

## Abort and fault policy

E-STOP, External STOP, either limit, servo fault, communication invalid/recovering, overload, motion failure, maximum travel, wrong polarity, insufficient rise, or User Abort requests existing UTM Stop.

On abort/fault, no result is committed, active curves remain unchanged, automatic return/restart is prohibited, and the existing UTM latch/ACK policy remains authoritative. Calibration never resets Safety.

## Normal release and return

Only after every point succeeds, existing Velocity Motion releases load in the opposite direction at Fine Speed. At near-zero force the state machine requests Stop, then existing Move Absolute returns to the saved start test position at Return Speed. Any fault interrupts this path. Fault and abort paths never return automatically.

## Pending result and save

Normal completion produces `COMPLETE_PENDING_SAVE`; it does not change active curves. The operator chooses DISCARD, SAVE CURVE disabled, or SAVE & ENABLE. A direction-specific version increments once in the candidate profile immediately before successful atomic `QSaveFile` commit. Start, pre-check, capture, abort, preview, and pending completion do not increment versions.

## Profile structure

```json
"machineCompliance": {
  "activeMode": "compression",
  "compression": {"enabled": true, "version": 4, "points": []},
  "tension": {"enabled": false, "version": 0, "points": []},
  "autoCalibration": {
    "maximumForceN": 90.0,
    "forceStepN": 10.0,
    "approachSpeedMmPerMin": 3.0,
    "calibrationSpeedMmPerMin": 1.0,
    "fineSpeedMmPerMin": 0.2,
    "returnSpeedMmPerMin": 2.0,
    "maximumTravelMm": 5.0,
    "stabilizationTimeMs": 300,
    "forceToleranceN": 0.5
  }
}
```

Loadcell capacity remains the existing profile value and is not independently edited here.

## UI, protocol, and recording

The main header shows only C/T enabled state. Calibration presents settings, live safety values, direction start, full-start, abort, discard, and save actions. The legacy manual editor remains available.

Protocol V1 framing and LIVE_DATA layout remain byte-compatible. Additive in-memory metadata prepares mode, enabled, and direction versions for future result metadata. The incomplete recording subsystem was not expanded.

## Required hardware validation

Before enabling automatic motion, verify E-STOP/External STOP/limits/overload/servo fault, compression/tension motion direction, raw force polarity, position sign, no-contact detection, overshoot in all speed bands, stabilization/hysteresis, every Safety during every phase, EtherCAT disconnect with no restart/return, pending-result isolation, atomic save, and restart/reload of both curves.

No real EtherCAT hardware was operated during this implementation.
