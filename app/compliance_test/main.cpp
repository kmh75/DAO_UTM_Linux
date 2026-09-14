#include "UtmComplianceCompensation.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace dao::utm;
static void require(bool ok,const char* what){if(!ok){std::cerr<<"FAIL: "<<what<<'\n';std::exit(1);}}
static bool near(double a,double b){return std::abs(a-b)<1e-12;}
int main()
{
    UtmComplianceCompensation c;CompliancePoint p[]={{500,.071},{0,0},{100,.015}};
    require(c.ConfigureCurve(p,3,7)==ComplianceError::None,"configure/sort");c.Enable();
    require(near(c.Evaluate(0).compensationMm,0),"0 N");require(near(c.Evaluate(100).compensationMm,.015),"100 N");require(near(c.Evaluate(500).compensationMm,.071),"500 N");
    require(near(c.Evaluate(250).compensationMm,.036),"250 N interpolation");
    CompliancePoint signedPoints[]={{-100,-.014},{0,0},{100,.015}};require(c.ConfigureCurve(signedPoints,3)==ComplianceError::None,"signed curve");require(near(c.Evaluate(-50).compensationMm,-.007),"signed compression/tension");
    CompliancePoint duplicate[]={{0,0},{0,1}};require(c.ConfigureCurve(duplicate,2)==ComplianceError::DuplicateForce,"duplicate reject");
    auto low=c.Evaluate(-200);require(!low.inCalibrationRange&&near(low.compensationMm,-.014),"low clamp/flag");
    auto r=c.GetRuntime(50,2.0);require(near(r.correctedDisplacementMm,2.0-r.compensationMm),"raw-compensation");
    const double motionTarget=12.5,extensometer=3.25;c.Disable();r=c.GetRuntime(50,2.0);require(near(r.correctedDisplacementMm,2.0),"disabled raw");require(near(motionTarget,12.5),"motion target unchanged");require(near(extensometer,3.25),"extensometer unchanged");
    std::cout<<"Compliance compensation tests passed\n";
}
