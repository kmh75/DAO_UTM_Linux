#pragma once
#include <functional>
#include <QString>
struct ApplicationActivitySnapshot
{bool jog=false,motion=false,stopping=false,force=false,sequence=false,calibration=false,homing=false,recording=false,criticalFinalize=false;};
class ApplicationShutdownCoordinator
{
public:
    using Snapshot=std::function<ApplicationActivitySnapshot()>;using Cleanup=std::function<bool(QString&)>;using Power=std::function<bool(QString&)>;
    ApplicationShutdownCoordinator(Snapshot s,Cleanup c,Power p):snapshot_(std::move(s)),cleanup_(std::move(c)),power_(std::move(p)){}
    bool Request(bool powerOff,QString& error)
    {if(busy_){error="Shutdown is already in progress.";return false;}const auto s=snapshot_();if(s.jog||s.motion||s.stopping||s.force||s.sequence||s.calibration||s.homing||s.recording||s.criticalFinalize){error="Cannot exit while motion or test is active.";return false;}busy_=true;if(!cleanup_(error)){busy_=false;return false;}if(powerOff&&!power_(error)){busy_=false;return false;}completed_=true;return true;}
    bool Busy()const{return busy_;}bool Completed()const{return completed_;}
private:Snapshot snapshot_;Cleanup cleanup_;Power power_;bool busy_=false,completed_=false;
};
