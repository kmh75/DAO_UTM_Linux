#include "UtmJogController.h"

#include <algorithm>
#include <cmath>
#include <limits>

bool UtmJogController::Configure(
    const UtmJogConfig& config)
{
    if (!std::isfinite(config.servoUnitsPerMm) ||
        config.servoUnitsPerMm <= 0.0)
    {
        return false;
    }

    UtmJogConfigV2 compatibleConfig{};
    compatibleConfig.config = config;
    compatibleConfig.minJogSpeedMmPerMin =
        60.0 / config.servoUnitsPerMm;
    compatibleConfig.maxJogSpeedMmPerMin =
        static_cast<double>(std::numeric_limits<int>::max()) *
        60.0 / config.servoUnitsPerMm;

    return ConfigureV2(compatibleConfig);
}

bool UtmJogController::ConfigureV2(
    const UtmJogConfigV2& config)
{
    const UtmJogConfig& base = config.config;

    if (!std::isfinite(base.servoUnitsPerMm) ||
        base.servoUnitsPerMm <= 0.0 ||
        !std::isfinite(base.physicalJogSpeedMmPerMin) ||
        !std::isfinite(config.minJogSpeedMmPerMin) ||
        !std::isfinite(config.maxJogSpeedMmPerMin) ||
        config.minJogSpeedMmPerMin <= 0.0 ||
        config.maxJogSpeedMmPerMin <
            config.minJogSpeedMmPerMin ||
        base.physicalJogSpeedMmPerMin <
            config.minJogSpeedMmPerMin ||
        base.physicalJogSpeedMmPerMin >
            config.maxJogSpeedMmPerMin ||
        base.acceleration == 0 ||
        base.deceleration == 0 ||
        (base.servoDirectionSign != 1 &&
            base.servoDirectionSign != -1))
    {
        return false;
    }

    const double minimumTarget =
        config.minJogSpeedMmPerMin *
        base.servoUnitsPerMm / 60.0;
    const double maximumTarget =
        config.maxJogSpeedMmPerMin *
        base.servoUnitsPerMm / 60.0;

    if (!std::isfinite(minimumTarget) ||
        !std::isfinite(maximumTarget) ||
        minimumTarget > static_cast<double>(
            std::numeric_limits<int>::max()) ||
        maximumTarget > static_cast<double>(
            std::numeric_limits<int>::max()) ||
        std::llround(minimumTarget) < 1)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    configured_ = true;
    return true;
}

bool UtmJogController::StartRequest(
    int source,
    int direction,
    double speedMmPerMin)
{
    if (!IsCommandJogSource(source) ||
        (direction != UTM_DIRECTION_UP &&
            direction != UTM_DIRECTION_DOWN) ||
        !std::isfinite(speedMmPerMin) ||
        speedMmPerMin <= 0.0)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (!configured_)
    {
        return false;
    }

    if (speedMmPerMin < config_.minJogSpeedMmPerMin ||
        speedMmPerMin > config_.maxJogSpeedMmPerMin)
    {
        return false;
    }

    const double targetMagnitude =
        speedMmPerMin *
        config_.config.servoUnitsPerMm / 60.0;

    if (!std::isfinite(targetMagnitude) ||
        targetMagnitude > static_cast<double>(
            std::numeric_limits<int>::max()) ||
        std::llround(targetMagnitude) < 1)
    {
        return false;
    }

    SourceRequest& request =
        requests_[static_cast<std::size_t>(source)];
    request.active = true;
    request.direction = direction;
    request.speedMmPerMin = speedMmPerMin;
    return true;
}

bool UtmJogController::StopRequest(
    int source)
{
    if (!IsCommandJogSource(source))
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    requests_[static_cast<std::size_t>(source)] = {};
    return true;
}

void UtmJogController::ClearCommandRequests()
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (SourceRequest& request : requests_)
    {
        request = {};
    }
}

void UtmJogController::InhibitPhysicalUntilReleased()
{
    std::lock_guard<std::mutex> lock(mutex_);
    physicalArmed_ = false;
}

UtmJogArbitration UtmJogController::Resolve(
    bool physicalUp,
    bool physicalDown)
{
    std::lock_guard<std::mutex> lock(mutex_);
    UtmJogArbitration result{};

    if (!configured_)
    {
        return result;
    }

    if (!physicalUp && !physicalDown)
    {
        physicalArmed_ = true;
    }

    const bool physicalConflict =
        physicalUp && physicalDown;

    if (physicalUp || physicalDown)
    {
        result.sourceMask |=
            1U << static_cast<unsigned int>(
                UTM_COMMAND_SOURCE_DIGITAL_JOG);
    }

    bool anyUp = false;
    bool anyDown = false;
    double selectedSpeed =
        std::numeric_limits<double>::infinity();

    for (std::size_t index = 0;
        index < requests_.size();
        ++index)
    {
        const SourceRequest& request = requests_[index];

        if (!request.active)
        {
            continue;
        }

        result.sourceMask |=
            1U << static_cast<unsigned int>(index);
        selectedSpeed =
            std::min(selectedSpeed, request.speedMmPerMin);
        anyUp = anyUp ||
            request.direction == UTM_DIRECTION_UP;
        anyDown = anyDown ||
            request.direction == UTM_DIRECTION_DOWN;
    }

    if (physicalArmed_ && (physicalUp || physicalDown))
    {
        selectedSpeed =
            std::min(
                selectedSpeed,
                config_.config.physicalJogSpeedMmPerMin);
        anyUp = anyUp || physicalUp;
        anyDown = anyDown || physicalDown;
    }

    result.conflict =
        physicalConflict || (anyUp && anyDown) ? 1 : 0;

    if (result.conflict != 0 || (!anyUp && !anyDown))
    {
        result.direction = result.conflict != 0
            ? UTM_DIRECTION_UNKNOWN
            : UTM_DIRECTION_NONE;
        return result;
    }

    result.requested = 1;
    result.direction = anyUp
        ? UTM_DIRECTION_UP
        : UTM_DIRECTION_DOWN;
    result.speedMmPerMin = selectedSpeed;

    const double unsignedTarget =
        result.speedMmPerMin *
        config_.config.servoUnitsPerMm / 60.0;

    if (!std::isfinite(unsignedTarget) ||
        unsignedTarget > static_cast<double>(
            std::numeric_limits<int>::max()) ||
        std::llround(unsignedTarget) < 1)
    {
        result.requested = 0;
        result.direction = UTM_DIRECTION_NONE;
        result.speedMmPerMin = 0.0;
        result.sourceMask = 0;
        return result;
    }

    const int logicalSign =
        result.direction == UTM_DIRECTION_UP ? 1 : -1;
    result.servoTargetVelocity =
        static_cast<int>(std::llround(unsignedTarget)) *
        logicalSign *
        config_.config.servoDirectionSign;
    return result;
}

bool UtmJogController::IsConfigured() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return configured_;
}

UtmJogConfigV2 UtmJogController::GetConfig() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

bool UtmJogController::IsCommandJogSource(
    int source)
{
    return source == UTM_COMMAND_SOURCE_UI ||
        source == UTM_COMMAND_SOURCE_REMOTE;
}
