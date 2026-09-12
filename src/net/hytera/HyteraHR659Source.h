#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "../../core/CallRecord.h"
#include "../INetworkIngestSource.h"
#include "../UdpRawLogger.h"

namespace biem::net::hytera {

// See docs/HYTERA_HR659.md for the full writeup - short version here:
//
// This is a BEST-EFFORT parser built from published reverse-engineering of
// Hytera's "IP Multi Site Connect" protocol (OpenIPSC project,
// github.com/gopher2/OpenIPSC, README.hytera.md), NOT from an HR659
// datasheet, an official Hytera protocol document, or a real packet
// capture - the user has none of those available yet ("elimde paket yok").
// Two distinct kinds of claim are mixed together below, and the comments
// try hard to keep them visually separate:
//
//  - CONFIRMED (per OpenIPSC): three UDP packet types exist (voice/service,
//    sync, RDAC), each on its own port; a service/voice packet carries a
//    source radio ID and destination group ID (both 24-bit), a TDMA slot
//    marker, a call-type (private/group) indicator, a 6-byte DMR sync, and
//    two 13.5-byte AMBE+2 voice segments.
//  - ASSUMED BY US (unconfirmed): the exact byte OFFSET of every field
//    below, and the exact UDP port HR659 uses for it. OpenIPSC documents
//    field existence/type/size, not a byte-for-byte struct layout, and
//    HR659 is a compact standalone repeater, not necessarily configured
//    for multi-site linking - it may speak a related-but-different local
//    dialect of this protocol, or something else entirely.
//
// Every offset assumption lives in one place (the kFieldOffset* constants
// in the .cpp) specifically so a real capture can correct them with a
// small, localized edit instead of a rewrite. Every parse is defensively
// bounds-checked - a wrong guess must never crash the ingest pipeline,
// only fail to extract fields. Every packet, parsed or not, is logged with
// a one-line diagnostic (type byte + length + whatever was extracted) so
// running this against a real repeater immediately shows whether the
// guesses are in the right neighborhood.
//
// Voice payload is captured for future use but never decoded (AMBE+2 is
// patented - see IVoiceDecoder.h / docs/DMR_NOTES.md); calls are logged as
// metadata-only (CallRecord::voiceDecoded = false), same as the RF DMR
// path today.
class HyteraHR659Source : public INetworkIngestSource {
public:
    HyteraHR659Source(uint16_t udpPort, std::string captureDir, std::string channelLabel);

    bool start() override;
    void stop() override;
    bool isRunning() const override;

    void setCallStartCallback(CallStartCallback cb) override { callStartCb_ = std::move(cb); }
    void setAudioCallback(AudioCallback cb) override { audioCb_ = std::move(cb); }
    void setCallEndCallback(CallEndCallback cb) override { callEndCb_ = std::move(cb); }

    // Public (not just wired internally to UdpRawLogger) so it can be fed
    // directly - by tests/test_hytera_hr659.cpp with a synthetic packet
    // matching the assumed layout, or later by an offline tool replaying a
    // real .raw capture through the exact same parsing code path used
    // live. Safe to call with arbitrary/malformed data: every field read
    // is bounds-checked (see the .cpp).
    void onPacket(const RawPacket& pkt);

private:
    uint16_t udpPort_;
    std::string channelLabel_;
    UdpRawLogger logger_;

    CallStartCallback callStartCb_;
    AudioCallback audioCb_;
    CallEndCallback callEndCb_;

    // Tracks whether we currently believe a call is in progress, so a
    // voice-frame packet either starts a new call or continues one, and an
    // end-transmission packet (or a voice frame for a different radio/TG)
    // closes it. See docs/HYTERA_HR659.md - this state machine itself is a
    // reasonable, low-risk assumption (packetized voice protocols
    // universally look like this); it's the FIELD OFFSETS feeding it that
    // are unconfirmed.
    bool callActive_ = false;
    std::optional<uint32_t> activeRadioId_;
    std::optional<uint32_t> activeDestField_;
    bool activeIsPrivate_ = false;

    void handleVoiceFrame(const RawPacket& pkt);
    void handleEndTransmission(const RawPacket& pkt);
    void endActiveCallIfAny();
};

} // namespace biem::net::hytera
