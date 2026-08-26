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
   
	bool RequestAllSlavesSafeOp(); // 검색된 모든 Slave를 SAFE-OP 상태로 전환합니다.

	bool RequestAllSlavesOperational(); // 검색된 모든 Slave를 OP 상태로 전환합니다.

    bool RequestAllSlavesInit(); // 검색된 모든 Slave를 INIT 상태로 전환합니다.

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

    bool SetServoOutputCommand(
        int logicalServoIndex,
		const DaoInternalLsServoOutputPdo& command);  // 논리 Servo에 송신할 Output PDO 명령을 저장합니다.

    bool ServoOn(
        int logicalServoIndex);

    bool ServoOff(
        int logicalServoIndex);

    bool ServoHome(
        int logicalServoIndex,
        unsigned int timeoutMs); // 논리 Servo를 Homing 상태로 전환합니다.

    bool ServoMoveAbsolute(
        int logicalServoIndex,
        int targetPosition,
        unsigned int profileVelocity,
        unsigned int profileAcceleration,
        unsigned int profileDeceleration,
		unsigned int timeoutMs); // 논리 Servo를 절대 위치로 이동합니다.

    bool ServoVelocity(
        int logicalServoIndex,
        int targetVelocity,
        unsigned int acceleration,
		unsigned int deceleration); // 논리 Servo를 속도제어모드로 운전합니다.
    
   

    bool ServoJogPositive(
        int logicalServoIndex,
        int speed,
        unsigned int acceleration,
		unsigned int deceleration); // 논리 Servo를 정방향 jog 속도제어모드로 운전합니다.

    bool ServoJogNegative(
        int logicalServoIndex,
        int speed,
        unsigned int acceleration,
		unsigned int deceleration); // 논리 Servo를 역방향 jog 속도제어모드로 운전합니다.

    bool ServoStop(
		int logicalServoIndex); // 논리 Servo를 정지시킵니다.




    // 논리 Servo 번호를 물리 Slave 번호로 변환한 뒤
    // LS L7NH의 CiA402 운전모드를 설정합니다.
    //
    // mode:
    // 1 = Profile Position
    // 3 = Profile Velocity
    // 6 = Homing
    //
    // SDO 통신을 사용하므로 순환통신이 정지된 상태에서 호출합니다.
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

	bool StartCommunication(); // 통신 스레드 시작

	void StopCommunication(); // 통신 스레드 종료 요청

	bool IsCommunicationRunning() const; // 통신 스레드가 실행 중인지 확인

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
};