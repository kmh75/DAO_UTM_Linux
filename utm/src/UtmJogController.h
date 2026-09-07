#pragma once

#include "DaoUtm.Types.h"

#include <array>
#include <mutex>

struct UtmJogArbitration
{
    int requested = 0;
    int direction = UTM_DIRECTION_NONE;
    double speedMmPerMin = 0.0;
    int conflict = 0;
    unsigned int sourceMask = 0;
    int servoTargetVelocity = 0;
};

class UtmJogController
{
public:
    bool Configure(
        const UtmJogConfig& config);

    bool ConfigureV2(
        const UtmJogConfigV2& config);

    bool StartRequest(
        int source,
        int direction,
        double speedMmPerMin);

    bool StopRequest(
        int source);

    void ClearCommandRequests();
    void InhibitPhysicalUntilReleased();

    UtmJogArbitration Resolve(
        bool physicalUp,
        bool physicalDown);

    bool IsConfigured() const;
    UtmJogConfigV2 GetConfig() const;

private:
    struct SourceRequest
    {
        bool active = false;
        int direction = UTM_DIRECTION_NONE;
        double speedMmPerMin = 0.0;
    };

    static constexpr std::size_t SOURCE_COUNT =
        static_cast<std::size_t>(UTM_COMMAND_SOURCE_REMOTE) + 1;

    static bool IsCommandJogSource(int source);

private:
    mutable std::mutex mutex_;
    UtmJogConfigV2 config_{};
    bool configured_ = false;
    bool physicalArmed_ = true;
    std::array<SourceRequest, SOURCE_COUNT> requests_{};
};
