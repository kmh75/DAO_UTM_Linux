#pragma once

#include "DaoUtm.Types.h"

#include <mutex>

class UtmRuntimeStore
{
public:
    void Publish(
        const UtmRuntimeInfo& runtime,
        const UtmJogRuntimeInfo& jog = {},
        const UtmStartupRuntimeInfo& startup = {},
        const UtmGeneralMotionRuntimeInfo& motion = {},
        const UtmForceMotionRuntimeInfo& forceMotion = {},
        const UtmSequenceRuntimeInfo& sequence = {});
    void PublishCommunication(const UtmCommunicationRuntimeInfoV1& runtime);
    bool ReadCommunication(UtmCommunicationRuntimeInfoV1& runtime) const;

    bool Read(
        UtmRuntimeInfo& runtime) const;

    bool ReadV2(
        UtmRuntimeInfoV2& runtime) const;

    bool ReadV3(
        UtmRuntimeInfoV3& runtime) const;

    bool ReadV4(
        UtmRuntimeInfoV4& runtime) const;
    bool ReadV5(UtmRuntimeInfoV5& runtime) const;
    bool ReadV6(UtmRuntimeInfoV6& runtime) const;

private:
    mutable std::mutex mutex_;
    UtmRuntimeInfo runtime_{};
    UtmJogRuntimeInfo jog_{};
    UtmStartupRuntimeInfo startup_{};
    UtmGeneralMotionRuntimeInfo motion_{};
    UtmForceMotionRuntimeInfo forceMotion_{};
    UtmSequenceRuntimeInfo sequence_{};
    UtmCommunicationRuntimeInfoV1 communication_{};
};
