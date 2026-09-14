#pragma once

#include "DaoProtocolServer.h"
#include "DaoUtm.Types.h"
#include "UtmComplianceCompensation.h"

#include <cstdint>
#include <string>

class UtmProtocolService
{
public:
    explicit UtmProtocolService(dao::protocol::ProtocolServerConfig config={});
    bool Start();
    void Stop();
    bool IsRunning() const noexcept;
    bool IsListening() const noexcept;
    bool IsClientConnected() const noexcept;
    std::uint16_t Port() const noexcept;
    dao::protocol::ServerError LastError() const noexcept;

    // Called only by the application polling layer, never by the 2 ms control loop.
    bool PublishCurrentRuntime(const UtmRuntimeInfoV6& runtime,
        const dao::utm::ComplianceRuntime& compliance) noexcept;

    static dao::protocol::MonitoringSnapshot MapRuntime(const UtmRuntimeInfoV6& runtime,
        const dao::utm::ComplianceRuntime& compliance, bool& valid) noexcept;

private:
    dao::protocol::ProtocolServer server_;
};
