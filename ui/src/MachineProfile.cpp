#include "MachineProfile.h"
#include "ForceUnit.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <cstring>
#include <cmath>
#include <limits>

MachineProfile::MachineProfile()
{
    startup.autoServoOn = 1;
    jog.config.servoUnitsPerMm = 1000.0;
    jog.config.physicalJogSpeedMmPerMin = 1.0;
    jog.config.servoDirectionSign = 1;
    jog.config.acceleration = 1000;
    jog.config.deceleration = 1000;
    jog.minJogSpeedMmPerMin = 0.1;
    jog.maxJogSpeedMmPerMin = 1000.0;
}

bool MachineProfile::applyMotionDynamics(QString& error)
{
    const double acceleration=motionAccelerationMmPerSec2*jog.config.servoUnitsPerMm;
    const double deceleration=motionDecelerationMmPerSec2*jog.config.servoUnitsPerMm;
    if(!std::isfinite(motionAccelerationMmPerSec2)||motionAccelerationMmPerSec2<=0.0||
       !std::isfinite(motionDecelerationMmPerSec2)||motionDecelerationMmPerSec2<=0.0||
       !std::isfinite(acceleration)||!std::isfinite(deceleration)||acceleration<1.0||deceleration<1.0||
       acceleration>std::numeric_limits<unsigned int>::max()||deceleration>std::numeric_limits<unsigned int>::max())
    {error="Motion acceleration/deceleration cannot be represented in Servo UU/s^2";return false;}
    jog.config.acceleration=static_cast<unsigned int>(std::llround(acceleration));
    jog.config.deceleration=static_cast<unsigned int>(std::llround(deceleration));
    return true;
}

QString MachineProfileStore::profileDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + "/profiles";
}

QStringList MachineProfileStore::availableProfiles()
{
    QDir dir(profileDirectory());
    QStringList names;
    for (const QString& file : dir.entryList({"*.json"}, QDir::Files, QDir::Name))
        names << file.left(file.size()-5);
    return names;
}

static QString safeName(QString name)
{
    name = name.trimmed();
    name.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
    return name.left(64);
}

QString MachineProfileStore::profilePath(const QString& name)
{
    return profileDirectory()+"/"+safeName(name)+".json";
}

bool MachineProfileStore::validate(const MachineProfile& p, QString& error)
{
    const auto finitePositive=[](double value){return std::isfinite(value)&&value>0.0;};
    const auto& j=p.jog;const auto& f=p.force;const auto& m=p.startup.ioMapping;
    if(!finitePositive(p.motionAccelerationMmPerSec2)||!finitePositive(p.motionDecelerationMmPerSec2))
    {error="Motion acceleration/deceleration must be finite and > 0 mm/s^2";return false;}
    const double accelerationUu=p.motionAccelerationMmPerSec2*j.config.servoUnitsPerMm;
    const double decelerationUu=p.motionDecelerationMmPerSec2*j.config.servoUnitsPerMm;
    if(!std::isfinite(accelerationUu)||!std::isfinite(decelerationUu)||accelerationUu<1.0||decelerationUu<1.0||accelerationUu>std::numeric_limits<unsigned int>::max()||decelerationUu>std::numeric_limits<unsigned int>::max()||std::llround(accelerationUu)!=j.config.acceleration||std::llround(decelerationUu)!=j.config.deceleration)
    {error="Motion dynamics mm/s^2 and Servo UU/s^2 values are inconsistent";return false;}
    if(p.startup.autoServoOn!=0&&p.startup.autoServoOn!=1){error="autoServoOn must be true or false";return false;}
    if(!finitePositive(j.config.servoUnitsPerMm)||!finitePositive(j.minJogSpeedMmPerMin)||
       !finitePositive(j.maxJogSpeedMmPerMin)||j.minJogSpeedMmPerMin>j.maxJogSpeedMmPerMin||
       !finitePositive(j.config.physicalJogSpeedMmPerMin)||
       j.config.physicalJogSpeedMmPerMin<j.minJogSpeedMmPerMin||
       j.config.physicalJogSpeedMmPerMin>j.maxJogSpeedMmPerMin||
       j.config.acceleration==0||j.config.deceleration==0)
    {error="Invalid servo or jog range";return false;}
    const double minTarget=j.minJogSpeedMmPerMin*j.config.servoUnitsPerMm/60.0;
    const double maxTarget=j.maxJogSpeedMmPerMin*j.config.servoUnitsPerMm/60.0;
    if(!std::isfinite(minTarget)||std::llround(minTarget)<1)
    {error="Jog minimum converts to less than one Servo unit/s";return false;}
    if(!std::isfinite(maxTarget)||maxTarget>static_cast<double>(std::numeric_limits<int>::max()))
    {error="Jog maximum exceeds Servo target range";return false;}
    if((j.config.servoDirectionSign!=1&&j.config.servoDirectionSign!=-1)||
       (f.forceDirectionSign!=1&&f.forceDirectionSign!=-1))
    {error="Direction sign must be +1 or -1";return false;}
    if(!finitePositive(f.approachSpeedMmPerMin)||!finitePositive(f.mediumSpeedMmPerMin)||
       !finitePositive(f.fineSpeedMmPerMin)||!finitePositive(f.reverseSpeedMmPerMin)||
       !finitePositive(f.mediumErrorN)||!finitePositive(f.fineErrorN)||
       !finitePositive(f.toleranceN)||!finitePositive(f.maxTravelMm)||
       !finitePositive(f.timeoutSec)||!finitePositive(p.protection.maxAllowedForceN))
    {error="Invalid force-control or protection value";return false;}
    for(unsigned int bit:{m.emergencyBit,m.upperLimitBit,m.lowerLimitBit,m.jogUpBit,
        m.jogDownBit,m.externalGoBit,m.externalStopBit})
        if(bit>7){error="IO mapping bit must be 0..7";return false;}
    if((p.adcCalibrationScaleValid&&!finitePositive(p.adcCalibrationScale))||
       (p.encoderCalibrationScaleValid&&!finitePositive(p.encoderCalibrationScale)))
    {error="Invalid calibration scale";return false;}
    if((p.adcFilter.lowLevelFilterEnabled!=0&&p.adcFilter.lowLevelFilterEnabled!=1)||
       !std::isfinite(p.adcFilter.lowLevelFilterAlpha)||p.adcFilter.lowLevelFilterAlpha<=0.0||p.adcFilter.lowLevelFilterAlpha>1.0||
       p.adcFilter.powerLineFilterMode<0||p.adcFilter.powerLineFilterMode>5||
       (p.adcFilter.medianFilterEnabled!=0&&p.adcFilter.medianFilterEnabled!=1)||
       p.adcFilter.movingAverageSampleCount<1||p.adcFilter.movingAverageSampleCount>64)
    {error="Invalid ADC filter settings";return false;}
    if(!ForceUnits::isValidText(p.displayForceUnit)||!ForceUnits::isValidText(p.calibrationReferenceUnit)||!ForceUnits::isValidText(p.loadcellCapacityDisplayUnit))
    {error="Force unit must be N, kgf, or gf";return false;}
    if(p.forceDisplayDecimals<0||p.forceDisplayDecimals>6)
    {error="Force display decimals must be 0..6";return false;}
    if(!finitePositive(p.loadcellCapacityN))
    {error="Loadcell capacity must be finite and > 0 N";return false;}
    if(p.forceDisplayAverageSamples<20||p.forceDisplayAverageSamples>50)
    {error="Force display average must be 20..50 samples";return false;}
    return true;
}

bool MachineProfileStore::save(const MachineProfile& p, QString& error)
{
    const QString name = safeName(p.name);
    if (name.isEmpty()) { error="Profile name is empty"; return false; }
    if (!validate(p,error)) return false;
    qInfo().noquote()<<QString("[FILTER JSON WRITE] iirEnabled=%1 alpha=%2 powerLineMode=%3 median=%4 movingAverageN=%5 displayAverageSamples=%6 lastTabSerialized=0")
        .arg(p.adcFilter.lowLevelFilterEnabled).arg(p.adcFilter.lowLevelFilterAlpha,0,'g',17)
        .arg(p.adcFilter.powerLineFilterMode).arg(p.adcFilter.medianFilterEnabled)
        .arg(p.adcFilter.movingAverageSampleCount).arg(p.forceDisplayAverageSamples);
    QDir dir; if (!dir.mkpath(profileDirectory())) { error="Cannot create profile directory"; return false; }
    QJsonObject root{{"schemaVersion",1},{"name",name},{"adapterName",p.adapterName}};
    root["devices"] = QJsonObject{{"servo",p.startup.logicalServoIndex},{"adc",p.startup.logicalAdcIndex},
        {"io",p.startup.logicalIoIndex},{"encoder",p.startup.logicalEncoderIndex},{"encoderRequired",p.startup.encoderRequired},{"autoServoOn",p.startup.autoServoOn!=0}};
    root["motion"] = QJsonObject{{"servoUnitsPerMm",p.jog.config.servoUnitsPerMm},
        {"servoDirectionSign",p.jog.config.servoDirectionSign},{"jogMin",p.jog.minJogSpeedMmPerMin},
        {"jogMax",p.jog.maxJogSpeedMmPerMin},{"jogDefault",p.jog.config.physicalJogSpeedMmPerMin},
        {"accelerationMmPerSec2",p.motionAccelerationMmPerSec2},{"decelerationMmPerSec2",p.motionDecelerationMmPerSec2}};
    root["forceControl"] = QJsonObject{{"forceDirectionSign",p.force.forceDirectionSign},
        {"approachSpeed",p.force.approachSpeedMmPerMin},{"mediumSpeed",p.force.mediumSpeedMmPerMin},
        {"fineSpeed",p.force.fineSpeedMmPerMin},{"reverseSpeed",p.force.reverseSpeedMmPerMin},
        {"mediumErrorN",p.force.mediumErrorN},{"fineErrorN",p.force.fineErrorN},
        {"toleranceN",p.force.toleranceN},{"maxTravelMm",p.force.maxTravelMm},{"timeoutSec",p.force.timeoutSec},
        {"acceleration",static_cast<int>(p.force.acceleration)},{"deceleration",static_cast<int>(p.force.deceleration)}};
    root["protection"] = QJsonObject{{"enabled",p.protection.overloadEnabled},{"maxAllowedForceN",p.protection.maxAllowedForceN}};
    const auto& m=p.startup.ioMapping;
    root["ioMapping"] = QJsonObject{{"emergency",m.emergencyBit},{"upperLimit",m.upperLimitBit},
        {"lowerLimit",m.lowerLimitBit},{"jogUp",m.jogUpBit},{"jogDown",m.jogDownBit},
        {"externalGo",m.externalGoBit},{"externalStop",m.externalStopBit},{"activeLowMask",m.activeLowMask}};
    root["calibrationScales"] = QJsonObject{{"adcValid",p.adcCalibrationScaleValid},
        {"adc",p.adcCalibrationScale},{"encoderValid",p.encoderCalibrationScaleValid},
        {"encoder",p.encoderCalibrationScale}};
    root["adcFilter"] = QJsonObject{{"lowLevelEnabled",p.adcFilter.lowLevelFilterEnabled},
        {"lowLevelAlpha",p.adcFilter.lowLevelFilterAlpha},{"powerLineMode",p.adcFilter.powerLineFilterMode},
        {"medianEnabled",p.adcFilter.medianFilterEnabled},{"movingAverageN",static_cast<int>(p.adcFilter.movingAverageSampleCount)}};
    root["force"] = QJsonObject{{"displayUnit",p.displayForceUnit},{"displayDecimals",p.forceDisplayDecimals},{"loadcellCapacityN",p.loadcellCapacityN},{"loadcellCapacityDisplayUnit",p.loadcellCapacityDisplayUnit}};
    root["ui"] = QJsonObject{{"fullscreen",p.fullscreen},{"forceDisplayUnit",p.displayForceUnit},{"forceDisplayAverageSamples",static_cast<int>(p.forceDisplayAverageSamples)},{"calibrationReferenceUnit",p.calibrationReferenceUnit}};
    root["notPersisted"] = QJsonObject{{"forceZero","operator action required"},
        {"positionZero","operator action required"},{"encoderZero","operator action required"}};
    QSaveFile file(profileDirectory()+"/"+name+".json");
    if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)){error=file.errorString();return false;}
    if(file.write(QJsonDocument(root).toJson(QJsonDocument::Indented))<0){error=file.errorString();file.cancelWriting();return false;}
    const bool committed=file.commit();
    qInfo().noquote()<<QString("[PROFILE SAVE] commitResult=%1 path=%2").arg(committed?1:0).arg(profilePath(name));
    if(!committed){error=file.errorString();return false;}
    return true;
}

bool MachineProfileStore::load(const QString& requested, MachineProfile& p, QString& error)
{
    QFile file(profilePath(requested));
    if(!file.open(QIODevice::ReadOnly)){error=file.errorString();return false;}
    QJsonParseError parse{};const QJsonDocument doc=QJsonDocument::fromJson(file.readAll(),&parse);
    if(parse.error!=QJsonParseError::NoError||!doc.isObject()){error=parse.errorString();return false;}
    const QJsonObject root=doc.object();if(root.value("schemaVersion").toInt()!=1){error="Unsupported profile schema";return false;}
    MachineProfile out;out.name=root.value("name").toString();out.adapterName=root.value("adapterName").toString();
    const auto dev=root.value("devices").toObject();out.startup.logicalServoIndex=dev.value("servo").toInt();out.startup.logicalAdcIndex=dev.value("adc").toInt();out.startup.logicalIoIndex=dev.value("io").toInt();out.startup.logicalEncoderIndex=dev.value("encoder").toInt();out.startup.encoderRequired=dev.value("encoderRequired").toInt();out.startup.autoServoOn=dev.value("autoServoOn").toBool(true)?1:0;
    const auto motion=root.value("motion").toObject();out.jog.config.servoUnitsPerMm=motion.value("servoUnitsPerMm").toDouble();out.jog.config.servoDirectionSign=motion.value("servoDirectionSign").toInt();out.jog.minJogSpeedMmPerMin=motion.value("jogMin").toDouble();out.jog.maxJogSpeedMmPerMin=motion.value("jogMax").toDouble();out.jog.config.physicalJogSpeedMmPerMin=motion.value("jogDefault").toDouble();
    if(motion.contains("accelerationMmPerSec2")&&motion.contains("decelerationMmPerSec2")){out.motionAccelerationMmPerSec2=motion.value("accelerationMmPerSec2").toDouble();out.motionDecelerationMmPerSec2=motion.value("decelerationMmPerSec2").toDouble();}else if(out.jog.config.servoUnitsPerMm>0.0){out.motionAccelerationMmPerSec2=1000.0/out.jog.config.servoUnitsPerMm;out.motionDecelerationMmPerSec2=1000.0/out.jog.config.servoUnitsPerMm;}
    if(!out.applyMotionDynamics(error))return false;
    const auto force=root.value("forceControl").toObject();out.force.forceDirectionSign=force.value("forceDirectionSign").toInt();out.force.approachSpeedMmPerMin=force.value("approachSpeed").toDouble();out.force.mediumSpeedMmPerMin=force.value("mediumSpeed").toDouble();out.force.fineSpeedMmPerMin=force.value("fineSpeed").toDouble();out.force.reverseSpeedMmPerMin=force.value("reverseSpeed").toDouble();out.force.mediumErrorN=force.value("mediumErrorN").toDouble();out.force.fineErrorN=force.value("fineErrorN").toDouble();out.force.toleranceN=force.value("toleranceN").toDouble();out.force.maxTravelMm=force.value("maxTravelMm").toDouble();out.force.timeoutSec=force.value("timeoutSec").toDouble();out.force.acceleration=force.value("acceleration").toInt();out.force.deceleration=force.value("deceleration").toInt();
    const auto protect=root.value("protection").toObject();out.protection.overloadEnabled=protect.value("enabled").toInt();out.protection.maxAllowedForceN=protect.value("maxAllowedForceN").toDouble();
    const auto map=root.value("ioMapping").toObject();out.startup.ioMapping.emergencyBit=map.value("emergency").toInt();out.startup.ioMapping.upperLimitBit=map.value("upperLimit").toInt();out.startup.ioMapping.lowerLimitBit=map.value("lowerLimit").toInt();out.startup.ioMapping.jogUpBit=map.value("jogUp").toInt();out.startup.ioMapping.jogDownBit=map.value("jogDown").toInt();out.startup.ioMapping.externalGoBit=map.value("externalGo").toInt();out.startup.ioMapping.externalStopBit=map.value("externalStop").toInt();out.startup.ioMapping.activeLowMask=map.value("activeLowMask").toInt();
    const auto calibration=root.value("calibrationScales").toObject();out.adcCalibrationScaleValid=calibration.value("adcValid").toBool();out.adcCalibrationScale=calibration.value("adc").toDouble(1);out.encoderCalibrationScaleValid=calibration.value("encoderValid").toBool();out.encoderCalibrationScale=calibration.value("encoder").toDouble(1);
    const auto filter=root.value("adcFilter").toObject();if(!filter.isEmpty()){out.adcFilter.lowLevelFilterEnabled=filter.value("lowLevelEnabled").toInt(1);out.adcFilter.lowLevelFilterAlpha=filter.value("lowLevelAlpha").toDouble(.1);out.adcFilter.powerLineFilterMode=filter.value("powerLineMode").toInt();out.adcFilter.medianFilterEnabled=filter.value("medianEnabled").toInt(1);out.adcFilter.movingAverageSampleCount=filter.value("movingAverageN").toInt(16);}
    const auto ui=root.value("ui").toObject();out.lastSelectedTab=0;out.fullscreen=ui.value("fullscreen").toBool();out.forceDisplayAverageSamples=ui.value("forceDisplayAverageSamples").toInt(20);out.calibrationReferenceUnit=ui.value("calibrationReferenceUnit").toString("N");
    const auto forcePreferences=root.value("force").toObject();out.displayForceUnit=forcePreferences.value("displayUnit").toString(ui.value("forceDisplayUnit").toString("N"));out.forceDisplayDecimals=forcePreferences.value("displayDecimals").toInt(3);out.loadcellCapacityN=forcePreferences.value("loadcellCapacityN").toDouble(1000.0);out.loadcellCapacityDisplayUnit=forcePreferences.value("loadcellCapacityDisplayUnit").toString("N");
    if(!validate(out,error)) return false;
    p=out;return true;
}
