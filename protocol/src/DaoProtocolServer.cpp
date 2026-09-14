#include "DaoProtocolServer.h"

#include <arpa/inet.h>
#include <chrono>
#include <cerrno>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace dao::protocol
{
namespace
{
bool command(MessageType t)
{
    switch(t){case MessageType::LoadRecipe:case MessageType::CommitRecipe:
    case MessageType::StartTest:case MessageType::StopTest:case MessageType::AckReset:
    case MessageType::GetTestData:return true;default:return false;}
}
std::vector<std::uint8_t> nackPayload(NackReason r){const auto v=static_cast<std::uint16_t>(r);return {std::uint8_t(v>>8),std::uint8_t(v)};}
void closeSocket(std::atomic<int>& slot)
{
    const int fd=slot.exchange(-1);
    if(fd>=0){shutdown(fd,SHUT_RDWR);close(fd);}
}
}

ProtocolServer::ProtocolServer(std::uint16_t port):ProtocolServer(ProtocolServerConfig{port}){}
ProtocolServer::ProtocolServer(ProtocolServerConfig config):config_(std::move(config)),boundPort_(config_.port){}

bool ProtocolServer::Start()
{
    if(running_.load()||thread_.joinable())return false;
    lastError_=ServerError::None;boundPort_=config_.port;
    const int fd=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK,0);
    if(fd<0){lastError_=ServerError::Socket;return false;}
    int one=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(config_.port);
    if(config_.bindAddress.empty())addr.sin_addr.s_addr=htonl(INADDR_ANY);
    else if(inet_pton(AF_INET,config_.bindAddress.c_str(),&addr.sin_addr)!=1){close(fd);lastError_=ServerError::InvalidBindAddress;return false;}
    if(bind(fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))!=0){close(fd);lastError_=ServerError::Bind;return false;}
    if(listen(fd,1)!=0){close(fd);lastError_=ServerError::Listen;return false;}
    sockaddr_in actual{};socklen_t actualSize=sizeof(actual);
    if(getsockname(fd,reinterpret_cast<sockaddr*>(&actual),&actualSize)==0)boundPort_=ntohs(actual.sin_port);
    listener_=fd;running_=true;listening_=true;
    try{thread_=std::thread(&ProtocolServer::Run,this);}
    catch(...){running_=false;listening_=false;closeSocket(listener_);lastError_=ServerError::Thread;return false;}
    return true;
}

void ProtocolServer::Stop()
{
    running_=false;listening_=false;closeSocket(listener_);closeSocket(client_);
    if(thread_.joinable())thread_.join();clientConnected_=false;
}

void ProtocolServer::PublishLiveData(const LiveData& value) noexcept
{MonitoringSnapshot next{};snapshot_.Read(next);next.live=value;next.liveValid=true;snapshot_.Publish(next);}
void ProtocolServer::PublishStatus(const MachineStatus& value) noexcept
{MonitoringSnapshot next{};snapshot_.Read(next);next.status=value;snapshot_.Publish(next);}
void ProtocolServer::QueueEvent(MessageType type,const std::vector<std::uint8_t>&payload)
{std::lock_guard<std::mutex> lock(eventMutex_);events_.push_back(Frame{Header{kMagic,kVersion,type,0,0,0,0},payload});}

bool ProtocolServer::SendFrame(int fd,MessageType type,std::uint32_t requestId,std::uint32_t&seq,const std::vector<std::uint8_t>&payload)
{
    const auto bytes=EncodeFrame(Frame{Header{kMagic,kVersion,type,0,0,++seq,requestId},payload});
    std::size_t sent=0;
    while(sent<bytes.size()){
        const auto n=send(fd,bytes.data()+sent,bytes.size()-sent,MSG_NOSIGNAL|MSG_DONTWAIT);
        if(n>0)sent+=static_cast<std::size_t>(n);else if(n<0&&errno==EINTR)continue;else return false;
    }
    return true;
}

bool ProtocolServer::HandleFrame(int fd,const Frame&f,bool&helloDone,std::uint32_t&seq)
{
    if(f.header.type==MessageType::Hello){
        if(f.header.version!=kVersion)return SendFrame(fd,MessageType::CommandNack,f.header.requestId,seq,nackPayload(NackReason::VersionMismatch));
        helloDone=true;if(!SendFrame(fd,MessageType::HelloAck,f.header.requestId,seq))return false;
        MonitoringSnapshot value{};return !snapshot_.Read(value)||SendFrame(fd,MessageType::MachineStatus,0,seq,EncodeMachineStatus(value.status));
    }
    if(!helloDone)return SendFrame(fd,MessageType::CommandNack,f.header.requestId,seq,nackPayload(NackReason::InvalidState));
    if(f.header.type==MessageType::Heartbeat)return SendFrame(fd,MessageType::Heartbeat,f.header.requestId,seq);
    if(!command(f.header.type))return SendFrame(fd,MessageType::CommandNack,f.header.requestId,seq,nackPayload(NackReason::UnsupportedMessage));
    const auto reason=handler_?handler_(f.header.type,f):NackReason::UnsupportedMessage;
    return SendFrame(fd,reason==NackReason::None?MessageType::CommandAck:MessageType::CommandNack,
        f.header.requestId,seq,reason==NackReason::None?std::vector<std::uint8_t>{}:nackPayload(reason));
}

void ProtocolServer::Run()
{
    using Clock=std::chrono::steady_clock;
    while(running_){
        const int listenFd=listener_.load();if(listenFd<0)break;
        pollfd lp{listenFd,POLLIN,0};const int apr=poll(&lp,1,50);
        if(!running_)break;if(apr<=0||!(lp.revents&POLLIN))continue;
        const int accepted=accept4(listenFd,nullptr,nullptr,SOCK_NONBLOCK);if(accepted<0)continue;
        int expected=-1;if(!client_.compare_exchange_strong(expected,accepted)){shutdown(accepted,SHUT_RDWR);close(accepted);continue;}
        clientConnected_=true;StreamDecoder decoder;bool hello=false;std::uint32_t seq=0;
        const auto acceptedAt=Clock::now();auto lastActivity=acceptedAt;auto nextLive=acceptedAt;bool session=true;
        std::uint64_t lastLiveTimestampUs=0;
        while(running_&&session){
            pollfd fds[2]={{client_.load(),POLLIN,0},{listenFd,POLLIN,0}};
            const int pr=poll(fds,2,10);const auto now=Clock::now();
            if(pr>0&&(fds[1].revents&POLLIN)){const int extra=accept4(listenFd,nullptr,nullptr,SOCK_NONBLOCK);if(extra>=0){shutdown(extra,SHUT_RDWR);close(extra);}}
            if(pr>0&&(fds[0].revents&(POLLERR|POLLHUP|POLLNVAL)))break;
            if(pr>0&&(fds[0].revents&POLLIN)){
                std::uint8_t data[8192];const auto n=recv(fds[0].fd,data,sizeof(data),0);if(n<=0)break;
                std::vector<Frame> frames;if(decoder.Feed(data,static_cast<std::size_t>(n),frames)==StreamDecoder::Result::Malformed)break;
                if(!frames.empty())lastActivity=now;
                for(const auto& frame:frames)if(!HandleFrame(fds[0].fd,frame,hello,seq)){session=false;break;}
            }
            if(!hello&&now-acceptedAt>=std::chrono::milliseconds(config_.helloTimeoutMs))break;
            if(hello&&now-lastActivity>=std::chrono::milliseconds(config_.idleTimeoutMs))break;
            if(hello&&now>=nextLive){
                MonitoringSnapshot value{};
                if(snapshot_.Read(value)&&value.liveValid&&value.live.timestampUs!=lastLiveTimestampUs){
                    if(!SendFrame(fds[0].fd,MessageType::LiveData,0,seq,EncodeLiveData(value.live)))break;
                    lastLiveTimestampUs=value.live.timestampUs;
                }
                nextLive=now+std::chrono::milliseconds(config_.livePeriodMs);
            }
            if(hello){std::vector<Frame> pending;{std::lock_guard<std::mutex>lock(eventMutex_);pending.swap(events_);}for(const auto&e:pending)if(!SendFrame(fds[0].fd,e.header.type,0,seq,e.payload)){session=false;break;}}
        }
        closeSocket(client_);clientConnected_=false;
    }
    closeSocket(listener_);listening_=false;running_=false;
}
}
