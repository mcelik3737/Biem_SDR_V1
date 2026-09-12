#include "DmrSlotDecoder.h"

#include <cstddef>

namespace biem::dsp::dmr {

DmrSlotDecoder::DmrSlotDecoder() = default;

SlotTypeInfo DmrSlotDecoder::decodeSlotType(const DmrBurstBytes& burst) const {
    // Verbatim-ported extraction (see docs/DMR_NOTES.md for the fetched
    // source snippet this comes from):
    //   DMRSlotType[0] = (data[12]<<2)&0xFC | (data[13]>>6)&0x03
    //   DMRSlotType[1] = (data[13]<<2)&0xC0 | (data[19]<<2)&0x3C | (data[20]>>6)&0x03
    //   DMRSlotType[2] = (data[20]<<2)&0xF0
    uint8_t st0 = static_cast<uint8_t>(((burst[12] << 2) & 0xFCu) | ((burst[13] >> 6) & 0x03u));
    uint8_t st1 = static_cast<uint8_t>(((burst[13] << 2) & 0xC0u) | ((burst[19] << 2) & 0x3Cu) |
                                        ((burst[20] >> 6) & 0x03u));
    uint8_t st2 = static_cast<uint8_t>((burst[20] << 2) & 0xF0u);

    // Reassemble into the 19-bit Golay2087 codeword the same way the
    // reference decode() does: (st[0]<<11) + (st[1]<<3) + (st[2]>>5).
    uint32_t codeword19 = (static_cast<uint32_t>(st0) << 11) | (static_cast<uint32_t>(st1) << 3) |
                           (static_cast<uint32_t>(st2) >> 5);

    int corrected = -1;
    uint8_t value = golay_.decode(codeword19, &corrected);

    SlotTypeInfo info;
    info.colorCode = (value >> 4) & 0x0F;
    info.dataType = dataTypeFromCode(value & 0x0F);
    info.correctedBits = corrected;
    return info;
}

std::array<uint8_t, 196> DmrSlotDecoder::extractInfoBitsForBptc(const DmrBurstBytes& burst) const {
    std::array<uint8_t, 196> bits{};
    // *** UNVERIFIED - see class header comment and docs/DMR_NOTES.md ***
    // Best-effort: Info1 = bytes 0-11 (96 bits) + top 2 bits of byte 12 (98
    // bits); Info2 mirrored from the tail = bottom 2 bits of byte 20 +
    // bytes 21-32 (98 bits); 98+98 = 196.
    int idx = 0;
    for (int byteIdx = 0; byteIdx <= 11; ++byteIdx) {
        for (int b = 7; b >= 0; --b) bits[static_cast<size_t>(idx++)] = (burst[byteIdx] >> b) & 1u;
    }
    bits[static_cast<size_t>(idx++)] = (burst[12] >> 7) & 1u;
    bits[static_cast<size_t>(idx++)] = (burst[12] >> 6) & 1u;

    bits[static_cast<size_t>(idx++)] = (burst[20] >> 1) & 1u;
    bits[static_cast<size_t>(idx++)] = (burst[20] >> 0) & 1u;
    for (int byteIdx = 21; byteIdx <= 32; ++byteIdx) {
        for (int b = 7; b >= 0; --b) bits[static_cast<size_t>(idx++)] = (burst[byteIdx] >> b) & 1u;
    }
    return bits;
}

} // namespace biem::dsp::dmr
