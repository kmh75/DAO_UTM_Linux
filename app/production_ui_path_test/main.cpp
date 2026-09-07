#include <QCoreApplication>
#include <QProcess>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {
[[noreturn]] void fail(const char* message,const QByteArray& output={}){std::cerr<<"FAIL: "<<message<<'\n'<<output.constData();std::exit(1);}
QByteArray run(const QString& executable,const QStringList& arguments,const QString& configRoot)
{
    QProcess process;QProcessEnvironment environment=QProcessEnvironment::systemEnvironment();environment.insert("XDG_CONFIG_HOME",configRoot);environment.insert("QT_QPA_PLATFORM","offscreen");process.setProcessEnvironment(environment);process.setProcessChannelMode(QProcess::MergedChannels);process.start(executable,arguments);if(!process.waitForStarted(5000)||!process.waitForFinished(15000))fail("production executable timeout",process.readAll());const QByteArray output=process.readAll();if(process.exitStatus()!=QProcess::NormalExit||process.exitCode()!=0)fail("production executable failed",output);return output;
}
}

int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);QTemporaryDir config;if(!config.isValid())fail("temporary config");const QString executable=QStringLiteral(DAO_UI_EXECUTABLE);
    const QByteArray save=run(executable,{"--hardware","--no-auto-connect","--persistence-save-smoke","--verbose"},config.path());
    for(const char* marker:{"profilePersistenceRevision=ADC_FILTER_SAVE_V3","[FILTER UI SAVE] iirEnabled=1","notchIndex=5 notchText=60 + 120 Hz median=1 movingAverageN=37 displayAverageSamples=43","[FILTER CANDIDATE] iirEnabled=1","powerLineMode=5 median=1 movingAverageN=37 displayAverageSamples=43","[FILTER PROFILE COMMIT] iirEnabled=1","[FILTER JSON WRITE] iirEnabled=1","displayAverageSamples=43 lastTabSerialized=0","[PROFILE SAVE] requested","[PROFILE SAVE] active=production_path_test","motionDeceleration=812","displayUnit=gf","forceScale=2.5079","adcFilter.lowLevelEnabled=1","adcFilter.alpha=0.17","adcFilter.powerLineMode=5","adcFilter.medianEnabled=1","adcFilter.movingAverageN=37","displayAverageSamples=43","[PROFILE SAVE] commitResult=1","[PRODUCTION PATH TEST] save=1"})if(!save.contains(marker))fail("missing production save log",save);
    const QString profilePath=config.path()+"/DAO/DAO UTM Local HMI/profiles/production_path_test.json";QFile profile(profilePath);if(!profile.open(QIODevice::ReadOnly))fail("open production profile");QJsonDocument document=QJsonDocument::fromJson(profile.readAll());profile.close();const QJsonObject savedRoot=document.object(),savedFilter=savedRoot.value("adcFilter").toObject(),savedUi=savedRoot.value("ui").toObject();if(savedFilter.value("lowLevelEnabled").toInt()!=1||!qFuzzyCompare(savedFilter.value("lowLevelAlpha").toDouble(),.17)||savedFilter.value("powerLineMode").toInt()!=5||savedFilter.value("medianEnabled").toInt()!=1||savedFilter.value("movingAverageN").toInt()!=37||savedUi.value("forceDisplayAverageSamples").toInt()!=43)fail("production JSON filter fields do not match widgets");if(savedUi.contains("lastTab"))fail("new profile unexpectedly persisted lastTab");QJsonObject root=savedRoot,ui=savedUi;ui["lastTab"]=3;root["ui"]=ui;QSaveFile legacy(profilePath);if(!legacy.open(QIODevice::WriteOnly)||legacy.write(QJsonDocument(root).toJson(QJsonDocument::Indented))<0||!legacy.commit())fail("inject legacy lastTab");
    const QByteArray load=run(executable,{"--hardware","--no-auto-connect","--persistence-load-smoke","--verbose"},config.path());
    for(const char* marker:{"[PROFILE LOAD] lastSelected=production_path_test","[PROFILE LOAD] active=production_path_test","motionDeceleration=812","displayUnit=gf","[UI RESTORE] motionDeceleration widget=812","[UI RESTORE] displayUnit widget=gf","[UI RESTORE] adcFilter IIR=1","alpha=0.170000","notch=5 median=1 avgN=37 displayAvg=43","[PRODUCTION PATH TEST] restart=1 currentTab=0 filters=1"})if(!load.contains(marker))fail("missing production load log",load);
    std::cout<<"Production dao_utm_ui startup/save/restart path passed\n";return 0;
}
