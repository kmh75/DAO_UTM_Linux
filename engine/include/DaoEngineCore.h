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

	bool RequestAllSlavesPreOp(); // �˻��� ��� Slave�� PRE-OP ���·� ��ȯ�մϴ�.
   
	bool RequestAllSlavesSafeOp(); // �˻��� ��� Slave�� SAFE-OP ���·� ��ȯ�մϴ�.

	bool RequestAllSlavesOperational(); // �˻��� ��� Slave�� OP ���·� ��ȯ�մϴ�.

    bool RequestAllSlavesInit(); // �˻��� ��� Slave�� INIT ���·� ��ȯ�մϴ�.

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
		const DaoInternalLsServoOutputPdo& command);  // ���� Servo�� �۽��� Output PDO ������ �����մϴ�.

    bool ServoOn(
        int logicalServoIndex);

    bool ServoOff(
        int logicalServoIndex);

    bool ServoHome(
        int logicalServoIndex,
        unsigned int timeoutMs); // ���� Servo�� Homing ���·� ��ȯ�մϴ�.

    bool ServoMoveAbsolute(
        int logicalServoIndex,
        int targetPosition,
        unsigned int profileVelocity,
        unsigned int profileAcceleration,
        unsigned int profileDeceleration,
		unsigned int timeoutMs); // ���� Servo�� ���� ��ġ�� �̵��մϴ�.

    bool ServoVelocity(
        int logicalServoIndex,
        int targetVelocity,
        unsigned int acceleration,
		unsigned int deceleration); // ���� Servo�� �ӵ�������� �����մϴ�.
    
   

    bool ServoJogPositive(
        int logicalServoIndex,
        int speed,
        unsigned int acceleration,
		unsigned int deceleration); // ���� Servo�� ������ jog �ӵ�������� �����մϴ�.

    bool ServoJogNegative(
        int logicalServoIndex,
        int speed,
        unsigned int acceleration,
		unsigned int deceleration); // ���� Servo�� ������ jog �ӵ�������� �����մϴ�.

    bool ServoStop(
		int logicalServoIndex); // ���� Servo�� ������ŵ�ϴ�.




    // ���� Servo ��ȣ�� ���� Slave ��ȣ�� ��ȯ�� ��
    // LS L7NH�� CiA402 ������带 �����մϴ�.
    //
    // mode:
    // 1 = Profile Position
    // 3 = Profile Velocity
    // 6 = Homing
    //
    // SDO ����� ����ϹǷ� ��ȯ����� ������ ���¿��� ȣ���մϴ�.
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

	bool StartCommunication(); // ��� ������ ����

	void StopCommunication(); // ��� ������ ���� ��û

	bool IsCommunicationRunning() const; // ��� �����尡 ���� ������ Ȯ��

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