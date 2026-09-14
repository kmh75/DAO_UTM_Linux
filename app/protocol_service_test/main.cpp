#include "UtmProtocolService.h"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace dao::protocol;
using namespace std::chrono_literals;
static void require(bool ok,const char* what){if(!ok){std::cerr<<"FAIL: "<<what<<'\n';std::exit(1);}}
static int connectLocal(std::uint16_t port){sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);inet_pton(AF_INET,"127.0.0.1",&a.sin_addr);for(int i=0;i<40;++i){int fd=socket(AF_INET,SOCK_STREAM,0);if(fd>=0&&connect(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0)return fd;if(fd>=0)close(fd);std::this_thread::sleep_for(5ms);}return -1;}
static void sendRequest(int fd,MessageType type,std::uint32_t request){Frame f;f.header.type=type;f.header.requestId=request;const auto bytes=EncodeFrame(f);require(send(fd,bytes.data(),bytes.size(),MSG_NOSIGNAL)==static_cast<ssize_t>(bytes.size()),"send frame");}
static std::vector<Frame> receiveFor(int fd,StreamDecoder& decoder,std::chrono::milliseconds duration)
{
    std::vector<Frame> out;const auto end=std::chrono::steady_clock::now()+duration;std::uint8_t bytes[8192];
    while(std::chrono::steady_clock::now()<end){const auto n=recv(fd,bytes,sizeof(bytes),MSG_DONTWAIT);if(n>0)require(decoder.Feed(bytes,n,out)==StreamDecoder::Result::Ok,"decode network frames");else if(n==0)break;std::this_thread::sleep_for(2ms);}return out;
}
static UtmRuntimeInfoV6 runtimeSample(std::uint64_t timestampNs)
{
    UtmRuntimeInfoV6 r{};auto& b=r.runtime.runtime.runtime.runtime.runtime;auto& m=r.runtime.runtime.motion;auto& s=r.sequence;
    b.publishedTimestampNs=timestampNs;b.initialized=1;b.controlLoopRunning=1;b.machineState=UTM_MACHINE_RUNNING;
    b.input.forceN=321.25;b.input.forceValid=1;b.input.servoPositionValid=1;b.input.encoderPresent=1;b.input.encoderValid=1;b.input.encoderPosition=7.75;
    m.testPositionMm=12.5;m.machinePositionMm=99.0;m.positionZeroValid=1;
    s.recordingSessionId=42;s.sequenceState=UTM_SEQUENCE_STATE_RUNNING;s.currentStepIndex=3;s.sequenceRunning=1;return r;
}

int main()
{
    require(EncodeMachineStatus({}).size()==24,"STATUS payload 24");require(EncodeLiveData({}).size()==76,"LIVE payload 76");
    const auto endian=EncodeLiveData(LiveData{0x0102030405060708ULL});require(endian[0]==1&&endian[7]==8,"LIVE big endian");
    bool mappedValid=false;dao::utm::ComplianceRuntime c{};c.enabled=true;c.inCalibrationRange=false;c.compensationMm=.125;c.correctedDisplacementMm=12.375;
    const auto mapped=UtmProtocolService::MapRuntime(runtimeSample(123456789000ULL),c,mappedValid);
    require(mappedValid&&mapped.status.testId==42&&mapped.status.machineState==UTM_MACHINE_RUNNING&&mapped.status.currentStep==3&&mapped.status.testRunning,"STATUS runtime mapping");
    require(mapped.live.timestampUs==123456789&&mapped.live.forceN==321.25&&mapped.live.rawDisplacementMm==12.5&&mapped.live.rawDisplacementMm!=99.0&&mapped.live.correctedDisplacementMm==12.375&&mapped.live.extensometerMm==7.75,"LIVE runtime mapping");
    require(mapped.live.sampleFlags==0x1f,"sample flags mapping");
    ProtocolServerConfig serviceCfg;serviceCfg.port=0;serviceCfg.bindAddress="127.0.0.1";serviceCfg.helloTimeoutMs=100;serviceCfg.idleTimeoutMs=200;
    auto invalid=runtimeSample(0);UtmProtocolService service(serviceCfg);require(!service.PublishCurrentRuntime(invalid,c),"initial invalid runtime withheld");
    require(service.Start()&&service.IsRunning()&&service.IsListening(),"ProtocolService start");require(service.PublishCurrentRuntime(runtimeSample(123456789000ULL),c),"ProtocolService publish");
    int serviceFd=connectLocal(service.Port());require(serviceFd>=0,"ProtocolService connect");StreamDecoder serviceDecoder;sendRequest(serviceFd,MessageType::Hello,1);receiveFor(serviceFd,serviceDecoder,30ms);
    sendRequest(serviceFd,MessageType::StartTest,2);const auto serviceFrames=receiveFor(serviceFd,serviceDecoder,30ms);bool serviceNack=false;for(const auto& f:serviceFrames)serviceNack|=f.header.type==MessageType::CommandNack&&f.header.requestId==2;require(serviceNack,"production monitoring-only handler NACK");close(serviceFd);service.Stop();require(!service.IsRunning()&&!service.IsListening(),"ProtocolService stop");

    SnapshotMailbox<LiveData> mailbox;std::atomic<bool> raceOk{true},done{false};
    std::thread writer([&]{for(std::uint64_t i=1;i<100000;++i){LiveData v;v.timestampUs=i;v.testId=i;v.machineState=static_cast<std::uint32_t>(i);mailbox.Publish(v);}done=true;});
    std::thread reader([&]{while(!done){LiveData v;if(mailbox.Read(v)&&!(v.timestampUs==v.testId&&v.timestampUs==v.machineState))raceOk=false;}});writer.join();reader.join();require(raceOk,"race-safe snapshot handoff");

    ProtocolServer occupied(0);require(occupied.Start(),"occupy ephemeral port");ProtocolServer conflict(occupied.Port());require(!conflict.Start()&&conflict.LastError()==ServerError::Bind,"synchronous bind failure");occupied.Stop();
    ProtocolServerConfig cfg;cfg.port=0;cfg.bindAddress="127.0.0.1";cfg.helloTimeoutMs=80;cfg.idleTimeoutMs=130;cfg.livePeriodMs=40;
    ProtocolServer server(cfg);server.PublishSnapshot(mapped);require(server.Start()&&server.Listening()&&server.Port()!=0,"service start/listen");require(!server.Start(),"duplicate start rejected");
    int silent=connectLocal(server.Port());require(silent>=0,"HELLO timeout connect");std::this_thread::sleep_for(140ms);char byte=0;require(recv(silent,&byte,1,MSG_DONTWAIT)==0,"HELLO timeout closes client");close(silent);

    int fd=connectLocal(server.Port());require(fd>=0,"HELLO connect");StreamDecoder decoder;sendRequest(fd,MessageType::Hello,10);auto frames=receiveFor(fd,decoder,90ms);bool hello=false,status=false,live=false;for(const auto& f:frames){hello|=f.header.type==MessageType::HelloAck;status|=f.header.type==MessageType::MachineStatus&&f.payload.size()==24;live|=f.header.type==MessageType::LiveData&&f.payload.size()==76;}require(hello&&status&&live,"HELLO ACK STATUS LIVE");
    sendRequest(fd,MessageType::Heartbeat,11);frames=receiveFor(fd,decoder,30ms);bool heartbeat=false;for(const auto& f:frames)heartbeat|=f.header.type==MessageType::Heartbeat&&f.header.requestId==11;require(heartbeat,"heartbeat echo");
    const MessageType controls[]={MessageType::LoadRecipe,MessageType::CommitRecipe,MessageType::StartTest,MessageType::StopTest,MessageType::AckReset,MessageType::GetTestData};std::uint32_t request=20;for(auto type:controls)sendRequest(fd,type,request++);frames=receiveFor(fd,decoder,80ms);int nacks=0;for(const auto& f:frames)if(f.header.type==MessageType::CommandNack)nacks++;require(nacks==6,"missing handler fail-closed NACK all controls");
    int second=connectLocal(server.Port());require(second>=0,"second TCP connect");std::this_thread::sleep_for(30ms);require(recv(second,&byte,1,MSG_DONTWAIT)==0,"second client immediately closed");close(second);
    std::this_thread::sleep_for(150ms);require(recv(fd,&byte,1,MSG_DONTWAIT)==0,"idle timeout closes session");close(fd);

    int stale=connectLocal(server.Port());require(stale>=0,"stale timestamp connect");StreamDecoder staleDecoder;sendRequest(stale,MessageType::Hello,50);frames=receiveFor(stale,staleDecoder,125ms);int liveCount=0;for(const auto& f:frames)liveCount+=f.header.type==MessageType::LiveData;require(liveCount==1,"stale source timestamp not replayed");close(stale);
    int failed=connectLocal(server.Port());require(failed>=0,"send failure connect");sendRequest(failed,MessageType::Hello,60);linger reset{1,0};setsockopt(failed,SOL_SOCKET,SO_LINGER,&reset,sizeof(reset));close(failed);std::this_thread::sleep_for(60ms);int recovered=connectLocal(server.Port());require(recovered>=0,"listener survives send failure");close(recovered);
    server.Stop();require(!server.Running()&&!server.Listening(),"service stop");require(server.Start(),"repeated start");server.Stop();
    std::cout<<"Protocol production service tests passed; UTM command calls=0\n";
}
