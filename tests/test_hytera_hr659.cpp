// Unit tests for HyteraHR659Source::onPacket() against SYNTHETIC packets
// built to match this file's own assumed byte layout (see the header
// comment on HyteraHR659Source and docs/HYTERA_HR659.md). This proves the
// parsing code is internally consistent and doesn't crash on edge cases -
// it does NOT and CANNOT prove the assumed layout matches what a real
// HR659 actually sends, since no real capture exists yet. That
// verification can only happen once real traffic is available.
#include "test_util.h"

#include <cstdint>
#include <vector>

#include "../src/net/hytera/HyteraHR659Source.h"

using namespace biem::net;
using namespace biem::net::hytera;

namespace {

// Builds a synthetic voice-frame packet matching the ASSUMED layout in
// HyteraHR659Source.cpp: [type=0x01][radioId 24-bit LE][dest 24-bit LE]
// [callType][slotMarker 16-bit LE].
std::vector<uint8_t> makeVoiceFrame(uint32_t radioId, uint32_t dest, uint8_t callType, uint16_t slotMarker) {
    std::vector<uint8_t> pkt(10, 0);
    pkt[0] = 0x01;
    pkt[1] = static_cast<uint8_t>(radioId & 0xFF);
    pkt[2] = static_cast<uint8_t>((radioId >> 8) & 0xFF);
    pkt[3] = static_cast<uint8_t>((radioId >> 16) & 0xFF);
    pkt[4] = static_cast<uint8_t>(dest & 0xFF);
    pkt[5] = static_cast<uint8_t>((dest >> 8) & 0xFF);
    pkt[6] = static_cast<uint8_t>((dest >> 16) & 0xFF);
    pkt[7] = callType;
    pkt[8] = static_cast<uint8_t>(slotMarker & 0xFF);
    pkt[9] = static_cast<uint8_t>((slotMarker >> 8) & 0xFF);
    return pkt;
}

RawPacket toRawPacket(const std::vector<uint8_t>& buf) {
    RawPacket pkt;
    pkt.unixTimeMs = 1000;
    pkt.sourceIp = "127.0.0.1";
    pkt.sourcePort = 50000;
    pkt.data = buf.data();
    pkt.length = buf.size();
    return pkt;
}

void testGroupCallStartAndContinue() {
    HyteraHR659Source src(0, "/tmp/biem_test_hytera_unused", "Test Kanal");

    int startCount = 0, endCount = 0;
    biem::core::CallRecord lastMeta;
    src.setCallStartCallback([&](biem::core::CallRecord meta) {
        ++startCount;
        lastMeta = meta;
    });
    src.setCallEndCallback([&]() { ++endCount; });

    auto frame = makeVoiceFrame(/*radioId=*/1234567, /*dest=*/100, /*callType=*/0x00,
                                 /*slotMarker=*/0x1111);
    RawPacket pkt = toRawPacket(frame);

    src.onPacket(pkt);
    BIEM_CHECK(startCount == 1);
    BIEM_CHECK(endCount == 0);
    BIEM_CHECK(lastMeta.radioId.has_value() && *lastMeta.radioId == 1234567u);
    BIEM_CHECK(lastMeta.talkgroupId.has_value() && *lastMeta.talkgroupId == 100u);
    BIEM_CHECK(!lastMeta.destRadioId.has_value()); // group call - no dest radio
    BIEM_CHECK(lastMeta.slot.has_value() && *lastMeta.slot == 1);
    BIEM_CHECK(lastMeta.source == biem::core::CallSource::NetworkRepeater);
    BIEM_CHECK(lastMeta.modulation == biem::core::Modulation::DmrDigital);
    BIEM_CHECK(lastMeta.channelLabel == "Test Kanal");
    BIEM_CHECK(!lastMeta.voiceDecoded); // AMBE+2 not decoded

    // Same radio/TG again (as if a second voice frame of the same
    // transmission arrived) - must NOT start a second call.
    src.onPacket(pkt);
    BIEM_CHECK(startCount == 1);
    BIEM_CHECK(endCount == 0);
}

void testEndTransmissionClosesCall() {
    HyteraHR659Source src(0, "/tmp/biem_test_hytera_unused", "Test Kanal");
    int startCount = 0, endCount = 0;
    src.setCallStartCallback([&](biem::core::CallRecord) { ++startCount; });
    src.setCallEndCallback([&]() { ++endCount; });

    auto frame = makeVoiceFrame(1234567, 100, 0x00, 0x1111);
    RawPacket voicePkt = toRawPacket(frame);
    src.onPacket(voicePkt);
    BIEM_CHECK(startCount == 1);
    BIEM_CHECK(endCount == 0);

    std::vector<uint8_t> endFrame{0x03};
    RawPacket endPkt = toRawPacket(endFrame);
    src.onPacket(endPkt);
    BIEM_CHECK(endCount == 1);

    // A second end-transmission with no active call must be a no-op, not
    // an extra callEndCb_ invocation.
    src.onPacket(endPkt);
    BIEM_CHECK(endCount == 1);
}

void testNewCallImplicitlyEndsPrevious() {
    HyteraHR659Source src(0, "/tmp/biem_test_hytera_unused", "Test Kanal");
    int startCount = 0, endCount = 0;
    src.setCallStartCallback([&](biem::core::CallRecord) { ++startCount; });
    src.setCallEndCallback([&]() { ++endCount; });

    auto frameA = makeVoiceFrame(1234567, 100, 0x00, 0x1111);
    RawPacket pktA = toRawPacket(frameA);
    src.onPacket(pktA);
    BIEM_CHECK(startCount == 1);
    BIEM_CHECK(endCount == 0);

    // Different radio+TG, no end-transmission in between - must close A
    // and open B (real repeater traffic can plausibly drop/reorder an
    // end-transmission packet; the ingest must not get stuck on a stale
    // "call active" state because of that).
    auto frameB = makeVoiceFrame(7654321, 200, 0x00, 0x2222);
    RawPacket pktB = toRawPacket(frameB);
    src.onPacket(pktB);
    BIEM_CHECK(startCount == 2);
    BIEM_CHECK(endCount == 1);
}

void testPrivateCallUsesDestRadioId() {
    HyteraHR659Source src(0, "/tmp/biem_test_hytera_unused", "Test Kanal");
    biem::core::CallRecord lastMeta;
    src.setCallStartCallback([&](biem::core::CallRecord meta) { lastMeta = meta; });
    src.setCallEndCallback([]() {});

    auto frame = makeVoiceFrame(1234567, 555, /*callType=*/0x01 /*private-guess*/, 0x2222);
    RawPacket pkt = toRawPacket(frame);
    src.onPacket(pkt);

    BIEM_CHECK(lastMeta.radioId.has_value() && *lastMeta.radioId == 1234567u);
    BIEM_CHECK(!lastMeta.talkgroupId.has_value()); // private call - no talkgroup
    BIEM_CHECK(lastMeta.destRadioId.has_value() && *lastMeta.destRadioId == 555u);
    BIEM_CHECK(lastMeta.slot.has_value() && *lastMeta.slot == 2);
}

void testUnrecognizedSlotMarkerLeavesSlotUnset() {
    HyteraHR659Source src(0, "/tmp/biem_test_hytera_unused", "Test Kanal");
    biem::core::CallRecord lastMeta;
    src.setCallStartCallback([&](biem::core::CallRecord meta) { lastMeta = meta; });

    // 0x3333 matches neither guessed marker (0x1111 / 0x2222).
    auto frame = makeVoiceFrame(1234567, 100, 0x00, 0x3333);
    RawPacket pkt = toRawPacket(frame);
    src.onPacket(pkt);

    BIEM_CHECK(!lastMeta.slot.has_value());
    // Radio/TG extraction is independent of the slot marker and should
    // still succeed.
    BIEM_CHECK(lastMeta.radioId.has_value() && *lastMeta.radioId == 1234567u);
}

void testMalformedPacketsDoNotCrashOrFireCallbacks() {
    HyteraHR659Source src(0, "/tmp/biem_test_hytera_unused", "Test Kanal");
    int startCount = 0, endCount = 0;
    src.setCallStartCallback([&](biem::core::CallRecord) { ++startCount; });
    src.setCallEndCallback([&]() { ++endCount; });

    // Empty packet.
    std::vector<uint8_t> empty;
    RawPacket emptyPkt = toRawPacket(empty);
    src.onPacket(emptyPkt);

    // Voice-frame type byte but far too short to hold any fields.
    std::vector<uint8_t> tooShort{0x01, 0xAA, 0xBB};
    RawPacket shortPkt = toRawPacket(tooShort);
    src.onPacket(shortPkt);

    // Unrecognized type byte.
    std::vector<uint8_t> unknown{0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    RawPacket unknownPkt = toRawPacket(unknown);
    src.onPacket(unknownPkt);

    BIEM_CHECK(startCount == 0);
    BIEM_CHECK(endCount == 0);
}

} // namespace

int main() {
    testGroupCallStartAndContinue();
    testEndTransmissionClosesCall();
    testNewCallImplicitlyEndsPrevious();
    testPrivateCallUsesDestRadioId();
    testUnrecognizedSlotMarkerLeavesSlotUnset();
    testMalformedPacketsDoNotCrashOrFireCallbacks();
    BIEM_TEST_MAIN_RETURN();
}
