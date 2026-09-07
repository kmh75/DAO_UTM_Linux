#include "UtmRuntimeStore.h"

void UtmRuntimeStore::Publish(
    const UtmRuntimeInfo& runtime,
    const UtmJogRuntimeInfo& jog,
    const UtmStartupRuntimeInfo& startup,
    const UtmGeneralMotionRuntimeInfo& motion,
    const UtmForceMotionRuntimeInfo& forceMotion,
    const UtmSequenceRuntimeInfo& sequence)
{
    std::lock_guard<std::mutex> lock(mutex_);
    runtime_ = runtime;
    jog_ = jog;
    startup_ = startup;
    motion_ = motion;
    forceMotion_ = forceMotion;
    sequence_ = sequence;
}

bool UtmRuntimeStore::ReadV6(UtmRuntimeInfoV6& runtime) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    runtime.runtime.runtime.runtime.runtime.runtime = runtime_;
    runtime.runtime.runtime.runtime.runtime.jog = jog_;
    runtime.runtime.runtime.runtime.startup = startup_;
    runtime.runtime.runtime.motion = motion_;
    runtime.runtime.forceMotion = forceMotion_;
    runtime.sequence = sequence_;
    return true;
}

bool UtmRuntimeStore::ReadV5(UtmRuntimeInfoV5& runtime) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    runtime.runtime.runtime.runtime.runtime = runtime_;
    runtime.runtime.runtime.runtime.jog = jog_;
    runtime.runtime.runtime.startup = startup_;
    runtime.runtime.motion = motion_;
    runtime.forceMotion = forceMotion_;
    return true;
}

bool UtmRuntimeStore::Read(
    UtmRuntimeInfo& runtime) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    runtime = runtime_;
    return true;
}

bool UtmRuntimeStore::ReadV2(
    UtmRuntimeInfoV2& runtime) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    runtime.runtime = runtime_;
    runtime.jog = jog_;
    return true;
}

bool UtmRuntimeStore::ReadV3(
    UtmRuntimeInfoV3& runtime) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    runtime.runtime.runtime = runtime_;
    runtime.runtime.jog = jog_;
    runtime.startup = startup_;
    return true;
}

bool UtmRuntimeStore::ReadV4(
    UtmRuntimeInfoV4& runtime) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    runtime.runtime.runtime.runtime = runtime_;
    runtime.runtime.runtime.jog = jog_;
    runtime.runtime.startup = startup_;
    runtime.motion = motion_;
    return true;
}
