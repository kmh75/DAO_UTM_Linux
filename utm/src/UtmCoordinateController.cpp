#include "UtmCoordinateController.h"

#include "DaoUtm.Types.h"

#include <cmath>
#include <limits>

bool UtmCoordinateController::Configure(
    double servoUnitsPerMm)
{
    if (!std::isfinite(servoUnitsPerMm) ||
        servoUnitsPerMm <= 0.0)
    {
        return false;
    }

    servoUnitsPerMm_ = servoUnitsPerMm;
    configured_ = true;
    return true;
}

double UtmCoordinateController::MachinePositionMm(
    int servoPosition) const
{
    return configured_
        ? static_cast<double>(servoPosition) / servoUnitsPerMm_
        : 0.0;
}

double UtmCoordinateController::TestPositionMm(
    int servoPosition) const
{
    return MachinePositionMm(servoPosition) -
        testZeroOffsetMm_;
}

bool UtmCoordinateController::TestTargetToServoPosition(
    double testTargetMm,
    int& servoTargetPosition) const
{
    if (!configured_ || !std::isfinite(testTargetMm))
    {
        return false;
    }

    const double machineTargetMm =
        testTargetMm + testZeroOffsetMm_;
    const double servoTarget =
        machineTargetMm * servoUnitsPerMm_;

    if (!std::isfinite(servoTarget) ||
        servoTarget < static_cast<double>(
            std::numeric_limits<int>::min()) ||
        servoTarget > static_cast<double>(
            std::numeric_limits<int>::max()))
    {
        return false;
    }

    servoTargetPosition =
        static_cast<int>(std::llround(servoTarget));
    return true;
}

bool UtmCoordinateController::IncrementalTargetToServoPosition(
    int currentServoPosition,
    double incrementalDistanceMm,
    double& targetTestPositionMm,
    int& servoTargetPosition) const
{
    if (!configured_ ||
        !std::isfinite(incrementalDistanceMm))
    {
        return false;
    }

    targetTestPositionMm =
        TestPositionMm(currentServoPosition) +
        incrementalDistanceMm;

    return TestTargetToServoPosition(
        targetTestPositionMm,
        servoTargetPosition);
}

bool UtmCoordinateController::SpeedToServoVelocity(
    double speedMmPerMin,
    int direction,
    int servoDirectionSign,
    int& servoTargetVelocity) const
{
    unsigned int magnitude = 0;

    if ((direction != UTM_DIRECTION_UP &&
        direction != UTM_DIRECTION_DOWN) ||
        (servoDirectionSign != 1 &&
            servoDirectionSign != -1) ||
        !SpeedToServoMagnitude(speedMmPerMin, magnitude) ||
        magnitude > static_cast<unsigned int>(
            std::numeric_limits<int>::max()))
    {
        return false;
    }

    const int directionSign =
        direction == UTM_DIRECTION_UP ? 1 : -1;
    servoTargetVelocity =
        static_cast<int>(magnitude) *
        directionSign * servoDirectionSign;
    return true;
}

bool UtmCoordinateController::SpeedToServoMagnitude(
    double speedMmPerMin,
    unsigned int& servoSpeed) const
{
    if (!configured_ ||
        !std::isfinite(speedMmPerMin) ||
        speedMmPerMin <= 0.0)
    {
        return false;
    }

    const double converted =
        speedMmPerMin * servoUnitsPerMm_ / 60.0;

    if (!std::isfinite(converted) ||
        converted > static_cast<double>(
            std::numeric_limits<int>::max()))
    {
        return false;
    }

    const long long rounded = std::llround(converted);

    if (rounded < 1)
    {
        return false;
    }

    servoSpeed = static_cast<unsigned int>(rounded);
    return true;
}

double UtmCoordinateController::GetTestZeroOffsetMm() const
{
    return testZeroOffsetMm_;
}

bool UtmCoordinateController::SetPositionZero(int currentServoPosition)
{
    if (!configured_)
    {
        return false;
    }
    testZeroOffsetMm_ = MachinePositionMm(currentServoPosition);
    positionZeroValid_ = true;
    return true;
}

void UtmCoordinateController::ClearPositionZero()
{
    testZeroOffsetMm_ = 0.0;
    positionZeroValid_ = false;
}

bool UtmCoordinateController::IsPositionZeroValid() const
{
    return positionZeroValid_;
}
