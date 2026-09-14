# Machine Compliance Compensation

The raw displacement path is servo actual position → `UtmCoordinateController` machine/test conversion → `UtmGeneralMotionRuntimeInfo::testPositionMm`. Jog, absolute/incremental/velocity motion, servo targets, travel/limit/safety checks, force control, completion, and Sequencer remain on that raw coordinate.

Compensation is inserted after the public runtime snapshot in the application/service analysis path:

`correctedDisplacementMm = rawTestDisplacementMm - LookupCompliance(measuredForceN)`

The curve is at most 64 signed canonical `(forceN, deformationMm)` points. Configuration validates finite values and unique force coordinates, sorts once, and requires at least two points. Runtime performs allocation-free linear interpolation. Outside the calibrated force interval it clamps to the nearest endpoint and clears `inCalibrationRange`; it never silently extrapolates or applies `abs()`.

The Calibration tab uses a separate, temporary compliance baseline. CAPTURE ZERO does not alter Position Zero. ADD POINT captures current canonical force and `raw test displacement - compliance baseline`. Editing does not save. APPLY / SAVE validates, updates the active `MachineProfile`, applies the fixed curve, and atomically writes with `QSaveFile`. The JSON member is:

```json
"complianceCompensation": {
  "enabled": true,
  "version": 1,
  "points": [{"forceN": 0.0, "deformationMm": 0.0}]
}
```

Missing legacy fields load as disabled with no points. Curves restore at process/profile reload and remain available across hardware reconnect. The temporary baseline is not persisted. Raw sample values are never discarded. Extensometer data is direct specimen displacement and is never compensated.
