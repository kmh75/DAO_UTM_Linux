#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dao::protocol
{
constexpr std::uint32_t kMagic = 0x44415554U; // "DAUT"
constexpr std::uint16_t kVersion = 1;
constexpr std::uint16_t kDefaultPort = 45550;
constexpr std::uint32_t kMaxPayload = 1024U * 1024U;
constexpr std::size_t kHeaderSize = 24;

enum class MessageType : std::uint16_t
{
    Hello=1, HelloAck=2, Heartbeat=3,
    MachineStatus=10, LiveData=11,
    LoadRecipe=20, CommitRecipe=21, StartTest=22, StopTest=23, AckReset=24,
    CommandAck=30, CommandNack=31,
    TestStarted=40, StepChanged=41, TestComplete=42, TestAborted=43, FaultEvent=44,
    GetTestData=50, TestDataBegin=51, TestDataChunk=52, TestDataEnd=53
};

enum class NackReason : std::uint16_t
{
    None=0, Malformed=1, VersionMismatch=2, UnsupportedMessage=3,
    InvalidState=4, MachineNotReady=5, RecipeInvalid=6, Busy=7
};

struct Header
{
    std::uint32_t magic = kMagic;
    std::uint16_t version = kVersion;
    MessageType type = MessageType::Heartbeat;
    std::uint32_t flags = 0;
    std::uint32_t payloadLength = 0;
    std::uint32_t sequenceNumber = 0;
    std::uint32_t requestId = 0;
};

struct Frame { Header header{}; std::vector<std::uint8_t> payload; };

bool EncodeHeader(const Header& header, std::array<std::uint8_t,kHeaderSize>& output) noexcept;
bool DecodeHeader(const std::uint8_t* data, std::size_t size, Header& output) noexcept;
std::vector<std::uint8_t> EncodeFrame(const Frame& frame);

class StreamDecoder
{
public:
    enum class Result { Ok, Malformed };
    Result Feed(const std::uint8_t* data, std::size_t size, std::vector<Frame>& frames);
    void Reset() { buffer_.clear(); malformed_ = false; }
private:
    std::vector<std::uint8_t> buffer_;
    bool malformed_ = false;
};

struct LiveData
{
    std::uint64_t timestampUs = 0;
    std::uint64_t testId = 0;
    double forceN = 0.0;
    double rawDisplacementMm = 0.0;
    double complianceCompensationMm = 0.0;
    double correctedDisplacementMm = 0.0;
    double extensometerMm = 0.0;
    std::uint32_t machineState = 0;
    std::uint32_t sequenceState = 0;
    std::uint32_t currentStep = 0;
    bool testRunning = false;
    std::uint32_t sampleFlags = 0;
};

struct MachineStatus
{
    std::uint64_t testId = 0;
    std::uint32_t machineState = 0;
    std::uint32_t sequenceState = 0;
    std::uint32_t currentStep = 0;
    bool testRunning = false;
};

struct TestDataChunk
{
    std::uint64_t testId = 0;
    std::uint32_t chunkIndex = 0;
    std::vector<std::uint8_t> bytes;
};

// Future additive metadata extension; not serialized into V1 LiveData.
struct ComplianceMetadata
{
    std::uint32_t mode = 0;
    std::uint32_t compressionCurveVersion = 0;
    std::uint32_t tensionCurveVersion = 0;
    std::uint32_t activeCurveVersion = 0;
    bool enabled = false;
};

std::vector<std::uint8_t> EncodeLiveData(const LiveData& value);
bool DecodeLiveData(const std::vector<std::uint8_t>& data, LiveData& value) noexcept;
std::vector<std::uint8_t> EncodeMachineStatus(const MachineStatus& value);
bool DecodeMachineStatus(const std::vector<std::uint8_t>& data, MachineStatus& value) noexcept;
std::vector<std::uint8_t> EncodeTestDataChunk(const TestDataChunk& value);
bool DecodeTestDataChunk(const std::vector<std::uint8_t>& data, TestDataChunk& value);

// Lock-free publication: writer never waits for the protocol reader. T must be trivially copyable.
template<class T> class SnapshotMailbox
{
public:
    void Publish(const T& value) noexcept
    {
        const unsigned next = (published_.load(std::memory_order_relaxed) + 1U) & 1U;
        slots_[next] = value;
        published_.store(next, std::memory_order_release);
    }
    T Read() const noexcept { return slots_[published_.load(std::memory_order_acquire)]; }
private:
    std::array<T,2> slots_{};
    std::atomic<unsigned> published_{0};
};
}
