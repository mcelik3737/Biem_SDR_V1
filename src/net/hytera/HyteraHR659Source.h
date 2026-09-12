#pragma once

#include <cstdint>
#include <string>

#include "../INetworkIngestSource.h"
#include "../UdpRawLogger.h"

namespace biem::net::hytera {

// STUB - see docs/HYTERA_HR659.md. Does not parse anything yet: it runs a
// UdpRawLogger against the configured port so real HR659 traffic can be
// captured (hex + raw) for protocol reverse-engineering, while already
// implementing INetworkIngestSource so it plugs into the same pipeline
// (CallRecorder/Database/search UI) as the SDR/DMR path the moment
// onPacket() actually parses something.
class HyteraHR659Source : public INetworkIngestSource {
public:
    HyteraHR659Source(uint16_t udpPort, std::string captureDir, std::string channelLabel);

    bool start() override;
    void stop() override;
    bool isRunning() const override;

    void setCallStartCallback(CallStartCallback cb) override { callStartCb_ = std::move(cb); }
    void setAudioCallback(AudioCallback cb) override { audioCb_ = std::move(cb); }
    void setCallEndCallback(CallEndCallback cb) override { callEndCb_ = std::move(cb); }

private:
    uint16_t udpPort_;
    std::string channelLabel_;
    UdpRawLogger logger_;

    CallStartCallback callStartCb_;
    AudioCallback audioCb_;
    CallEndCallback callEndCb_;

    void onPacket(const RawPacket& pkt);
};

} // namespace biem::net::hytera
