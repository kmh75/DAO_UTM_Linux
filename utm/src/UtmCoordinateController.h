#pragma once

class UtmCoordinateController
{
public:
    bool Configure(double servoUnitsPerMm);

    double MachinePositionMm(int servoPosition) const;
    double TestPositionMm(int servoPosition) const;

    bool TestTargetToServoPosition(
        double testTargetMm,
        int& servoTargetPosition) const;

    bool IncrementalTargetToServoPosition(
        int currentServoPosition,
        double incrementalDistanceMm,
        double& targetTestPositionMm,
        int& servoTargetPosition) const;

    bool SpeedToServoVelocity(
        double speedMmPerMin,
        int direction,
        int servoDirectionSign,
        int& servoTargetVelocity) const;

    bool SpeedToServoMagnitude(
        double speedMmPerMin,
        unsigned int& servoSpeed) const;

    double GetTestZeroOffsetMm() const;
    bool SetPositionZero(int currentServoPosition);
    void ClearPositionZero();
    bool IsPositionZeroValid() const;

private:
    bool configured_ = false;
    double servoUnitsPerMm_ = 0.0;
    double testZeroOffsetMm_ = 0.0;
    bool positionZeroValid_ = false;
};
