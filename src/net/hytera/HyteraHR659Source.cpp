#include "HyteraHR659Source.h"

namespace biem::net::hytera {

HyteraHR659Source::HyteraHR659Source(uint16_t udpPort, std::string captureDir, std::string channelLabel)
    : udpPort_(udpPort), channelLabel_(std::move(channelLabel)), logger_(udpPort, std::move(captureDir)) {
    logger_.setPacketCallback([this](const RawPacket& pkt) { onPacket(pkt); });
}

bool HyteraHR659Source::start() {
    return logger_.start();
}

void HyteraHR659Source::stop() {
    logger_.stop();
}

bool HyteraHR659Source::isRunning() const {
    return logger_.isRunning();
}

void HyteraHR659Source::onPacket(const RawPacket& pkt) {
    // TODO (see docs/HYTERA_HR659.md): once the real HR659 UDP packet
    // format is known, parse Radio ID / TG(Grup) / Slot / GPS / SDS /
    // audio payload here and invoke callStartCb_ / audioCb_ / callEndCb_
    // accordingly. The rest of the app doesn't care whether a call came
    // from here or from the SDR/DMR path - same CallRecord, same
    // CallRecorder, same Database.
    //
    // For now every packet is already captured to disk by UdpRawLogger
    // (hex + raw framing) via the callback wired up in the constructor -
    // nothing else happens here yet.
    (void)pkt;
}

} // namespace biem::net::hytera
