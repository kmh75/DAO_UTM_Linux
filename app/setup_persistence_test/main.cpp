#include "MainWindow.h"
#include "MachineProfile.h"
#include "UtmUiController.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
[[noreturn]] void fail(const char* text){std::cerr<<"FAIL: "<<text<<'\n';std::exit(1);}
bool near(double a,double b,double e=1e-9){return std::abs(a-b)<=e;}
template<class T>T* control(MainWindow& window,const char* name){auto* result=window.findChild<T*>(name);if(!result)fail(name);return result;}
void setComboData(QComboBox* box,int value){const int index=box->findData(value);if(index<0)fail("combo data");box->setCurrentIndex(index);}
}

int main(int argc,char** argv)
{
    QTemporaryDir config;if(!config.isValid())fail("temporary config");qputenv("XDG_CONFIG_HOME",config.path().toUtf8());qputenv("QT_QPA_PLATFORM","offscreen");
    QApplication app(argc,argv);QApplication::setApplicationName("dao-setup-persistence-test");QApplication::setOrganizationName("DAO-Test");
    {
        UtmUiController controller;controller.profile().lastSelectedTab=3;controller.startOffline();MainWindow window(&controller);
        auto* tabs=window.findChild<QTabWidget*>();if(!tabs)fail("tabs");int setupIndex=-1;for(int i=0;i<tabs->count();++i)if(tabs->tabText(i)=="Setup")setupIndex=i;if(setupIndex<0)fail("Setup tab");QWidget* setup=tabs->widget(setupIndex);
        if(tabs->currentIndex()!=0||tabs->tabText(0)!="Main")fail("offline startup must show Main tab");tabs->setCurrentIndex(3);if(tabs->currentIndex()!=3)fail("normal tab navigation");tabs->setCurrentIndex(0);
        for(auto* widget:setup->findChildren<QWidget*>())if((qobject_cast<QSpinBox*>(widget)||qobject_cast<QDoubleSpinBox*>(widget)||qobject_cast<QComboBox*>(widget)||qobject_cast<QCheckBox*>(widget))&&widget->property("profileJsonField").toString().isEmpty())fail("editable Setup widget without persistence mapping");
        control<QComboBox>(window,"Setup_adapterName")->setEditText("test_eth9");control<QSpinBox>(window,"Setup_devices.servo")->setValue(3);control<QSpinBox>(window,"Setup_devices.adc")->setValue(4);control<QSpinBox>(window,"Setup_devices.io")->setValue(5);control<QSpinBox>(window,"Setup_devices.encoder")->setValue(6);control<QCheckBox>(window,"Setup_devices.encoderRequired")->setChecked(true);control<QCheckBox>(window,"Setup_devices.autoServoOn")->setChecked(true);
        control<QDoubleSpinBox>(window,"Setup_motion.servoUnitsPerMm")->setValue(1234.5);setComboData(control<QComboBox>(window,"Setup_motion.servoDirectionSign"),-1);setComboData(control<QComboBox>(window,"Setup_forceControl.forceDirectionSign"),-1);control<QDoubleSpinBox>(window,"Setup_motion.jogMin")->setValue(.25);control<QDoubleSpinBox>(window,"Setup_motion.jogMax")->setValue(876.5);control<QDoubleSpinBox>(window,"Setup_motion.jogDefault")->setValue(23.75);control<QDoubleSpinBox>(window,"Setup_motion.accelerationMmPerSec2")->setValue(27.5);control<QDoubleSpinBox>(window,"Setup_motion.decelerationMmPerSec2")->setValue(812.0);
        control<QDoubleSpinBox>(window,"Setup_forceControl.approachSpeed")->setValue(8);control<QDoubleSpinBox>(window,"Setup_forceControl.mediumSpeed")->setValue(4);control<QDoubleSpinBox>(window,"Setup_forceControl.fineSpeed")->setValue(2);control<QDoubleSpinBox>(window,"Setup_forceControl.reverseSpeed")->setValue(1);control<QDoubleSpinBox>(window,"Setup_forceControl.mediumErrorN")->setValue(9);control<QDoubleSpinBox>(window,"Setup_forceControl.fineErrorN")->setValue(3);control<QDoubleSpinBox>(window,"Setup_forceControl.toleranceN")->setValue(.7);control<QDoubleSpinBox>(window,"Setup_forceControl.maxTravelMm")->setValue(44);control<QDoubleSpinBox>(window,"Setup_forceControl.timeoutSec")->setValue(77);
        control<QDoubleSpinBox>(window,"Setup_protection.maxAllowedForceN")->setValue(321);control<QComboBox>(window,"Setup_force.displayUnit")->setCurrentText("gf");control<QSpinBox>(window,"Setup_force.displayDecimals")->setValue(2);control<QComboBox>(window,"Setup_force.loadcellCapacityDisplayUnit")->setCurrentText("gf");control<QDoubleSpinBox>(window,"Setup_force.loadcellCapacityN")->setValue(500);
        control<QSpinBox>(window,"Setup_ioMapping.emergency")->setValue(7);control<QSpinBox>(window,"Setup_ioMapping.upperLimit")->setValue(6);control<QSpinBox>(window,"Setup_ioMapping.lowerLimit")->setValue(5);control<QSpinBox>(window,"Setup_ioMapping.jogUp")->setValue(4);control<QSpinBox>(window,"Setup_ioMapping.jogDown")->setValue(3);control<QSpinBox>(window,"Setup_ioMapping.externalGo")->setValue(2);control<QSpinBox>(window,"Setup_ioMapping.externalStop")->setValue(1);control<QSpinBox>(window,"Setup_ioMapping.activeLowMask")->setValue(165);
        control<QCheckBox>(window,"Setup_adcFilter.lowLevelEnabled")->setChecked(true);control<QDoubleSpinBox>(window,"Setup_adcFilter.lowLevelAlpha")->setValue(.17);setComboData(control<QComboBox>(window,"Setup_adcFilter.powerLineMode"),4);control<QCheckBox>(window,"Setup_adcFilter.medianEnabled")->setChecked(true);control<QSpinBox>(window,"Setup_adcFilter.movingAverageN")->setValue(37);control<QSpinBox>(window,"Setup_ui.forceDisplayAverageSamples")->setValue(43);control<QComboBox>(window,"Setup_ui.calibrationReferenceUnit")->setCurrentText("kgf");
        control<QComboBox>(window,"Setup_profile.name")->setEditText("setup_roundtrip");control<QPushButton>(window,"SaveProfileButton")->click();
        if(QSettings().value("profiles/lastSelected").toString()!="setup_roundtrip")fail("profile save selection");
    }
    UtmUiController restarted;const MachineProfile loadedBeforeUi=restarted.profile();if(loadedBeforeUi.name!="setup_roundtrip")fail("restart profile load");restarted.startOffline();MainWindow restored(&restarted);
    const auto& p=restarted.profile();if(p.adapterName!="test_eth9"||p.startup.logicalServoIndex!=3||p.startup.logicalAdcIndex!=4||p.startup.logicalIoIndex!=5||p.startup.logicalEncoderIndex!=6||!p.startup.encoderRequired||!p.startup.autoServoOn)fail("device roundtrip");
    if(!near(p.jog.config.servoUnitsPerMm,1234.5)||p.jog.config.servoDirectionSign!=-1||!near(p.jog.minJogSpeedMmPerMin,.25)||!near(p.jog.maxJogSpeedMmPerMin,876.5)||!near(p.jog.config.physicalJogSpeedMmPerMin,23.75)||!near(p.motionAccelerationMmPerSec2,27.5)||!near(p.motionDecelerationMmPerSec2,812))fail("motion roundtrip");
    if(p.force.forceDirectionSign!=-1||!near(p.force.approachSpeedMmPerMin,8)||!near(p.force.mediumSpeedMmPerMin,4)||!near(p.force.fineSpeedMmPerMin,2)||!near(p.force.reverseSpeedMmPerMin,1)||!near(p.force.mediumErrorN,9)||!near(p.force.fineErrorN,3)||!near(p.force.toleranceN,.7)||!near(p.force.maxTravelMm,44)||!near(p.force.timeoutSec,77))fail("force roundtrip");
    if(!near(p.protection.maxAllowedForceN,321)||p.displayForceUnit!="gf"||p.forceDisplayDecimals!=2||p.loadcellCapacityDisplayUnit!="gf"||!near(p.loadcellCapacityN,4.903325,1e-12))fail("force display canonical roundtrip");
    if(!p.adcFilter.lowLevelFilterEnabled||!near(p.adcFilter.lowLevelFilterAlpha,.17)||p.adcFilter.powerLineFilterMode!=4||!p.adcFilter.medianFilterEnabled||p.adcFilter.movingAverageSampleCount!=37||p.forceDisplayAverageSamples!=43)fail("filter roundtrip");
    const auto& io=p.startup.ioMapping;if(io.emergencyBit!=7||io.upperLimitBit!=6||io.lowerLimitBit!=5||io.jogUpBit!=4||io.jogDownBit!=3||io.externalGoBit!=2||io.externalStopBit!=1||io.activeLowMask!=165)fail("IO roundtrip");
    if(p.calibrationReferenceUnit!="kgf")fail("reference unit roundtrip");
    const UtmConnectionConfig applied=restarted.hardwareConfiguration();if(!near(applied.jog.config.servoUnitsPerMm,1234.5)||applied.jog.config.servoDirectionSign!=-1||!near(applied.jog.maxJogSpeedMmPerMin,876.5)||applied.force.forceDirectionSign!=-1||!near(applied.force.approachSpeedMmPerMin,8)||!near(applied.protection.maxAllowedForceN,321)||applied.startup.ioMapping.activeLowMask!=165||applied.startup.autoServoOn!=1)fail("reconnect hardware configuration snapshot");
    if(!near(control<QDoubleSpinBox>(restored,"Setup_motion.servoUnitsPerMm")->value(),1234.5)||control<QComboBox>(restored,"Setup_force.displayUnit")->currentText()!="gf"||control<QSpinBox>(restored,"Setup_adcFilter.movingAverageN")->value()!=37||!near(control<QDoubleSpinBox>(restored,"Setup_force.loadcellCapacityN")->value(),500,1e-6))fail("UI restore");
    if(loadedBeforeUi.adapterName!=p.adapterName||!near(loadedBeforeUi.loadcellCapacityN,p.loadcellCapacityN)||loadedBeforeUi.adcFilter.movingAverageSampleCount!=p.adcFilter.movingAverageSampleCount)fail("profile load signal side effect");
    control<QDoubleSpinBox>(restored,"Setup_adcFilter.lowLevelAlpha")->setValue(.29);setComboData(control<QComboBox>(restored,"Setup_adcFilter.powerLineMode"),5);control<QSpinBox>(restored,"Setup_adcFilter.movingAverageN")->setValue(41);control<QSpinBox>(restored,"Setup_ui.forceDisplayAverageSamples")->setValue(47);control<QPushButton>(restored,"CalibrationApplyFilters")->click();
    MachineProfile filterAutosaved;QString filterError;if(!MachineProfileStore::load("setup_roundtrip",filterAutosaved,filterError)||!near(filterAutosaved.adcFilter.lowLevelFilterAlpha,.29)||filterAutosaved.adcFilter.powerLineFilterMode!=5||filterAutosaved.adcFilter.movingAverageSampleCount!=41||filterAutosaved.forceDisplayAverageSamples!=47)fail("APPLY FILTERS autosave");
    std::cout<<"Setup persistence/UI restart tests passed\n";return 0;
}
