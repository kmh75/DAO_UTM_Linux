#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "DaoEtherCATMaster.h"

struct DaoInternalAdapterInfo
{
    std::string name;
    std::string description;
};

struct DaoInternalLogicalDeviceInfo
{
    int deviceType;
    int logicalIndex;
    int physicalSlaveIndex;

    std::string name;

    unsigned int vendorId;
    unsigned int productCode;
    unsigned int revision;
};

class DaoEngineCore
{
public:
    DaoEngineCore();

    bool Initialize();
    void Shutdown();

    bool IsInitialized() const;

    int GetAdapterCount();
    bool GetAdapterInfo(
        int adapterIndex,
        DaoInternalAdapterInfo& adapterInfo);

    bool OpenAdapter(int adapterIndex);
    void CloseAdapter();
    bool IsAdapterOpen() const;
    int ScanSlaves();
    int GetSlaveCount() const;

    bool GetSlaveInfo(
        int slaveListIndex,
        DaoInternalSlaveInfo& slaveInfo) const;

    int GetLogicalDeviceCount(
        int deviceType) const;

    bool GetLogicalDeviceInfo(
        int deviceType,
        int logicalIndex,
        DaoInternalLogicalDeviceInfo& deviceInfo) const;

	bool RequestAllSlavesPreOp(); // 검색된 모든 Slave를 PRE-OP 상태로 전환합니다.
   
	bool RequestAllSlavesSafeOp(); // 검색된 모든 Slave를 PRE-OP 상태로 전환합니다.

	bool RequestAllSlavesOperational(); // 검색된 모든 Slave를 SAFE-OP 상태로 전환합니다.

    bool RequestAllSlavesInit(); // 검색된 모든 Slave를 OP 상태로 전환합니다.

    bool MapProcessData();

    bool GetProcessDataMapInfo(
        DaoInternalProcessDataMapInfo& mapInfo) const;

    bool GetSlavePdoInfo(
        int slaveListIndex,
        DaoInternalSlavePdoInfo& pdoInfo) const;

    bool ValidateDaoAdcPdo(
        int physicalSlaveIndex,
        DaoInternalAdcValidationInfo& validationInfo) const;

    bool RequestDaoAdcSafeOp(
        int physicalSlaveIndex);

    bool ExchangeDaoAdcProcessDataOnce(
        int physicalSlaveIndex,
        DaoInternalProcessExchangeInfo& exchangeInfo);

    bool PrimeDaoAdcProcessData(
        int physicalSlaveIndex,
        int roundCount,
        DaoInternalPrimingInfo& primingInfo);

    bool RequestDaoAdcOperational(
        int physicalSlaveIndex,
        DaoInternalOperationalInfo& operationalInfo);

    bool ReadDaoAdcOnce(
        int physicalSlaveIndex,
        DaoInternalAdcReadInfo& readInfo);

    bool GetDaoAdcRuntimeInfo(
        int physicalSlaveIndex,
        DaoInternalAdcRuntimeInfo& runtimeInfo) const;

    bool GetAdcRuntimeInfo(
        int logicalAdcIndex,
        DaoInternalAdcRuntimeInfo& runtimeInfo) const;

    bool SetAdcZero(
        int logicalAdcIndex);

    bool SetAdcCalibration(
        int logicalAdcIndex,
        double referenceValue); 

    bool SetAdcPowerLineFilterMode(
        int logicalAdcIndex,
        int mode); 

    bool SetAdcFilterN(
        int logicalAdcIndex,
        unsigned int filterN);


    bool StartAdcDiagnosticCapture(
        int logicalAdcIndex,
        unsigned int targetSampleCount);

    bool GetAdcDiagnosticCaptureInfo(
        int logicalAdcIndex,
        bool& captureActive,
        unsigned int& capturedSampleCount,
        unsigned int& targetSampleCount) const;

    bool GetAdcDiagnosticSample(
        int logicalAdcIndex,
        unsigned int sampleIndex,
        DaoInternalAdcDiagnosticSample& sample) const;

    bool GetAdcRingBufferInfo(
        int logicalAdcIndex,
        unsigned int& sampleCount,
        unsigned long long& overflowCount) const;

    bool ReadAdcRingBuffer(
        int logicalAdcIndex,
        DaoInternalAdcBufferedSample* samples,
        unsigned int maxSampleCount,
        unsigned int& readSampleCount);

    bool ClearAdcRingBuffer(
        int logicalAdcIndex);


    bool GetServoRuntimeInfo(
        int logicalServoIndex,
        DaoInternalServoRuntimeInfo& runtimeInfo) const;

    bool GetIoRuntimeInfo(
        int logicalIoIndex,
        DaoInternalIoRuntimeInfo& runtimeInfo) const;

    bool GetEncoderRuntimeInfo(
    int logicalEncoderIndex,
    DaoInternalEncoderRuntimeInfo& runtimeInfo) const;

    bool SetServoOutputCommand(
        int logicalServoIndex,
		const DaoInternalLsServoOutputPdo& command);  // Servo ON 명령을 등록하고 CiA 402 활성화 절차를 시작합니다.

    bool ServoOn(
        int logicalServoIndex);

    bool ServoOff(
        int logicalServoIndex);

    bool ServoHome(
        int logicalServoIndex,
        unsigned int timeoutMs); // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.

    bool ServoMoveAbsolute(
        int logicalServoIndex,
        int targetPosition,
        unsigned int profileVelocity,
        unsigned int profileAcceleration,
        unsigned int profileDeceleration,
		unsigned int timeoutMs); // 요청한 EtherCAT 상태에 도달했는지 제한 시간 동안 확인합니다.

    bool ServoVelocity(
        int logicalServoIndex,
        int targetVelocity,
        unsigned int acceleration,
		unsigned int deceleration); // 목표 속도와 가감속 값을 설정해 속도 운전을 요청합니다.
    
   

    bool ServoJogPositive(
        int logicalServoIndex,
        int speed,
        unsigned int acceleration,
		unsigned int deceleration); // 이 값은 해당 처리의 실행 상태와 결과를 나타냅니다.

    bool ServoJogNegative(
        int logicalServoIndex,
        int speed,
        unsigned int acceleration,
		unsigned int deceleration); // 현재 Servo 운전 명령을 정지 상태로 전환합니다.

    bool ServoStop(
		int logicalServoIndex); // 현재 Servo 운전 명령을 정지 상태로 전환합니다.




    // 아래 코드는 현재 장치 상태에 맞는 처리 단계를 수행합니다.
    // Servo Homing 명령과 제한 시간을 설정합니다.
    //
    // mode:
    // 1 = Profile Position
    // 3 = Profile Velocity
    // 6 = Homing
    //
    // Servo의 운전 모드를 SDO로 설정합니다.
    bool SetServoOperationMode(
        int logicalServoIndex,
        signed char mode);
   
    bool BeginServoCommand(
        int logicalServoIndex,
        int commandType);

    bool UpdateServoCommandState(
        int logicalServoIndex,
        int commandState,
        int commandResult);


    bool SetIoOutputCommand(
        int logicalIoIndex,
        unsigned short outputValue);

	bool StartCommunication(); // 통신 스레드의 실행 상태를 확인합니다.

	void StopCommunication(); // 통신 스레드의 실행 상태를 확인합니다.

	bool IsCommunicationRunning() const; // 통신 스레드의 실행 상태를 확인합니다.

private:
    bool RefreshAdapterList();

    void ClearLogicalDevices();
    void BuildLogicalDevices();

    int ClassifyDevice(
        const DaoInternalSlaveInfo& slaveInfo) const;

    const std::vector<DaoInternalLogicalDeviceInfo>*
        GetLogicalDeviceList(
            int deviceType) const;
    bool GetPhysicalSlaveIndex(
        int deviceType,
        int logicalIndex,
        int& physicalSlaveIndex) const;

private:
    std::atomic<bool> initialized_;
    std::vector<DaoInternalAdapterInfo> adapters_;
    DaoEtherCATMaster master_;

    std::vector<DaoInternalLogicalDeviceInfo> unknownDevices_;
    std::vector<DaoInternalLogicalDeviceInfo> servoDevices_;
    std::vector<DaoInternalLogicalDeviceInfo> adcDevices_;
    std::vector<DaoInternalLogicalDeviceInfo> ioDevices_;
    std::vector<DaoInternalLogicalDeviceInfo> encoderDevices_;
};