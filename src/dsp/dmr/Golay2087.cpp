#include "Golay2087.h"

#include "DmrConstants.h"

namespace biem::dsp::dmr {

namespace {
constexpr uint32_t kGenPoly = kGolaySlotTypeGenPoly;   // 0x00000C75, degree 11
constexpr int kGenDegree = kGolaySlotTypeGenDegree;    // 11
} // namespace

uint32_t Golay2087::polyMod(uint32_t value) {
    uint32_t remainder = value;
    for (int i = kCodewordBits - 1; i >= kGenDegree; --i) {
        if (remainder & (1u << i)) {
            remainder ^= (kGenPoly << (i - kGenDegree));
        }
    }
    return remainder & ((1u << kGenDegree) - 1);
}

Golay2087::Golay2087() {
    known_.fill(false);
    syndromeToError_.fill(0);

    known_[0] = true; // weight-0 pattern (no error) always maps to syndrome 0

    auto tryPattern = [&](uint32_t errorPattern) {
        uint32_t syn = polyMod(errorPattern);
        if (!known_[syn]) {
            known_[syn] = true;
            syndromeToError_[syn] = errorPattern;
        }
    };

    // Enumerate all weight-1 and weight-2 error patterns over the 19-bit
    // codeword, lowest weight first, so a syndrome already claimed by a
    // lower-weight pattern is never overwritten (standard minimum-weight
    // coset-leader decoding).
    for (int b0 = 0; b0 < kCodewordBits; ++b0) {
        tryPattern(1u << b0);
    }
    for (int b0 = 0; b0 < kCodewordBits; ++b0) {
        for (int b1 = b0 + 1; b1 < kCodewordBits; ++b1) {
            tryPattern((1u << b0) | (1u << b1));
        }
    }
}

uint32_t Golay2087::encode(uint8_t data8) const {
    uint32_t shifted = static_cast<uint32_t>(data8) << kGenDegree;
    uint32_t parity = polyMod(shifted);
    return shifted | parity;
}

uint8_t Golay2087::decode(uint32_t codeword19, int* correctedBits) const {
    codeword19 &= (1u << kCodewordBits) - 1;
    uint32_t syn = polyMod(codeword19);
    uint32_t corrected = codeword19;
    int flips = 0;

    if (syn != 0) {
        if (known_[syn]) {
            uint32_t errPattern = syndromeToError_[syn];
            corrected ^= errPattern;
            for (uint32_t p = errPattern; p; p >>= 1) {
                flips += static_cast<int>(p & 1u);
            }
        } else {
            flips = -1;
        }
    }

    if (correctedBits) *correctedBits = flips;
    return static_cast<uint8_t>((corrected >> kGenDegree) & 0xFFu);
}

} // namespace biem::dsp::dmr
