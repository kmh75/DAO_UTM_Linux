#pragma once

#include <cstdint>

class UtmCommunicationPolicy
{
public:
    enum State { NORMAL=0, TRANSIENT=1, DEGRADED=2, RECOVERING=3, FAULT=4 };
    struct Result { State state=NORMAL;bool stopRequired=false;bool hardFault=false;bool recovered=false; };
    static constexpr unsigned int DegradedBadCycles=3;
    static constexpr unsigned int RecoveryBadCycles=5;
    static constexpr unsigned int RecoveryGoodCycles=3;
    static constexpr std::uint64_t RecoveryWindowNs=300000000ULL;
    Result Update(bool cycleDataValid,bool basicRunning,std::uint64_t timestampNs)
    {
        const State previous=state_;
        if(!basicRunning){state_=FAULT;return {state_,true,true,false};}
        if(cycleDataValid){bad_=0;++good_;if(state_==RECOVERING&&good_<RecoveryGoodCycles){}else state_=NORMAL;}
        else{good_=0;++bad_;if(bad_>=RecoveryBadCycles){if(state_!=RECOVERING){recoveryStartedNs_=timestampNs;}state_=(timestampNs-recoveryStartedNs_>=RecoveryWindowNs)?FAULT:RECOVERING;}else state_=bad_>=DegradedBadCycles?DEGRADED:TRANSIENT;}
        return {state_,state_==RECOVERING||state_==FAULT,state_==FAULT,previous==RECOVERING&&state_==NORMAL};
    }
    State GetState() const{return state_;}
    unsigned int ConsecutiveBad() const{return bad_;}
private:
    State state_=NORMAL;unsigned int bad_=0;unsigned int good_=0;std::uint64_t recoveryStartedNs_=0;
};
