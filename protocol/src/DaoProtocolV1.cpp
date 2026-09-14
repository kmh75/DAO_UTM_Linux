#include "DaoProtocolV1.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>

namespace dao::protocol
{
namespace
{
void put16(std::uint8_t* p,std::uint16_t v){p[0]=v>>8;p[1]=v;}
void put32(std::uint8_t* p,std::uint32_t v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
void put64(std::uint8_t* p,std::uint64_t v){for(int i=7;i>=0;--i){p[i]=v;v>>=8;}}
std::uint16_t get16(const std::uint8_t*p){return std::uint16_t(p[0])<<8|p[1];}
std::uint32_t get32(const std::uint8_t*p){return std::uint32_t(p[0])<<24|std::uint32_t(p[1])<<16|std::uint32_t(p[2])<<8|p[3];}
std::uint64_t get64(const std::uint8_t*p){std::uint64_t v=0;for(int i=0;i<8;++i)v=(v<<8)|p[i];return v;}
void putDouble(std::uint8_t*p,double d){std::uint64_t v;std::memcpy(&v,&d,8);put64(p,v);}
double getDouble(const std::uint8_t*p){auto v=get64(p);double d;std::memcpy(&d,&v,8);return d;}
}
bool EncodeHeader(const Header& h,std::array<std::uint8_t,kHeaderSize>& o) noexcept
{
    if(h.payloadLength>kMaxPayload)return false;
    put32(o.data(),h.magic);put16(o.data()+4,h.version);put16(o.data()+6,static_cast<std::uint16_t>(h.type));
    put32(o.data()+8,h.flags);put32(o.data()+12,h.payloadLength);put32(o.data()+16,h.sequenceNumber);put32(o.data()+20,h.requestId);return true;
}
bool DecodeHeader(const std::uint8_t*d,std::size_t n,Header& h) noexcept
{
    if(!d||n<kHeaderSize)return false;h.magic=get32(d);h.version=get16(d+4);h.type=static_cast<MessageType>(get16(d+6));h.flags=get32(d+8);h.payloadLength=get32(d+12);h.sequenceNumber=get32(d+16);h.requestId=get32(d+20);
    return h.magic==kMagic&&h.payloadLength<=kMaxPayload;
}
std::vector<std::uint8_t> EncodeFrame(const Frame& f)
{
    Header h=f.header;h.payloadLength=static_cast<std::uint32_t>(f.payload.size());std::array<std::uint8_t,kHeaderSize>b{};
    if(f.payload.size()>kMaxPayload||!EncodeHeader(h,b))return {};std::vector<std::uint8_t> out(b.begin(),b.end());out.insert(out.end(),f.payload.begin(),f.payload.end());return out;
}
StreamDecoder::Result StreamDecoder::Feed(const std::uint8_t*d,std::size_t n,std::vector<Frame>& frames)
{
    if(malformed_||(!d&&n)){malformed_=true;return Result::Malformed;}buffer_.insert(buffer_.end(),d,d+n);
    while(buffer_.size()>=kHeaderSize){Header h;if(!DecodeHeader(buffer_.data(),buffer_.size(),h)){malformed_=true;return Result::Malformed;}const std::size_t total=kHeaderSize+h.payloadLength;if(buffer_.size()<total)break;Frame f;f.header=h;f.payload.assign(buffer_.begin()+kHeaderSize,buffer_.begin()+total);frames.push_back(std::move(f));buffer_.erase(buffer_.begin(),buffer_.begin()+total);}return Result::Ok;
}
std::vector<std::uint8_t> EncodeLiveData(const LiveData& v)
{
    std::vector<std::uint8_t> b(76);put64(&b[0],v.timestampUs);put64(&b[8],v.testId);putDouble(&b[16],v.forceN);putDouble(&b[24],v.rawDisplacementMm);putDouble(&b[32],v.complianceCompensationMm);putDouble(&b[40],v.correctedDisplacementMm);putDouble(&b[48],v.extensometerMm);put32(&b[56],v.machineState);put32(&b[60],v.sequenceState);put32(&b[64],v.currentStep);put32(&b[68],v.testRunning?1:0);put32(&b[72],v.sampleFlags);return b;
}
bool DecodeLiveData(const std::vector<std::uint8_t>&b,LiveData&v) noexcept
{if(b.size()!=76)return false;v.timestampUs=get64(&b[0]);v.testId=get64(&b[8]);v.forceN=getDouble(&b[16]);v.rawDisplacementMm=getDouble(&b[24]);v.complianceCompensationMm=getDouble(&b[32]);v.correctedDisplacementMm=getDouble(&b[40]);v.extensometerMm=getDouble(&b[48]);v.machineState=get32(&b[56]);v.sequenceState=get32(&b[60]);v.currentStep=get32(&b[64]);v.testRunning=get32(&b[68])!=0;v.sampleFlags=get32(&b[72]);return true;}
std::vector<std::uint8_t> EncodeMachineStatus(const MachineStatus&v){std::vector<std::uint8_t>b(24);put64(&b[0],v.testId);put32(&b[8],v.machineState);put32(&b[12],v.sequenceState);put32(&b[16],v.currentStep);put32(&b[20],v.testRunning?1:0);return b;}
bool DecodeMachineStatus(const std::vector<std::uint8_t>&b,MachineStatus&v) noexcept{if(b.size()!=24)return false;v.testId=get64(&b[0]);v.machineState=get32(&b[8]);v.sequenceState=get32(&b[12]);v.currentStep=get32(&b[16]);v.testRunning=get32(&b[20])!=0;return true;}
std::vector<std::uint8_t> EncodeTestDataChunk(const TestDataChunk&v){if(v.bytes.size()>kMaxPayload-16)return{};std::vector<std::uint8_t>b(16+v.bytes.size());put64(&b[0],v.testId);put32(&b[8],v.chunkIndex);put32(&b[12],static_cast<std::uint32_t>(v.bytes.size()));std::copy(v.bytes.begin(),v.bytes.end(),b.begin()+16);return b;}
bool DecodeTestDataChunk(const std::vector<std::uint8_t>&b,TestDataChunk&v){if(b.size()<16||get32(&b[12])!=b.size()-16)return false;v.testId=get64(&b[0]);v.chunkIndex=get32(&b[8]);v.bytes.assign(b.begin()+16,b.end());return true;}
}
