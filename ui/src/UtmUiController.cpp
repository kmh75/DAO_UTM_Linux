#include "UtmUiController.h"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QtMath>

#include <cstring>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
QString validateJogConfig(const UtmJogConfigV2& value)
{
    const auto& jog=value.config;
    if(!std::isfinite(jog.servoUnitsPerMm)||jog.servoUnitsPerMm<=0.0)return "servoUnitsPerMm must be finite and > 0";
    if(jog.servoDirectionSign!=1&&jog.servoDirectionSign!=-1)return "servoDirectionSign must be +1 or -1";
    if(!std::isfinite(value.minJogSpeedMmPerMin)||value.minJogSpeedMmPerMin<=0.0)return "minJogSpeedMmPerMin must be finite and > 0";
    if(!std::isfinite(value.maxJogSpeedMmPerMin)||value.maxJogSpeedMmPerMin<value.minJogSpeedMmPerMin)return "maxJogSpeedMmPerMin must be finite and >= minimum";
    if(!std::isfinite(jog.physicalJogSpeedMmPerMin)||jog.physicalJogSpeedMmPerMin<value.minJogSpeedMmPerMin||jog.physicalJogSpeedMmPerMin>value.maxJogSpeedMmPerMin)return "physicalJogSpeedMmPerMin must be inside min/max";
    if(jog.acceleration==0)return "acceleration must be > 0";
    if(jog.deceleration==0)return "deceleration must be > 0";
    const double minimumTarget=value.minJogSpeedMmPerMin*jog.servoUnitsPerMm/60.0;
    const double maximumTarget=value.maxJogSpeedMmPerMin*jog.servoUnitsPerMm/60.0;
    if(!std::isfinite(minimumTarget)||std::llround(minimumTarget)<1)return "minJogSpeedMmPerMin converts to less than one Servo unit/s";
    if(!std::isfinite(maximumTarget)||maximumTarget>static_cast<double>(std::numeric_limits<int>::max()))return "maxJogSpeedMmPerMin exceeds Servo target range";
    return {};
}
}

UtmUiController::UtmUiController(QObject* parent) : QObject(parent)
{
    ensureActiveProfile();
    timer_.setInterval(40);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &UtmUiController::pollRuntime);
}

bool UtmUiController::ensureActiveProfile()
{
    if(activeProfileValid_)return true;
    QSettings settings;const QString lastProfile=settings.value("profiles/lastSelected").toString();qInfo().noquote()<<QString("[PROFILE LOAD] lastSelected=%1 configRoot=%2").arg(lastProfile.isEmpty()?"<none>":lastProfile,QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    auto activate=[this,&settings](const QString& name)
    {
        QString error;MachineProfile loaded;
        if(!MachineProfileStore::load(name,loaded,error)){qWarning().noquote()<<QString("[PROFILE] load failed path=%1 error=%2").arg(MachineProfileStore::profilePath(name),error);return false;}
        profile_=loaded;activeProfileValid_=true;profileApplyState_="PROFILE_LOADED";settings.setValue("profiles/lastSelected",profile_.name);settings.sync();
        qInfo().noquote()<<QString("[PROFILE] active=%1 path=%2").arg(profile_.name,MachineProfileStore::profilePath(profile_.name));
        qInfo().noquote()<<QString("[PROFILE] loaded forceCalibrationScale=%1 valid=%2").arg(profile_.adcCalibrationScale,0,'g',17).arg(profile_.adcCalibrationScaleValid);
        qInfo().noquote()<<QString("[PROFILE] loaded extensometerScale=%1 valid=%2").arg(profile_.encoderCalibrationScale,0,'g',17).arg(profile_.encoderCalibrationScaleValid);
        qInfo().noquote()<<QString("[PROFILE LOAD] active=%1").arg(profile_.name);qInfo().noquote()<<QString("[PROFILE LOAD] path=%1").arg(MachineProfileStore::profilePath(profile_.name));qInfo().noquote()<<QString("[PROFILE LOAD] motionDeceleration=%1").arg(profile_.motionDecelerationMmPerSec2,0,'g',17);qInfo().noquote()<<QString("[PROFILE LOAD] displayUnit=%1").arg(profile_.displayForceUnit);qInfo().noquote()<<QString("[PROFILE LOAD] forceScale=%1").arg(profile_.adcCalibrationScale,0,'g',17);
        return true;
    };
    if(!lastProfile.isEmpty()&&activate(lastProfile))return true;
    if(lastProfile!="default_machine"&&QFileInfo::exists(MachineProfileStore::profilePath("default_machine"))&&activate("default_machine"))return true;
    if(!QFileInfo::exists(MachineProfileStore::profilePath("default_machine")))
    {
        MachineProfile created;created.name="default_machine";QString error;
        if(MachineProfileStore::save(created,error)){profile_=created;activeProfileValid_=true;profileApplyState_="PROFILE_LOADED";settings.setValue("profiles/lastSelected",created.name);settings.sync();qInfo().noquote()<<QString("[PROFILE] created default active=%1 path=%2 profilesDirectoryExists=%3 settingsStatus=%4").arg(created.name,MachineProfileStore::profilePath(created.name)).arg(QFileInfo::exists(MachineProfileStore::profileDirectory())).arg(settings.status());return true;}
        qWarning().noquote()<<QString("[PROFILE] default creation failed path=%1 error=%2").arg(MachineProfileStore::profilePath("default_machine"),error);
    }
    profile_=MachineProfile{};activeProfileValid_=false;
    qWarning().noquote()<<"[PROFILE] active=<none>; using non-persistent code safe defaults";
    return false;
}

UtmUiController::~UtmUiController()
{
    timer_.stop();
    if (ownsEngine_)
    {
        DaoUtm_Stop();
        DaoUtm_Shutdown();
    }
}

bool UtmUiController::startOffline()
{
    offline_ = true;
    offlineStartedMs_ = QDateTime::currentMSecsSinceEpoch();
    runtime_ = {};
    runtime_.runtime.runtime.runtime.runtime.runtime.initialized = 1;
    runtime_.runtime.runtime.runtime.runtime.runtime.controlLoopRunning = 1;
    runtime_.runtime.runtime.runtime.runtime.runtime.machineState = UTM_MACHINE_READY;
    runtime_.runtime.runtime.runtime.runtime.runtime.input.communicationValid = 1;
    runtime_.runtime.runtime.runtime.runtime.runtime.input.servoOn = 1;
    runtime_.runtime.runtime.runtime.runtime.runtime.input.servoReady = 1;
    runtime_.runtime.runtime.runtime.runtime.runtime.input.forceValid = 1;
    runtime_.runtime.runtime.runtime.startup.startupComplete = 1;
    runtime_.runtime.runtime.runtime.startup.startupPhase = UTM_STARTUP_COMPLETE;
    runtime_.runtime.runtime.motion.positionZeroValid = 1;
    calibration_.engineeringForceN = 24.0;
    calibration_.forceValid = 1;
    calibration_.forceCalibrationValid = profile_.adcCalibrationScaleValid ? 1 : 0;
    calibration_.forceZeroValid = 0;
    calibration_.forceCalibrationScale = profile_.adcCalibrationScaleValid ? profile_.adcCalibrationScale : 1.0;
    calibration_.encoderPresent = 1;
    calibration_.encoderValid = 1;
    calibration_.encoderSignedCount = 12000;
    calibration_.encoderRawCount = 12000;
    calibration_.encoderCalibrationScale = profile_.encoderCalibrationScaleValid ? profile_.encoderCalibrationScale : 1.0;
    calibration_.positionZeroValid = 1;
    timer_.start();
    emit runtimeUpdated();
    return true;
}

bool UtmUiController::startHardware(const UtmStartupConfig& startup,
    const UtmJogConfigV2& jog, const UtmForceControlConfig& force,
    const UtmMachineProtectionConfig& protection)
{
    profile_.startup=startup;profile_.jog=jog;profile_.force=force;
    profile_.protection=protection;profile_.adapterName=startup.ethercatAdapterName;
    return connectProfile();
}

UtmConnectionConfig UtmUiController::hardwareConfiguration() const
{
    UtmConnectionConfig config{};config.startup=profile_.startup;config.jog=profile_.jog;config.force=profile_.force;config.protection=profile_.protection;return config;
}

QStringList UtmUiController::refreshAdapters()
{
    UtmAdapterInfo adapters[UTM_MAX_ADAPTERS]{};unsigned int count=0;QStringList values;
    qInfo().noquote()<<"[UTM] adapter discovery start";
    const int resultValue=DaoUtm_GetAvailableAdapters(adapters,UTM_MAX_ADAPTERS,&count);
    qInfo().noquote()<<QString("[UTM] DaoUtm_GetAvailableAdapters result=%1 count=%2").arg(resultValue).arg(count);
    if(resultValue==0){emit commandFailed("Adapter scan","Unable to enumerate network adapters");return values;}
    for(unsigned int i=0;i<std::min(count,UTM_MAX_ADAPTERS);++i)
    {
        values << QString::fromUtf8(adapters[i].name)+" — "+QString::fromUtf8(adapters[i].description);
        if(verbose_)qInfo().noquote()<<QString("[UTM] adapter found index=%1 name=%2 description=%3")
            .arg(i).arg(QString::fromUtf8(adapters[i].name)).arg(QString::fromUtf8(adapters[i].description));
    }
    return values;
}

bool UtmUiController::connectProfile()
{
    if(!ensureActiveProfile()){emit commandFailed("Connect / Profile","No persistent active machine profile is available");return false;}
    if(ownsEngine_){emit commandFailed("Connect","UTM Engine is already connected");return false;}
    if(profile_.adapterName.trimmed().isEmpty())
    {
        qWarning().noquote()<<"[UTM] connect rejected: adapterName is empty";
        emit commandFailed("Connect","Select an EtherCAT adapter before connecting");
        return false;
    }
    QString dynamicsError;if(!profile_.applyMotionDynamics(dynamicsError)){emit commandFailed("Connect / Motion dynamics",dynamicsError);return false;}
    const QByteArray adapter=profile_.adapterName.toUtf8();
    std::memset(profile_.startup.ethercatAdapterName,0,sizeof(profile_.startup.ethercatAdapterName));
    std::strncpy(profile_.startup.ethercatAdapterName,adapter.constData(),sizeof(profile_.startup.ethercatAdapterName)-1);
    UtmConnectionConfig config=hardwareConfiguration();
    offline_=false;
    const auto& jog=config.jog.config;
    qInfo().noquote()<<QString("[UTM] Jog V2 config servoUnitsPerMm=%1 servoDirectionSign=%2 physicalJogSpeedMmPerMin=%3 minJogSpeedMmPerMin=%4 maxJogSpeedMmPerMin=%5 acceleration=%6 deceleration=%7")
        .arg(jog.servoUnitsPerMm,0,'g',15).arg(jog.servoDirectionSign).arg(jog.physicalJogSpeedMmPerMin,0,'g',15)
        .arg(config.jog.minJogSpeedMmPerMin,0,'g',15).arg(config.jog.maxJogSpeedMmPerMin,0,'g',15)
        .arg(jog.acceleration).arg(jog.deceleration);
    const QString jogError=validateJogConfig(config.jog);
    if(!jogError.isEmpty())
    {
        qWarning().noquote()<<QString("[UTM] ConfigureJogV2 preflight failed field=%1 adapterName=%2").arg(jogError).arg(profile_.adapterName);
        emit commandFailed("Connect / ConfigureJogV2",jogError);
        emit connectionChanged();return false;
    }
    qInfo().noquote()<<QString("[UTM] initialize/connect request adapter=%1 servo=%2 adc=%3 io=%4 encoder=%5 autoServoOn=%6")
        .arg(profile_.adapterName).arg(config.startup.logicalServoIndex).arg(config.startup.logicalAdcIndex)
        .arg(config.startup.logicalIoIndex).arg(config.startup.logicalEncoderIndex).arg(config.startup.autoServoOn);
    timer_.stop();
    const int connectResult=DaoUtm_Connect(&config);
    qInfo().noquote()<<QString("[UTM] DaoUtm_Connect result=%1 adapter=%2").arg(connectResult).arg(profile_.adapterName);
    if(!result("Connect",connectResult))
    {
        UtmRuntimeInfoV3 diagnostic{};const int runtimeResult=DaoUtm_GetRuntimeV3(&diagnostic);
        qWarning().noquote()<<QString("[UTM] connect failed function=DaoUtm_Connect return=%1 runtimeResult=%2 startupPhase=%3 startupFault=%4 adapter=%5")
            .arg(connectResult).arg(runtimeResult).arg(diagnostic.startup.startupPhase)
            .arg(diagnostic.startup.startupFault).arg(profile_.adapterName);
        emit connectionChanged();
        timer_.start();
        return false;
    }
    ownsEngine_=true;pendingScaleApply_=true;profileApplyFailureReported_=false;runtime_={};lastLoggedStartupPhase_=-1;lastLoggedStartupFault_=-1;timer_.start();emit connectionChanged();
    emit profileStatus("Connecting — active profile configuration pending verification");return true;
}

void UtmUiController::disconnectEngine()
{
    if(!ownsEngine_)return;
    timer_.stop();
    DaoUtm_Disconnect();
    ownsEngine_=false;pendingScaleApply_=false;runtime_={};calibration_={};lastLoggedStartupPhase_=-1;lastLoggedStartupFault_=-1;emit connectionChanged();
    profileApplying_=false;profileApplyState_="IDLE";timer_.start();
    emit profileStatus("Disconnected — zero states were not persisted");
}

QStringList UtmUiController::profileNames() const{return MachineProfileStore::availableProfiles();}
bool UtmUiController::saveProfile(const QString& name)
{
    qInfo().noquote()<<"[PROFILE SAVE] requested";
    qInfo().noquote()<<QString("[PROFILE SAVE] active=%1").arg(name);qInfo().noquote()<<QString("[PROFILE SAVE] path=%1").arg(MachineProfileStore::profilePath(name));qInfo().noquote()<<QString("[PROFILE SAVE] motionDeceleration=%1").arg(profile_.motionDecelerationMmPerSec2,0,'g',17);qInfo().noquote()<<QString("[PROFILE SAVE] displayUnit=%1").arg(profile_.displayForceUnit);qInfo().noquote()<<QString("[PROFILE SAVE] forceScale=%1").arg(profile_.adcCalibrationScale,0,'g',17);
    qInfo().noquote()<<QString("[PROFILE SAVE] adcFilter.lowLevelEnabled=%1").arg(profile_.adcFilter.lowLevelFilterEnabled);qInfo().noquote()<<QString("[PROFILE SAVE] adcFilter.alpha=%1").arg(profile_.adcFilter.lowLevelFilterAlpha,0,'g',17);qInfo().noquote()<<QString("[PROFILE SAVE] adcFilter.powerLineMode=%1").arg(profile_.adcFilter.powerLineFilterMode);qInfo().noquote()<<QString("[PROFILE SAVE] adcFilter.medianEnabled=%1").arg(profile_.adcFilter.medianFilterEnabled);qInfo().noquote()<<QString("[PROFILE SAVE] adcFilter.movingAverageN=%1").arg(profile_.adcFilter.movingAverageSampleCount);qInfo().noquote()<<QString("[PROFILE SAVE] displayAverageSamples=%1").arg(profile_.forceDisplayAverageSamples);
    const QString previousName=profile_.name;profile_.name=name;QString error;if(!MachineProfileStore::save(profile_,error)){profile_.name=previousName;emit commandFailed("Save profile",error);return false;}
    activeProfileValid_=true;profileApplyState_="PROFILE_LOADED";QSettings().setValue("profiles/lastSelected",profile_.name);emit profileStatus("Profile saved (zero states excluded)");return true;
}

bool UtmUiController::autosaveCalibrationScale(bool force)
{
    if(!ensureActiveProfile()){emit commandFailed(force?"Force calibration profile save":"Extensometer calibration profile save","No persistent active machine profile is available; calibration was NOT SAVED");emit profileStatus("CALIBRATION APPLIED / PROFILE SAVE FAILED");return false;}
    QString error;
    if(!MachineProfileStore::save(profile_,error))
    {
        const QString kind=force?"Force calibration":"Extensometer calibration";
        qWarning().noquote()<<QString("[PROFILE] autosave failed path=%1 error=%2").arg(MachineProfileStore::profilePath(profile_.name),error);
        emit commandFailed(kind+" profile save",error+"; calibration is applied for this session but was NOT SAVED");
        emit profileStatus("CALIBRATION APPLIED / PROFILE SAVE FAILED");
        return false;
    }
    QSettings().setValue("profiles/lastSelected",profile_.name);
    qInfo().noquote()<<QString("[PROFILE] autosaved %1=%2").arg(force?"forceCalibrationScale":"extensometerScale").arg(force?profile_.adcCalibrationScale:profile_.encoderCalibrationScale,0,'g',17);
    qInfo().noquote()<<QString("[PROFILE] path=%1").arg(MachineProfileStore::profilePath(profile_.name));
    emit profileStatus("CALIBRATION APPLIED / PROFILE SAVED");
    return true;
}
bool UtmUiController::loadProfile(const QString& name)
{
    QString error;MachineProfile loaded;if(!MachineProfileStore::load(name,loaded,error)){emit commandFailed("Load profile",error);return false;}
    if(ownsEngine_){emit commandFailed("Load profile","Disconnect before loading a machine profile");return false;}
    profile_=loaded;activeProfileValid_=true;pendingScaleApply_=true;profileApplyState_="PROFILE_LOADED";
    qInfo().noquote()<<QString("[PROFILE] active=%1 path=%2").arg(profile_.name,MachineProfileStore::profilePath(profile_.name));
    qInfo().noquote()<<QString("[PROFILE] loaded forceCalibrationScale=%1 valid=%2").arg(profile_.adcCalibrationScale,0,'g',17).arg(profile_.adcCalibrationScaleValid);
    qInfo().noquote()<<QString("[PROFILE] loaded extensometerScale=%1 valid=%2").arg(profile_.encoderCalibrationScale,0,'g',17).arg(profile_.encoderCalibrationScaleValid);
    QSettings().setValue("profiles/lastSelected",profile_.name);
    emit profileStatus("Profile loaded — calibration scales will apply after READY; force zero required; position/encoder zero not restored");return true;
}

QString UtmUiController::utmVersion() const
{ return QString::fromUtf8(DaoUtm_GetVersion()); }
QString UtmUiController::basicVersion() const
{ return QString::fromUtf8(DaoUtm_GetBasicEngineVersion()); }

bool UtmUiController::result(const QString& operation, int value)
{
    if (offline_) return true;
    if (value != 0) return true;
    const auto& motion = runtime_.runtime.runtime.motion;
    emit commandFailed(operation,
        QString("Engine rejected command (machine=%1, motion=%2, failure=%3, stop=%4)")
            .arg(runtime_.runtime.runtime.runtime.runtime.runtime.machineState)
            .arg(motion.motionState).arg(motion.motionFailureReason)
            .arg(runtime_.runtime.runtime.runtime.runtime.runtime.stop.primaryReason));
    return false;
}

bool UtmUiController::startJog(int d, double s)
{ if(verbose_)qInfo().noquote()<<QString("[UI JOG] press source=UI direction=%1 speed=%2").arg(d==UTM_DIRECTION_UP?"UP":"DOWN").arg(s);const int rc=offline_?1:DaoUtm_StartJog(d,s,UTM_COMMAND_SOURCE_UI);if(offline_&&rc)QTimer::singleShot(80,this,[this,d,s]{auto& jog=runtime_.runtime.runtime.runtime.runtime.jog;jog.jogActive=1;jog.jogDirection=d;jog.jogSpeedMmPerMin=s;runtime_.runtime.runtime.runtime.runtime.runtime.machineState=UTM_MACHINE_MANUAL;emit runtimeUpdated();});if(verbose_)qInfo().noquote()<<QString("[UI JOG] DaoUtm_StartJog result=%1").arg(rc);return result("Jog start",rc); }
bool UtmUiController::stopJog()
{ if(verbose_)qInfo().noquote()<<"[UI JOG] stop request source=UI";const int rc=offline_?1:DaoUtm_StopJog(UTM_COMMAND_SOURCE_UI);if(offline_){++offlineJogStopCount_;auto& jog=runtime_.runtime.runtime.runtime.runtime.jog;jog={};runtime_.runtime.runtime.runtime.runtime.runtime.machineState=UTM_MACHINE_READY;emit runtimeUpdated();}if(verbose_)qInfo().noquote()<<QString("[UI JOG] DaoUtm_StopJog result=%1").arg(rc);return result("Jog stop",rc); }
bool UtmUiController::moveAbsolute(double p, double s, unsigned int a,
    unsigned int d, unsigned int t)
{ if(verbose_)qInfo().noquote()<<QString("[UI MOTION] MOVE_ABSOLUTE targetMm=%1 speed=%2 accel=%3 decel=%4 timeoutMs=%5").arg(p).arg(s).arg(a).arg(d).arg(t);const int rc=offline_?1:DaoUtm_MoveAbsolute(p,s,a,d,t,UTM_COMMAND_SOURCE_UI);if(verbose_)qInfo().noquote()<<QString("[UI MOTION] DaoUtm_MoveAbsolute result=%1").arg(rc);return result("Move absolute",rc); }
bool UtmUiController::moveIncremental(double p, double s, unsigned int a,
    unsigned int d, unsigned int t)
{ if(verbose_)qInfo().noquote()<<QString("[UI MOTION] MOVE_INCREMENTAL distanceMm=%1 speed=%2 accel=%3 decel=%4 timeoutMs=%5").arg(p).arg(s).arg(a).arg(d).arg(t);const int rc=offline_?1:DaoUtm_MoveIncremental(p,s,a,d,t,UTM_COMMAND_SOURCE_UI);if(verbose_)qInfo().noquote()<<QString("[UI MOTION] DaoUtm_MoveIncremental result=%1").arg(rc);return result("Move incremental",rc); }
bool UtmUiController::moveVelocity(int dir, double s, unsigned int a, unsigned int d)
{ if(verbose_)qInfo().noquote()<<QString("[UI MOTION] MOVE_VELOCITY direction=%1 speed=%2 accel=%3 decel=%4").arg(dir).arg(s).arg(a).arg(d);const int rc=offline_?1:DaoUtm_MoveVelocity(dir,s,a,d,UTM_COMMAND_SOURCE_UI);if(verbose_)qInfo().noquote()<<QString("[UI MOTION] DaoUtm_MoveVelocity result=%1").arg(rc);return result("Move velocity",rc); }
bool UtmUiController::moveToForce(int dir, double f, double s, double travel,
    double timeout, unsigned int a, unsigned int d)
{ return result("Move to force", offline_ ? 1 : DaoUtm_MoveToForce(dir,f,s,a,d,travel,
    static_cast<unsigned int>(timeout*1000.0),UTM_COMMAND_SOURCE_UI)); }
bool UtmUiController::holdForce(int dir, double f, double hold, double tolerance,
    double travel, double timeout)
{ return result("Hold force", offline_ ? 1 : DaoUtm_HoldForce(dir,f,hold,tolerance,travel,
    static_cast<unsigned int>(timeout*1000.0),UTM_COMMAND_SOURCE_UI)); }
bool UtmUiController::stopMotion()
{ return result("Motion stop", offline_ ? 1 : DaoUtm_StopMotion(UTM_COMMAND_SOURCE_UI)); }
bool UtmUiController::setPositionZero()
{ return result("Set position zero", offline_ ? 1 : DaoUtm_SetPositionZero()); }
bool UtmUiController::clearPositionZero()
{ return result("Clear position zero", offline_ ? 1 : DaoUtm_ClearPositionZero()); }
bool UtmUiController::zeroForce()
{ if(offline_){calibration_.forceZeroValid=1;offlineDisplayCollected_=0;displayForceRuntime_.state=UTM_DISPLAY_FORCE_STABILIZING;++displayForceRuntime_.generation;++displayForceRuntime_.resetCount;displayForceRuntime_.lastResetReason=UTM_DISPLAY_FORCE_RESET_FORCE_ZERO;}return result("Zero force", offline_ ? 1 : DaoUtm_ZeroForce()); }
bool UtmUiController::zeroEncoder(unsigned int timeout)
{ if(offline_){calibration_.encoderResetState=1;QTimer::singleShot(120,this,[this]{calibration_.encoderResetState=2;calibration_.encoderResetCompletedStatus=1;emit runtimeUpdated();});}return result("Zero encoder", offline_ ? 1 : DaoUtm_ZeroEncoder(timeout)); }
bool UtmUiController::calibrateForce(double referenceValue, ForceUnit referenceUnit)
{ if(!ensureActiveProfile()){emit commandFailed("Force calibration","No persistent active machine profile is available");return false;}const double referenceN=ForceUnits::toNewtons(referenceValue,referenceUnit);lastCalibrationReferenceN_=referenceN;if(offline_){calibration_.forceCalibrationValid=1;offlineDisplayCollected_=0;displayForceRuntime_.state=UTM_DISPLAY_FORCE_STABILIZING;++displayForceRuntime_.generation;++displayForceRuntime_.resetCount;displayForceRuntime_.lastResetReason=UTM_DISPLAY_FORCE_RESET_FORCE_CALIBRATION;}const int rc=offline_?1:DaoUtm_CalibrateForce(referenceN);if(rc){if(offline_){profile_.adcCalibrationScale=calibration_.forceCalibrationScale;profile_.adcCalibrationScaleValid=true;qInfo().noquote()<<QString("[CAL] force calibration success scale=%1").arg(calibration_.forceCalibrationScale,0,'g',17);autosaveCalibrationScale(true);}else{forceCalibrationSavePending_=true;forceCalibrationCaptureObserved_=false;}}return result("Force calibration",rc); }
bool UtmUiController::configureAdcFilters(const UtmAdcFilterConfig& config)
{ if(offline_){calibration_.lowLevelFilterEnabled=config.lowLevelFilterEnabled;calibration_.lowLevelFilterAlpha=config.lowLevelFilterAlpha;calibration_.powerLineFilterMode=config.powerLineFilterMode;calibration_.medianFilterEnabled=config.medianFilterEnabled;calibration_.movingAverageSampleCount=config.movingAverageSampleCount;offlineDisplayCollected_=0;displayForceRuntime_.state=UTM_DISPLAY_FORCE_STABILIZING;++displayForceRuntime_.generation;++displayForceRuntime_.resetCount;displayForceRuntime_.lastResetReason=UTM_DISPLAY_FORCE_RESET_ADC_FILTER;}const int rc=offline_?1:DaoUtm_ConfigureAdcFilters(&config);if(rc)profile_.adcFilter=config;return result("ADC filter configuration",rc); }

bool UtmUiController::applyAdcFilterConfiguration(const UtmAdcFilterConfig& config,unsigned int displayAverageSamples)
{
    if(!ensureActiveProfile()){emit commandFailed("Apply filters","No persistent active machine profile is available");return false;}
    const MachineProfile previous=profile_;MachineProfile candidate=previous;candidate.adcFilter=config;candidate.forceDisplayAverageSamples=displayAverageSamples;QString error;if(!MachineProfileStore::validate(candidate,error)){emit commandFailed("Apply filters",error);return false;}
    qInfo().noquote()<<QString("[FILTER APPLY] IIR=%1 alpha=%2 notch=%3 median=%4 avgN=%5 displayAvg=%6")
        .arg(config.lowLevelFilterEnabled).arg(config.lowLevelFilterAlpha,0,'g',17).arg(config.powerLineFilterMode)
        .arg(config.medianFilterEnabled).arg(config.movingAverageSampleCount).arg(displayAverageSamples);
    if(!configureAdcFilters(config)||!configureDisplayForceAverage(displayAverageSamples)){profile_=previous;return false;}
    UtmCalibrationRuntimeInfo adc{};UtmDisplayForceRuntimeInfo display{};bool readback=true;
    if(offline_){adc=calibration_;display=displayForceRuntime_;display.configuredSampleCount=displayAverageSamples;displayForceRuntime_.configuredSampleCount=displayAverageSamples;}
    else readback=DaoUtm_GetCalibrationRuntime(&adc)!=0&&DaoUtm_GetDisplayForceRuntime(&display)!=0;
    const bool verified=readback&&adc.lowLevelFilterEnabled==config.lowLevelFilterEnabled&&qFuzzyCompare(adc.lowLevelFilterAlpha,config.lowLevelFilterAlpha)&&adc.powerLineFilterMode==config.powerLineFilterMode&&adc.medianFilterEnabled==config.medianFilterEnabled&&adc.movingAverageSampleCount==config.movingAverageSampleCount&&display.configuredSampleCount==displayAverageSamples;
    qInfo().noquote()<<QString("[FILTER VERIFY] IIR=%1 alpha=%2 notch=%3 median=%4 avgN=%5 displayAvg=%6 verified=%7")
        .arg(adc.lowLevelFilterEnabled).arg(adc.lowLevelFilterAlpha,0,'g',17).arg(adc.powerLineFilterMode)
        .arg(adc.medianFilterEnabled).arg(adc.movingAverageSampleCount).arg(display.configuredSampleCount).arg(verified?1:0);
    if(!verified){profile_=previous;emit commandFailed("Apply filters","Runtime readback does not match the requested filter configuration; profile was not saved");return false;}
    profile_=candidate;
    const bool saved=MachineProfileStore::save(profile_,error);
    qInfo().noquote()<<QString("[PROFILE AUTOSAVE] filter active=%1 path=%2 IIR=%3 alpha=%4 notch=%5 median=%6 avgN=%7 displayAvg=%8 commitResult=%9")
        .arg(profile_.name,MachineProfileStore::profilePath(profile_.name)).arg(profile_.adcFilter.lowLevelFilterEnabled)
        .arg(profile_.adcFilter.lowLevelFilterAlpha,0,'g',17).arg(profile_.adcFilter.powerLineFilterMode)
        .arg(profile_.adcFilter.medianFilterEnabled).arg(profile_.adcFilter.movingAverageSampleCount)
        .arg(profile_.forceDisplayAverageSamples).arg(saved?1:0);
    if(!saved){profile_=previous;emit commandFailed("Apply filters","Runtime filters were applied but profile autosave failed: "+error);emit profileStatus("FILTER APPLIED / SAVE FAILED");return false;}
    QSettings().setValue("profiles/lastSelected",profile_.name);activeProfileValid_=true;
    emit profileStatus("FILTER APPLIED / SAVED");return true;
}
bool UtmUiController::calibrateEncoder(double reference)
{ if(!ensureActiveProfile()){emit commandFailed("Extensometer calibration","No persistent active machine profile is available");return false;}if(offline_&&calibration_.encoderSignedCount!=0){calibration_.encoderCalibrationScale=reference/static_cast<double>(calibration_.encoderSignedCount);calibration_.encoderEngineeringPosition=reference;}const int rc=offline_?1:DaoUtm_CalibrateEncoder(reference);if(rc){if(offline_){profile_.encoderCalibrationScale=calibration_.encoderCalibrationScale;profile_.encoderCalibrationScaleValid=true;qInfo().noquote()<<QString("[CAL] extensometer calibration success scale=%1").arg(calibration_.encoderCalibrationScale,0,'g',17);autosaveCalibrationScale(false);}else encoderCalibrationSavePending_=true;}return result("Encoder calibration",rc); }

bool UtmUiController::applyStoredConfiguration()
{
    if(!activeProfileValid_){profileApplyState_="PROFILE_APPLY_FAILED: NO_ACTIVE_PROFILE";emit profileStatus(profileApplyState_);return false;}
    profileApplying_=true;profileApplyState_="PROFILE_APPLYING";emit profileStatus(profileApplyState_);
    bool applied=true;qInfo().noquote()<<QString("[ADC FILTER] stored: IIR=%1 alpha=%2 notch=%3 median=%4 avgN=%5 displayAvg=%6").arg(profile_.adcFilter.lowLevelFilterEnabled).arg(profile_.adcFilter.lowLevelFilterAlpha,0,'g',17).arg(profile_.adcFilter.powerLineFilterMode).arg(profile_.adcFilter.medianFilterEnabled).arg(profile_.adcFilter.movingAverageSampleCount).arg(profile_.forceDisplayAverageSamples);qInfo().noquote()<<"[ADC FILTER] applying...";
    profileApplyState_="ADC_FILTER";applied=DaoUtm_ConfigureAdcFilters(&profile_.adcFilter)!=0;
    if(applied&&profile_.adcCalibrationScaleValid){profileApplyState_="FORCE_SCALE";qInfo().noquote()<<QString("[CAL] applying stored force scale=%1").arg(profile_.adcCalibrationScale,0,'g',17);const int rc=DaoUtm_SetForceCalibrationScale(profile_.adcCalibrationScale);qInfo().noquote()<<QString("[CAL] DaoUtm_SetForceCalibrationScale result=%1").arg(rc);applied=rc!=0;}
    const auto& base=runtime_.runtime.runtime.runtime.runtime.runtime;
    if(applied&&profile_.encoderCalibrationScaleValid&&base.input.encoderPresent){profileApplyState_="EXTENSOMETER_SCALE";qInfo().noquote()<<QString("[CAL] applying stored extensometer scale=%1").arg(profile_.encoderCalibrationScale,0,'g',17);applied=DaoUtm_SetEncoderCalibrationScale(profile_.encoderCalibrationScale)!=0;}
    if(applied){profileApplyState_="DISPLAY_AVERAGE";applied=DaoUtm_ConfigureDisplayForceAverage(profile_.forceDisplayAverageSamples)!=0;}
    UtmCalibrationRuntimeInfo verified{};if(applied)applied=DaoUtm_GetCalibrationRuntime(&verified)!=0;
    UtmDisplayForceRuntimeInfo displayVerified{};if(applied)applied=DaoUtm_GetDisplayForceRuntime(&displayVerified)!=0;
    if(applied)applied=verified.lowLevelFilterEnabled==profile_.adcFilter.lowLevelFilterEnabled&&qFuzzyCompare(verified.lowLevelFilterAlpha,profile_.adcFilter.lowLevelFilterAlpha)&&verified.powerLineFilterMode==profile_.adcFilter.powerLineFilterMode&&verified.medianFilterEnabled==profile_.adcFilter.medianFilterEnabled&&verified.movingAverageSampleCount==profile_.adcFilter.movingAverageSampleCount&&displayVerified.configuredSampleCount==profile_.forceDisplayAverageSamples;
    qInfo().noquote()<<QString("[ADC FILTER] runtime: IIR=%1 alpha=%2 notch=%3 median=%4 avgN=%5 displayAvg=%6").arg(verified.lowLevelFilterEnabled).arg(verified.lowLevelFilterAlpha,0,'g',17).arg(verified.powerLineFilterMode).arg(verified.medianFilterEnabled).arg(verified.movingAverageSampleCount).arg(displayVerified.configuredSampleCount);
    qInfo().noquote()<<QString("[ADC FILTER] verified=%1").arg(applied?1:0);
    if(applied&&profile_.adcCalibrationScaleValid)applied=verified.forceCalibrationValid&&qFuzzyCompare(verified.forceCalibrationScale,profile_.adcCalibrationScale);
    if(applied&&profile_.encoderCalibrationScaleValid&&base.input.encoderPresent)applied=qFuzzyCompare(verified.encoderCalibrationScale,profile_.encoderCalibrationScale);
    qInfo().noquote()<<QString("[CAL] runtime scale after apply=%1").arg(verified.forceCalibrationScale,0,'g',17);
    qInfo().noquote()<<QString("[CAL] calibrationValid=%1").arg(verified.forceCalibrationValid);
    if(applied){calibration_=verified;profileApplyFailureReported_=false;}else if(!profileApplyFailureReported_){profileApplyFailureReported_=true;emit commandFailed("Profile apply",profileApplyState_+" failed or runtime readback mismatched; PROFILE_APPLIED withheld");}
    profileApplying_=false;profileApplyState_=applied?"PROFILE_APPLIED":"PROFILE_APPLY_FAILED: "+profileApplyState_;
    emit profileStatus(applied?"PROFILE_APPLIED — calibration restored; force/position/extensometer zero still required":profileApplyState_);
    return applied;
}
bool UtmUiController::configureDisplayForceAverage(unsigned int count)
{ if(count<20||count>50){emit commandFailed("Display average","Sample count must be 20..50");return false;}const bool changed=profile_.forceDisplayAverageSamples!=count;const int rc=offline_?1:DaoUtm_ConfigureDisplayForceAverage(count);if(rc)profile_.forceDisplayAverageSamples=count;if(offline_){displayForceRuntime_.configuredSampleCount=count;if(changed){offlineDisplayCollected_=0;displayForceRuntime_.state=UTM_DISPLAY_FORCE_STABILIZING;++displayForceRuntime_.generation;++displayForceRuntime_.resetCount;displayForceRuntime_.lastResetReason=UTM_DISPLAY_FORCE_RESET_SAMPLE_COUNT;}}return result("Display average",rc); }
bool UtmUiController::requestStop()
{ if (!offline_) DaoUtm_RequestUserStop(); return true; }
bool UtmUiController::acknowledgeStop()
{ return result("Acknowledge stop", offline_ ? 1 : DaoUtm_AcknowledgeStop()); }
bool UtmUiController::retryStartup()
{ return result("Retry startup", offline_ ? 1 : DaoUtm_RetryStartup()); }
bool UtmUiController::configureForce(const UtmForceControlConfig& c)
{ return result("Configure force", offline_ ? 1 : DaoUtm_ConfigureForceControl(&c)); }
bool UtmUiController::configureProtection(const UtmMachineProtectionConfig& c)
{ return result("Configure protection", offline_ ? 1 : DaoUtm_ConfigureMachineProtection(&c)); }
bool UtmUiController::loadSequence(const UtmSequenceDefinition& d)
{ return result("Load sequence", offline_ ? 1 : DaoUtm_LoadSequence(&d)); }
bool UtmUiController::validateSequence()
{ return result("Validate sequence", offline_ ? 1 : DaoUtm_ValidateSequence()); }
bool UtmUiController::commitSequence()
{ return result("Commit sequence", offline_ ? 1 : DaoUtm_CommitSequence()); }
bool UtmUiController::startSequence()
{ return result("Start sequence", offline_ ? 1 : DaoUtm_StartSequence()); }
bool UtmUiController::stopSequence()
{ return result("Stop sequence", offline_ ? 1 : DaoUtm_StopSequence()); }

void UtmUiController::pollRuntime()
{
    if (offline_) updateOffline();
    else if (ownsEngine_)
    {
        UtmRuntimeInfoV6 next{};
        if(DaoUtm_GetRuntimeV6(&next)!=0){runtime_=next;++runtimePollSuccessCount_;}
        else{++runtimePollFailureCount_;emit commandFailed("Runtime refresh", "DaoUtm_GetRuntimeV6 failed; retaining last valid snapshot");}
    }
    if(ownsEngine_)
    {
        UtmCalibrationRuntimeInfo nextCalibration{};
        if(DaoUtm_GetCalibrationRuntime(&nextCalibration)!=0)calibration_=nextCalibration;
        UtmDisplayForceRuntimeInfo nextDisplay{};
        if(DaoUtm_GetDisplayForceRuntime(&nextDisplay)!=0){displayForceRuntime_=nextDisplay;displayForceN_=nextDisplay.displayForceN;displayForceValid_=nextDisplay.state==UTM_DISPLAY_FORCE_VALID;displayForceAverageSamples_=nextDisplay.configuredSampleCount;}
        const auto& startup=runtime_.runtime.runtime.runtime.startup;
        if(startup.startupPhase!=lastLoggedStartupPhase_||startup.startupFault!=lastLoggedStartupFault_)
        {
            const auto& servoRuntime=runtime_.runtime.runtime.runtime.runtime.runtime.input;
            const QString message=QString("[UTM] runtime startup phase=%1 fault=%2 adapterName=%3 slaveCount=%4 autoServoOn=%5 servoOn/operationEnabled=%6 servoState=0x%7")
                .arg(startup.startupPhase).arg(startup.startupFault)
                .arg(QString::fromUtf8(startup.selectedAdapterName,static_cast<int>(strnlen(startup.selectedAdapterName,sizeof(startup.selectedAdapterName))))).arg(startup.slaveCount).arg(profile_.startup.autoServoOn).arg(servoRuntime.servoOn).arg(servoRuntime.servoOperationState,0,16);
            if(startup.startupFault!=UTM_STARTUP_FAULT_NONE)qWarning().noquote()<<message;
            else qInfo().noquote()<<message;
            lastLoggedStartupPhase_=startup.startupPhase;lastLoggedStartupFault_=startup.startupFault;
        }
        const auto& base=runtime_.runtime.runtime.runtime.runtime.runtime;
        const auto& jog=runtime_.runtime.runtime.runtime.runtime.jog;const auto& motion=runtime_.runtime.runtime.motion;
        if(verbose_&&(jog.jogActive!=lastLoggedJogActive_||jog.jogDirection!=lastLoggedJogDirection_||base.machineState!=lastLoggedMachineState_))
        {qInfo().noquote()<<QString("[UTM JOG] machine=%1 runtime active=%2 direction=%3 targetVelocity=%4 sourceMask=0x%5").arg(base.machineState).arg(jog.jogActive).arg(jog.jogDirection).arg(jog.servoTargetVelocity).arg(jog.activeJogSourceMask,0,16);lastLoggedJogActive_=jog.jogActive;lastLoggedJogDirection_=jog.jogDirection;lastLoggedMachineState_=base.machineState;}
        if(verbose_&&motion.motionState!=lastLoggedMotionState_){qInfo().noquote()<<QString("[UTM MOTION] type=%1 state=%2 source=%3 targetPosition=%4 targetVelocity=%5 failure=%6").arg(motion.motionType).arg(motion.motionState).arg(motion.motionSource).arg(motion.servoTargetPosition).arg(motion.servoTargetVelocity).arg(motion.motionFailureReason);lastLoggedMotionState_=motion.motionState;}
        if(pendingScaleApply_&&base.initialized&&base.controlLoopRunning&&base.input.communicationValid)
        {
            if(applyStoredConfiguration())pendingScaleApply_=false;
        }
        if(forceCalibrationSavePending_&&calibration_.forceCaptureActive&&calibration_.forceCaptureType==2)forceCalibrationCaptureObserved_=true;
        if(forceCalibrationSavePending_&&forceCalibrationCaptureObserved_&&!calibration_.forceCaptureActive&&calibration_.forceCalibrationValid&&std::isfinite(calibration_.forceCalibrationScale)&&calibration_.forceCalibrationScale>0.0){qInfo().noquote()<<QString("[CAL] force calibration success scale=%1").arg(calibration_.forceCalibrationScale,0,'g',17);profile_.adcCalibrationScale=calibration_.forceCalibrationScale;profile_.adcCalibrationScaleValid=true;forceCalibrationSavePending_=false;forceCalibrationCaptureObserved_=false;autosaveCalibrationScale(true);}
        if(encoderCalibrationSavePending_&&calibration_.encoderPresent&&std::isfinite(calibration_.encoderCalibrationScale)&&calibration_.encoderCalibrationScale>0.0){qInfo().noquote()<<QString("[CAL] extensometer calibration success scale=%1").arg(calibration_.encoderCalibrationScale,0,'g',17);profile_.encoderCalibrationScale=calibration_.encoderCalibrationScale;profile_.encoderCalibrationScaleValid=true;encoderCalibrationSavePending_=false;autosaveCalibrationScale(false);}
        const qint64 now=QDateTime::currentMSecsSinceEpoch();
        if(verboseRuntime_&&now-lastRuntimeLogMs_>=1000){const auto& sequence=runtime_.sequence;qInfo().noquote()<<QString("[UI-RUNTIME] pollOk=%1 pollFail=%2 machine=%3 forceValid=%4 encoderPresent=%5 step=%6/%7 profile=%8 capture=%9:%10").arg(runtimePollSuccessCount_).arg(runtimePollFailureCount_).arg(base.machineState).arg(base.input.forceValid).arg(base.input.encoderPresent).arg(sequence.currentStepIndex).arg(sequence.stepCount).arg(profileApplyState_).arg(calibration_.forceCaptureType).arg(calibration_.forceCaptureActive);lastRuntimeLogMs_=now;}
    }
    emit runtimeUpdated();
}

void UtmUiController::updateOffline()
{
    const double t = (QDateTime::currentMSecsSinceEpoch() - offlineStartedMs_) / 1000.0;
    auto& base = runtime_.runtime.runtime.runtime.runtime.runtime;
    auto& motion = runtime_.runtime.runtime.motion;
    base.controlCycle += 20;
    base.publishedTimestampNs += 40000000ULL;
    base.input.forceN = 24.0 + 4.0 * qSin(t * 1.3);
    calibration_.engineeringForceN=base.input.forceN;
    calibration_.lowLevelFiltered=base.input.forceN;
    calibration_.powerLineFiltered=base.input.forceN;
    calibration_.zeroedValue=base.input.forceN;
    calibration_.calibratedValue=base.input.forceN;
    const unsigned int displayCount=std::clamp(profile_.forceDisplayAverageSamples,20u,50u);
    double displaySum=0.0;for(unsigned int i=0;i<displayCount;++i)displaySum+=24.0+4.0*qSin((t-static_cast<double>(i)*.002)*1.3);
    displayForceAverageSamples_=displayCount;displayForceRuntime_.configuredSampleCount=displayCount;
    offlineDisplayCollected_=std::min(displayCount,offlineDisplayCollected_+4);displayForceRuntime_.validSampleCount=offlineDisplayCollected_;
    if(offlineDisplayCollected_>=displayCount){displayForceN_=displaySum/displayCount;displayForceValid_=true;displayForceRuntime_.displayForceN=displayForceN_;displayForceRuntime_.state=UTM_DISPLAY_FORCE_VALID;}
    else{displayForceValid_=false;displayForceRuntime_.state=UTM_DISPLAY_FORCE_STABILIZING;}
    base.input.encoderPresent = 1;
    base.input.encoderValid = 1;
    base.input.encoderPosition = 12.0 + qSin(t * 0.4);
    calibration_.encoderPresent=1;calibration_.encoderValid=1;calibration_.encoderSignedCount=12000+static_cast<int>(1000*qSin(t*.4));calibration_.encoderRawCount=static_cast<unsigned int>(calibration_.encoderSignedCount);
    calibration_.encoderEngineeringPosition=base.input.encoderPosition;
    motion.machinePositionMm = 120.0 + 2.0 * qSin(t * 0.4);
    motion.testPositionMm = 12.0 + 2.0 * qSin(t * 0.4);
    motion.testZeroOffsetMm = 108.0;
    base.lastCycleTimeNs = 410000;
    base.maximumCycleTimeNs = 780000;
}
