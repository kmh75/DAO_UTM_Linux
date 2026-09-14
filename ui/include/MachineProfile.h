#pragma once

#include "DaoUtm.Types.h"
#include "UtmComplianceCompensation.h"

#include <QString>
#include <QStringList>
#include <QVector>

struct MachineComplianceCurveProfile
{
    bool enabled = false;
    unsigned int version = 0;
    QVector<dao::utm::CompliancePoint> points;
};

struct MachineProfile
{
    QString name = "default";
    QString adapterName;
    UtmStartupConfig startup{};
    UtmJogConfigV2 jog{};
    UtmForceControlConfig force{};
    UtmMachineProtectionConfig protection{};
    UtmAdcFilterConfig adcFilter{};
    double motionAccelerationMmPerSec2 = 1.0;
    double motionDecelerationMmPerSec2 = 1.0;
    QString displayForceUnit = "N";
    int forceDisplayDecimals = 3;
    double loadcellCapacityN = 1000.0;
    QString loadcellCapacityDisplayUnit = "N";
    QString calibrationReferenceUnit = "N";
    unsigned int forceDisplayAverageSamples = 20;
    double adcCalibrationScale = 1.0;
    bool adcCalibrationScaleValid = false;
    double encoderCalibrationScale = 1.0;
    bool encoderCalibrationScaleValid = false;
    int lastSelectedTab = 0;
    bool fullscreen = false;
    bool complianceEnabled = false;
    unsigned int complianceVersion = 1;
    QVector<dao::utm::CompliancePoint> compliancePoints;
    MachineComplianceCurveProfile compressionCompliance;
    MachineComplianceCurveProfile tensionCompliance;
    int activeComplianceMode = UTM_COMPLIANCE_MODE_COMPRESSION;
    UtmComplianceCalibrationConfigV1 complianceAutoCalibration{};

    MachineProfile();
    bool applyMotionDynamics(QString& error);
};

class MachineProfileStore
{
public:
    static QString profileDirectory();
    static QString profilePath(const QString& name);
    static QStringList availableProfiles();
    static bool validate(const MachineProfile& profile, QString& error);
    static bool save(const MachineProfile& profile, QString& error);
    static bool load(const QString& name, MachineProfile& profile, QString& error);
};
