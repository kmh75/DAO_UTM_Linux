#include "UtmCommandMailbox.h"

bool UtmCommandMailbox::Push(
    const UtmCommandRequest& request)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (queue_.size() >= MAX_QUEUE_SIZE)
    {
        return false;
    }

    queue_.push_back(request);
    return true;
}

bool UtmCommandMailbox::TryPop(
    UtmCommandRequest& request)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (queue_.empty())
    {
        return false;
    }

    request = queue_.front();
    queue_.pop_front();

    return true;
}

void UtmCommandMailbox::InvalidateMotionCommands(
    unsigned long long commandEpoch)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto iterator = queue_.begin();
        iterator != queue_.end();)
    {
        const bool motionCommand =
            iterator->type == UTM_COMMAND_START ||
            iterator->type == UTM_COMMAND_JOG_UP_REQUEST ||
            iterator->type == UTM_COMMAND_JOG_DOWN_REQUEST;

        if (motionCommand ||
            iterator->commandEpoch < commandEpoch)
        {
            iterator = queue_.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }
}

void UtmCommandMailbox::Clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
}
