#include "MainWindow.h"
#include "TrendGraphWidget.h"
#include "UtmUiController.h"
#include "ForceUnit.h"

#include <QApplication>
#include <QDateTime>
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace
{
QDoubleSpinBox* number(double value, double minimum=-1000000.0,
    double maximum=1000000.0, int decimals=3)
{
    auto* box = new QDoubleSpinBox;
    box->setRange(minimum, maximum); box->setDecimals(decimals);
    box->setValue(value); box->setKeyboardTracking(false); return box;
}
QSpinBox* integer(int value, int minimum=0, int maximum=100000000)
{
    auto* box = new QSpinBox; box->setRange(minimum, maximum); box->setValue(value);
    return box;
}
QPushButton* button(const QString& text, const char* role=nullptr)
{
    auto* b = new QPushButton(text); b->setMinimumHeight(42);
    if (role) b->setProperty("role", role); return b;
}
QWidget* scrollPage(QWidget* contents)
{
    auto* area = new QScrollArea; area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame); area->setWidget(contents); return area;
}
void setActionState(QPushButton* action,const char* state)
{if(!action||action->property("actionState").toString()==QString::fromLatin1(state))return;action->setProperty("actionState",state);action->style()->unpolish(action);action->style()->polish(action);}
QString powerLineModeName(int mode)
{
    switch(mode){case 1:return "50 Hz";case 2:return "60 Hz";case 3:return "120 Hz";
    case 4:return "50 + 60 Hz";case 5:return "60 + 120 Hz";default:return "OFF";}
}
QString displayForceStateName(int state)
{switch(state){case UTM_DISPLAY_FORCE_VALID:return "VALID";case UTM_DISPLAY_FORCE_STABILIZING:return "DISPLAY AVERAGE STABILIZING";default:return "FORCE INVALID";}}
qint64 monotonicMilliseconds()
{return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
}

MainWindow::MainWindow(UtmUiController* controller, QWidget* parent)
    : QMainWindow(parent), controller_(controller)
{
    setWindowTitle("DAO UTM Local HMI"); resize(1280, 800); setMinimumSize(1024, 680);
    auto* central = new QWidget; auto* root = new QVBoxLayout(central);
    root->setContentsMargins(12, 10, 12, 10); root->setSpacing(8);

    auto* header = new QFrame; header->setObjectName("statusHeader");
    auto* headerLayout = new QVBoxLayout(header);auto* identityRow=new QHBoxLayout;auto* statusRow=new QHBoxLayout;
    auto* brand = new QLabel("DAO UTM"); brand->setObjectName("brand");
    machineState_ = new QLabel("INITIALIZING"); machineState_->setObjectName("machineState");
    ethercatState_ = new QLabel("EtherCAT —"); servoState_ = new QLabel("Servo —");
    safetyState_ = new QLabel("E-STOP —");upperLimitState_=new QLabel("UPPER LIMIT —");lowerLimitState_=new QLabel("LOWER LIMIT —");externalStopState_=new QLabel("EXT STOP —");
    identityRow->addWidget(brand); identityRow->addWidget(machineState_);identityRow->addStretch();
    connectionState_=new QLabel("DISCONNECTED");identityRow->addWidget(connectionState_);headerLayout->addLayout(identityRow);
    for(auto* chip:{ethercatState_,servoState_,safetyState_,upperLimitState_,lowerLimitState_,externalStopState_}){chip->setObjectName("statusChip");chip->setMinimumWidth(105);chip->setAlignment(Qt::AlignCenter);statusRow->addWidget(chip);}
    statusRow->addStretch();
    auto* connectButton=button("CONNECT","primary");connectButton->setObjectName("ConnectButton");auto* disconnectButton=button("DISCONNECT");disconnectButton->setObjectName("DisconnectButton");
    connect(connectButton,&QPushButton::clicked,this,[this]{if(adapterBox_&&adapterBox_->currentIndex()>=0){QString adapter=adapterBox_->currentData().toString();if(adapter.isEmpty())adapter=adapterBox_->currentText().section(" — ",0,0);controller_->profile().adapterName=adapter;}controller_->connectProfile();});
    connect(disconnectButton,&QPushButton::clicked,controller_,&UtmUiController::disconnectEngine);
    connectButton->setMinimumWidth(105);disconnectButton->setMinimumWidth(115);statusRow->addWidget(connectButton);statusRow->addWidget(disconnectButton);
    auto* stop = button("■  STOP", "danger");stop->setObjectName("HeaderStopButton"); stop->setMinimumWidth(135);
    auto* ack = button("ACK / RESET", "warning");ack->setObjectName("AcknowledgeResetButton");
    connect(stop, &QPushButton::clicked, this, [this]{ controller_->requestStop(); });
    connect(ack, &QPushButton::clicked, this, [this]{ controller_->acknowledgeStop(); });
    ack->setMinimumWidth(115);statusRow->addWidget(stop); statusRow->addWidget(ack);headerLayout->addLayout(statusRow); root->addWidget(header);

    tabs_ = new QTabWidget;
    tabs_->addTab(buildDashboard(), "Main"); tabs_->addTab(buildControl(), "Manual");
    tabs_->addTab(buildSequence(), "Sequence");tabs_->addTab(buildCalibration(),"Calibration");
    tabs_->addTab(buildDiagnostics(), "Diagnostics");tabs_->addTab(buildSetup(), "Setup");
    root->addWidget(tabs_, 1);
    commandMessage_ = new QLabel(controller_->isOffline()
        ? "OFFLINE MODE — no EtherCAT commands are issued" : "Connected to UTM runtime");
    commandMessage_->setObjectName("messageBar"); root->addWidget(commandMessage_);
    setCentralWidget(central); applyStyle();

    connect(controller_, &UtmUiController::runtimeUpdated, this, &MainWindow::refresh);
    connect(controller_, &UtmUiController::commandFailed, this, &MainWindow::showCommandFailure);
    connect(controller_,&UtmUiController::profileStatus,this,[this](const QString& m){commandMessage_->setText(m);});
    connect(controller_,&UtmUiController::connectionChanged,this,[this]{noiseSamplesN_.clear();if(graph_)graph_->clear();if(calibrationGraph_)calibrationGraph_->clear();});
    connect(tabs_,&QTabWidget::currentChanged,this,[this](int){if(jogHeld_)safeJogStop("TabChanged");});
    tabs_->setCurrentIndex(0);
    qApp->installEventFilter(this); refresh();
}

void MainWindow::runOfflineCalibrationSelfTest()
{
    if(!controller_->isOffline())return;
    const auto& profile=controller_->profile();if(std::llround(profile.motionAccelerationMmPerSec2*profile.jog.config.servoUnitsPerMm)!=profile.jog.config.acceleration||std::llround(profile.motionDecelerationMmPerSec2*profile.jog.config.servoUnitsPerMm)!=profile.jog.config.deceleration)qFatal("Offline Machine Motion Dynamics conversion test failed");
    displayForceUnit_->setCurrentText("kgf");
    refresh();const QString stableDisplay=forceValue_->text();
    const QVariant untouchedEncoderZero=encoderZeroAction_->property("actionState");forceZeroAction_->click();if(encoderZeroAction_->property("actionState")!=untouchedEncoderZero)qFatal("Button isolation failed: Force Zero changed Extensometer Zero");
    refresh();
    if(controller_->displayForceRuntime().state!=UTM_DISPLAY_FORCE_STABILIZING||forceValue_->text()!=stableDisplay)
        qFatal("Offline display lifecycle test failed: Main fell back during stabilization");
    QTimer::singleShot(300,this,[this]{
        if(controller_->displayForceRuntime().state!=UTM_DISPLAY_FORCE_VALID)qFatal("Offline display lifecycle test failed: average did not become valid");
        QWidget* main=tabs_->widget(0);const QPoint jogCenter=mainJogUp_->mapTo(main,mainJogUp_->rect().center());auto* connectButton=findChild<QPushButton*>("ConnectButton");auto* disconnectButton=findChild<QPushButton*>("DisconnectButton");bool clipped=false;for(auto* chip:{ethercatState_,servoState_,safetyState_,upperLimitState_,lowerLimitState_,externalStopState_})clipped=clipped||chip->width()<chip->sizeHint().width();if(width()!=1280||height()!=800||!main->rect().contains(jogCenter)||!connectButton||!disconnectButton||connectButton->width()<connectButton->sizeHint().width()||disconnectButton->width()<disconnectButton->sizeHint().width()||clipped)qFatal("Offline 1280x800 layout/header clipping test failed");
        mainJogSpeed_->setValue(5.0);if(std::abs(jogSpeed_->value()-5.0)>1e-9)qFatal("Offline jog speed synchronization failed");if(encoderCurrentValue_->text().contains("UNAVAILABLE")||!encoderScaleValue_->text().contains("mm/count"))qFatal("Offline extensometer value presentation failed");
        controller_->resetOfflineJogStopCount();QMetaObject::invokeMethod(mainJogUp_,"pressed");refresh();if(mainJogUp_->property("actionState").toString()!="requested"||jogUp_->property("actionState").toString()=="requested"||mainJogDown_->property("actionState").toString()=="requested")qFatal("Offline Jog interaction isolation failed");
        moveAbsoluteButton_->click();refresh();if(moveAbsoluteButton_->property("actionState").toString()!="requested"||moveIncrementalButton_->property("actionState").toString()=="requested"||moveVelocityButton_->property("actionState").toString()=="requested")qFatal("Offline motion button isolation failed");
        QTimer::singleShot(500,this,[this]{refresh();mainJogUp_->update();mainJogDown_->update();forceZeroAction_->update();if(controller_->offlineJogStopCount()!=0)qFatal("Offline Jog hold failed: repaint/runtime update issued StopJog");});
        QTimer::singleShot(1400,this,[this]{refresh();if(controller_->offlineJogStopCount()!=0)qFatal("Offline Jog hold failed: StopJog before release");if(mainJogUp_->property("actionState").toString()!="active"||jogUp_->property("actionState").toString()!="active")qFatal("Offline shared Jog Runtime active feedback failed");QMetaObject::invokeMethod(mainJogUp_,"released");if(controller_->offlineJogStopCount()!=1)qFatal("Offline Jog release failed: expected exactly one StopJog");const QVariant forceCalibrationState=forceCalibrateAction_->property("actionState");encoderCalibrateAction_->click();if(forceCalibrateAction_->property("actionState")!=forceCalibrationState)qFatal("Button isolation failed: Extensometer Calibration changed Force Calibration");forceCalibrateAction_->click();controller_->configureAdcFilters(controller_->profile().adcFilter);controller_->configureDisplayForceAverage(controller_->profile().forceDisplayAverageSamples);});
    });
}

QWidget* MainWindow::card(const QString& title, QLabel*& value, QLabel*& detail,
    QPushButton*& zeroAction)
{
    auto* frame = new QFrame; frame->setObjectName("measureCard");
    auto* layout = new QVBoxLayout(frame); auto* heading = new QLabel(title);
    heading->setObjectName("cardTitle"); value = new QLabel("—"); value->setObjectName("bigValue");
    detail = new QLabel("—"); detail->setObjectName("cardDetail"); detail->setWordWrap(true);
    zeroAction=button("ZERO");zeroAction->setProperty("role","zero");
    layout->addWidget(heading); layout->addWidget(value); layout->addWidget(detail);layout->addWidget(zeroAction);
    return frame;
}

QWidget* MainWindow::buildDashboard()
{
    auto* page = new QWidget; auto* layout = new QVBoxLayout(page);
    auto* cards = new QHBoxLayout;
    cards->addWidget(card("FORCE", forceValue_, forceDetail_,mainForceZero_));
    cards->addWidget(card("DISPLACEMENT", positionValue_, positionDetail_,mainPositionZero_));
    cards->addWidget(card("EXTENSOMETER", encoderValue_, encoderDetail_,mainEncoderZero_));
    mainForceZero_->setObjectName("MainForceZero");mainPositionZero_->setObjectName("MainDisplacementZero");mainEncoderZero_->setObjectName("MainExtensometerZero");
    zeroControls_<<mainForceZero_<<mainPositionZero_<<mainEncoderZero_;
    connect(mainForceZero_,&QPushButton::clicked,this,[this]{forceOperationRequested_=1;forceOperationObservedActive_=false;forceOperationRequestedMs_=QDateTime::currentMSecsSinceEpoch();setActionState(mainForceZero_,"requested");if(!controller_->zeroForce())setActionState(mainForceZero_,"failed");});
    connect(mainPositionZero_,&QPushButton::clicked,this,[this]{setActionState(mainPositionZero_,"requested");if(!controller_->setPositionZero())setActionState(mainPositionZero_,"failed");});
    connect(mainEncoderZero_,&QPushButton::clicked,this,[this]{setActionState(mainEncoderZero_,"requested");if(!controller_->zeroEncoder())setActionState(mainEncoderZero_,"failed");});
    layout->addLayout(cards);
    auto* summary = new QHBoxLayout;
    motionSummary_ = new QLabel; motionSummary_->setObjectName("summaryPanel");
    sequenceSummary_ = new QLabel; sequenceSummary_->setObjectName("summaryPanel");
    calibrationSummary_ = new QLabel; calibrationSummary_->setObjectName("summaryPanel");
    summary->addWidget(motionSummary_); summary->addWidget(sequenceSummary_); summary->addWidget(calibrationSummary_);
    layout->addLayout(summary);
    auto* mainBody=new QHBoxLayout;graph_ = new TrendGraphWidget;mainBody->addWidget(graph_,3);
    auto* mainJog=new QGroupBox("JOG CONTROL — HOLD TO RUN");auto* mj=new QGridLayout(mainJog);mainJogUp_=button("JOG UP","primary");mainJogUp_->setObjectName("MainJogUp");mainJogDown_=button("JOG DOWN","primary");mainJogDown_->setObjectName("MainJogDown");mainJogSpeed_=number(controller_->profile().jog.config.physicalJogSpeedMmPerMin,.001,10000);mainJogSpeed_->setSuffix(" mm/min");auto* mainStop=button("MOTION STOP","danger");mainStop->setObjectName("MainMotionStop");mj->addWidget(new QLabel("Jog Speed"),0,0);mj->addWidget(mainJogSpeed_,0,1);mj->addWidget(mainJogUp_,1,0,1,2);mj->addWidget(mainJogDown_,2,0,1,2);mj->addWidget(mainStop,3,0,1,2);mainBody->addWidget(mainJog,1);layout->addLayout(mainBody,1);
    for(auto* b:{mainJogUp_,mainJogDown_}){b->installEventFilter(this);b->setFocusPolicy(Qt::StrongFocus);}connect(mainJogUp_,&QPushButton::pressed,this,[this]{jogHoldTimer_.restart();jogHeld_=controller_->startJog(UTM_DIRECTION_UP,mainJogSpeed_->value());jogRequestedDirection_=jogHeld_?UTM_DIRECTION_UP:UTM_DIRECTION_NONE;jogPressOwner_=jogHeld_?mainJogUp_:nullptr;setActionState(mainJogUp_,jogHeld_?"requested":"inhibited");});connect(mainJogDown_,&QPushButton::pressed,this,[this]{jogHoldTimer_.restart();jogHeld_=controller_->startJog(UTM_DIRECTION_DOWN,mainJogSpeed_->value());jogRequestedDirection_=jogHeld_?UTM_DIRECTION_DOWN:UTM_DIRECTION_NONE;jogPressOwner_=jogHeld_?mainJogDown_:nullptr;setActionState(mainJogDown_,jogHeld_?"requested":"inhibited");});connect(mainJogUp_,&QPushButton::released,this,[this]{if(jogPressOwner_==mainJogUp_)safeJogStop("MouseRelease");});connect(mainJogDown_,&QPushButton::released,this,[this]{if(jogPressOwner_==mainJogDown_)safeJogStop("MouseRelease");});connect(mainStop,&QPushButton::clicked,this,[this]{safeJogStop("ExplicitStopButton");controller_->stopMotion();});
    return page;
}

QWidget* MainWindow::buildControl()
{
    auto* contents = new QWidget; auto* grid = new QGridLayout(contents);
    auto* jog = new QGroupBox("Local Jog — hold to run"); auto* jl = new QGridLayout(jog);
    jogSpeed_ = number(1.0, 0.001, 10000.0); jogSpeed_->setSuffix(" mm/min");
    jogUp_ = button("JOG UP", "primary");jogUp_->setObjectName("ManualJogUp"); jogDown_ = button("JOG DOWN", "primary");jogDown_->setObjectName("ManualJogDown");
    auto* jogStop = button("JOG STOP", "danger");jogStop->setObjectName("ManualJogStop");
    jl->addWidget(new QLabel("Speed"),0,0); jl->addWidget(jogSpeed_,0,1);
    jl->addWidget(jogUp_,1,0,1,2); jl->addWidget(jogDown_,2,0,1,2); jl->addWidget(jogStop,3,0,1,2);
    for (auto* b : {jogUp_, jogDown_}) { b->installEventFilter(this); b->setFocusPolicy(Qt::StrongFocus); }
    connect(jogUp_, &QPushButton::pressed, this, [this]{jogHoldTimer_.restart();jogHeld_=controller_->startJog(UTM_DIRECTION_UP,jogSpeed_->value());jogRequestedDirection_=jogHeld_?UTM_DIRECTION_UP:UTM_DIRECTION_NONE;jogPressOwner_=jogHeld_?jogUp_:nullptr;setActionState(jogUp_,jogHeld_?"requested":"inhibited");});
    connect(jogDown_, &QPushButton::pressed, this, [this]{jogHoldTimer_.restart();jogHeld_=controller_->startJog(UTM_DIRECTION_DOWN,jogSpeed_->value());jogRequestedDirection_=jogHeld_?UTM_DIRECTION_DOWN:UTM_DIRECTION_NONE;jogPressOwner_=jogHeld_?jogDown_:nullptr;setActionState(jogDown_,jogHeld_?"requested":"inhibited");});
    connect(jogUp_, &QPushButton::released, this,[this]{if(jogPressOwner_==jogUp_)safeJogStop("MouseRelease");});
    connect(jogDown_, &QPushButton::released, this,[this]{if(jogPressOwner_==jogDown_)safeJogStop("MouseRelease");});
    connect(jogStop,&QPushButton::clicked,this,[this]{safeJogStop("ExplicitStopButton");});
    connect(mainJogSpeed_,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double v){QSignalBlocker lock(jogSpeed_);jogSpeed_->setValue(v);});
    connect(jogSpeed_,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double v){QSignalBlocker lock(mainJogSpeed_);mainJogSpeed_->setValue(v);});
    grid->addWidget(jog,0,0);

    auto* pos = new QGroupBox("Position / Velocity Motion"); auto* pl = new QGridLayout(pos);
    auto* absTarget=number(0); auto* incDistance=number(1); auto* speed=number(2,0.001,10000);
    auto* timeout=number(30,0.1,3600);
    auto* direction=new QComboBox; direction->addItem("Tension / Up",UTM_DIRECTION_UP);
    direction->addItem("Compression / Down",UTM_DIRECTION_DOWN);
    int row=0; pl->addWidget(new QLabel("Absolute target mm"),row,0);pl->addWidget(absTarget,row++,1);
    pl->addWidget(new QLabel("Incremental distance mm"),row,0);pl->addWidget(incDistance,row++,1);
    pl->addWidget(new QLabel("Speed mm/min"),row,0);pl->addWidget(speed,row++,1);
    pl->addWidget(new QLabel("Machine acceleration / deceleration"),row,0);manualDynamics_=new QLabel;pl->addWidget(manualDynamics_,row++,1);
    pl->addWidget(new QLabel("Timeout sec"),row,0);pl->addWidget(timeout,row++,1);
    pl->addWidget(new QLabel("Velocity direction"),row,0);pl->addWidget(direction,row++,1);
    moveAbsoluteButton_=button("MOVE ABSOLUTE","primary");moveAbsoluteButton_->setObjectName("MoveAbsoluteButton");moveIncrementalButton_=button("MOVE INCREMENTAL","primary");moveIncrementalButton_->setObjectName("MoveIncrementalButton");
    moveVelocityButton_=button("MOVE VELOCITY","primary");moveVelocityButton_->setObjectName("MoveVelocityButton");auto* motionStop=button("MOTION STOP","danger");motionStop->setObjectName("ManualMotionStop");
    executionControls_ << moveAbsoluteButton_ << moveIncrementalButton_ << moveVelocityButton_;
    pl->addWidget(moveAbsoluteButton_,row,0);pl->addWidget(moveIncrementalButton_,row++,1);pl->addWidget(moveVelocityButton_,row,0);pl->addWidget(motionStop,row++,1);manualMotionStatus_=new QLabel;manualMotionStatus_->setObjectName("summaryPanel");pl->addWidget(manualMotionStatus_,row++,0,1,2);
    connect(moveAbsoluteButton_,&QPushButton::clicked,this,[=,this]{setActionState(moveAbsoluteButton_,"requested");requestedManualMotionType_=UTM_MOTION_ABSOLUTE;const auto& d=controller_->profile().jog.config;if(!controller_->moveAbsolute(absTarget->value(),speed->value(),d.acceleration,d.deceleration,timeout->value()*1000)){setActionState(moveAbsoluteButton_,"failed");requestedManualMotionType_=UTM_MOTION_NONE;}});
    connect(moveIncrementalButton_,&QPushButton::clicked,this,[=,this]{setActionState(moveIncrementalButton_,"requested");requestedManualMotionType_=UTM_MOTION_INCREMENTAL;const auto& d=controller_->profile().jog.config;if(!controller_->moveIncremental(incDistance->value(),speed->value(),d.acceleration,d.deceleration,timeout->value()*1000)){setActionState(moveIncrementalButton_,"failed");requestedManualMotionType_=UTM_MOTION_NONE;}});
    connect(moveVelocityButton_,&QPushButton::clicked,this,[=,this]{setActionState(moveVelocityButton_,"requested");requestedManualMotionType_=UTM_MOTION_VELOCITY;const auto& d=controller_->profile().jog.config;if(!controller_->moveVelocity(direction->currentData().toInt(),speed->value(),d.acceleration,d.deceleration)){setActionState(moveVelocityButton_,"failed");requestedManualMotionType_=UTM_MOTION_NONE;}});
    connect(motionStop,&QPushButton::clicked,controller_,&UtmUiController::stopMotion); grid->addWidget(pos,0,1);

    auto* zero = new QGroupBox("Coordinate / Sensor Zero"); auto* zl=new QGridLayout(zero);
    auto* setZero=button("SET POSITION ZERO");auto* clearZero=button("CLEAR POSITION ZERO");
    auto* forceZero=button("ZERO FORCE");auto* encoderZero=button("ZERO ENCODER");
    zl->addWidget(setZero,0,0);zl->addWidget(clearZero,0,1);zl->addWidget(forceZero,1,0);zl->addWidget(encoderZero,1,1);
    connect(setZero,&QPushButton::clicked,controller_,&UtmUiController::setPositionZero);
    connect(clearZero,&QPushButton::clicked,controller_,&UtmUiController::clearPositionZero);
    connect(forceZero,&QPushButton::clicked,controller_,&UtmUiController::zeroForce);
    connect(encoderZero,&QPushButton::clicked,this,[this]{controller_->zeroEncoder();}); grid->addWidget(zero,1,0);

    auto* force = new QGroupBox("Force Motion"); auto* fl=new QGridLayout(force);
    auto* fdir=new QComboBox;fdir->addItem("Tension",UTM_DIRECTION_UP);fdir->addItem("Compression",UTM_DIRECTION_DOWN);
    auto* target=number(2,0.001,100000);auto* fspeed=number(2,0.001,10000);auto* travel=number(10,0.001,10000);
    auto* ftimeout=number(30,0.1,3600);auto* hold=number(2,0.01,3600);auto* tolerance=number(0.1,0.0001,10000,4);
    forceWarning_=new QLabel;forceWarning_->setObjectName("warningText");
    int fr=0;for(auto pair:{QPair<QString,QWidget*>("Direction",fdir),{"Target force N",target},{"Approach speed mm/min",fspeed},{"Max travel mm",travel},{"Timeout sec",ftimeout},{"Hold time sec",hold},{"Tolerance N",tolerance}}){fl->addWidget(new QLabel(pair.first),fr,0);fl->addWidget(pair.second,fr++,1);}
    moveToForceButton_=button("MOVE TO FORCE","primary");holdForceButton_=button("HOLD FORCE","primary");
    fl->addWidget(moveToForceButton_,fr,0);fl->addWidget(holdForceButton_,fr++,1);fl->addWidget(forceWarning_,fr,0,1,2);
    connect(moveToForceButton_,&QPushButton::clicked,this,[=,this]{controller_->moveToForce(fdir->currentData().toInt(),target->value(),fspeed->value(),travel->value(),ftimeout->value(),1000,1000);});
    connect(holdForceButton_,&QPushButton::clicked,this,[=,this]{controller_->holdForce(fdir->currentData().toInt(),target->value(),hold->value(),tolerance->value(),travel->value(),ftimeout->value());});
    grid->addWidget(force,1,1); return scrollPage(contents);
}

QWidget* MainWindow::buildSequence()
{
    auto* page=new QWidget;auto* layout=new QVBoxLayout(page);auto* tools=new QHBoxLayout;
    auto* fresh=button("New");auto* add=button("Add Step");auto* del=button("Delete");auto* up=button("Move Up");auto* down=button("Move Down");
    auto* validate=button("Validate","warning");validate->setObjectName("SequenceValidate");auto* commit=button("Commit","primary");commit->setObjectName("SequenceCommit");auto* start=button("Start","primary");start->setObjectName("SequenceStart");auto* stop=button("Stop","danger");stop->setObjectName("SequenceStop");
    for(auto* b:{fresh,add,del,up,down,validate,commit,start,stop})tools->addWidget(b);layout->addLayout(tools);
    auto* split=new QSplitter;sequenceTable_=new QTableWidget(0,8);sequenceTable_->setHorizontalHeaderLabels({"Step","Type","Value / Target","Speed","Direction","Time","Stop Condition","Status"});
    sequenceTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);sequenceTable_->setSelectionBehavior(QAbstractItemView::SelectRows);split->addWidget(sequenceTable_);
    auto* editor=new QGroupBox("Selected Step Parameters");auto* form=new QFormLayout(editor);
    stepType_=new QComboBox;for(int i=UTM_SEQUENCE_STEP_ZERO_FORCE;i<=UTM_SEQUENCE_STEP_END;++i)stepType_->addItem(stepTypeName(i),i);
    stepValue_=number(0);stepSpeed_=number(2,0,10000);stepTime_=number(1,0,3600);stepDirection_=new QComboBox;stepDirection_->addItem("Up / Tension",UTM_DIRECTION_UP);stepDirection_->addItem("Down / Compression",UTM_DIRECTION_DOWN);
    stepIoBit_=integer(0,0,7);stepLoopCount_=integer(1,1,1000000);stepStopType_=new QComboBox;stepStopType_->addItems({"None","Timeout","Max Force","Max Travel","Break"});stepStopValue_=number(1,0,1000000);
    form->addRow("Type",stepType_);form->addRow("Value / Target",stepValue_);form->addRow("Speed mm/min",stepSpeed_);form->addRow("Time sec",stepTime_);form->addRow("Direction",stepDirection_);form->addRow("Stop condition",stepStopType_);form->addRow("Stop value",stepStopValue_);form->addRow("IO bit",stepIoBit_);form->addRow("Loop count",stepLoopCount_);
    auto* apply=button("Apply Parameters","primary");form->addRow(apply);split->addWidget(editor);split->setStretchFactor(0,4);split->setStretchFactor(1,1);layout->addWidget(split,1);
    validationMessage_=new QLabel("Editing sequence");layout->addWidget(validationMessage_);
    connect(add,&QPushButton::clicked,this,[this]{addSequenceStep(stepType_->currentData().toInt());});
    connect(fresh,&QPushButton::clicked,this,[this]{sequenceTable_->setRowCount(0);addSequenceStep(UTM_SEQUENCE_STEP_END);});
    connect(del,&QPushButton::clicked,this,[this]{if(sequenceTable_->currentRow()>=0)sequenceTable_->removeRow(sequenceTable_->currentRow());reindexSequence();});
    connect(up,&QPushButton::clicked,this,[this]{int r=sequenceTable_->currentRow();if(r>0){for(int c=0;c<sequenceTable_->columnCount();++c){auto* a=sequenceTable_->takeItem(r,c);auto* b=sequenceTable_->takeItem(r-1,c);sequenceTable_->setItem(r-1,c,a);sequenceTable_->setItem(r,c,b);}sequenceTable_->selectRow(r-1);reindexSequence();}});
    connect(down,&QPushButton::clicked,this,[this]{int r=sequenceTable_->currentRow();if(r>=0&&r+1<sequenceTable_->rowCount()){for(int c=0;c<sequenceTable_->columnCount();++c){auto* a=sequenceTable_->takeItem(r,c);auto* b=sequenceTable_->takeItem(r+1,c);sequenceTable_->setItem(r+1,c,a);sequenceTable_->setItem(r,c,b);}sequenceTable_->selectRow(r+1);reindexSequence();}});
    connect(sequenceTable_,&QTableWidget::itemSelectionChanged,this,&MainWindow::updateStepEditor);
    connect(apply,&QPushButton::clicked,this,[this]{int r=sequenceTable_->currentRow();if(r<0)return;sequenceTable_->item(r,1)->setText(stepType_->currentText());sequenceTable_->item(r,1)->setData(Qt::UserRole,stepType_->currentData());sequenceTable_->item(r,2)->setText(QString::number(stepValue_->value()));sequenceTable_->item(r,3)->setText(QString::number(stepSpeed_->value()));sequenceTable_->item(r,4)->setText(stepDirection_->currentText());sequenceTable_->item(r,4)->setData(Qt::UserRole,stepDirection_->currentData());sequenceTable_->item(r,5)->setText(QString::number(stepTime_->value()));sequenceTable_->item(r,6)->setText(QString("%1 %2 • IO%3 • Loop %4").arg(stepStopType_->currentText()).arg(stepStopValue_->value()).arg(stepIoBit_->value()).arg(stepLoopCount_->value()));sequenceTable_->item(r,6)->setData(Qt::UserRole,stepIoBit_->value());sequenceTable_->item(r,6)->setData(Qt::UserRole+2,stepStopType_->currentIndex());sequenceTable_->item(r,6)->setData(Qt::UserRole+3,stepStopValue_->value());sequenceTable_->item(r,7)->setData(Qt::UserRole+1,stepLoopCount_->value());});
    connect(validate,&QPushButton::clicked,this,[this]{auto d=sequenceFromTable();if(controller_->loadSequence(d)&&controller_->validateSequence())validationMessage_->setText("✓ Sequence validation passed");});
    executionControls_ << start;
    connect(commit,&QPushButton::clicked,controller_,&UtmUiController::commitSequence);connect(start,&QPushButton::clicked,controller_,&UtmUiController::startSequence);connect(stop,&QPushButton::clicked,controller_,&UtmUiController::stopSequence);
    addSequenceStep(UTM_SEQUENCE_STEP_ZERO_POSITION);addSequenceStep(UTM_SEQUENCE_STEP_WAIT_TIME);addSequenceStep(UTM_SEQUENCE_STEP_END);return page;
}

QWidget* MainWindow::buildDiagnostics()
{
    auto* page=new QWidget;auto* layout=new QVBoxLayout(page);diagnosticText_=new QLabel;diagnosticText_->setTextInteractionFlags(Qt::TextSelectableByMouse);diagnosticText_->setAlignment(Qt::AlignTop);diagnosticText_->setObjectName("diagnostic");layout->addWidget(diagnosticText_,1);return page;
}

QWidget* MainWindow::buildSetup()
{
    auto* contents=new QWidget;auto* layout=new QGridLayout(contents);
    auto* connection=new QGroupBox("Connection Setup");auto* c=new QFormLayout(connection);
    adapterBox_=new QComboBox;adapterBox_->setEditable(true);auto* scan=button("REFRESH ADAPTERS");
    auto* adapterRow=new QWidget;auto* ar=new QHBoxLayout(adapterRow);ar->setContentsMargins(0,0,0,0);ar->addWidget(adapterBox_,1);ar->addWidget(scan);c->addRow("EtherCAT adapter",adapterRow);
    servoIndex_=integer(0,0,31);adcIndex_=integer(0,0,31);ioIndex_=integer(0,0,31);encoderIndex_=integer(0,0,31);
    encoderRequired_=new QCheckBox("Required for startup");autoServoOn_=new QCheckBox("Automatic Servo ON");autoServoOn_->setChecked(controller_->profile().startup.autoServoOn!=0);
    connect(autoServoOn_,&QCheckBox::toggled,this,[this](bool enabled){if(!loadingProfile_)controller_->profile().startup.autoServoOn=enabled?1:0;});
    c->addRow("Logical Servo index",servoIndex_);c->addRow("Logical ADC index",adcIndex_);c->addRow("Logical IO index",ioIndex_);c->addRow("Logical Encoder index",encoderIndex_);c->addRow("Encoder policy",encoderRequired_);c->addRow("Startup policy",autoServoOn_);
    auto* connectNow=button("CONNECT / START LIFECYCLE","primary");auto* retry=button("RETRY STARTUP","warning");auto* disconnect=button("DISCONNECT / SHUTDOWN","danger");c->addRow(connectNow);c->addRow(retry);c->addRow(disconnect);layout->addWidget(connection,0,0);
    auto* motionSetup=new QGroupBox("Motion / Jog Setup");auto* m=new QFormLayout(motionSetup);
    unitsPerMm_=number(1000,0.001,1e9);jogMin_=number(.1,.001,1e6);jogMax_=number(1000,.001,1e6);jogDefault_=number(1,.001,1e6);motionAcceleration_=number(controller_->profile().motionAccelerationMmPerSec2,.001,1e6,3);motionDeceleration_=number(controller_->profile().motionDecelerationMmPerSec2,.001,1e6,3);motionAcceleration_->setSuffix(" mm/s²");motionDeceleration_->setSuffix(" mm/s²");
    servoDirection_=new QComboBox;servoDirection_->addItem("Positive = Up",1);servoDirection_->addItem("Positive = Down",-1);
    forceDirection_=new QComboBox;forceDirection_->addItem("Normal loadcell polarity",1);forceDirection_->addItem("Inverted loadcell polarity",-1);
    m->addRow("Servo units/mm",unitsPerMm_);m->addRow("Servo direction",servoDirection_);m->addRow("Force direction",forceDirection_);m->addRow("Jog minimum mm/min",jogMin_);m->addRow("Jog maximum mm/min",jogMax_);m->addRow("Default jog mm/min",jogDefault_);m->addRow("Jog acceleration",motionAcceleration_);m->addRow("Jog deceleration",motionDeceleration_);layout->addWidget(motionSetup,0,1);
    auto* force=new QGroupBox("Force Controller Tuning");auto* f=new QFormLayout(force);
    forceApproach_=number(2,0.001,10000);forceMedium_=number(.5,0.001,10000);forceFine_=number(.1,0.001,10000);forceReverse_=number(.05,0.001,10000);forceMediumError_=number(5,.0001,100000);forceFineError_=number(1,.0001,100000);forceTolerance_=number(.1,.0001,100000);forceMaxTravel_=number(10,.001,100000);forceTimeout_=number(60,.1,36000);
    for(auto pair:{QPair<QString,QWidget*>("Approach speed",forceApproach_),{"Medium speed",forceMedium_},{"Fine speed",forceFine_},{"Reverse speed",forceReverse_},{"Medium error N",forceMediumError_},{"Fine error N",forceFineError_},{"Tolerance N",forceTolerance_},{"Max travel mm",forceMaxTravel_},{"Timeout sec",forceTimeout_}})f->addRow(pair.first,pair.second);
    auto* apply=button("APPLY FORCE CONFIG","warning");f->addRow(apply);connect(apply,&QPushButton::clicked,this,[this]{if(syncSetupToProfile())controller_->configureForce(controller_->profile().force);});layout->addWidget(force,1,0);
    auto* protection=new QGroupBox("Force Display / Machine Protection");auto* p=new QFormLayout(protection);overloadLimit_=number(controller_->profile().protection.maxAllowedForceN,.001,1000000);p->addRow("Absolute overload N",overloadLimit_);
    displayForceUnit_=new QComboBox;displayForceUnit_->addItems({"N","kgf","gf"});displayForceUnit_->setCurrentText(controller_->profile().displayForceUnit);forceDisplayDecimals_=integer(controller_->profile().forceDisplayDecimals,0,6);
    loadcellCapacity_=number(controller_->profile().loadcellCapacityN,.000001,1000000000,6);loadcellCapacityUnit_=new QComboBox;loadcellCapacityUnit_->addItems({"N","kgf","gf"});loadcellCapacity_->setProperty("unit","N");auto* capacityRow=new QWidget;auto* capacityLayout=new QHBoxLayout(capacityRow);capacityLayout->setContentsMargins(0,0,0,0);capacityLayout->addWidget(loadcellCapacity_,1);capacityLayout->addWidget(loadcellCapacityUnit_);
    p->addRow("Force display unit",displayForceUnit_);p->addRow("Force display decimals",forceDisplayDecimals_);p->addRow("Loadcell capacity",capacityRow);auto* protect=button("APPLY FORCE DISPLAY / LIMIT","danger");p->addRow(protect);p->addRow(new QLabel("Capacity is stored in N and does not change the existing overload limit."));
    connect(protect,&QPushButton::clicked,this,[this]{if(syncSetupToProfile())controller_->configureProtection(controller_->profile().protection);});
    connect(loadcellCapacityUnit_,&QComboBox::currentTextChanged,this,[this](const QString& unit){if(loadingProfile_)return;const ForceUnit oldUnit=ForceUnits::fromText(loadcellCapacity_->property("unit").toString());const double capacityN=ForceUnits::toNewtons(loadcellCapacity_->value(),oldUnit);QSignalBlocker lock(loadcellCapacity_);loadcellCapacity_->setValue(ForceUnits::fromNewtons(capacityN,ForceUnits::fromText(unit)));loadcellCapacity_->setProperty("unit",unit);controller_->profile().loadcellCapacityDisplayUnit=unit;});
    connect(loadcellCapacity_,&QDoubleSpinBox::valueChanged,this,[this](double value){if(!loadingProfile_)controller_->profile().loadcellCapacityN=ForceUnits::toNewtons(value,ForceUnits::fromText(loadcellCapacityUnit_->currentText()));});
    connect(displayForceUnit_,&QComboBox::currentTextChanged,this,[this](const QString& unit){if(!loadingProfile_)controller_->profile().displayForceUnit=unit;if(graph_)graph_->setForceUnitLabel(unit);if(calibrationGraph_)calibrationGraph_->setForceUnitLabel(unit);noiseSamplesN_.clear();});connect(forceDisplayDecimals_,&QSpinBox::valueChanged,this,[this](int decimals){if(!loadingProfile_)controller_->profile().forceDisplayDecimals=decimals;});layout->addWidget(protection,1,1);
    auto* io=new QGroupBox("IO Mapping");auto* im=new QFormLayout(io);ioEmergency_=integer(0,0,7);ioUpper_=integer(1,0,7);ioLower_=integer(2,0,7);ioJogUp_=integer(3,0,7);ioJogDown_=integer(4,0,7);ioGo_=integer(5,0,7);ioStop_=integer(6,0,7);ioActiveLow_=integer(0,0,255);im->addRow("Emergency DI",ioEmergency_);im->addRow("Upper / Lower DI",new QLabel("Configured below"));im->addRow("Upper",ioUpper_);im->addRow("Lower",ioLower_);im->addRow("Jog Up",ioJogUp_);im->addRow("Jog Down",ioJogDown_);im->addRow("External GO",ioGo_);im->addRow("External STOP",ioStop_);im->addRow("Active-low mask",ioActiveLow_);layout->addWidget(io,2,0);
    auto* profiles=new QGroupBox("Machine Profiles — JSON");auto* pf=new QFormLayout(profiles);profileBox_=new QComboBox;profileBox_->setEditable(true);profileBox_->addItems(controller_->profileNames());auto* save=button("SAVE PROFILE");save->setObjectName("SaveProfileButton");auto* load=button("LOAD PROFILE","warning");load->setObjectName("LoadProfileButton");pf->addRow("Profile",profileBox_);pf->addRow(save);pf->addRow(load);auto* policy=new QLabel("Automatically applied: adapter, motion/jog/force settings, overload, direction, calibration scales.\nNever restored: Force Zero, Position Zero, Encoder Zero, motion/sequence/fault state.");policy->setWordWrap(true);pf->addRow(policy);layout->addWidget(profiles,2,1);
    connect(scan,&QPushButton::clicked,this,[this]{const auto values=controller_->refreshAdapters();adapterBox_->clear();for(const auto& value:values)adapterBox_->addItem(value,value.section(" — ",0,0));});
    connect(connectNow,&QPushButton::clicked,this,[this]{if(syncSetupToProfile())controller_->connectProfile();});
    connect(retry,&QPushButton::clicked,controller_,&UtmUiController::retryStartup);
    connect(disconnect,&QPushButton::clicked,controller_,&UtmUiController::disconnectEngine);
    connect(save,&QPushButton::clicked,this,[this]{if(!syncSetupToProfile())return;if(controller_->saveProfile(profileBox_->currentText())){QSignalBlocker lock(profileBox_);profileBox_->clear();profileBox_->addItems(controller_->profileNames());profileBox_->setCurrentText(controller_->profile().name);}});
    connect(load,&QPushButton::clicked,this,[this]{if(controller_->loadProfile(profileBox_->currentText()))restoreSetupFromProfile();});
    const QList<QPair<QWidget*,QString>> persistenceMappings={
        {adapterBox_,"adapterName"},{servoIndex_,"devices.servo"},{adcIndex_,"devices.adc"},{ioIndex_,"devices.io"},{encoderIndex_,"devices.encoder"},{encoderRequired_,"devices.encoderRequired"},{autoServoOn_,"devices.autoServoOn"},
        {unitsPerMm_,"motion.servoUnitsPerMm"},{servoDirection_,"motion.servoDirectionSign"},{forceDirection_,"forceControl.forceDirectionSign"},{jogMin_,"motion.jogMin"},{jogMax_,"motion.jogMax"},{jogDefault_,"motion.jogDefault"},{motionAcceleration_,"motion.accelerationMmPerSec2"},{motionDeceleration_,"motion.decelerationMmPerSec2"},
        {forceApproach_,"forceControl.approachSpeed"},{forceMedium_,"forceControl.mediumSpeed"},{forceFine_,"forceControl.fineSpeed"},{forceReverse_,"forceControl.reverseSpeed"},{forceMediumError_,"forceControl.mediumErrorN"},{forceFineError_,"forceControl.fineErrorN"},{forceTolerance_,"forceControl.toleranceN"},{forceMaxTravel_,"forceControl.maxTravelMm"},{forceTimeout_,"forceControl.timeoutSec"},
        {overloadLimit_,"protection.maxAllowedForceN"},{displayForceUnit_,"force.displayUnit"},{forceDisplayDecimals_,"force.displayDecimals"},{loadcellCapacity_,"force.loadcellCapacityN"},{loadcellCapacityUnit_,"force.loadcellCapacityDisplayUnit"},
        {ioEmergency_,"ioMapping.emergency"},{ioUpper_,"ioMapping.upperLimit"},{ioLower_,"ioMapping.lowerLimit"},{ioJogUp_,"ioMapping.jogUp"},{ioJogDown_,"ioMapping.jogDown"},{ioGo_,"ioMapping.externalGo"},{ioStop_,"ioMapping.externalStop"},{ioActiveLow_,"ioMapping.activeLowMask"},{profileBox_,"profile.name"}};
    for(const auto& mapping:persistenceMappings){mapping.first->setObjectName("Setup_"+mapping.second);mapping.first->setProperty("profileJsonField",mapping.second);}
    restoreSetupFromProfile();
    return scrollPage(contents);
}

bool MainWindow::syncSetupToProfile()
{
    qInfo().noquote()<<QString("[FILTER UI SAVE] iirEnabled=%1 alpha=%2 notchIndex=%3 notchText=%4 median=%5 movingAverageN=%6 displayAverageSamples=%7 widgets=%8,%9,%10,%11,%12,%13")
        .arg(lowLevelEnabled_->isChecked()?1:0).arg(lowLevelAlpha_->value(),0,'g',17)
        .arg(powerLineMode_->currentIndex()).arg(powerLineMode_->currentText())
        .arg(medianEnabled_->isChecked()?1:0).arg(movingAverageN_->value()).arg(displayAverageSamples_->value())
        .arg(reinterpret_cast<quintptr>(lowLevelEnabled_),0,16).arg(reinterpret_cast<quintptr>(lowLevelAlpha_),0,16)
        .arg(reinterpret_cast<quintptr>(powerLineMode_),0,16).arg(reinterpret_cast<quintptr>(medianEnabled_),0,16)
        .arg(reinterpret_cast<quintptr>(movingAverageN_),0,16).arg(reinterpret_cast<quintptr>(displayAverageSamples_),0,16);
    MachineProfile candidate=controller_->profile();QString adapter=adapterBox_->currentData().toString();if(adapter.isEmpty())adapter=adapterBox_->currentText().section(" — ",0,0);candidate.adapterName=adapter;
    candidate.startup.logicalServoIndex=servoIndex_->value();candidate.startup.logicalAdcIndex=adcIndex_->value();candidate.startup.logicalIoIndex=ioIndex_->value();candidate.startup.logicalEncoderIndex=encoderIndex_->value();candidate.startup.encoderRequired=encoderRequired_->isChecked();candidate.startup.autoServoOn=autoServoOn_->isChecked()?1:0;
    candidate.jog.config.servoUnitsPerMm=unitsPerMm_->value();candidate.jog.config.servoDirectionSign=servoDirection_->currentData().toInt();candidate.jog.minJogSpeedMmPerMin=jogMin_->value();candidate.jog.maxJogSpeedMmPerMin=jogMax_->value();candidate.jog.config.physicalJogSpeedMmPerMin=jogDefault_->value();candidate.motionAccelerationMmPerSec2=motionAcceleration_->value();candidate.motionDecelerationMmPerSec2=motionDeceleration_->value();
    candidate.force.forceDirectionSign=forceDirection_->currentData().toInt();candidate.force.approachSpeedMmPerMin=forceApproach_->value();candidate.force.mediumSpeedMmPerMin=forceMedium_->value();candidate.force.fineSpeedMmPerMin=forceFine_->value();candidate.force.reverseSpeedMmPerMin=forceReverse_->value();candidate.force.mediumErrorN=forceMediumError_->value();candidate.force.fineErrorN=forceFineError_->value();candidate.force.toleranceN=forceTolerance_->value();candidate.force.maxTravelMm=forceMaxTravel_->value();candidate.force.timeoutSec=forceTimeout_->value();
    candidate.protection.maxAllowedForceN=overloadLimit_->value();candidate.displayForceUnit=displayForceUnit_->currentText();candidate.forceDisplayDecimals=forceDisplayDecimals_->value();candidate.loadcellCapacityDisplayUnit=loadcellCapacityUnit_->currentText();candidate.loadcellCapacityN=ForceUnits::toNewtons(loadcellCapacity_->value(),ForceUnits::fromText(candidate.loadcellCapacityDisplayUnit));
    candidate.forceDisplayAverageSamples=displayAverageSamples_->value();candidate.adcFilter.lowLevelFilterEnabled=lowLevelEnabled_->isChecked();candidate.adcFilter.lowLevelFilterAlpha=lowLevelAlpha_->value();candidate.adcFilter.powerLineFilterMode=powerLineMode_->currentData().toInt();candidate.adcFilter.medianFilterEnabled=medianEnabled_->isChecked();candidate.adcFilter.movingAverageSampleCount=movingAverageN_->value();
    qInfo().noquote()<<QString("[FILTER CANDIDATE] iirEnabled=%1 alpha=%2 powerLineMode=%3 median=%4 movingAverageN=%5 displayAverageSamples=%6")
        .arg(candidate.adcFilter.lowLevelFilterEnabled).arg(candidate.adcFilter.lowLevelFilterAlpha,0,'g',17)
        .arg(candidate.adcFilter.powerLineFilterMode).arg(candidate.adcFilter.medianFilterEnabled)
        .arg(candidate.adcFilter.movingAverageSampleCount).arg(candidate.forceDisplayAverageSamples);
    auto& map=candidate.startup.ioMapping;map.emergencyBit=ioEmergency_->value();map.upperLimitBit=ioUpper_->value();map.lowerLimitBit=ioLower_->value();map.jogUpBit=ioJogUp_->value();map.jogDownBit=ioJogDown_->value();map.externalGoBit=ioGo_->value();map.externalStopBit=ioStop_->value();map.activeLowMask=ioActiveLow_->value();
    QString error;if(!candidate.applyMotionDynamics(error)||!MachineProfileStore::validate(candidate,error)){showCommandFailure("Setup validation",error);return false;}
    controller_->profile()=candidate;
    const auto& committed=controller_->profile();
    qInfo().noquote()<<QString("[FILTER PROFILE COMMIT] iirEnabled=%1 alpha=%2 powerLineMode=%3 median=%4 movingAverageN=%5 displayAverageSamples=%6")
        .arg(committed.adcFilter.lowLevelFilterEnabled).arg(committed.adcFilter.lowLevelFilterAlpha,0,'g',17)
        .arg(committed.adcFilter.powerLineFilterMode).arg(committed.adcFilter.medianFilterEnabled)
        .arg(committed.adcFilter.movingAverageSampleCount).arg(committed.forceDisplayAverageSamples);
    return true;
}

void MainWindow::restoreSetupFromProfile()
{
    loadingProfile_=true;const auto& p=controller_->profile();
    adapterBox_->setEditText(p.adapterName);servoIndex_->setValue(p.startup.logicalServoIndex);adcIndex_->setValue(p.startup.logicalAdcIndex);ioIndex_->setValue(p.startup.logicalIoIndex);encoderIndex_->setValue(p.startup.logicalEncoderIndex);encoderRequired_->setChecked(p.startup.encoderRequired!=0);autoServoOn_->setChecked(p.startup.autoServoOn!=0);
    unitsPerMm_->setValue(p.jog.config.servoUnitsPerMm);servoDirection_->setCurrentIndex(servoDirection_->findData(p.jog.config.servoDirectionSign));forceDirection_->setCurrentIndex(forceDirection_->findData(p.force.forceDirectionSign));jogMin_->setValue(p.jog.minJogSpeedMmPerMin);jogMax_->setValue(p.jog.maxJogSpeedMmPerMin);jogDefault_->setValue(p.jog.config.physicalJogSpeedMmPerMin);motionAcceleration_->setValue(p.motionAccelerationMmPerSec2);motionDeceleration_->setValue(p.motionDecelerationMmPerSec2);
    forceApproach_->setValue(p.force.approachSpeedMmPerMin);forceMedium_->setValue(p.force.mediumSpeedMmPerMin);forceFine_->setValue(p.force.fineSpeedMmPerMin);forceReverse_->setValue(p.force.reverseSpeedMmPerMin);forceMediumError_->setValue(p.force.mediumErrorN);forceFineError_->setValue(p.force.fineErrorN);forceTolerance_->setValue(p.force.toleranceN);forceMaxTravel_->setValue(p.force.maxTravelMm);forceTimeout_->setValue(p.force.timeoutSec);
    overloadLimit_->setValue(p.protection.maxAllowedForceN);displayForceUnit_->setCurrentText(p.displayForceUnit);forceDisplayDecimals_->setValue(p.forceDisplayDecimals);loadcellCapacityUnit_->setCurrentText(p.loadcellCapacityDisplayUnit);loadcellCapacity_->setProperty("unit",p.loadcellCapacityDisplayUnit);loadcellCapacity_->setValue(ForceUnits::fromNewtons(p.loadcellCapacityN,ForceUnits::fromText(p.loadcellCapacityDisplayUnit)));
    ioEmergency_->setValue(p.startup.ioMapping.emergencyBit);ioUpper_->setValue(p.startup.ioMapping.upperLimitBit);ioLower_->setValue(p.startup.ioMapping.lowerLimitBit);ioJogUp_->setValue(p.startup.ioMapping.jogUpBit);ioJogDown_->setValue(p.startup.ioMapping.jogDownBit);ioGo_->setValue(p.startup.ioMapping.externalGoBit);ioStop_->setValue(p.startup.ioMapping.externalStopBit);ioActiveLow_->setValue(p.startup.ioMapping.activeLowMask);
    restoreCalibrationFilterFromProfile();
    if(calibrationReferenceUnit_){const double referenceN=ForceUnits::toNewtons(referenceForce_->value(),ForceUnits::fromText(calibrationReferenceUnit_->currentText()));calibrationReferenceUnit_->setCurrentText(p.calibrationReferenceUnit);referenceForce_->setValue(ForceUnits::fromNewtons(referenceN,ForceUnits::fromText(p.calibrationReferenceUnit)));referenceForce_->setSuffix(" "+p.calibrationReferenceUnit);}if(jogSpeed_)jogSpeed_->setValue(p.jog.config.physicalJogSpeedMmPerMin);if(mainJogSpeed_)mainJogSpeed_->setValue(p.jog.config.physicalJogSpeedMmPerMin);profileBox_->setCurrentText(p.name);loadingProfile_=false;
    qInfo().noquote()<<QString("[UI RESTORE] motionDeceleration widget=%1").arg(motionDeceleration_->value(),0,'g',17);
    qInfo().noquote()<<QString("[UI RESTORE] displayUnit widget=%1").arg(displayForceUnit_->currentText());
}

void MainWindow::restoreCalibrationFilterFromProfile()
{
    const auto& p=controller_->profile();lowLevelEnabled_->setChecked(p.adcFilter.lowLevelFilterEnabled!=0);lowLevelAlpha_->setValue(p.adcFilter.lowLevelFilterAlpha);powerLineMode_->setCurrentIndex(powerLineMode_->findData(p.adcFilter.powerLineFilterMode));medianEnabled_->setChecked(p.adcFilter.medianFilterEnabled!=0);movingAverageN_->setValue(p.adcFilter.movingAverageSampleCount);displayAverageSamples_->setValue(p.forceDisplayAverageSamples);
    qInfo().noquote()<<QString("[UI RESTORE] adcFilter IIR=%1 alpha=%2 notch=%3 median=%4 avgN=%5 displayAvg=%6").arg(lowLevelEnabled_->isChecked()).arg(lowLevelAlpha_->value(),0,'g',17).arg(powerLineMode_->currentData().toInt()).arg(medianEnabled_->isChecked()).arg(movingAverageN_->value()).arg(displayAverageSamples_->value());
    qInfo().noquote()<<QString("[FILTER UI POINTERS] restore widgets=%1,%2,%3,%4,%5,%6")
        .arg(reinterpret_cast<quintptr>(lowLevelEnabled_),0,16).arg(reinterpret_cast<quintptr>(lowLevelAlpha_),0,16)
        .arg(reinterpret_cast<quintptr>(powerLineMode_),0,16).arg(reinterpret_cast<quintptr>(medianEnabled_),0,16)
        .arg(reinterpret_cast<quintptr>(movingAverageN_),0,16).arg(reinterpret_cast<quintptr>(displayAverageSamples_),0,16);
}

QWidget* MainWindow::buildCalibration()
{
    auto* contents=new QWidget;auto* layout=new QGridLayout(contents);
    auto* live=new QGroupBox("Engineer Diagnostics — Force Signal Pipeline");auto* ll=new QVBoxLayout(live);calibrationDetail_=new QLabel;calibrationDetail_->setObjectName("summaryPanel");calibrationDetail_->setTextInteractionFlags(Qt::TextSelectableByMouse);ll->addWidget(calibrationDetail_);layout->addWidget(live,2,0,1,2);
    calibrationActions_=new QWidget;auto* actions=new QGridLayout(calibrationActions_);
    auto* forceBox=new QGroupBox("FORCE CALIBRATION STATUS");auto* ff=new QFormLayout(forceBox);forceOperatorStatus_=new QLabel;forceOperatorStatus_->setObjectName("operatorStatus");calibrationReferenceUnit_=new QComboBox;calibrationReferenceUnit_->addItems({"N","kgf","gf"});calibrationReferenceUnit_->setCurrentText(controller_->profile().calibrationReferenceUnit);referenceForce_=number(10,0.000001,1000000000,6);referenceForce_->setSuffix(" "+calibrationReferenceUnit_->currentText());forceZeroAction_=button("ZERO FORCE");forceZeroAction_->setObjectName("CalibrationForceZero");forceZeroAction_->setProperty("role","zero");forceCalibrateAction_=button("CALIBRATE FORCE","calibrate");forceCalibrateAction_->setObjectName("CalibrationForceCalibrate");forceZeroStatus_=new QLabel("ZERO REQUIRED");forceCalibrationStatus_=new QLabel("CALIBRATION REQUIRED");calibrationResult_=new QLabel("Last operation: none");ff->addRow(forceOperatorStatus_);ff->addRow("Reference unit",calibrationReferenceUnit_);ff->addRow("Reference force",referenceForce_);ff->addRow(forceZeroAction_,forceZeroStatus_);ff->addRow(forceCalibrateAction_,forceCalibrationStatus_);ff->addRow(calibrationResult_);actions->addWidget(forceBox,0,0);
    auto* filterBox=new QGroupBox("Existing Basic ADC Filters");auto* filterForm=new QFormLayout(filterBox);const auto& savedFilter=controller_->profile().adcFilter;lowLevelEnabled_=new QCheckBox("Enabled");lowLevelEnabled_->setChecked(savedFilter.lowLevelFilterEnabled);lowLevelAlpha_=number(savedFilter.lowLevelFilterAlpha,.001,1,4);powerLineMode_=new QComboBox;powerLineMode_->addItem("OFF",0);powerLineMode_->addItem("50 Hz",1);powerLineMode_->addItem("60 Hz",2);powerLineMode_->addItem("120 Hz",3);powerLineMode_->addItem("50 + 60 Hz",4);powerLineMode_->addItem("60 + 120 Hz",5);powerLineMode_->setCurrentIndex(powerLineMode_->findData(savedFilter.powerLineFilterMode));medianEnabled_=new QCheckBox("3-sample median enabled");medianEnabled_->setChecked(savedFilter.medianFilterEnabled);movingAverageN_=integer(savedFilter.movingAverageSampleCount,1,64);displayAverageSamples_=integer(controller_->profile().forceDisplayAverageSamples,20,50);auto* applyFilter=button("APPLY FILTERS","primary");applyFilter->setObjectName("CalibrationApplyFilters");filterForm->addRow("Low-level IIR",lowLevelEnabled_);filterForm->addRow("IIR alpha",lowLevelAlpha_);filterForm->addRow("Power-line notch",powerLineMode_);filterForm->addRow("Median",medianEnabled_);filterForm->addRow("Moving average N",movingAverageN_);filterForm->addRow("Main display average (2ms samples)",displayAverageSamples_);filterForm->addRow(applyFilter);actions->addWidget(filterBox,0,1);
    for(const auto& mapping:QList<QPair<QWidget*,QString>>{{calibrationReferenceUnit_,"ui.calibrationReferenceUnit"},{lowLevelEnabled_,"adcFilter.lowLevelEnabled"},{lowLevelAlpha_,"adcFilter.lowLevelAlpha"},{powerLineMode_,"adcFilter.powerLineMode"},{medianEnabled_,"adcFilter.medianEnabled"},{movingAverageN_,"adcFilter.movingAverageN"},{displayAverageSamples_,"ui.forceDisplayAverageSamples"}}){mapping.first->setObjectName("Setup_"+mapping.second);mapping.first->setProperty("profileJsonField",mapping.second);}
    auto* encoderBox=new QGroupBox("EXTENSOMETER CALIBRATION");auto* ef=new QFormLayout(encoderBox);encoderCurrentValue_=new QLabel("— mm");encoderCurrentValue_->setObjectName("calibrationCurrentValue");encoderRawValue_=new QLabel("—");encoderSignedValue_=new QLabel("—");encoderScaleValue_=new QLabel("— mm/count");encoderReference_=number(10,-1000000,1000000,6);encoderReference_->setSuffix(" mm");encoderZeroAction_=button("ZERO EXTENSOMETER");encoderZeroAction_->setObjectName("CalibrationExtensometerZero");encoderZeroAction_->setProperty("role","zero");encoderCalibrateAction_=button("CALIBRATE EXTENSOMETER","calibrate");encoderCalibrateAction_->setObjectName("CalibrationExtensometerCalibrate");encoderZeroStatus_=new QLabel("REQUIRED");encoderCalibrationStatus_=new QLabel("REQUIRED");ef->addRow("Current Displacement",encoderCurrentValue_);ef->addRow("Raw Count",encoderRawValue_);ef->addRow("Signed Count",encoderSignedValue_);ef->addRow("Current Scale",encoderScaleValue_);ef->addRow("Zero Status",encoderZeroStatus_);ef->addRow("Calibration Status",encoderCalibrationStatus_);ef->addRow("Reference Displacement",encoderReference_);ef->addRow(encoderZeroAction_);ef->addRow(encoderCalibrateAction_);actions->addWidget(encoderBox,1,0);
    auto* positionBox=new QGroupBox("DISPLACEMENT ZERO");auto* pf=new QFormLayout(positionBox);positionOperatorStatus_=new QLabel;positionOperatorStatus_->setObjectName("operatorStatus");auto* setPosition=button("ZERO DISPLACEMENT");setPosition->setProperty("role","zero");auto* clearPosition=button("CLEAR ZERO");pf->addRow(positionOperatorStatus_);pf->addRow(setPosition);pf->addRow(clearPosition);actions->addWidget(positionBox,1,1);layout->addWidget(calibrationActions_,0,0,1,2);
    auto* noiseBox=new QGroupBox("Engineer Noise Diagnostic — latest ~1 second UI samples");auto* nl=new QVBoxLayout(noiseBox);noiseDetail_=new QLabel("Waiting for samples");nl->addWidget(noiseDetail_);layout->addWidget(noiseBox,3,0);
    calibrationGraph_=new TrendGraphWidget;calibrationGraph_->setMinimumHeight(180);layout->addWidget(calibrationGraph_,3,1);
    auto* note=new QLabel("Engine/control/sequence values remain Newton. N/kgf/gf conversion is presentation/input-boundary only. Zero offsets are never persisted.");note->setWordWrap(true);layout->addWidget(note,4,0,1,2);
    connect(calibrationReferenceUnit_,&QComboBox::currentTextChanged,this,[this](const QString& unit){if(loadingProfile_)return;const ForceUnit oldUnit=ForceUnits::fromText(controller_->profile().calibrationReferenceUnit);const double referenceN=ForceUnits::toNewtons(referenceForce_->value(),oldUnit);controller_->profile().calibrationReferenceUnit=unit;referenceForce_->setValue(ForceUnits::fromNewtons(referenceN,ForceUnits::fromText(unit)));referenceForce_->setSuffix(" "+unit);});
    connect(forceZeroAction_,&QPushButton::clicked,this,[this]{forceOperationRequested_=1;forceOperationObservedActive_=false;forceOperationRequestedMs_=QDateTime::currentMSecsSinceEpoch();setActionState(forceZeroAction_,"requested");forceZeroStatus_->setText("ZERO REQUESTED");if(!controller_->zeroForce()){forceOperationRequested_=0;setActionState(forceZeroAction_,"failed");forceZeroStatus_->setText("ZERO FAILED");calibrationResult_->setText("Last operation: Force Zero FAILED");}});connect(forceCalibrateAction_,&QPushButton::clicked,this,[this]{forceOperationRequested_=2;forceOperationObservedActive_=false;forceOperationRequestedMs_=QDateTime::currentMSecsSinceEpoch();setActionState(forceCalibrateAction_,"requested");forceCalibrationStatus_->setText("CALIBRATION REQUESTED");if(!controller_->calibrateForce(referenceForce_->value(),ForceUnits::fromText(calibrationReferenceUnit_->currentText()))){forceOperationRequested_=0;setActionState(forceCalibrateAction_,"failed");forceCalibrationStatus_->setText("CALIBRATION FAILED");calibrationResult_->setText("Last operation: Calibration FAILED");}});
    connect(applyFilter,&QPushButton::clicked,this,[this]{UtmAdcFilterConfig config{};config.lowLevelFilterEnabled=lowLevelEnabled_->isChecked();config.lowLevelFilterAlpha=lowLevelAlpha_->value();config.powerLineFilterMode=powerLineMode_->currentData().toInt();config.medianFilterEnabled=medianEnabled_->isChecked();config.movingAverageSampleCount=movingAverageN_->value();controller_->applyAdcFilterConfiguration(config,displayAverageSamples_->value());});
    connect(encoderZeroAction_,&QPushButton::clicked,this,[this]{setActionState(encoderZeroAction_,"requested");encoderZeroStatus_->setText("REQUESTED");if(!controller_->zeroEncoder()){setActionState(encoderZeroAction_,"failed");encoderZeroStatus_->setText("FAILED");}});connect(encoderCalibrateAction_,&QPushButton::clicked,this,[this]{encoderCalibrationRequested_=true;encoderCalibrationBaselineScale_=controller_->calibration().encoderCalibrationScale;encoderCalibrationReferenceMm_=encoderReference_->value();encoderCalibrationRequestedMs_=QDateTime::currentMSecsSinceEpoch();setActionState(encoderCalibrateAction_,"requested");encoderCalibrationStatus_->setText("REQUESTED");if(!controller_->calibrateEncoder(encoderCalibrationReferenceMm_)){encoderCalibrationRequested_=false;setActionState(encoderCalibrateAction_,"failed");encoderCalibrationStatus_->setText("FAILED");}});connect(setPosition,&QPushButton::clicked,controller_,&UtmUiController::setPositionZero);connect(clearPosition,&QPushButton::clicked,controller_,&UtmUiController::clearPositionZero);return scrollPage(contents);
}

void MainWindow::refresh()
{
    const auto& r=controller_->runtime();const auto& base=r.runtime.runtime.runtime.runtime.runtime;
    const auto& startup=r.runtime.runtime.runtime.startup;const auto& motion=r.runtime.runtime.motion;
    const auto& force=r.runtime.forceMotion;const auto& seq=r.sequence;const auto& cal=controller_->calibration();
    const QString selectedAdapter=QString::fromUtf8(startup.selectedAdapterName,static_cast<int>(strnlen(startup.selectedAdapterName,sizeof(startup.selectedAdapterName))));
    machineState_->setText(machineStateName(base.machineState));
    machineState_->setProperty("alarm",base.machineState==UTM_MACHINE_FAULT||base.machineState==UTM_MACHINE_EMERGENCY||base.machineState==UTM_MACHINE_STOPPED);
    machineState_->style()->unpolish(machineState_);machineState_->style()->polish(machineState_);
    ethercatState_->setText(QString("EtherCAT %1").arg(base.input.communicationValid?"● ONLINE":"✕ OFFLINE"));
    servoState_->setText(QString("Servo %1%2").arg(base.input.servoOn?"● ON":"○ OFF",base.input.servoFault?" / FAULT":""));
    safetyState_->setText(QString("E-STOP %1").arg(base.input.emergency?"ACTIVE":"OK"));upperLimitState_->setText(QString("UPPER LIMIT %1").arg(base.input.upperLimit?"ON":"OFF"));lowerLimitState_->setText(QString("LOWER LIMIT %1").arg(base.input.lowerLimit?"ON":"OFF"));externalStopState_->setText(QString("EXT STOP %1").arg(base.input.externalStop?"ACTIVE":"OK"));
    connectionState_->setText(controller_->isOffline()?"OFFLINE DEMO":(controller_->isConnected()?QString("%1 • %2 slaves").arg(startupPhaseName(startup.startupPhase)).arg(startup.slaveCount):"DISCONNECTED"));
    const QString forceUnit=controller_->profile().displayForceUnit;const ForceUnit selectedForceUnit=ForceUnits::fromText(forceUnit);auto displayForce=[selectedForceUnit](double valueN){return ForceUnits::fromNewtons(valueN,selectedForceUnit);};
    const auto& displayRuntime=controller_->displayForceRuntime();
    const double mainForceN=controller_->displayForceN();
    forceValue_->setText(QString::number(displayForce(mainForceN),'f',controller_->profile().forceDisplayDecimals)+" "+forceUnit);
    forceValue_->setProperty("forceState",displayRuntime.state);forceValue_->style()->unpolish(forceValue_);forceValue_->style()->polish(forceValue_);
    forceDetail_->setText(QString("%1  •  Samples %2/%3  •  ForceValid %4").arg(displayForceStateName(displayRuntime.state)).arg(displayRuntime.validSampleCount).arg(displayRuntime.configuredSampleCount).arg(base.input.forceValid?"YES":"NO"));
    positionValue_->setText(QString::number(motion.testPositionMm,'f',3)+" mm");
    positionDetail_->setText(QString("Machine %1 mm  •  Zero offset %2 mm  •  Zero %3").arg(motion.machinePositionMm,0,'f',3).arg(motion.testZeroOffsetMm,0,'f',3).arg(motion.positionZeroValid?"VALID":"NOT SET"));
    encoderValue_->setText(base.input.encoderPresent?QString::number(base.input.encoderPosition,'f',3)+" mm":"NOT INSTALLED");
    encoderValue_->setProperty("unavailable",!base.input.encoderPresent);encoderValue_->style()->unpolish(encoderValue_);encoderValue_->style()->polish(encoderValue_);
    encoderDetail_->setText(QString("Present %1  •  Valid %2  •  Signed count %3").arg(base.input.encoderPresent).arg(base.input.encoderValid).arg(base.input.encoderSignedCount));
    encoderCurrentValue_->setText(cal.encoderPresent?QString::number(cal.encoderEngineeringPosition,'f',6)+" mm":"UNAVAILABLE");
    encoderRawValue_->setText(cal.encoderPresent?QString::number(cal.encoderRawCount):"—");encoderSignedValue_->setText(cal.encoderPresent?QString::number(cal.encoderSignedCount):"—");encoderScaleValue_->setText(cal.encoderPresent?QString::number(cal.encoderCalibrationScale,'g',10)+" mm/count":"—");
    positionOperatorStatus_->setText(QString("Machine Position      %1 mm\nTest Displacement     %2 mm\nZero Offset           %3 mm\nZero Status           %4").arg(motion.machinePositionMm,0,'f',6).arg(motion.testPositionMm,0,'f',6).arg(motion.testZeroOffsetMm,0,'f',6).arg(motion.positionZeroValid?"VALID":"REQUIRED"));
    motionSummary_->setText(QString("MOTION\n%1 / %2\nDirection %3  •  Speed %4 mm/min\nTarget %5 mm")
        .arg(motionTypeName(motion.motionType),motionStateName(motion.motionState)).arg(motion.motionDirection).arg(motion.commandSpeedMmPerMin,0,'f',3).arg(motion.targetPositionMm,0,'f',3));
    sequenceSummary_->setText(QString("SEQUENCE\n%1\nStep %2 / %3  •  %4\nLoop %5/%6  •  %7")
        .arg(sequenceStateName(seq.sequenceState)).arg(seq.currentStepIndex).arg(seq.stepCount).arg(stepTypeName(seq.currentStepType)).arg(seq.currentLoopIteration).arg(seq.currentLoopCount).arg(completionName(seq.lastStepCompletionReason)));
    calibrationSummary_->setText(QString("RESULT\nCompletion %1\nStop %2 • latch %3\nForce target %4 %5")
        .arg(completionName(seq.lastStepCompletionReason)).arg(stopReasonName(base.stop.primaryReason)).arg(base.stop.latched)
        .arg(displayForce(force.targetForceN),0,'f',3).arg(forceUnit));
    forceOperatorStatus_->setText(QString("Current Engineering   %1 %2\nDisplay Average       %3 %2   (%4 x 2 ms)\nZERO                  %5\nCALIBRATION           %6\nCalibration Scale     %7\nApplied Filter        IIR %8 α=%9 • Notch %10 • Median %11 • Avg %12")
        .arg(displayForce(cal.engineeringForceN),0,'f',6).arg(forceUnit).arg(displayForce(controller_->displayForceN()),0,'f',6).arg(controller_->displayForceAverageSamples())
        .arg(cal.forceZeroValid?"VALID":"REQUIRED").arg(cal.forceCalibrationValid?"VALID":"REQUIRED").arg(cal.forceCalibrationScale,0,'g',10)
        .arg(cal.lowLevelFilterEnabled?"ON":"OFF").arg(cal.lowLevelFilterAlpha,0,'g',5).arg(powerLineModeName(cal.powerLineFilterMode)).arg(cal.medianFilterEnabled?"ON":"OFF").arg(cal.movingAverageSampleCount));
    calibrationDetail_->setText(QString("RAW ADC   %1   %2   %3   %4\nLow-level %5   Power-line %6   Zeroed %7   Calibrated %8\nEngineering %9 %10   Valid %11   Zero initialized %12   Calibration initialized %13   Scale %14\nFilters: Low-level %15 α=%16   Notch=%17   Median=%18   Moving average N=%19\nENCODER %20 mm   Present/valid %21/%22   Scale %23\nPOSITION Machine %24 mm   Test %25 mm   Zero valid %26   Offset %27 mm")
        .arg(cal.adcRaw0).arg(cal.adcRaw1).arg(cal.adcRaw2).arg(cal.adcRaw3).arg(cal.lowLevelFiltered,0,'f',3).arg(cal.powerLineFiltered,0,'f',3).arg(cal.zeroedValue,0,'f',3).arg(cal.calibratedValue,0,'f',6)
        .arg(displayForce(cal.engineeringForceN),0,'f',6).arg(forceUnit).arg(cal.forceValid).arg(cal.forceZeroValid).arg(cal.forceCalibrationValid).arg(cal.forceCalibrationScale,0,'g',10)
        .arg(cal.lowLevelFilterEnabled?"ON":"OFF").arg(cal.lowLevelFilterAlpha,0,'g',5).arg(powerLineModeName(cal.powerLineFilterMode)).arg(cal.medianFilterEnabled?"ON":"OFF").arg(cal.movingAverageSampleCount)
        .arg(cal.encoderEngineeringPosition,0,'f',6).arg(cal.encoderPresent).arg(cal.encoderValid).arg(cal.encoderCalibrationScale,0,'g',10)
        .arg(motion.machinePositionMm,0,'f',6).arg(motion.testPositionMm,0,'f',6).arg(cal.positionZeroValid).arg(cal.testZeroOffsetMm,0,'f',6));
    calibrationDetail_->setText(calibrationDetail_->text()+QString("\nENCODER raw/signed %1/%2   Reset state/status %3/%4\nPROFILE apply %5   Stored ADC scale %6   Applied ADC scale %7   Stored filter α/N %8/%9   Applied α/N %10/%11")
        .arg(cal.encoderRawCount).arg(cal.encoderSignedCount).arg(cal.encoderResetState).arg(cal.encoderResetCompletedStatus).arg(controller_->profileApplyState())
        .arg(controller_->profile().adcCalibrationScale,0,'g',10).arg(cal.forceCalibrationScale,0,'g',10)
        .arg(controller_->profile().adcFilter.lowLevelFilterAlpha,0,'g',5).arg(controller_->profile().adcFilter.movingAverageSampleCount)
        .arg(cal.lowLevelFilterAlpha,0,'g',5).arg(cal.movingAverageSampleCount));
    calibrationDetail_->setText(calibrationDetail_->text()+QString("\nDISPLAY state %1   Samples %2/%3   Generation %4   Reset count/reason %5/%6")
        .arg(displayForceStateName(displayRuntime.state)).arg(displayRuntime.validSampleCount).arg(displayRuntime.configuredSampleCount)
        .arg(displayRuntime.generation).arg(displayRuntime.resetCount).arg(displayRuntime.lastResetReason));
    diagnosticText_->setText(QString("SOFTWARE\n  UTM Engine: %1\n  EtherCAT Engine: %2\n  Mode: %3\n\nETHERCAT\n  Adapter: %4 [%5]\n  Slaves: %6\n  Communication: %7\n  Startup: %37 / fault %38\n\nSERVO\n  State: 0x%8\n  Actual position: %9\n  ON / Ready / Fault: %10 / %11 / %12\n\nADC\n  Force: %13 N\n  Force valid: %14\n  Raw 0..3: %39 / %40 / %41 / %42\n  Calibration / zero / scale: %43 / %44 / %45\n\nENCODER\n  Present / Valid: %15 / %16\n  Signed count: %17\n  Engineering: %18\n\nUTM\n  Cycle: %19\n  Cycle time / maximum: %20 / %21 ns\n  Overruns: %22\n  Stop: %23 (%24)\n  Motion: %25 / %26\n  Sequence: %27\n\nIO INPUTS\n  DI0 Emergency: %28\n  DI1 Upper limit: %29\n  DI2 Lower limit: %30\n  DI3 Jog up: %31\n  DI4 Jog down: %32\n  DI5 External GO: %33\n  DI6 External STOP: %34\n  DI7 Reserved: %35\n\nSEQUENCE OUTPUTS\n  DO0..DO7: 0x%36")
        .arg(controller_->utmVersion(),controller_->basicVersion(),controller_->isOffline()?"OFFLINE":"HARDWARE")
        .arg(selectedAdapter).arg(startup.selectedAdapterIndex).arg(startup.slaveCount).arg(base.input.communicationValid)
        .arg(base.input.servoOperationState,0,16).arg(base.input.servoActualPosition).arg(base.input.servoOn).arg(base.input.servoReady).arg(base.input.servoFault)
        .arg(base.input.forceN,0,'f',6).arg(base.input.forceValid).arg(base.input.encoderPresent).arg(base.input.encoderValid).arg(base.input.encoderSignedCount).arg(base.input.encoderPosition,0,'f',6)
        .arg(base.controlCycle).arg(base.lastCycleTimeNs).arg(base.maximumCycleTimeNs).arg(base.cycleOverrunCount).arg(stopReasonName(base.stop.primaryReason)).arg(base.stop.latched)
        .arg(motionTypeName(motion.motionType),motionStateName(motion.motionState),sequenceStateName(seq.sequenceState))
        .arg(base.input.emergency).arg(base.input.upperLimit).arg(base.input.lowerLimit).arg(base.input.jogUp).arg(base.input.jogDown).arg(base.input.externalGo).arg(base.input.externalStop).arg((base.input.rawDigitalInputs>>7)&1).arg(seq.sequenceOwnedOutputs,2,16,QChar('0'))
        .arg(startupPhaseName(startup.startupPhase)).arg(startup.startupFault).arg(cal.adcRaw0).arg(cal.adcRaw1).arg(cal.adcRaw2).arg(cal.adcRaw3).arg(cal.forceCalibrationValid).arg(cal.forceZeroValid).arg(cal.forceCalibrationScale,0,'g',10));
    const bool available=controller_->isOffline()||controller_->isConnected();
    const bool ready=available&&base.machineState==UTM_MACHINE_READY&&base.stop.latched==0&&!seq.sequenceRunning;
    const auto& jogRuntime=r.runtime.runtime.runtime.runtime.jog;
    for(auto* control:executionControls_)control->setEnabled(ready);
    const bool jogInteractionEnabled=ready||jogRuntime.jogActive||jogHeld_;
    jogUp_->setEnabled(jogInteractionEnabled);jogDown_->setEnabled(jogInteractionEnabled);mainJogUp_->setEnabled(jogInteractionEnabled);mainJogDown_->setEnabled(jogInteractionEnabled);
    const auto jogButtonState=[&](QPushButton* button,int direction){if(jogRuntime.jogActive&&jogRuntime.jogDirection==direction)return "active";if(jogHeld_&&jogPressOwner_==button)return "requested";if(!ready)return "inhibited";return "idle";};
    setActionState(mainJogUp_,jogButtonState(mainJogUp_,UTM_DIRECTION_UP));setActionState(mainJogDown_,jogButtonState(mainJogDown_,UTM_DIRECTION_DOWN));setActionState(jogUp_,jogButtonState(jogUp_,UTM_DIRECTION_UP));setActionState(jogDown_,jogButtonState(jogDown_,UTM_DIRECTION_DOWN));
    QPushButton* activeMotionButton=motion.motionType==UTM_MOTION_ABSOLUTE?moveAbsoluteButton_:motion.motionType==UTM_MOTION_INCREMENTAL?moveIncrementalButton_:motion.motionType==UTM_MOTION_VELOCITY?moveVelocityButton_:nullptr;
    if(activeMotionButton&&(motion.motionActive||motion.motionState==UTM_MOTION_STATE_PREPARING||motion.motionState==UTM_MOTION_STATE_COMMAND_SENT)){setActionState(activeMotionButton,"active");requestedManualMotionType_=motion.motionType;}
    else if(requestedManualMotionType_!=UTM_MOTION_NONE){QPushButton* requested=requestedManualMotionType_==UTM_MOTION_ABSOLUTE?moveAbsoluteButton_:requestedManualMotionType_==UTM_MOTION_INCREMENTAL?moveIncrementalButton_:moveVelocityButton_;if(motion.motionState==UTM_MOTION_STATE_FAILED){setActionState(requested,"failed");requestedManualMotionType_=UTM_MOTION_NONE;}else if(motion.motionState==UTM_MOTION_STATE_COMPLETED||motion.motionState==UTM_MOTION_STATE_ABORTED){setActionState(requested,motion.motionState==UTM_MOTION_STATE_COMPLETED?"success":"failed");requestedManualMotionType_=UTM_MOTION_NONE;}else setActionState(requested,"requested");}
    for(auto* b:{moveAbsoluteButton_,moveIncrementalButton_,moveVelocityButton_})if(b!=activeMotionButton&&(requestedManualMotionType_==UTM_MOTION_NONE||b!=(requestedManualMotionType_==UTM_MOTION_ABSOLUTE?moveAbsoluteButton_:requestedManualMotionType_==UTM_MOTION_INCREMENTAL?moveIncrementalButton_:moveVelocityButton_)))setActionState(b,ready?"idle":"inhibited");
    manualMotionStatus_->setText(QString("Machine Position %1 mm   Test Displacement %2 mm\nTarget %3 mm   Incremental %4 mm   Speed %5 mm/min\nMotion %6 / %7   Direction %8   Source %9\nServo Target Position %10 uu   Target Velocity %11 uu/s\nLast Command %12   Complete/Abort %13/%14   Failure %15")
        .arg(motion.machinePositionMm,0,'f',4).arg(motion.testPositionMm,0,'f',4).arg(motion.targetPositionMm,0,'f',4).arg(motion.incrementalDistanceMm,0,'f',4).arg(motion.commandSpeedMmPerMin,0,'f',3)
        .arg(motionTypeName(motion.motionType),motionStateName(motion.motionState)).arg(motion.motionDirection).arg(motion.motionSource).arg(motion.servoTargetPosition).arg(motion.servoTargetVelocity)
        .arg(base.lastAcceptedCommand).arg(motion.motionComplete).arg(motion.motionAborted).arg(motion.motionFailureReason));
    manualDynamics_->setText(QString("%1 / %2 mm/s²  (%3 / %4 UU/s²)").arg(controller_->profile().motionAccelerationMmPerSec2,0,'f',3).arg(controller_->profile().motionDecelerationMmPerSec2,0,'f',3).arg(controller_->profile().jog.config.acceleration).arg(controller_->profile().jog.config.deceleration));
    const bool forceReady=ready&&base.input.forceValid&&(controller_->isOffline()||cal.forceCalibrationValid);moveToForceButton_->setEnabled(forceReady);holdForceButton_->setEnabled(forceReady);
    forceWarning_->setText(forceReady?"Force feedback and calibration valid":"⚠ Force calibration/zero required — force motion disabled");
    const bool calibrationSafe=ready&&!motion.motionActive;
    calibrationActions_->setEnabled(calibrationSafe);
    for(auto* control:zeroControls_)control->setEnabled(calibrationSafe);
    encoderZeroAction_->setEnabled(calibrationSafe&&cal.encoderPresent);encoderCalibrateAction_->setEnabled(calibrationSafe&&cal.encoderPresent);mainEncoderZero_->setEnabled(calibrationSafe&&cal.encoderPresent);
    if(!calibrationSafe){for(auto* b:{forceZeroAction_,forceCalibrateAction_,encoderZeroAction_,encoderCalibrateAction_,mainForceZero_,mainPositionZero_,mainEncoderZero_})setActionState(b,"disabled");}
    else if(forceOperationRequested_==0){setActionState(forceZeroAction_,cal.forceZeroValid?"success":"idle");setActionState(forceCalibrateAction_,cal.forceCalibrationValid?"success":"idle");}
    if(calibrationSafe&&forceOperationRequested_==0)setActionState(mainForceZero_,cal.forceZeroValid?"success":"idle");
    if(calibrationSafe)setActionState(mainPositionZero_,motion.positionZeroValid?"success":"idle");
    forceZeroStatus_->setText(cal.forceZeroValid?"ZERO VALID":"ZERO REQUIRED");forceCalibrationStatus_->setText(cal.forceCalibrationValid?"CALIBRATION VALID":"CALIBRATION REQUIRED");
    if(forceOperationRequested_!=0){auto* action=forceOperationRequested_==1?forceZeroAction_:forceCalibrateAction_;auto* status=forceOperationRequested_==1?forceZeroStatus_:forceCalibrationStatus_;if(cal.forceCaptureActive&&cal.forceCaptureType==forceOperationRequested_){forceOperationObservedActive_=true;setActionState(action,"in_progress");if(forceOperationRequested_==1)setActionState(mainForceZero_,"in_progress");status->setText("IN PROGRESS");}else if(forceOperationObservedActive_||(controller_->isOffline()&&(forceOperationRequested_==1?cal.forceZeroValid:cal.forceCalibrationValid))){const bool success=forceOperationRequested_==1?cal.forceZeroValid:cal.forceCalibrationValid;setActionState(action,success?"success":"failed");if(forceOperationRequested_==1)setActionState(mainForceZero_,success?"success":"failed");status->setText(success?"APPLIED / VALID":"FAILED");calibrationResult_->setText(QString("Last operation: %1 %2 at %3").arg(forceOperationRequested_==1?"Force Zero":"Force Calibration").arg(success?"SUCCESS":"FAILED").arg(QDateTime::currentDateTime().toString(Qt::ISODate)));forceOperationRequested_=0;}else if(QDateTime::currentMSecsSinceEpoch()-forceOperationRequestedMs_>10000){setActionState(action,"failed");if(forceOperationRequested_==1)setActionState(mainForceZero_,"failed");status->setText("FAILED / TIMEOUT");calibrationResult_->setText("Last operation: timeout waiting for Runtime confirmation");forceOperationRequested_=0;}}
    if(cal.encoderResetState==1){setActionState(encoderZeroAction_,"in_progress");setActionState(mainEncoderZero_,"in_progress");encoderZeroStatus_->setText("IN PROGRESS");}else if(cal.encoderResetState==2){setActionState(encoderZeroAction_,"success");setActionState(mainEncoderZero_,"success");encoderZeroStatus_->setText("VALID");}else if(cal.encoderResetState==3){setActionState(encoderZeroAction_,"failed");setActionState(mainEncoderZero_,"failed");encoderZeroStatus_->setText("FAILED");}else{if(calibrationSafe){setActionState(encoderZeroAction_,"idle");setActionState(mainEncoderZero_,"idle");}encoderZeroStatus_->setText("REQUIRED");}
    if(encoderCalibrationRequested_){const bool scaleChanged=std::abs(cal.encoderCalibrationScale-encoderCalibrationBaselineScale_)>std::max(1e-12,std::abs(encoderCalibrationBaselineScale_)*1e-9);const bool referenceApplied=cal.encoderValid&&std::abs(cal.encoderEngineeringPosition-encoderCalibrationReferenceMm_)<=std::max(.001,std::abs(encoderCalibrationReferenceMm_)*.01);if(cal.encoderValid&&std::isfinite(cal.encoderCalibrationScale)&&(scaleChanged||referenceApplied)){encoderCalibrationRequested_=false;setActionState(encoderCalibrateAction_,"success");encoderCalibrationStatus_->setText("VALID");}else if(QDateTime::currentMSecsSinceEpoch()-encoderCalibrationRequestedMs_>10000){encoderCalibrationRequested_=false;setActionState(encoderCalibrateAction_,"failed");encoderCalibrationStatus_->setText("FAILED / NOT APPLIED");showCommandFailure("Extensometer calibration","Runtime did not confirm scale/engineering value application");}else{setActionState(encoderCalibrateAction_,"in_progress");encoderCalibrationStatus_->setText("IN PROGRESS");}}
    else if(cal.encoderValid&&std::isfinite(cal.encoderCalibrationScale)){if(calibrationSafe)setActionState(encoderCalibrateAction_,"success");encoderCalibrationStatus_->setText("VALID");}else{if(calibrationSafe)setActionState(encoderCalibrateAction_,"idle");encoderCalibrationStatus_->setText("REQUIRED");}
    if(seq.validationError!=UTM_SEQUENCE_VALIDATION_NONE)validationMessage_->setText(QString("✕ Validation error %1 at step %2").arg(seq.validationError).arg(seq.validationErrorStepIndex));
    sequenceTable_->setEnabled(!seq.sequenceRunning);
    if(cal.forceValid&&std::isfinite(cal.engineeringForceN))noiseSamplesN_.append(cal.engineeringForceN);while(noiseSamplesN_.size()>25)noiseSamplesN_.removeFirst();
    if(!noiseSamplesN_.isEmpty()){double sum=0,min=noiseSamplesN_.front(),max=min;for(double value:noiseSamplesN_){sum+=value;min=std::min(min,value);max=std::max(max,value);}double mean=sum/noiseSamplesN_.size(),variance=0;for(double value:noiseSamplesN_)variance+=(value-mean)*(value-mean);variance/=noiseSamplesN_.size();noiseDetail_->setText(QString("Mean %1   Min %2   Max %3   Peak-to-peak %4   Std dev %5  %6").arg(displayForce(mean),0,'f',6).arg(displayForce(min),0,'f',6).arg(displayForce(max),0,'f',6).arg(displayForce(max-min),0,'f',6).arg(displayForce(std::sqrt(variance)),0,'f',6).arg(forceUnit));}
    if(graph_){graph_->setForceUnitLabel(forceUnit);if(base.input.forceValid)graph_->append(base.input.forceN,motion.testPositionMm);}if(calibrationGraph_){calibrationGraph_->setForceUnitLabel(forceUnit);if(cal.forceValid)calibrationGraph_->append(cal.engineeringForceN,0);}
    for(int row=0;row<sequenceTable_->rowCount();++row){QString state="Pending";if(seq.sequenceRunning&&row<static_cast<int>(seq.currentStepIndex))state="Completed";else if(seq.sequenceRunning&&row==static_cast<int>(seq.currentStepIndex))state="Running";else if((seq.sequenceFailed||seq.sequenceAborted)&&row==static_cast<int>(seq.currentStepIndex))state=seq.sequenceFailed?"Failed":"Aborted";sequenceTable_->item(row,7)->setText(state);}
}

void MainWindow::showCommandFailure(const QString& operation,const QString& detail)
{commandMessage_->setText("✕ "+operation+": "+detail);commandMessage_->setProperty("error",true);commandMessage_->style()->unpolish(commandMessage_);commandMessage_->style()->polish(commandMessage_);}

void MainWindow::safeJogStop(const char* reason)
{
    if(!jogHeld_)return;
    const QString owner=jogPressOwner_&& !jogPressOwner_->objectName().isEmpty()?jogPressOwner_->objectName():QStringLiteral("UnknownJogButton");
    const qint64 elapsed=jogHoldTimer_.isValid()?jogHoldTimer_.elapsed():-1;
    if(controller_->isVerbose())qInfo().noquote()<<QString("[UI JOG STOP] reason=%1 owner=%2 timestampMs=%3 elapsed=%4ms").arg(QString::fromLatin1(reason),owner).arg(monotonicMilliseconds()).arg(elapsed);
    jogHeld_=false;jogRequestedDirection_=UTM_DIRECTION_NONE;jogPressOwner_=nullptr;controller_->stopJog();
}
bool MainWindow::eventFilter(QObject* watched,QEvent* event)
{
    if(watched==jogPressOwner_&&event->type()==QEvent::Leave&&jogHeld_&&(QApplication::mouseButtons()&Qt::LeftButton)){
        auto* owner=qobject_cast<QPushButton*>(watched);if(owner&&!owner->rect().contains(owner->mapFromGlobal(QCursor::pos())))safeJogStop("MouseLeave");
    }
    if(event->type()==QEvent::ApplicationDeactivate)safeJogStop("ApplicationDeactivate");return QMainWindow::eventFilter(watched,event);
}
void MainWindow::changeEvent(QEvent* event){if(event->type()==QEvent::ActivationChange&&!isActiveWindow())safeJogStop("WindowDeactivate");QMainWindow::changeEvent(event);}
void MainWindow::closeEvent(QCloseEvent* event){safeJogStop("WindowClose");controller_->stopMotion();event->accept();}

void MainWindow::addSequenceStep(int type)
{
    int row=sequenceTable_->rowCount();sequenceTable_->insertRow(row);for(int c=0;c<8;++c)sequenceTable_->setItem(row,c,new QTableWidgetItem);
    sequenceTable_->item(row,1)->setText(stepTypeName(type));sequenceTable_->item(row,1)->setData(Qt::UserRole,type);sequenceTable_->item(row,2)->setText("0");sequenceTable_->item(row,3)->setText("2");sequenceTable_->item(row,4)->setText("Up / Tension");sequenceTable_->item(row,4)->setData(Qt::UserRole,UTM_DIRECTION_UP);sequenceTable_->item(row,5)->setText("1");sequenceTable_->item(row,6)->setText("None • IO0 • Loop 1");sequenceTable_->item(row,6)->setData(Qt::UserRole,0);sequenceTable_->item(row,6)->setData(Qt::UserRole+2,0);sequenceTable_->item(row,6)->setData(Qt::UserRole+3,1.0);sequenceTable_->item(row,7)->setData(Qt::UserRole+1,1);sequenceTable_->item(row,7)->setText("Pending");reindexSequence();sequenceTable_->selectRow(row);
}
void MainWindow::reindexSequence(){for(int i=0;i<sequenceTable_->rowCount();++i)sequenceTable_->item(i,0)->setText(QString::number(i));}
void MainWindow::updateStepEditor(){int r=sequenceTable_->currentRow();if(r<0)return;stepType_->setCurrentIndex(stepType_->findData(sequenceTable_->item(r,1)->data(Qt::UserRole)));stepValue_->setValue(sequenceTable_->item(r,2)->text().toDouble());stepSpeed_->setValue(sequenceTable_->item(r,3)->text().toDouble());stepDirection_->setCurrentIndex(stepDirection_->findData(sequenceTable_->item(r,4)->data(Qt::UserRole)));stepTime_->setValue(sequenceTable_->item(r,5)->text().toDouble());stepStopType_->setCurrentIndex(sequenceTable_->item(r,6)->data(Qt::UserRole+2).toInt());stepStopValue_->setValue(sequenceTable_->item(r,6)->data(Qt::UserRole+3).toDouble());stepIoBit_->setValue(sequenceTable_->item(r,6)->data(Qt::UserRole).toInt());stepLoopCount_->setValue(sequenceTable_->item(r,7)->data(Qt::UserRole+1).toInt());}

UtmSequenceDefinition MainWindow::sequenceFromTable() const
{
    UtmSequenceDefinition d{};d.stepCount=std::min(sequenceTable_->rowCount(),static_cast<int>(UTM_SEQUENCE_MAX_STEPS));
    for(unsigned int i=0;i<d.stepCount;++i){auto& s=d.steps[i];s.stepIndex=i;s.stepType=sequenceTable_->item(i,1)->data(Qt::UserRole).toInt();s.direction=sequenceTable_->item(i,4)->data(Qt::UserRole).toInt();double v=sequenceTable_->item(i,2)->text().toDouble();s.positionMm=v;s.distanceMm=v;s.forceN=v;s.speedMmPerMin=sequenceTable_->item(i,3)->text().toDouble();s.durationSec=sequenceTable_->item(i,5)->text().toDouble();s.holdTimeSec=s.durationSec;s.toleranceN=.1;s.timeoutMs=static_cast<unsigned int>(std::max(1.0,s.durationSec)*1000);s.acceleration=s.deceleration=1000;s.ioBit=sequenceTable_->item(i,6)->data(Qt::UserRole).toUInt();s.ioState=v!=0;s.loopCount=sequenceTable_->item(i,7)->data(Qt::UserRole+1).toUInt();int stopType=sequenceTable_->item(i,6)->data(Qt::UserRole+2).toInt();double stopValue=sequenceTable_->item(i,6)->data(Qt::UserRole+3).toDouble();if(stopType==1){s.stopConditions.enableTimeout=1;s.stopConditions.timeoutSec=stopValue;}else if(stopType==2){s.stopConditions.enableMaxForce=1;s.stopConditions.maxForceN=stopValue;}else if(stopType==3){s.stopConditions.enableMaxTravel=1;s.stopConditions.maxTravelMm=stopValue;}else if(stopType==4){s.stopConditions.enableBreakDetection=1;s.stopConditions.minimumBreakPeakN=stopValue;s.stopConditions.breakDropPercent=80;s.stopConditions.breakConfirmMs=20;}if(s.stepType==UTM_SEQUENCE_STEP_MOVE_VELOCITY&&stopType==0){s.stopConditions.enableTimeout=1;s.stopConditions.timeoutSec=std::max(0.1,s.durationSec);}if((s.stepType==UTM_SEQUENCE_STEP_MOVE_TO_FORCE||s.stepType==UTM_SEQUENCE_STEP_HOLD_FORCE)&&!s.stopConditions.enableMaxTravel){s.stopConditions.enableMaxTravel=1;s.stopConditions.maxTravelMm=10;}}
    return d;
}

QString MainWindow::machineStateName(int v){switch(v){case UTM_MACHINE_READY:return"READY";case UTM_MACHINE_MANUAL:return"MANUAL";case UTM_MACHINE_RUNNING:return"RUNNING";case UTM_MACHINE_STOPPING:return"STOPPING";case UTM_MACHINE_STOPPED:return"STOPPED";case UTM_MACHINE_EMERGENCY:return"EMERGENCY";case UTM_MACHINE_FAULT:return"FAULT";default:return"INITIALIZING";}}
QString MainWindow::startupPhaseName(int v){switch(v){case UTM_STARTUP_CONFIG_LOADED:return"Configuration loaded";case UTM_STARTUP_ADAPTER_SELECTING:return"Selecting EtherCAT adapter";case UTM_STARTUP_ADAPTER_OPENING:return"Opening adapter";case UTM_STARTUP_SLAVE_SCANNING:return"Scanning slaves";case UTM_STARTUP_COMMUNICATION_WAIT:return"Stabilizing communication";case UTM_STARTUP_SERVO_PREPARING:return"Preparing servo";case UTM_STARTUP_SERVO_ON_WAIT:return"Waiting for Servo ON";case UTM_STARTUP_READY_STABILIZING:return"Stabilizing READY";case UTM_STARTUP_COMPLETE:return"Ready";case UTM_STARTUP_FAILED:return"Startup failed";default:return"Initializing";}}
QString MainWindow::motionTypeName(int v){switch(v){case UTM_MOTION_JOG:return"JOG";case UTM_MOTION_ABSOLUTE:return"ABSOLUTE";case UTM_MOTION_INCREMENTAL:return"INCREMENTAL";case UTM_MOTION_VELOCITY:return"VELOCITY";case UTM_MOTION_MOVE_TO_FORCE:return"MOVE TO FORCE";case UTM_MOTION_HOLD_FORCE:return"HOLD FORCE";default:return"NONE";}}
QString MainWindow::motionStateName(int v){switch(v){case UTM_MOTION_STATE_PREPARING:return"PREPARING";case UTM_MOTION_STATE_COMMAND_SENT:return"COMMAND SENT";case UTM_MOTION_STATE_MOVING:return"MOVING";case UTM_MOTION_STATE_STOPPING:return"STOPPING";case UTM_MOTION_STATE_COMPLETED:return"COMPLETED";case UTM_MOTION_STATE_ABORTED:return"ABORTED";case UTM_MOTION_STATE_FAILED:return"FAILED";default:return"IDLE";}}
QString MainWindow::sequenceStateName(int v){switch(v){case UTM_SEQUENCE_STATE_EDITING:return"EDITING";case UTM_SEQUENCE_STATE_VALIDATED:return"VALIDATED";case UTM_SEQUENCE_STATE_COMMITTED:return"COMMITTED";case UTM_SEQUENCE_STATE_RUNNING:return"RUNNING";case UTM_SEQUENCE_STATE_STOPPING:return"STOPPING";case UTM_SEQUENCE_STATE_COMPLETED:return"COMPLETED";case UTM_SEQUENCE_STATE_ABORTED:return"ABORTED";case UTM_SEQUENCE_STATE_FAILED:return"FAILED";default:return"EMPTY";}}
QString MainWindow::stepTypeName(int v){static const char* n[]={"NONE","ZERO FORCE","ZERO POSITION","ZERO ENCODER","MOVE ABSOLUTE","MOVE INCREMENTAL","MOVE VELOCITY","MOVE TO FORCE","HOLD FORCE","WAIT TIME","WAIT INPUT","SET OUTPUT","PULSE OUTPUT","LOOP START","LOOP END","END"};return v>=0&&v<16?n[v]:"UNKNOWN";}
QString MainWindow::completionName(int v){static const char* n[]={"NONE","TARGET POSITION","TARGET FORCE","BREAK DETECTED","MAX FORCE","MAX TRAVEL","TIME COMPLETED","INPUT MATCHED","OUTPUT COMPLETED","ZERO COMPLETED","END"};return v>=0&&v<11?n[v]:"UNKNOWN";}
QString MainWindow::stopReasonName(int v){switch(v){case UTM_STOP_EMERGENCY:return"EMERGENCY";case UTM_STOP_UPPER_LIMIT:return"UPPER LIMIT";case UTM_STOP_LOWER_LIMIT:return"LOWER LIMIT";case UTM_STOP_EXTERNAL_STOP:return"EXTERNAL STOP";case UTM_STOP_SERVO_FAULT:return"SERVO FAULT";case UTM_STOP_COMMUNICATION_FAULT:return"COMMUNICATION";case UTM_STOP_USER_STOP:return"USER STOP";case UTM_STOP_OVERLOAD:return"OVERLOAD";default:return"NONE";}}

void MainWindow::applyStyle()
{
    qApp->setStyleSheet(R"(
        * { font-family: "Noto Sans", "DejaVu Sans"; font-size: 13px; }
        QMainWindow, QWidget { background:#0b1117; color:#dce6ef; }
        QFrame#statusHeader { background:#17222c; border:1px solid #2d3b47; border-radius:8px; }
        QLabel#brand { font-size:24px; font-weight:800; color:#33d6b3; }
        QLabel#machineState { font-size:18px; font-weight:800; color:#33d6b3; padding:6px 14px; background:#102d29; border-radius:5px; }
        QLabel#machineState[alarm="true"] { color:#fff; background:#b8323d; }
        QLabel#bigValue[forceState="0"] { color:#6f7b84; }
        QLabel#bigValue[forceState="1"] { color:#aeb9c1; }
        QLabel#bigValue[unavailable="true"] { color:#65717a; }
        QLabel#calibrationCurrentValue { font-size:19px; font-weight:700; color:#f2f7fa; padding:5px 0; }
        QLabel#statusChip { background:#111c24; border:1px solid #31424f; border-radius:4px; padding:6px 7px; font-size:11px; font-weight:700; }
        QTabWidget::pane { border:1px solid #26343f; background:#0f161d; }
        QTabBar::tab { background:#17222c; padding:11px 18px; margin-right:2px; }
        QTabBar::tab:selected { background:#24546a; color:white; }
        QGroupBox { border:1px solid #30404d; border-radius:7px; margin-top:14px; padding:14px 10px 10px; font-weight:700; }
        QGroupBox::title { subcontrol-origin:margin; left:12px; padding:0 6px; color:#8fc9df; }
        QFrame#measureCard { background:#14202a; border:1px solid #2f414f; border-radius:9px; }
        QLabel#cardTitle { color:#8fa5b5; font-weight:700; letter-spacing:1px; }
        QLabel#bigValue { font-size:38px; font-weight:700; color:#f2f7fa; }
        QLabel#cardDetail { color:#8fa5b5; }
        QLabel#summaryPanel { background:#121c24; border:1px solid #293a47; border-radius:6px; padding:12px; }
        QPushButton { background:#263743; border:1px solid #3c5261; border-radius:5px; padding:7px 14px; font-weight:700; }
        QPushButton:hover { background:#324b5b; } QPushButton:pressed { background:#17242d; }
        QPushButton:disabled { color:#5f6b73; background:#151c21; border-color:#232d34; }
        QPushButton[role="primary"] { background:#176783; border-color:#2d91b4; }
        QPushButton[role="danger"] { background:#a72c39; border-color:#e0525f; }
        QPushButton[role="warning"] { background:#8a6219; border-color:#d39a2d; }
        QPushButton[role="zero"] { background:#344552; border-color:#718795; color:#eef4f7; }
        QPushButton[role="calibrate"] { background:#176783; border:2px solid #42b8df; color:white; }
        QPushButton[actionState="requested"] { background:#68551f; border:2px solid #e2bd51; }
        QPushButton[actionState="in_progress"] { background:#185d75; border:2px solid #58c9ec; }
        QPushButton[actionState="active"] { background:#176c58; border:2px solid #4ee0ba; }
        QPushButton[actionState="success"] { background:#245d48; border:2px solid #53ca93; }
        QPushButton[actionState="failed"] { background:#674022; border:2px solid #e79a4f; }
        QPushButton[actionState="inhibited"], QPushButton[actionState="disabled"] { background:#252d33; border-color:#46515a; color:#77838c; }
        QDoubleSpinBox, QSpinBox, QComboBox { background:#111a21; border:1px solid #405260; border-radius:4px; padding:7px; min-height:24px; }
        QTableWidget { background:#0f171e; alternate-background-color:#141f27; gridline-color:#283844; selection-background-color:#205b73; }
        QHeaderView::section { background:#1d2a34; color:#dbe7ed; border:0; padding:8px; }
        QLabel#messageBar { background:#14202a; padding:8px 12px; border-radius:5px; color:#8fa5b5; }
        QLabel#messageBar[error="true"], QLabel#warningText { color:#ffb35c; }
        QLabel#startupSummary { font-size:20px; } QLabel#diagnostic { font-family:monospace; font-size:14px; padding:14px; }
    )");
}
