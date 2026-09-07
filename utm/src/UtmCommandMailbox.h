#pragma once

#include "DaoUtm.Types.h"

#include <cstddef>
#include <deque>
#include <mutex>

class UtmCommandMailbox
{
public:
    bool Push(
        const UtmCommandRequest& request);

    bool TryPop(
        UtmCommandRequest& request);

    void InvalidateMotionCommands(
        unsigned long long commandEpoch);

    void Clear();

private:
    static constexpr std::size_t MAX_QUEUE_SIZE = 64;

    std::mutex mutex_;
    std::deque<UtmCommandRequest> queue_;
};
