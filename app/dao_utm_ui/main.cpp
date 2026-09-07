#include "MainWindow.h"
#include "UtmUiController.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>

#include <cstring>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("DAO UTM Local HMI");
    QApplication::setOrganizationName("DAO");
    qInfo().noquote()<<QString("[BUILD] profilePersistenceRevision=ADC_FILTER_SAVE_V3 executable=%1 compiled=%2 %3")
        .arg(QCoreApplication::applicationFilePath(),QStringLiteral(__DATE__),QStringLiteral(__TIME__));

    QCommandLineParser parser;
    parser.setApplicationDescription("DAO UTM 10.1-inch Local HMI");
    parser.addHelpOption();
    QCommandLineOption offline("offline", "Run UI with an isolated display-only runtime source.");
    QCommandLineOption fullscreen("fullscreen", "Open in full-screen appliance mode.");
    QCommandLineOption hardware("hardware", "Enable explicit EtherCAT hardware mode.");
    QCommandLineOption smoke("smoke-test", "Open offline UI briefly and exit successfully.");
    QCommandLineOption verbose("verbose", "Print detailed UI and EtherCAT startup diagnostics.");
    QCommandLineOption verboseRuntime("verbose-runtime", "Print a one-second UI runtime stability summary.");
    QCommandLineOption uiSelfTest("ui-self-test", "Exercise profile and calibration UI transitions without hardware.");
    QCommandLineOption adapter("adapter", "EtherCAT adapter name.", "name");
    QCommandLineOption units("servo-units-per-mm", "Override profile Servo units per mm.", "value");
    QCommandLineOption noAutoConnect("no-auto-connect", "Do not issue EtherCAT connect (integration diagnostics only).");
    QCommandLineOption persistenceSaveSmoke("persistence-save-smoke", "Exercise the production MainWindow SAVE PROFILE event path.");
    QCommandLineOption persistenceLoadSmoke("persistence-load-smoke", "Verify the production startup/profile/UI restore path.");
    parser.addOptions({offline, fullscreen, hardware, smoke, verbose, verboseRuntime, uiSelfTest, adapter, units,noAutoConnect,persistenceSaveSmoke,persistenceLoadSmoke});
    parser.process(app);

    UtmUiController controller;
    controller.setVerbose(parser.isSet(verbose));
    controller.setVerboseRuntime(parser.isSet(verboseRuntime));
    if(parser.isSet(verbose)||parser.isSet(verboseRuntime))qInfo().noquote()<<QString("[UI-ABI] UTM=%1 RuntimeV6=%2 CalibrationRuntime=%3")
        .arg(controller.utmVersion()).arg(sizeof(UtmRuntimeInfoV6)).arg(sizeof(UtmCalibrationRuntimeInfo));
    const bool hardwareRequested=parser.isSet(hardware);
    if (hardwareRequested)
    {
        qInfo().noquote()<<"[UI] hardware mode enabled";
        auto& profile=controller.profile();
        if(parser.isSet(adapter))profile.adapterName=parser.value(adapter);
        if(parser.isSet(units))profile.jog.config.servoUnitsPerMm=parser.value(units).toDouble();
        qInfo().noquote()<<QString("[UI] adapter requested: %1").arg(profile.adapterName.isEmpty()?"<not specified>":profile.adapterName);
    }
    else
    {
        controller.startOffline();
    }

    MainWindow window(&controller);
    qInfo().noquote()<<"[UI] window created";
    if (parser.isSet(fullscreen)) window.showFullScreen(); else window.show();
    qInfo().noquote()<<"[UI] window shown";
    if(hardwareRequested&&!parser.isSet(noAutoConnect))
    {
        QTimer::singleShot(0,&controller,[&controller]{
            qInfo().noquote()<<"[UI] hardware connect request dispatched";
            const bool connected=controller.connectProfile();
            qInfo().noquote()<<QString("[UI] hardware connect request completed result=%1; UI remains active").arg(connected?1:0);
        });
    }
    if(parser.isSet(persistenceSaveSmoke))QTimer::singleShot(0,&window,[&window,&controller,&app]{auto* deceleration=window.findChild<QDoubleSpinBox*>("Setup_motion.decelerationMmPerSec2");auto* unit=window.findChild<QComboBox*>("Setup_force.displayUnit");auto* profile=window.findChild<QComboBox*>("Setup_profile.name");auto* iir=window.findChild<QCheckBox*>("Setup_adcFilter.lowLevelEnabled");auto* alpha=window.findChild<QDoubleSpinBox*>("Setup_adcFilter.lowLevelAlpha");auto* notch=window.findChild<QComboBox*>("Setup_adcFilter.powerLineMode");auto* median=window.findChild<QCheckBox*>("Setup_adcFilter.medianEnabled");auto* avg=window.findChild<QSpinBox*>("Setup_adcFilter.movingAverageN");auto* displayAvg=window.findChild<QSpinBox*>("Setup_ui.forceDisplayAverageSamples");auto* save=window.findChild<QPushButton*>("SaveProfileButton");if(!deceleration||!unit||!profile||!iir||!alpha||!notch||!median||!avg||!displayAvg||!save){app.exit(2);return;}deceleration->setValue(812.0);unit->setCurrentText("gf");iir->setChecked(true);alpha->setValue(.17);notch->setCurrentIndex(notch->findData(5));median->setChecked(true);avg->setValue(37);displayAvg->setValue(43);profile->setEditText("production_path_test");controller.profile().adcCalibrationScale=0.000025079123456;controller.profile().adcCalibrationScaleValid=true;save->click();MachineProfile loaded;QString error;const bool ok=MachineProfileStore::load("production_path_test",loaded,error)&&qFuzzyCompare(loaded.motionDecelerationMmPerSec2,812.0)&&loaded.displayForceUnit=="gf"&&qFuzzyCompare(loaded.adcCalibrationScale,0.000025079123456)&&loaded.adcFilter.lowLevelFilterEnabled&&qFuzzyCompare(loaded.adcFilter.lowLevelFilterAlpha,.17)&&loaded.adcFilter.powerLineFilterMode==5&&loaded.adcFilter.medianFilterEnabled&&loaded.adcFilter.movingAverageSampleCount==37&&loaded.forceDisplayAverageSamples==43;qInfo().noquote()<<QString("[PRODUCTION PATH TEST] save=%1 error=%2").arg(ok).arg(error);app.exit(ok?0:3);});
    if(parser.isSet(persistenceLoadSmoke))QTimer::singleShot(0,&window,[&window,&controller,&app]{auto* deceleration=window.findChild<QDoubleSpinBox*>("Setup_motion.decelerationMmPerSec2");auto* unit=window.findChild<QComboBox*>("Setup_force.displayUnit");auto* iir=window.findChild<QCheckBox*>("Setup_adcFilter.lowLevelEnabled");auto* alpha=window.findChild<QDoubleSpinBox*>("Setup_adcFilter.lowLevelAlpha");auto* notch=window.findChild<QComboBox*>("Setup_adcFilter.powerLineMode");auto* median=window.findChild<QCheckBox*>("Setup_adcFilter.medianEnabled");auto* avg=window.findChild<QSpinBox*>("Setup_adcFilter.movingAverageN");auto* displayAvg=window.findChild<QSpinBox*>("Setup_ui.forceDisplayAverageSamples");auto* tabs=window.findChild<QTabWidget*>();const auto& p=controller.profile();const bool filters=p.adcFilter.lowLevelFilterEnabled&&qFuzzyCompare(p.adcFilter.lowLevelFilterAlpha,.17)&&p.adcFilter.powerLineFilterMode==5&&p.adcFilter.medianFilterEnabled&&p.adcFilter.movingAverageSampleCount==37&&p.forceDisplayAverageSamples==43&&iir&&iir->isChecked()&&alpha&&qFuzzyCompare(alpha->value(),.17)&&notch&&notch->currentData().toInt()==5&&median&&median->isChecked()&&avg&&avg->value()==37&&displayAvg&&displayAvg->value()==43;const bool ok=p.name=="production_path_test"&&qFuzzyCompare(p.motionDecelerationMmPerSec2,812.0)&&p.displayForceUnit=="gf"&&qFuzzyCompare(p.adcCalibrationScale,0.000025079123456)&&deceleration&&qFuzzyCompare(deceleration->value(),812.0)&&unit&&unit->currentText()=="gf"&&filters&&tabs&&tabs->currentIndex()==0&&tabs->tabText(0)=="Main";qInfo().noquote()<<QString("[PRODUCTION PATH TEST] restart=%1 currentTab=%2 filters=%3").arg(ok).arg(tabs?tabs->currentIndex():-1).arg(filters);app.exit(ok?0:4);});
    if(parser.isSet(uiSelfTest))QTimer::singleShot(50,&window,[&window,&controller]{controller.saveProfile("_offline_ui_self_test");controller.loadProfile("_offline_ui_self_test");window.runOfflineCalibrationSelfTest();});
    if (parser.isSet(smoke)) QTimer::singleShot(parser.isSet(uiSelfTest)?1900:750, &app, &QApplication::quit);
    return app.exec();
}
