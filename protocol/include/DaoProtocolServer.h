#pragma once

#include "DaoProtocolV1.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace dao::protocol
{
class ProtocolServer
{
public:
    using CommandHandler = std::function<NackReason(MessageType,const Frame&)>;
    explicit ProtocolServer(std::uint16_t port=kDefaultPort) : port_(port) {}
    ~ProtocolServer() { Stop(); }
    bool Start();
    void Stop();
    void PublishLiveData(const LiveData& value) noexcept { live_.Publish(value); }
    void PublishStatus(const MachineStatus& value) noexcept { status_.Publish(value); }
    void QueueEvent(MessageType type, const std::vector<std::uint8_t>& payload={});
    void SetCommandHandler(CommandHandler handler) { handler_=std::move(handler); }
    bool Running() const noexcept { return running_.load(); }
    std::uint16_t Port() const noexcept { return port_; }
private:
    void Run();
    void HandleFrame(int fd,const Frame& frame,bool& helloDone,std::uint32_t& sequence);
    bool SendFrame(int fd,MessageType type,std::uint32_t requestId,std::uint32_t& sequence,const std::vector<std::uint8_t>& payload={});
    std::uint16_t port_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    SnapshotMailbox<LiveData> live_;
    SnapshotMailbox<MachineStatus> status_;
    CommandHandler handler_;
    std::mutex eventMutex_;
    std::vector<Frame> events_;
};
}
