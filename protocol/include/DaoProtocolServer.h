#pragma once

#include "DaoProtocolV1.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <string>

namespace dao::protocol
{
struct MonitoringSnapshot { MachineStatus status{}; LiveData live{}; bool liveValid=false; };
struct ProtocolServerConfig
{
    std::uint16_t port=kDefaultPort;
    std::string bindAddress; // Empty means INADDR_ANY.
    std::uint32_t helloTimeoutMs=5000,idleTimeoutMs=10000,livePeriodMs=100;
};
enum class ServerError { None, Socket, InvalidBindAddress, Bind, Listen, Thread };

class ProtocolServer
{
public:
    using CommandHandler = std::function<NackReason(MessageType,const Frame&)>;
    explicit ProtocolServer(std::uint16_t port=kDefaultPort);
    explicit ProtocolServer(ProtocolServerConfig config);
    ~ProtocolServer() { Stop(); }
    bool Start();
    void Stop();
    void PublishLiveData(const LiveData& value) noexcept;
    void PublishStatus(const MachineStatus& value) noexcept;
    void PublishSnapshot(const MonitoringSnapshot& value) noexcept { snapshot_.Publish(value); }
    void QueueEvent(MessageType type, const std::vector<std::uint8_t>& payload={});
    void SetCommandHandler(CommandHandler handler) { handler_=std::move(handler); }
    bool Running() const noexcept { return running_.load(); }
    bool Listening() const noexcept { return listening_.load(); }
    bool ClientConnected() const noexcept { return clientConnected_.load(); }
    ServerError LastError() const noexcept { return lastError_.load(); }
    std::uint16_t Port() const noexcept { return boundPort_.load(); }
private:
    void Run();
    bool HandleFrame(int fd,const Frame& frame,bool& helloDone,std::uint32_t& sequence);
    bool SendFrame(int fd,MessageType type,std::uint32_t requestId,std::uint32_t& sequence,const std::vector<std::uint8_t>& payload={});
    ProtocolServerConfig config_;
    std::atomic<bool> running_{false};
    std::atomic<bool> listening_{false},clientConnected_{false};
    std::atomic<ServerError> lastError_{ServerError::None};
    std::atomic<std::uint16_t> boundPort_{0};
    std::atomic<int> listener_{-1},client_{-1};
    std::thread thread_;
    SnapshotMailbox<MonitoringSnapshot> snapshot_;
    CommandHandler handler_;
    std::mutex eventMutex_;
    std::vector<Frame> events_;
};
}
