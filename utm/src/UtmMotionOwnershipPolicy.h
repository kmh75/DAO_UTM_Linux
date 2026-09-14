#pragma once

struct UtmMotionOwnershipSnapshot
{
    int jogRequested=0, jogActive=0;
    int generalMotionActive=0, generalMotionStopping=0;
    int sequenceRunning=0, sequenceActionPending=0;
    int calibrationActive=0, calibrationVelocityRequested=0;
    int calibrationReturnPending=0, calibrationForceStopRequested=0;
    int outputPositionActive=0, outputVelocityActive=0;
    int pendingMotionCommand=0;
    int commandType=0, commandSource=0, sequenceStep=0;
};

class UtmMotionOwnershipPolicy
{
public:
    static bool IsActive(const UtmMotionOwnershipSnapshot& s) noexcept
    {
        return s.jogRequested||s.jogActive||s.generalMotionActive||s.generalMotionStopping||
            s.sequenceRunning||s.sequenceActionPending||s.calibrationActive||
            s.calibrationVelocityRequested||s.calibrationReturnPending||s.calibrationForceStopRequested||
            s.outputPositionActive||s.outputVelocityActive||s.pendingMotionCommand;
    }
};

class UtmCommunicationWarningPolicy
{
public:
    static constexpr unsigned int WarningIncidentCount=3;
    static constexpr unsigned long long WindowNs=3600000000000ULL;
    void Add(unsigned long long timestampNs) noexcept
    { timestamps_[write_]=timestampNs;write_=(write_+1)%Capacity;if(count_<Capacity)++count_; }
    unsigned int Recent(unsigned long long nowNs) const noexcept
    { unsigned int n=0;for(unsigned int i=0;i<count_;++i)if(nowNs>=timestamps_[i]&&nowNs-timestamps_[i]<=WindowNs)++n;return n; }
    bool Unstable(unsigned long long nowNs) const noexcept { return Recent(nowNs)>=WarningIncidentCount; }
private:
    static constexpr unsigned int Capacity=32;
    unsigned long long timestamps_[Capacity]{};unsigned int write_=0,count_=0;
};
