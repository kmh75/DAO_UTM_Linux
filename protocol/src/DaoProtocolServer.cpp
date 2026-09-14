#include "DaoProtocolServer.h"

#include <arpa/inet.h>
#include <chrono>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace dao::protocol
{
namespace
{
bool known(MessageType t)
{
    switch(t){case MessageType::Hello:case MessageType::Heartbeat:case MessageType::LoadRecipe:
    case MessageType::CommitRecipe:case MessageType::StartTest:case MessageType::StopTest:
    case MessageType::AckReset:case MessageType::GetTestData:return true;default:return false;}
}
std::vector<std::uint8_t> nackPayload(NackReason reason){auto v=static_cast<std::uint16_t>(reason);return {std::uint8_t(v>>8),std::uint8_t(v)};}
}
bool ProtocolServer::Start(){if(running_.exchange(true))return false;thread_=std::thread(&ProtocolServer::Run,this);return true;}
void ProtocolServer::Stop(){running_.store(false);if(thread_.joinable())thread_.join();}
void ProtocolServer::QueueEvent(MessageType type,const std::vector<std::uint8_t>&payload){std::lock_guard<std::mutex> lock(eventMutex_);events_.push_back(Frame{Header{kMagic,kVersion,type,0,0,0,0},payload});}
bool ProtocolServer::SendFrame(int fd,MessageType type,std::uint32_t requestId,std::uint32_t&seq,const std::vector<std::uint8_t>&payload)
{auto bytes=EncodeFrame(Frame{Header{kMagic,kVersion,type,0,0,++seq,requestId},payload});std::size_t sent=0;while(sent<bytes.size()){const auto n=send(fd,bytes.data()+sent,bytes.size()-sent,MSG_NOSIGNAL);if(n>0)sent+=static_cast<std::size_t>(n);else if(errno==EINTR)continue;else return false;}return true;}
void ProtocolServer::HandleFrame(int fd,const Frame&f,bool&helloDone,std::uint32_t&seq)
{
    if(f.header.type==MessageType::Hello){if(f.header.version!=kVersion){SendFrame(fd,MessageType::CommandNack,f.header.requestId,seq,nackPayload(NackReason::VersionMismatch));return;}helloDone=true;SendFrame(fd,MessageType::HelloAck,f.header.requestId,seq);SendFrame(fd,MessageType::MachineStatus,0,seq,EncodeMachineStatus(status_.Read()));return;}
    if(!helloDone){SendFrame(fd,MessageType::CommandNack,f.header.requestId,seq,nackPayload(NackReason::InvalidState));return;}
    if(f.header.type==MessageType::Heartbeat){SendFrame(fd,MessageType::Heartbeat,f.header.requestId,seq);return;}
    if(!known(f.header.type)){SendFrame(fd,MessageType::CommandNack,f.header.requestId,seq,nackPayload(NackReason::UnsupportedMessage));return;}
    const auto reason=handler_?handler_(f.header.type,f):NackReason::None;
    SendFrame(fd,reason==NackReason::None?MessageType::CommandAck:MessageType::CommandNack,f.header.requestId,seq,reason==NackReason::None?std::vector<std::uint8_t>{}:nackPayload(reason));
}
void ProtocolServer::Run()
{
    int listener=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK,0);if(listener<0){running_=false;return;}int one=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_ANY);addr.sin_port=htons(port_);if(bind(listener,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))||listen(listener,1)){close(listener);running_=false;return;}
    while(running_){pollfd lp{listener,POLLIN,0};if(poll(&lp,1,50)<=0)continue;int client=accept4(listener,nullptr,nullptr,SOCK_NONBLOCK);if(client<0)continue;StreamDecoder decoder;bool hello=false;std::uint32_t seq=0;auto nextLive=std::chrono::steady_clock::now();
        while(running_){pollfd cp{client,POLLIN,0};const int pr=poll(&cp,1,10);if(pr>0&&(cp.revents&(POLLERR|POLLHUP|POLLNVAL)))break;if(pr>0&&(cp.revents&POLLIN)){std::uint8_t data[8192];const auto n=recv(client,data,sizeof(data),0);if(n<=0)break;std::vector<Frame> frames;if(decoder.Feed(data,static_cast<std::size_t>(n),frames)==StreamDecoder::Result::Malformed)break;for(const auto&f:frames)HandleFrame(client,f,hello,seq);}
            if(hello&&std::chrono::steady_clock::now()>=nextLive){if(!SendFrame(client,MessageType::LiveData,0,seq,EncodeLiveData(live_.Read())))break;nextLive+=std::chrono::milliseconds(100);}
            if(hello){std::vector<Frame> pending;{std::lock_guard<std::mutex>lock(eventMutex_);pending.swap(events_);}for(const auto&e:pending)if(!SendFrame(client,e.header.type,0,seq,e.payload)){hello=false;break;}}
        }close(client);
    }close(listener);
}
}
