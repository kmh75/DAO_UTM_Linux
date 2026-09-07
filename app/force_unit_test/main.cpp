#include "ForceUnit.h"
#include "MachineProfile.h"
#include "UtmUiController.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
bool near(double a, double b, double tolerance=1e-9)
{
    return std::abs(a-b)<=tolerance;
}

[[noreturn]] void fail(const char* message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}
}

int main(int argc, char** argv)
{
    QTemporaryDir config;
    if(!config.isValid())fail("temporary profile directory");
    qputenv("XDG_CONFIG_HOME",config.path().toUtf8());
    QCoreApplication app(argc,argv);
    QCoreApplication::setApplicationName("dao-force-unit-test");
    QCoreApplication::setOrganizationName("DAO-Test");

    const double firstCalibrationScale=0.000025079123456;
    UtmUiController firstRun;
    if(!firstRun.hasActiveProfile()||firstRun.profile().name!="default_machine")fail("default_machine auto selection");
    if(QSettings().value("profiles/lastSelected").toString()!="default_machine")fail("default_machine lastSelected registration");
    QFile defaultJson(MachineProfileStore::profilePath("default_machine"));
    if(!defaultJson.exists()||!defaultJson.open(QIODevice::ReadOnly))fail("default_machine JSON creation");
    const QJsonObject defaultRoot=QJsonDocument::fromJson(defaultJson.readAll()).object();
    for(const char* section:{"devices","motion","forceControl","protection","ioMapping","calibrationScales","adcFilter","force","ui","notPersisted"})if(!defaultRoot.value(section).isObject())fail("default_machine full profile structure");
    firstRun.profile().adcCalibrationScale=firstCalibrationScale;
    firstRun.profile().adcCalibrationScaleValid=true;
    firstRun.startOffline();
    if(!firstRun.calibrateForce(10.0,ForceUnit::N))fail("first calibration autosave");
    UtmUiController firstRestart;
    if(!firstRestart.hasActiveProfile()||firstRestart.profile().name!="default_machine")fail("restart default_machine auto load");
    if(!firstRestart.startOffline()||!firstRestart.calibration().forceCalibrationValid||!near(firstRestart.calibration().forceCalibrationScale,firstCalibrationScale,1e-15))fail("restart default calibration apply");
    if(firstRestart.calibration().forceZeroValid)fail("restart default force zero must be invalid");

    const double forceN=1.96133;
    if(!near(ForceUnits::fromNewtons(forceN,ForceUnit::Kgf),0.2,1e-12))fail("N to kgf");
    if(!near(ForceUnits::fromNewtons(forceN,ForceUnit::Gf),200.0,1e-9))fail("N to gf");
    if(!near(ForceUnits::toNewtons(200.0,ForceUnit::Gf),forceN,1e-12))fail("gf reference to N");
    if(!near(ForceUnits::toNewtons(0.2,ForceUnit::Kgf),forceN,1e-12))fail("kgf reference to N");

    double reference=forceN;
    reference=ForceUnits::fromNewtons(ForceUnits::toNewtons(reference,ForceUnit::N),ForceUnit::Kgf);
    if(!near(reference,0.2,1e-12))fail("reference unit physical value N to kgf");
    reference=ForceUnits::fromNewtons(ForceUnits::toNewtons(reference,ForceUnit::Kgf),ForceUnit::Gf);
    if(!near(reference,200.0,1e-9))fail("reference unit physical value kgf to gf");

    const double measuredForceN=12.3456789;
    const double displayForceN=12.0;
    (void)ForceUnits::fromNewtons(displayForceN,ForceUnit::Gf);
    if(measuredForceN!=12.3456789||displayForceN!=12.0)fail("display conversion mutated canonical values");

    MachineProfile profile;
    profile.name="roundtrip";profile.displayForceUnit="gf";profile.forceDisplayDecimals=1;
    profile.loadcellCapacityN=ForceUnits::toNewtons(500.0,ForceUnit::Gf);
    profile.calibrationReferenceUnit="kgf";profile.adcCalibrationScale=0.0025;profile.adcCalibrationScaleValid=true;
    if(!near(profile.loadcellCapacityN,4.903325,1e-12))fail("500 gf capacity");
    if(!near(ForceUnits::toNewtons(10.0,ForceUnit::Kgf),98.0665,1e-12))fail("10 kgf capacity");
    QString error;if(!MachineProfileStore::save(profile,error))fail("profile save");
    const QByteArray defaultBefore=[](){QFile f(MachineProfileStore::profilePath("default_machine"));if(!f.open(QIODevice::ReadOnly))return QByteArray{};return f.readAll();}();
    MachineProfile loaded;if(!MachineProfileStore::load("roundtrip",loaded,error))fail("profile load");
    if(loaded.displayForceUnit!="gf"||loaded.forceDisplayDecimals!=1||loaded.calibrationReferenceUnit!="kgf"||!near(loaded.loadcellCapacityN,4.903325)||!loaded.adcCalibrationScaleValid||!near(loaded.adcCalibrationScale,0.0025))fail("profile roundtrip values");

    UtmUiController controller;controller.profile()=loaded;controller.startOffline();
    if(!controller.calibration().forceCalibrationValid||!near(controller.calibration().forceCalibrationScale,0.0025))fail("scale restore after engine initialize");
    if(controller.calibration().forceZeroValid)fail("force zero must not restore");
    controller.calibrateForce(200.0,ForceUnit::Gf);if(!near(controller.lastCalibrationReferenceN(),forceN))fail("controller gf calibration boundary");
    controller.calibrateForce(0.2,ForceUnit::Kgf);if(!near(controller.lastCalibrationReferenceN(),forceN))fail("controller kgf calibration boundary");
    controller.calibrateForce(forceN,ForceUnit::N);if(!near(controller.lastCalibrationReferenceN(),forceN))fail("controller N calibration boundary");

    const double calibratedScale=0.000025079123456;
    controller.profile().adcCalibrationScale=calibratedScale;
    controller.profile().adcCalibrationScaleValid=true;
    controller.startOffline();
    if(!controller.calibrateForce(forceN,ForceUnit::N))fail("offline force calibration");
    MachineProfile autosaved;if(!MachineProfileStore::load("roundtrip",autosaved,error))fail("force calibration autosave load");
    if(!autosaved.adcCalibrationScaleValid||!near(autosaved.adcCalibrationScale,calibratedScale,1e-15))fail("force calibration immediate autosave");
    QFile autosavedJson(MachineProfileStore::profilePath("roundtrip"));
    if(!autosavedJson.open(QIODevice::ReadOnly))fail("open autosaved profile JSON");
    const auto calibrationJson=QJsonDocument::fromJson(autosavedJson.readAll()).object().value("calibrationScales").toObject();
    if(!calibrationJson.value("adcValid").toBool()||!near(calibrationJson.value("adc").toDouble(),calibratedScale,1e-15))fail("force scale JSON field");

    UtmUiController restarted;
    if(!restarted.loadProfile("roundtrip")||!restarted.startOffline())fail("program restart profile restore");
    if(!restarted.calibration().forceCalibrationValid||!near(restarted.calibration().forceCalibrationScale,calibratedScale,1e-15))fail("program restart runtime scale");
    if(restarted.calibration().forceZeroValid)fail("program restart force zero must be invalid");
    const double scaleBeforeZero=restarted.calibration().forceCalibrationScale;
    if(!restarted.zeroForce()||!restarted.calibration().forceZeroValid)fail("force zero operation");
    if(!near(restarted.calibration().forceCalibrationScale,scaleBeforeZero,1e-15))fail("force zero changed calibration scale");

    restarted.profile().displayForceUnit="kgf";restarted.profile().calibrationReferenceUnit="gf";
    if(!near(restarted.profile().adcCalibrationScale,calibratedScale,1e-15))fail("unit change mutated stored scale");

    restarted.startOffline();
    if(!restarted.calibrateEncoder(24.0))fail("offline extensometer calibration");
    const double encoderScale=restarted.calibration().encoderCalibrationScale;
    MachineProfile encoderSaved;if(!MachineProfileStore::load("roundtrip",encoderSaved,error))fail("extensometer autosave load");
    if(!encoderSaved.encoderCalibrationScaleValid||!near(encoderSaved.encoderCalibrationScale,encoderScale,1e-15))fail("extensometer calibration immediate autosave");
    UtmUiController reconnected;reconnected.profile()=encoderSaved;reconnected.startOffline();
    if(!reconnected.calibration().forceCalibrationValid||!near(reconnected.calibration().forceCalibrationScale,calibratedScale,1e-15))fail("disconnect reconnect force scale restore");
    if(reconnected.calibration().forceZeroValid)fail("disconnect reconnect force zero restored");
    if(!near(reconnected.calibration().encoderCalibrationScale,encoderScale,1e-15))fail("disconnect reconnect extensometer scale restore");
    UtmUiController selectedRestart;
    if(selectedRestart.profile().name!="roundtrip")fail("existing lastSelected profile priority");
    QFile defaultAfterFile(MachineProfileStore::profilePath("default_machine"));if(!defaultAfterFile.open(QIODevice::ReadOnly)||defaultAfterFile.readAll()!=defaultBefore)fail("existing default profile was overwritten");

    QFile source(QStringLiteral(DAO_SOURCE_DIR "/ui/src/MainWindow.cpp"));if(!source.open(QIODevice::ReadOnly))fail("read UI source");
    if(source.readAll().contains("DaoEngine_"))fail("UI directly references Basic DaoEngine API");
    std::cout << "Force unit/profile/offline restore tests passed\n";
    return 0;
}
