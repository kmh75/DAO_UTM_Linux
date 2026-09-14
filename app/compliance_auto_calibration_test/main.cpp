#include "UtmComplianceCalibrationController.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
void require(bool value, const char* name)
{ if (!value) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); } }

UtmComplianceCalibrationConfigV1 config(int mode=UTM_COMPLIANCE_MODE_COMPRESSION)
{
    UtmComplianceCalibrationConfigV1 c;
    c.mode=mode;c.motionDirection=mode==UTM_COMPLIANCE_MODE_COMPRESSION?UTM_DIRECTION_DOWN:UTM_DIRECTION_UP;
    c.loadcellCapacityN=100;c.maximumForceN=90;c.forceStepN=10;
    c.approachSpeedMmPerMin=3;c.calibrationSpeedMmPerMin=1;c.fineSpeedMmPerMin=.2;c.returnSpeedMmPerMin=2;
    c.maximumTravelMm=2;c.precheckForceN=5;c.precheckMaximumTravelMm=.5;c.forceToleranceN=.5;
    c.releaseForceThresholdN=.5;c.releaseMaximumTravelMm=.5;c.minimumForceRiseN=1;c.forceRiseTravelThresholdMm=.1;
    c.maximumForceJumpN=20;c.overshootGuardN=2;c.oppositeForceGuardN=1;
    c.stabilizationTimeMs=4;c.minimumStableSampleCount=3;c.pointTimeoutMs=1000;c.releaseTimeoutMs=1000;c.precheckConfirmationTimeoutMs=20;
    return c;
}

UtmComplianceCalibrationInput ready()
{
    UtmComplianceCalibrationInput i;i.timestampNs=1000000;i.forceValid=1;i.machineReady=1;i.servoReady=1;
    i.communicationValid=1;i.motionOutputStopped=1;i.machinePositionMm=10;i.testPositionMm=4;return i;
}

void tick(UtmComplianceCalibrationController& c,UtmComplianceCalibrationInput& i,unsigned ms=2)
{i.timestampNs+=static_cast<unsigned long long>(ms)*1000000ULL;c.Update(i);}

unsigned long long startAndReference(UtmComplianceCalibrationController& c,UtmComplianceCalibrationInput& i,
    const UtmComplianceCalibrationConfigV1& cfg=config(),int overloadEnabled=1,double overload=95)
{
    unsigned long long id=0;require(c.StartPrecheck(cfg,overload,overloadEnabled,1,i,id),"start precheck");
    UtmComplianceCalibrationAction a;require(c.TakeAction(a)&&a.type==UtmComplianceCalibrationActionType::ZeroForce,"zero action");
    c.ReportActionResult(a.type,true);i.forceZeroCaptureActive=1;tick(c,i);i.forceZeroCaptureActive=0;tick(c,i);
    for(int n=0;n<4;++n)tick(c,i);
    UtmComplianceCalibrationRuntimeV1 r;c.GetRuntime(r);require(r.state==UTM_COMPLIANCE_CAL_PRECHECK_APPROACH,"reference captured");return id;
}

void passPrecheck(UtmComplianceCalibrationController& c,UtmComplianceCalibrationInput& i)
{
    i.motionOutputStopped=0;i.forceN=-5;i.machinePositionMm=9.995;tick(c,i);i.motionOutputStopped=1;
    for(int n=0;n<4;++n)tick(c,i);tick(c,i);
    UtmComplianceCalibrationRuntimeV1 r;c.GetRuntime(r);require(r.state==UTM_COMPLIANCE_CAL_WAITING_FULL_START&&r.precheckPassed,"precheck barrier");
}
}

int main()
{
    {double targets[64]{};auto c=config();require(UtmComplianceCalibrationController::GenerateTargets(c,targets,64)==10,"target count");require(targets[0]==0&&targets[9]==90,"final target");c.maximumForceN=87;require(UtmComplianceCalibrationController::GenerateTargets(c,targets,64)==10&&targets[9]==87,"non-divisible final");c.maximumForceN=64;c.forceStepN=1;require(UtmComplianceCalibrationController::GenerateTargets(c,targets,64)==0,"64 point limit");}
    {double allowed=0;auto c=config();require(UtmComplianceCalibrationController::ValidateConfig(c,95,1,allowed)&&allowed==90,"90 percent");c.maximumForceN=90.1;require(!UtmComplianceCalibrationController::ValidateConfig(c,95,1,allowed),"capacity reject");c=config();c.maximumForceN=79;require(UtmComplianceCalibrationController::ValidateConfig(c,80,1,allowed)&&allowed==80,"overload precedence");c=config();require(UtmComplianceCalibrationController::ValidateConfig(c,1,0,allowed)&&allowed==90,"hard guard overload disabled");c.manufacturerLimitN=70;c.maximumForceN=71;require(!UtmComplianceCalibrationController::ValidateConfig(c,100,1,allowed)&&allowed==70,"manufacturer precedence");}
    {UtmComplianceCalibrationController c;auto i=ready();auto id=startAndReference(c,i);passPrecheck(c,i);require(!c.ConfirmFull(id+1),"stale confirm");require(c.ConfirmFull(id),"confirm full");tick(c,i);for(int target=10;target<=90;target+=10){i.motionOutputStopped=0;i.forceN=-target;i.machinePositionMm=10-target*.001;tick(c,i);i.motionOutputStopped=1;for(int n=0;n<4;++n)tick(c,i);tick(c,i);tick(c,i);}for(double force:{-70.0,-50.0,-30.0,-10.0}){i.forceN=force;i.machinePositionMm+=.01;tick(c,i);}i.forceN=0;i.machinePositionMm=9.96;for(int n=0;n<12;++n)tick(c,i);UtmComplianceCalibrationAction a;const bool hasReturn=c.TakeAction(a);require(hasReturn&&a.type==UtmComplianceCalibrationActionType::ReturnAbsolute&&a.targetTestPositionMm==4,"release stop return order");c.ReportActionResult(a.type,true);i.returnMotionComplete=1;tick(c,i);UtmComplianceCalibrationRuntimeV1 r;c.GetRuntime(r);require(r.state==UTM_COMPLIANCE_CAL_COMPLETE_PENDING_SAVE&&r.pendingResult,"complete pending");unsigned count=0;require(c.GetPendingPoints(id,nullptr,0,count)&&count==10,"pending count");UtmComplianceCalibrationPoint points[64]{};require(c.GetPendingPoints(id,points,64,count),"pending copy");require(points[0].forceN<0&&points[0].deformationMm<0&&points[9].forceN==0,"compression signed sorted same-cycle points");for(unsigned n=1;n<count;++n)require(points[n-1].forceN<points[n].forceN,"pending force sort");require(!c.DiscardPending(id+1)&&c.DiscardPending(id),"stale discard");}
    {UtmComplianceCalibrationController c;auto i=ready();auto id=startAndReference(c,i,config(UTM_COMPLIANCE_MODE_TENSION));i.motionOutputStopped=0;i.forceN=5;i.machinePositionMm=10.05;tick(c,i);i.motionOutputStopped=1;for(int n=0;n<4;++n)tick(c,i);tick(c,i);UtmComplianceCalibrationRuntimeV1 r;c.GetRuntime(r);require(r.state==UTM_COMPLIANCE_CAL_WAITING_FULL_START&&id!=0,"tension precheck");}
    {UtmComplianceCalibrationController c;auto i=ready();startAndReference(c,i);i.forceN=2;tick(c,i);UtmComplianceCalibrationRuntimeV1 r;c.GetRuntime(r);require(r.fault==UTM_COMPLIANCE_FAULT_WRONG_FORCE_POLARITY,"wrong polarity");require(!c.GetOutput().velocityRequested,"fault output inhibited");}
    {UtmComplianceCalibrationController c;auto i=ready();startAndReference(c,i);i.machinePositionMm=10.01;i.forceN=-1;tick(c,i);UtmComplianceCalibrationRuntimeV1 r;c.GetRuntime(r);require(r.fault==UTM_COMPLIANCE_FAULT_WRONG_POSITION_DIRECTION,"wrong position direction");}
    {UtmComplianceCalibrationController c;auto i=ready();startAndReference(c,i);i.machinePositionMm=9.89;i.forceN=-.2;tick(c,i);UtmComplianceCalibrationRuntimeV1 r;c.GetRuntime(r);require(r.fault==UTM_COMPLIANCE_FAULT_INSUFFICIENT_FORCE_RISE,"insufficient rise");}
    {UtmComplianceCalibrationController c;auto i=ready();startAndReference(c,i);i.forceN=-25;tick(c,i);UtmComplianceCalibrationRuntimeV1 r;c.GetRuntime(r);require(r.fault==UTM_COMPLIANCE_FAULT_FORCE_JUMP,"force jump");}
    {UtmComplianceCalibrationController c;auto i=ready();startAndReference(c,i);i.forceN=-4;i.machinePositionMm=9.49;tick(c,i);UtmComplianceCalibrationRuntimeV1 r;c.GetRuntime(r);require(r.fault==UTM_COMPLIANCE_FAULT_PRECHECK_TRAVEL,"precheck max travel");}
    {UtmComplianceCalibrationController c;auto i=ready();auto cfg=config();cfg.maximumTravelMm=.2;cfg.precheckMaximumTravelMm=.1;auto id=startAndReference(c,i,cfg);passPrecheck(c,i);require(c.ConfirmFull(id),"confirm for full travel");tick(c,i);i.forceN=-10;i.machinePositionMm=9.79;tick(c,i);UtmComplianceCalibrationRuntimeV1 r;c.GetRuntime(r);require(r.fault==UTM_COMPLIANCE_FAULT_MAX_TRAVEL,"full max travel");}
    {UtmComplianceCalibrationController c;auto i=ready();startAndReference(c,i);tick(c,i,1001);UtmComplianceCalibrationRuntimeV1 r;c.GetRuntime(r);require(r.fault==UTM_COMPLIANCE_FAULT_TIMEOUT,"motion timeout");}
    {UtmComplianceCalibrationController c;auto i=ready();startAndReference(c,i);i.forceN=-5;i.machinePositionMm=9.995;i.motionOutputStopped=0;tick(c,i);i.motionOutputStopped=1;tick(c,i);tick(c,i);i.forceN=-3;tick(c,i);UtmComplianceCalibrationRuntimeV1 r;c.GetRuntime(r);require(r.state==UTM_COMPLIANCE_CAL_PRECHECK_STABILIZING&&r.stableSampleCount==0,"stabilization reset");i.forceN=-5;tick(c,i);tick(c,i);require(r.state!=UTM_COMPLIANCE_CAL_WAITING_FULL_START,"minimum stable samples");tick(c,i);tick(c,i);c.GetRuntime(r);require(r.state==UTM_COMPLIANCE_CAL_WAITING_FULL_START,"stable time and samples");}
    {for(int which=0;which<8;++which){UtmComplianceCalibrationController c;auto i=ready();startAndReference(c,i);if(which==0)i.emergency=1;if(which==1)i.externalStop=1;if(which==2)i.upperLimit=1;if(which==3)i.lowerLimit=1;if(which==4)i.servoFault=1;if(which==5)i.communicationRecovering=1;if(which==6)i.communicationValid=0;if(which==7)i.overload=1;tick(c,i);UtmComplianceCalibrationRuntimeV1 r;c.GetRuntime(r);require(r.state==UTM_COMPLIANCE_CAL_FAULTED,"safety fault");if(which==5)require(r.fault==UTM_COMPLIANCE_FAULT_COMMUNICATION_INTERRUPTED,"communication interrupted reason");UtmComplianceCalibrationAction a;require(!c.TakeAction(a),"fault no return");}}
    {UtmComplianceCalibrationController c;auto i=ready();auto id=startAndReference(c,i);require(!c.Abort(id+1)&&c.Abort(id),"stale/user abort");tick(c,i);UtmComplianceCalibrationRuntimeV1 r;c.GetRuntime(r);require(r.state==UTM_COMPLIANCE_CAL_ABORTED&&!r.pendingResult,"abort no pending");}
    {UtmComplianceCalibrationController c;auto i=ready();startAndReference(c,i);passPrecheck(c,i);tick(c,i,25);UtmComplianceCalibrationRuntimeV1 r;c.GetRuntime(r);require(r.state==UTM_COMPLIANCE_CAL_RELEASING_FORCE,"confirmation timeout release");}
    {UtmComplianceCalibrationController c;auto i=ready();startAndReference(c,i);passPrecheck(c,i);tick(c,i,25);i.forceN=2;tick(c,i);UtmComplianceCalibrationRuntimeV1 r;c.GetRuntime(r);require(r.fault==UTM_COMPLIANCE_FAULT_OPPOSITE_FORCE,"opposite force release guard");UtmComplianceCalibrationAction a;require(!c.TakeAction(a),"opposite fault no return");}
    {UtmComplianceCalibrationController c;auto i=ready();startAndReference(c,i);i.forceN=-95;tick(c,i);UtmComplianceCalibrationRuntimeV1 r;c.GetRuntime(r);require(r.fault==UTM_COMPLIANCE_FAULT_HARD_FORCE,"hard force runtime");}
    std::cout<<"PASS Engine-owned compliance calibration deterministic tests\n";
}
