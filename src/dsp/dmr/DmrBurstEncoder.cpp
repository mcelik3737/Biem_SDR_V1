#include "DmrBurstEncoder.h"

#include <array>
#include <cstddef>

#include "Bptc196x96.h"
#include "Golay2087.h"

namespace biem::dsp::dmr {

namespace {

DmrBurstBytes encodeCommon(SyncType syncType, int colorCode, int dataTypeCode, Flco flco,
                            uint32_t groupOrDestAddress, uint32_t sourceAddress) {
    // --- Build the 96-bit LC payload - exact mirror of what
    // DmrLinkControl::parse expects to read back (see that file: FLCO
    // byte, FID byte, ServiceOptions byte, 3-byte group/dest, 3-byte
    // source, 24 unused/reserved bits). ---
    std::array<uint8_t, 96> payload{};
    uint8_t flcoByte = (flco == Flco::GroupVoice) ? 0x00u : (flco == Flco::PrivateVoice ? 0x03u : 0x3Fu);
    auto putByte = [&](int startBit, uint8_t value) {
        for (int i = 0; i < 8; ++i) {
            payload[static_cast<size_t>(startBit + i)] = static_cast<uint8_t>((value >> (7 - i)) & 1u);
        }
    };
    auto put24 = [&](int startBit, uint32_t value) {
        putByte(startBit, static_cast<uint8_t>((value >> 16) & 0xFFu));
        putByte(startBit + 8, static_cast<uint8_t>((value >> 8) & 0xFFu));
        putByte(startBit + 16, static_cast<uint8_t>(value & 0xFFu));
    };
    putByte(0, flcoByte);
    putByte(8, 0x00u);  // featureSetId
    putByte(16, 0x00u); // serviceOptions
    put24(24, groupOrDestAddress);
    put24(48, sourceAddress);
    // bits 72-95 left at 0 - DmrLinkControl doesn't validate them either (see its class comment).

    std::array<uint8_t, 196> bptc196 = Bptc196x96::encode(payload);

    // --- Golay-encode the Slot Type field ---
    uint8_t value = static_cast<uint8_t>(((colorCode & 0x0F) << 4) | (dataTypeCode & 0x0F));
    Golay2087 golay; // small syndrome table; fine to build fresh per call (demo/test use only)
    uint32_t codeword19 = golay.encode(value);
    // Inverse of DmrSlotDecoder::decodeSlotType's:
    //   codeword19 = (st0<<11) | (st1<<3) | (st2>>5)
    uint8_t st0 = static_cast<uint8_t>((codeword19 >> 11) & 0xFFu);
    uint8_t st1 = static_cast<uint8_t>((codeword19 >> 3) & 0xFFu);
    uint8_t st2 = static_cast<uint8_t>((codeword19 & 0x07u) << 5);

    DmrBurstBytes burst{};

    // --- Info1: bptc196[0..97] -> bytes 0-11 + top 2 bits of byte 12 ---
    // (inverse of DmrSlotDecoder::extractInfoBitsForBptc's first loop + the two bits after it)
    for (int byteIdx = 0; byteIdx <= 11; ++byteIdx) {
        uint8_t b = 0;
        for (int bi = 0; bi < 8; ++bi) {
            b = static_cast<uint8_t>((b << 1) | (bptc196[static_cast<size_t>(byteIdx * 8 + bi)] & 1u));
        }
        burst[static_cast<size_t>(byteIdx)] = b;
    }
    uint8_t info1Tail = static_cast<uint8_t>(((bptc196[96] & 1u) << 1) | (bptc196[97] & 1u));

    // --- Info2: bptc196[98..195] -> bottom 2 bits of byte 20 + bytes 21-32 ---
    uint8_t info2Head = static_cast<uint8_t>(((bptc196[98] & 1u) << 1) | (bptc196[99] & 1u));
    for (int byteIdx = 21; byteIdx <= 32; ++byteIdx) {
        uint8_t b = 0;
        int base = 100 + (byteIdx - 21) * 8;
        for (int bi = 0; bi < 8; ++bi) {
            b = static_cast<uint8_t>((b << 1) | (bptc196[static_cast<size_t>(base + bi)] & 1u));
        }
        burst[static_cast<size_t>(byteIdx)] = b;
    }

    // --- byte 12: top 2 bits = info1Tail, bottom 6 bits = st0's top 6 bits ---
    // Inverse of decodeSlotType's: st0 = (data[12]<<2)&0xFC | (data[13]>>6)&0x03
    burst[12] = static_cast<uint8_t>((info1Tail << 6) | ((st0 >> 2) & 0x3Fu));

    // --- sync word bits, extracted once, used across bytes 13/14-18/19 ---
    uint64_t syncPattern = 0;
    for (const auto& entry : kSyncPatterns) {
        if (entry.type == syncType) {
            syncPattern = entry.pattern;
            break;
        }
    }
    auto syncBit = [&](int i) -> uint8_t { // i = 0..47, MSB-first - matches DmrSyntheticSource.cpp
        return static_cast<uint8_t>((syncPattern >> (kSyncPatternBits - 1 - i)) & 1u);
    };

    // --- byte 13: bits7-6 = st0's bottom 2 bits, bits5-4 = st1's top 2 bits, bits3-0 = sync[0..3] ---
    // Inverse of: st0's low 2 bits <- (data[13]>>6)&0x03; st1 = (data[13]<<2)&0xC0 | ... -> data[13] bits5-4 = st1 top 2 bits
    uint8_t sync0to3 = static_cast<uint8_t>((syncBit(0) << 3) | (syncBit(1) << 2) | (syncBit(2) << 1) | syncBit(3));
    burst[13] = static_cast<uint8_t>((static_cast<uint8_t>((st0 & 0x03u) << 6)) |
                                      (static_cast<uint8_t>(((st1 >> 6) & 0x03u) << 4)) | sync0to3);

    // --- bytes 14-18: sync[4..43] (40 bits = 5 bytes) ---
    for (int byteIdx = 14; byteIdx <= 18; ++byteIdx) {
        int base = 4 + (byteIdx - 14) * 8;
        uint8_t b = 0;
        for (int bi = 0; bi < 8; ++bi) b = static_cast<uint8_t>((b << 1) | syncBit(base + bi));
        burst[static_cast<size_t>(byteIdx)] = b;
    }

    // --- byte 19: bits7-4 = sync[44..47], bits3-0 = st1's middle 4 bits ---
    // Inverse of: st1's mid 4 bits <- (data[19]<<2)&0x3C, i.e. data[19] bits3-0 = st1 bits5-2
    uint8_t sync44to47 =
        static_cast<uint8_t>((syncBit(44) << 3) | (syncBit(45) << 2) | (syncBit(46) << 1) | syncBit(47));
    burst[19] = static_cast<uint8_t>((sync44to47 << 4) | ((st1 >> 2) & 0x0Fu));

    // --- byte 20: bits7-6 = st1's bottom 2 bits, bits5-2 = st2's top 4 bits, bits1-0 = info2Head ---
    // Inverse of: st1's low 2 bits <- (data[20]>>6)&0x03; st2 = (data[20]<<2)&0xF0 -> data[20] bits5-2 = st2 top 4 bits
    burst[20] = static_cast<uint8_t>((static_cast<uint8_t>((st1 & 0x03u) << 6)) |
                                      (static_cast<uint8_t>(((st2 >> 4) & 0x0Fu) << 2)) | info2Head);

    return burst;
}

} // namespace

DmrBurstBytes encodeVoiceLcHeaderBurst(SyncType syncType, int colorCode, Flco flco,
                                        uint32_t groupOrDestAddress, uint32_t sourceAddress) {
    return encodeCommon(syncType, colorCode, /*dataTypeCode=*/1 /* VoiceLcHeader - see DmrBurst.h */, flco,
                         groupOrDestAddress, sourceAddress);
}

DmrBurstBytes encodeTerminatorBurst(SyncType syncType, int colorCode, Flco flco,
                                     uint32_t groupOrDestAddress, uint32_t sourceAddress) {
    return encodeCommon(syncType, colorCode, /*dataTypeCode=*/2 /* Terminator - see DmrBurst.h */, flco,
                         groupOrDestAddress, sourceAddress);
}

} // namespace biem::dsp::dmr
