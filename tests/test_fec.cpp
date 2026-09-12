// Self-consistency tests for the DMR FEC primitives. IMPORTANT: these
// prove each class's encode()/decode() agree with EACH OTHER and correct
// injected errors as designed - they do NOT prove conformance to the real
// DMR spec (that needs either a byte-exact reference diff or a real
// captured signal - see docs/DMR_NOTES.md).
#include "test_util.h"

#include <array>
#include <cstdint>

#include "../src/dsp/dmr/Bptc196x96.h"
#include "../src/dsp/dmr/DmrConstants.h"
#include "../src/dsp/dmr/DmrFrameSync.h"
#include "../src/dsp/dmr/DmrLinkControl.h"
#include "../src/dsp/dmr/Golay2087.h"
#include "../src/dsp/dmr/Hamming139.h"
#include "../src/dsp/dmr/Hamming1511.h"

using namespace biem::dsp::dmr;

namespace {

void testHamming1511RoundTrip() {
    for (int trial = 0; trial < 64; ++trial) {
        std::array<uint8_t, 11> data{};
        for (int i = 0; i < 11; ++i) data[static_cast<size_t>(i)] = static_cast<uint8_t>((trial >> i) & 1);
        auto cw = Hamming1511::encode(data);

        std::array<uint8_t, 11> decoded{};
        bool corrected = Hamming1511::decode(cw, decoded);
        BIEM_CHECK(!corrected);
        BIEM_CHECK(decoded == data);

        for (int flip = 0; flip < 15; ++flip) {
            auto cwErr = cw;
            cwErr[static_cast<size_t>(flip)] ^= 1;
            std::array<uint8_t, 11> decodedErr{};
            bool correctedErr = Hamming1511::decode(cwErr, decodedErr);
            BIEM_CHECK(correctedErr);
            BIEM_CHECK(decodedErr == data);
        }
    }
}

void testHamming139RoundTrip() {
    for (int trial = 0; trial < 32; ++trial) {
        std::array<uint8_t, 9> data{};
        for (int i = 0; i < 9; ++i) data[static_cast<size_t>(i)] = static_cast<uint8_t>((trial >> i) & 1);
        auto cw = Hamming139::encode(data);

        std::array<uint8_t, 9> decoded{};
        BIEM_CHECK(!Hamming139::decode(cw, decoded));
        BIEM_CHECK(decoded == data);

        for (int flip = 0; flip < 13; ++flip) {
            auto cwErr = cw;
            cwErr[static_cast<size_t>(flip)] ^= 1;
            std::array<uint8_t, 9> decodedErr{};
            BIEM_CHECK(Hamming139::decode(cwErr, decodedErr));
            BIEM_CHECK(decodedErr == data);
        }
    }
}

void testGolay2087RoundTrip() {
    Golay2087 golay;
    for (int value = 0; value < 256; ++value) {
        uint32_t cw = golay.encode(static_cast<uint8_t>(value));

        int corrected = -2;
        uint8_t decoded = golay.decode(cw, &corrected);
        BIEM_CHECK(decoded == static_cast<uint8_t>(value));
        BIEM_CHECK(corrected == 0);

        for (int flip = 0; flip < Golay2087::kCodewordBits; ++flip) {
            uint32_t cwErr = cw ^ (1u << flip);
            int correctedErr = -2;
            uint8_t decodedErr = golay.decode(cwErr, &correctedErr);
            BIEM_CHECK(decodedErr == static_cast<uint8_t>(value));
            BIEM_CHECK(correctedErr == 1);
        }
    }
}

void testBptcInterleaveIsPermutation() {
    std::array<uint8_t, 196> input{};
    for (int i = 0; i < 196; ++i) input[static_cast<size_t>(i)] = static_cast<uint8_t>(i & 1);
    auto interleaved = Bptc196x96::interleave(input);
    auto roundTripped = Bptc196x96::deinterleave(interleaved);
    BIEM_CHECK(roundTripped == input);
}

void testBptcRoundTrip() {
    for (int trial = 0; trial < 20; ++trial) {
        std::array<uint8_t, 96> payload{};
        for (int i = 0; i < 96; ++i) {
            payload[static_cast<size_t>(i)] = static_cast<uint8_t>((trial * 7 + i * 13) % 2);
        }
        auto raw = Bptc196x96::encode(payload);
        std::array<uint8_t, 96> decoded{};
        bool ok = Bptc196x96::decode(raw, decoded);
        BIEM_CHECK(ok);
        BIEM_CHECK(decoded == payload);
    }
}

void testDmrFrameSyncDetectsExactPattern() {
    for (const auto& entry : kSyncPatterns) {
        DmrFrameSync sync;
        bool detected = false;
        SyncType detectedType = SyncType::Unknown;
        sync.setSyncCallback([&](SyncDetection d) {
            detected = true;
            detectedType = d.type;
        });
        for (int i = kSyncPatternBits - 1; i >= 0; --i) {
            sync.pushBit(static_cast<uint8_t>((entry.pattern >> i) & 1u));
        }
        BIEM_CHECK(detected);
        BIEM_CHECK(detectedType == entry.type);
    }
}

// DmrLinkControl's own bit-packing is simple and independent of the
// upstream open questions (Bptc196x96/DmrSlotDecoder) - this proves IT is
// self-consistent, i.e. correct GIVEN a correct 96-bit input.
void testDmrLinkControlRoundTrip() {
    auto packByte = [](std::array<uint8_t, 96>& bits, int startBit, uint8_t value) {
        for (int i = 0; i < 8; ++i) {
            bits[static_cast<size_t>(startBit + i)] = static_cast<uint8_t>((value >> (7 - i)) & 1);
        }
    };
    auto pack24 = [&](std::array<uint8_t, 96>& bits, int startBit, uint32_t value) {
        packByte(bits, startBit, static_cast<uint8_t>((value >> 16) & 0xFF));
        packByte(bits, startBit + 8, static_cast<uint8_t>((value >> 8) & 0xFF));
        packByte(bits, startBit + 16, static_cast<uint8_t>(value & 0xFF));
    };

    std::array<uint8_t, 96> bits{};
    packByte(bits, 0, 0x00);   // FLCO = GroupVoice
    packByte(bits, 8, 0x01);   // FID
    packByte(bits, 16, 0x02);  // service options
    pack24(bits, 24, 0x0000C8);
    pack24(bits, 48, 0x002A2A);

    LinkControlInfo info = DmrLinkControl::parse(bits);
    BIEM_CHECK(info.flco == Flco::GroupVoice);
    BIEM_CHECK(info.featureSetId == 0x01);
    BIEM_CHECK(info.serviceOptions == 0x02);
    BIEM_CHECK(info.groupOrDestAddress == 0x0000C8u);
    BIEM_CHECK(info.sourceAddress == 0x002A2Au);

    packByte(bits, 0, 0x03); // FLCO = PrivateVoice
    LinkControlInfo info2 = DmrLinkControl::parse(bits);
    BIEM_CHECK(info2.flco == Flco::PrivateVoice);
}

} // namespace

int main() {
    testHamming1511RoundTrip();
    testHamming139RoundTrip();
    testGolay2087RoundTrip();
    testBptcInterleaveIsPermutation();
    testBptcRoundTrip();
    testDmrFrameSyncDetectsExactPattern();
    testDmrLinkControlRoundTrip();
    BIEM_TEST_MAIN_RETURN();
}
