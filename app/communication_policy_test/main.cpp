#include "UtmCommunicationPolicy.h"
#include "UtmSafetyMonitor.h"
#include "UtmStateMachine.h"
#include <cstdlib>
#include <iostream>

namespace { [[noreturn]] void fail(const char* m){std::cerr<<"FAIL: "<<m<<'\n';std::exit(1);} }
int main()
{
    using P=UtmCommunicationPolicy;std::uint64_t t=0;auto step=[](P& p,bool good,std::uint64_t& time){time+=2000000ULL;return p.Update(good,true,time);};
    {P p;if(step(p,true,t).state!=P::NORMAL||step(p,true,t).state!=P::NORMAL)fail("normal");}
    t=0;{P p;step(p,true,t);if(step(p,false,t).state!=P::TRANSIENT||step(p,true,t).hardFault)fail("single bad regression");}
    t=0;{P p;step(p,true,t);step(p,false,t);step(p,false,t);if(step(p,true,t).hardFault)fail("two bad");}
    t=0;{P p;step(p,true,t);step(p,false,t);step(p,false,t);if(step(p,false,t).state!=P::DEGRADED||step(p,true,t).hardFault)fail("degraded recovery");}
    t=0;{P p;step(p,true,t);for(int i=0;i<5;++i)step(p,false,t);if(p.GetState()!=P::RECOVERING)fail("recovering threshold");auto a=step(p,true,t),b=step(p,true,t),c=step(p,true,t);if(a.state!=P::RECOVERING||b.state!=P::RECOVERING||c.state!=P::NORMAL||!c.recovered)fail("three good recovery");}
    {UtmSafetyMonitor safety;UtmStopLatch latch;UtmStateMachine machine;machine.CompleteStartup();UtmInputSnapshot snapshot{};snapshot.communicationValid=1;UtmSafetyContext context{};const auto evaluation=safety.Evaluate(snapshot,context,false,true,true);if(!evaluation.requested||evaluation.primaryReason!=UTM_STOP_COMMUNICATION_FAULT)fail("recovering stop request");latch.Update(evaluation,1,1);machine.Update(snapshot,latch.Get(),nullptr,false);if(machine.GetState()!=UTM_MACHINE_STOPPED)fail("recovering safe stopped state");machine.Update(snapshot,latch.Get(),nullptr,false);if(machine.GetState()!=UTM_MACHINE_STOPPED)fail("motion must not auto resume");}
    t=0;{P p;step(p,true,t);for(int i=0;i<5;++i)step(p,false,t);t+=P::RecoveryWindowNs;if(!p.Update(false,true,t).hardFault)fail("recovery timeout");}
    t=0;{P p;if(!p.Update(true,false,t).hardFault)fail("fatal basic communication");}
    static_assert(sizeof(P)<=32,"communication policy must remain fixed-size");
    std::cout<<"EtherCAT communication policy fault injection passed\n";return 0;
}
