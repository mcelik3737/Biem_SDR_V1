#pragma once

#include <array>
#include <cstdint>

namespace biem::dsp::dmr {

enum class Flco {
    GroupVoice,
    PrivateVoice,
    Other
};

struct LinkControlInfo {
    Flco flco = Flco::Other;
    uint8_t featureSetId = 0;
    uint8_t serviceOptions = 0;
    uint32_t groupOrDestAddress = 0; // talkgroup id (group call) or destination radio id (private call)
    uint32_t sourceAddress = 0;      // source radio id
};

// Parses the 96-bit BPTC payload of a Voice LC Header / Terminator burst
// into its Full Link Control fields. Layout assumed: FLCO byte, FID byte,
// Service Options byte, 3-byte group/dest address, 3-byte source address
// (9 bytes / 72 bits total) - the commonly-documented DMR Full LC
// structure. The remaining 24 of the 96 payload bits (typically involved
// in a PDU-type-dependent CRC mask in the real standard) are NOT validated
// here - see docs/DMR_NOTES.md. Net effect: this class's own bit-packing
// is simple and correct GIVEN a correct 96-bit input, but a corrupted or
// misaligned upstream frame (see the open questions in Bptc196x96.h and
// DmrSlotDecoder.h) can still produce plausible-looking-but-wrong numbers
// with no warning, since there's no CRC check to reject it.
class DmrLinkControl {
public:
    static LinkControlInfo parse(const std::array<uint8_t, 96>& payloadBits);
};

} // namespace biem::dsp::dmr
