#include "UtmProtocolService.h"

#include <cmath>

using namespace dao::protocol;

UtmProtocolService::UtmProtocolService(ProtocolServerConfig config):server_(std::move(config))
{
    // V0.1 is monitoring-only. No command reaches a UTM command API.
    server_.SetCommandHandler([](MessageType,const Frame&){return NackReason::UnsupportedMessage;});
}

bool UtmProtocolService::Start(){return server_.Start();}
void UtmProtocolService::Stop(){server_.Stop();}
bool UtmProtocolService::IsRunning()const noexcept{return server_.Running();}
bool UtmProtocolService::IsListening()const noexcept{return server_.Listening();}
bool UtmProtocolService::IsClientConnected()const noexcept{return server_.ClientConnected();}
std::uint16_t UtmProtocolService::Port()const noexcept{return server_.Port();}
ServerError UtmProtocolService::LastError()const noexcept{return server_.LastError();}

MonitoringSnapshot UtmProtocolService::MapRuntime(const UtmRuntimeInfoV6& runtime,
    const dao::utm::ComplianceRuntime& compliance,bool& valid) noexcept
{
    const auto& base=runtime.runtime.runtime.runtime.runtime.runtime;
    const auto& motion=runtime.runtime.runtime.motion;
    const auto& sequence=runtime.sequence;
    valid=base.publishedTimestampNs!=0&&base.initialized&&base.controlLoopRunning;
    MonitoringSnapshot out{};
    // V0.1 temporary process-local ID; recordingSessionId is not a persistent Test ID.
    out.status.testId=sequence.recordingSessionId;
    out.status.machineState=static_cast<std::uint32_t>(base.machineState);
    out.status.sequenceState=static_cast<std::uint32_t>(sequence.sequenceState);
    out.status.currentStep=sequence.currentStepIndex;
    out.status.testRunning=sequence.sequenceRunning!=0;
    out.live.timestampUs=base.publishedTimestampNs/1000ULL;
    out.live.testId=sequence.recordingSessionId;
    out.live.forceN=base.input.forceN;
    out.live.rawDisplacementMm=motion.testPositionMm;
    out.live.complianceCompensationMm=compliance.compensationMm;
    out.live.correctedDisplacementMm=compliance.correctedDisplacementMm;
    out.live.extensometerMm=base.input.encoderPosition;
    out.live.machineState=out.status.machineState;
    out.live.sequenceState=out.status.sequenceState;
    out.live.currentStep=out.status.currentStep;
    out.live.testRunning=out.status.testRunning;
    if(compliance.enabled)out.live.sampleFlags|=dao::utm::ComplianceEnabled;
    if(compliance.enabled&&!compliance.inCalibrationRange)out.live.sampleFlags|=dao::utm::ComplianceOutOfRange;
    if(base.input.forceValid&&std::isfinite(base.input.forceN))out.live.sampleFlags|=dao::utm::ForceValid;
    if(motion.positionZeroValid&&base.input.servoPositionValid&&std::isfinite(motion.testPositionMm))out.live.sampleFlags|=dao::utm::DisplacementValid;
    if(base.input.encoderPresent&&base.input.encoderValid&&std::isfinite(base.input.encoderPosition))out.live.sampleFlags|=dao::utm::ExtensometerValid;
    out.liveValid=valid;
    return out;
}

bool UtmProtocolService::PublishCurrentRuntime(const UtmRuntimeInfoV6& runtime,
    const dao::utm::ComplianceRuntime& compliance) noexcept
{
    bool valid=false;const auto snapshot=MapRuntime(runtime,compliance,valid);
    if(valid)server_.PublishSnapshot(snapshot);
    return valid;
}
