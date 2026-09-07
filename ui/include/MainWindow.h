#pragma once

#include "DaoUtm.Types.h"

#include <QMainWindow>
#include <QElapsedTimer>
#include <QVector>

class QLabel;
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QTableWidget;
class QTabWidget;
class QPushButton;
class QCheckBox;
class TrendGraphWidget;
class UtmUiController;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(UtmUiController* controller, QWidget* parent = nullptr);
    void runOfflineCalibrationSelfTest();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void refresh();
    void showCommandFailure(const QString& operation, const QString& detail);
    void updateStepEditor();

private:
    QWidget* buildDashboard();
    QWidget* buildControl();
    QWidget* buildSequence();
    QWidget* buildDiagnostics();
    QWidget* buildSetup();
    bool syncSetupToProfile();
    void restoreSetupFromProfile();
    void restoreCalibrationFilterFromProfile();
    QWidget* buildCalibration();
    QWidget* card(const QString& title, QLabel*& value, QLabel*& detail,
        QPushButton*& zeroAction);
    void applyStyle();
    void safeJogStop(const char* reason);
    void addSequenceStep(int type);
    void reindexSequence();
    UtmSequenceDefinition sequenceFromTable() const;
    static QString machineStateName(int value);
    static QString startupPhaseName(int value);
    static QString motionTypeName(int value);
    static QString motionStateName(int value);
    static QString sequenceStateName(int value);
    static QString stepTypeName(int value);
    static QString completionName(int value);
    static QString stopReasonName(int value);

    UtmUiController* controller_ = nullptr;
    QLabel* machineState_ = nullptr;
    QLabel* ethercatState_ = nullptr;
    QLabel* servoState_ = nullptr;
    QLabel* safetyState_ = nullptr;
    QLabel* upperLimitState_ = nullptr;
    QLabel* lowerLimitState_ = nullptr;
    QLabel* externalStopState_ = nullptr;
    QLabel* forceValue_ = nullptr;
    QLabel* forceDetail_ = nullptr;
    QLabel* positionValue_ = nullptr;
    QLabel* positionDetail_ = nullptr;
    QLabel* encoderValue_ = nullptr;
    QLabel* encoderDetail_ = nullptr;
    QLabel* motionSummary_ = nullptr;
    QLabel* sequenceSummary_ = nullptr;
    QLabel* diagnosticText_ = nullptr;
    QLabel* commandMessage_ = nullptr;
    QLabel* forceWarning_ = nullptr;
    QLabel* validationMessage_ = nullptr;
    QLabel* calibrationSummary_ = nullptr;
    QLabel* calibrationDetail_ = nullptr;
    QLabel* noiseDetail_ = nullptr;
    QLabel* calibrationResult_ = nullptr;
    QLabel* forceOperatorStatus_ = nullptr;
    QLabel* forceZeroStatus_ = nullptr;
    QLabel* forceCalibrationStatus_ = nullptr;
    QLabel* encoderZeroStatus_ = nullptr;
    QLabel* encoderCalibrationStatus_ = nullptr;
    QLabel* encoderCurrentValue_ = nullptr;
    QLabel* encoderRawValue_ = nullptr;
    QLabel* encoderSignedValue_ = nullptr;
    QLabel* encoderScaleValue_ = nullptr;
    QLabel* positionOperatorStatus_ = nullptr;
    QLabel* connectionState_ = nullptr;
    TrendGraphWidget* graph_ = nullptr;
    TrendGraphWidget* calibrationGraph_ = nullptr;
    QPushButton* jogUp_ = nullptr;
    QPushButton* jogDown_ = nullptr;
    QPushButton* mainJogUp_ = nullptr;
    QPushButton* mainJogDown_ = nullptr;
    QPushButton* moveToForceButton_ = nullptr;
    QPushButton* holdForceButton_ = nullptr;
    QPushButton* moveAbsoluteButton_ = nullptr;
    QPushButton* moveIncrementalButton_ = nullptr;
    QPushButton* moveVelocityButton_ = nullptr;
    QLabel* manualMotionStatus_ = nullptr;
    QLabel* manualDynamics_ = nullptr;
    QDoubleSpinBox* jogSpeed_ = nullptr;
    QDoubleSpinBox* mainJogSpeed_ = nullptr;
    QTableWidget* sequenceTable_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QComboBox* adapterBox_ = nullptr;
    QComboBox* profileBox_ = nullptr;
    QDoubleSpinBox* unitsPerMm_ = nullptr;
    QDoubleSpinBox* jogMin_ = nullptr;
    QDoubleSpinBox* jogMax_ = nullptr;
    QDoubleSpinBox* jogDefault_ = nullptr;
    QDoubleSpinBox* motionAcceleration_ = nullptr;
    QDoubleSpinBox* motionDeceleration_ = nullptr;
    QSpinBox* servoIndex_ = nullptr;
    QSpinBox* adcIndex_ = nullptr;
    QSpinBox* ioIndex_ = nullptr;
    QSpinBox* encoderIndex_ = nullptr;
    QCheckBox* encoderRequired_ = nullptr;
    QCheckBox* autoServoOn_ = nullptr;
    QComboBox* servoDirection_ = nullptr;
    QComboBox* forceDirection_ = nullptr;
    QDoubleSpinBox* forceApproach_ = nullptr;
    QDoubleSpinBox* forceMedium_ = nullptr;
    QDoubleSpinBox* forceFine_ = nullptr;
    QDoubleSpinBox* forceReverse_ = nullptr;
    QDoubleSpinBox* forceMediumError_ = nullptr;
    QDoubleSpinBox* forceFineError_ = nullptr;
    QDoubleSpinBox* forceTolerance_ = nullptr;
    QDoubleSpinBox* forceMaxTravel_ = nullptr;
    QDoubleSpinBox* forceTimeout_ = nullptr;
    QDoubleSpinBox* overloadLimit_ = nullptr;
    QSpinBox* ioEmergency_ = nullptr;
    QSpinBox* ioUpper_ = nullptr;
    QSpinBox* ioLower_ = nullptr;
    QSpinBox* ioJogUp_ = nullptr;
    QSpinBox* ioJogDown_ = nullptr;
    QSpinBox* ioGo_ = nullptr;
    QSpinBox* ioStop_ = nullptr;
    QSpinBox* ioActiveLow_ = nullptr;
    QWidget* calibrationActions_ = nullptr;
    QComboBox* displayForceUnit_ = nullptr;
    QSpinBox* forceDisplayDecimals_ = nullptr;
    QDoubleSpinBox* loadcellCapacity_ = nullptr;
    QComboBox* loadcellCapacityUnit_ = nullptr;
    QComboBox* calibrationReferenceUnit_ = nullptr;
    QDoubleSpinBox* referenceForce_ = nullptr;
    QDoubleSpinBox* lowLevelAlpha_ = nullptr;
    QComboBox* powerLineMode_ = nullptr;
    QSpinBox* movingAverageN_ = nullptr;
    QSpinBox* displayAverageSamples_ = nullptr;
    QCheckBox* lowLevelEnabled_ = nullptr;
    QCheckBox* medianEnabled_ = nullptr;
    QPushButton* forceZeroAction_ = nullptr;
    QPushButton* forceCalibrateAction_ = nullptr;
    QPushButton* encoderZeroAction_ = nullptr;
    QPushButton* encoderCalibrateAction_ = nullptr;
    QPushButton* mainForceZero_ = nullptr;
    QPushButton* mainPositionZero_ = nullptr;
    QPushButton* mainEncoderZero_ = nullptr;
    QDoubleSpinBox* encoderReference_ = nullptr;
    int forceOperationRequested_ = 0;
    bool forceOperationObservedActive_ = false;
    qint64 forceOperationRequestedMs_ = 0;
    bool encoderCalibrationRequested_ = false;
    double encoderCalibrationBaselineScale_ = 1.0;
    double encoderCalibrationReferenceMm_ = 0.0;
    qint64 encoderCalibrationRequestedMs_ = 0;
    QVector<double> noiseSamplesN_;
    QVector<QWidget*> executionControls_;
    QVector<QWidget*> zeroControls_;
    QComboBox* stepType_ = nullptr;
    QDoubleSpinBox* stepValue_ = nullptr;
    QDoubleSpinBox* stepSpeed_ = nullptr;
    QDoubleSpinBox* stepTime_ = nullptr;
    QComboBox* stepDirection_ = nullptr;
    QSpinBox* stepIoBit_ = nullptr;
    QSpinBox* stepLoopCount_ = nullptr;
    QComboBox* stepStopType_ = nullptr;
    QDoubleSpinBox* stepStopValue_ = nullptr;
    bool jogHeld_ = false;
    int jogRequestedDirection_ = UTM_DIRECTION_NONE;
    QPushButton* jogPressOwner_ = nullptr;
    QElapsedTimer jogHoldTimer_;
    int requestedManualMotionType_ = UTM_MOTION_NONE;
    bool loadingProfile_ = false;
};
