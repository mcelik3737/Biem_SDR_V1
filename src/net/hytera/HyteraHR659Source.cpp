#include "HyteraHR659Source.h"

#include <cstddef>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace biem::net::hytera {

namespace {

// --- CONFIRMED (OpenIPSC, github.com/gopher2/OpenIPSC, README.hytera.md):
// Hytera's "IP Multi Site Connect" protocol uses a leading packet-type
// byte with (at least) these three values. Their exact numeric position
// (offset 0) is OUR assumption - a leading discriminator byte is close to
// universal for this style of framed protocol, but it is not something
// OpenIPSC's description pinned down explicitly.
constexpr size_t kOffsetPacketType = 0;
constexpr uint8_t kPacketTypeVoiceFrame = 0x01;
constexpr uint8_t kPacketTypeSyncFrame = 0x02;
constexpr uint8_t kPacketTypeEndTransmission = 0x03;

// --- ASSUMED BY US, NOT CONFIRMED: everything below this line is our own
// placement of OpenIPSC's documented fields (source radio ID, destination
// group ID, slot marker, call type, sync, voice payload - all confirmed to
// EXIST with these types/sizes) into concrete byte offsets, which OpenIPSC
// does not specify. Treat these as a first hypothesis to be corrected the
// moment a real HR659 capture is available (see docs/HYTERA_HR659.md) -
// NOT as a verified struct layout. Every read against these offsets is
// bounds-checked so a wrong guess degrades to "field not extracted", never
// to undefined behavior.
constexpr size_t kOffsetSourceRadioId = 1;   // 3 bytes, 24-bit little-endian
constexpr size_t kOffsetDestGroupId = 4;     // 3 bytes, 24-bit little-endian
constexpr size_t kOffsetCallType = 7;        // 1 byte - guessed encoding, see below
constexpr size_t kOffsetSlotMarker = 8;      // 2 bytes little-endian - see below
constexpr size_t kMinVoiceFrameLength = 10;  // through the end of the slot marker

// Guessed encoding: 0 = group call (destination field is a talkgroup), 1 =
// private call (destination field is a radio ID instead) - mirrors how
// DMR's own Full Link Control reuses one "destination" field for both call
// types (see dsp/dmr/DmrLinkControl.*), which is at least a plausible
// convention, but this specific byte's meaning has no direct OpenIPSC
// citation - lowest-confidence guess in this file.
constexpr uint8_t kCallTypePrivateGuess = 0x01;

uint32_t readLe24(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16);
}

uint16_t readLe16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

std::string hexPreview(const uint8_t* data, size_t length, size_t maxBytes = 24) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (size_t i = 0; i < length && i < maxBytes; ++i) {
        out << std::setw(2) << static_cast<unsigned>(data[i]) << ' ';
    }
    if (length > maxBytes) out << "...";
    return out.str();
}

} // namespace

HyteraHR659Source::HyteraHR659Source(uint16_t udpPort, std::string captureDir, std::string channelLabel)
    : udpPort_(udpPort), channelLabel_(std::move(channelLabel)), logger_(udpPort, std::move(captureDir)) {
    logger_.setPacketCallback([this](const RawPacket& pkt) { onPacket(pkt); });
}

bool HyteraHR659Source::start() {
    return logger_.start();
}

void HyteraHR659Source::stop() {
    logger_.stop();
    endActiveCallIfAny();
}

bool HyteraHR659Source::isRunning() const {
    return logger_.isRunning();
}

void HyteraHR659Source::endActiveCallIfAny() {
    if (callActive_) {
        callActive_ = false;
        activeRadioId_.reset();
        activeDestField_.reset();
        activeIsPrivate_ = false;
        if (callEndCb_) callEndCb_();
    }
}

void HyteraHR659Source::onPacket(const RawPacket& pkt) {
    // UdpRawLogger already wrote this packet to disk (hex + raw) before
    // this callback fires - see docs/HYTERA_HR659.md. Everything from here
    // down is best-effort parsing on top of that unconditional capture, so
    // even a completely wrong guess loses nothing: the raw bytes are safe
    // on disk regardless.
    if (pkt.length <= kOffsetPacketType) {
        std::cerr << "[hytera-hr659] bos/cok kisa paket (" << pkt.length << " byte), atlaniyor\n";
        return;
    }

    uint8_t type = pkt.data[kOffsetPacketType];
    switch (type) {
        case kPacketTypeVoiceFrame:
            handleVoiceFrame(pkt);
            break;
        case kPacketTypeSyncFrame:
            // Per OpenIPSC, sync frames are pure DMR sync timing, no radio
            // ID/TG in their structure - nothing to extract, just log.
            std::cerr << "[hytera-hr659] sync-frame (0x02), " << pkt.length << " byte\n";
            break;
        case kPacketTypeEndTransmission:
            handleEndTransmission(pkt);
            break;
        default:
            std::cerr << "[hytera-hr659] bilinmeyen paket tipi 0x" << std::hex << std::setfill('0')
                       << std::setw(2) << static_cast<unsigned>(type) << std::dec << " (" << pkt.length
                       << " byte): " << hexPreview(pkt.data, pkt.length) << "\n";
            break;
    }
}

void HyteraHR659Source::handleVoiceFrame(const RawPacket& pkt) {
    if (pkt.length < kMinVoiceFrameLength) {
        std::cerr << "[hytera-hr659] voice-frame (0x01) ama beklenenden kisa (" << pkt.length << " < "
                   << kMinVoiceFrameLength << " byte varsayilan minimum) - alan cikarilamiyor: "
                   << hexPreview(pkt.data, pkt.length) << "\n";
        return;
    }

    uint32_t sourceRadioId = readLe24(pkt.data + kOffsetSourceRadioId);
    uint32_t destField = readLe24(pkt.data + kOffsetDestGroupId);
    uint8_t callType = pkt.data[kOffsetCallType];
    uint16_t slotMarker = readLe16(pkt.data + kOffsetSlotMarker);

    std::optional<int> slot;
    if (slotMarker == 0x1111) {
        slot = 1;
    } else if (slotMarker == 0x2222) {
        slot = 2;
    }
    // else: doesn't match either guessed marker value - left unset rather
    // than guessing further; see the diagnostic line below for the raw
    // value actually seen, which is what a real capture needs to confirm.

    bool isPrivateCall = (callType == kCallTypePrivateGuess);

    std::cerr << "[hytera-hr659] voice-frame: radioId=" << sourceRadioId << " dest=" << destField
              << " (" << (isPrivateCall ? "private-guess" : "group-guess") << ") slotMarker=0x"
              << std::hex << std::setfill('0') << std::setw(4) << slotMarker << std::dec
              << " slot=" << (slot ? std::to_string(*slot) : std::string("?")) << " len=" << pkt.length
              << " kaynak=" << pkt.sourceIp << ":" << pkt.sourcePort << "\n";

    bool isNewCall = !callActive_ || activeRadioId_ != sourceRadioId ||
                      activeIsPrivate_ != isPrivateCall || activeDestField_ != destField;
    if (isNewCall) {
        endActiveCallIfAny();

        core::CallRecord meta;
        meta.startUnixTimeMs = pkt.unixTimeMs;
        meta.modulation = core::Modulation::DmrDigital;
        meta.source = core::CallSource::NetworkRepeater;
        meta.channelLabel = channelLabel_;
        meta.slot = slot;
        meta.radioId = sourceRadioId;
        if (isPrivateCall) {
            meta.destRadioId = destField;
        } else {
            meta.talkgroupId = destField;
        }
        meta.voiceDecoded = false; // AMBE+2 not decoded - see IVoiceDecoder.h

        callActive_ = true;
        activeRadioId_ = sourceRadioId;
        activeDestField_ = destField;
        activeIsPrivate_ = isPrivateCall;

        if (callStartCb_) callStartCb_(meta);
    }

    // The two 13.5-byte AMBE+2 segments (per OpenIPSC) would follow the
    // header fields above; we don't know their exact offset any more
    // precisely than the rest of this layout, and there is no decoder to
    // hand them to yet regardless (see class comment) - so audioCb_ is
    // intentionally never invoked here. The call is still logged
    // (voiceDecoded=false), matching the RF DMR path's current behavior.
}

void HyteraHR659Source::handleEndTransmission(const RawPacket& pkt) {
    std::cerr << "[hytera-hr659] end-transmission (0x03), " << pkt.length << " byte\n";
    endActiveCallIfAny();
}

} // namespace biem::net::hytera
