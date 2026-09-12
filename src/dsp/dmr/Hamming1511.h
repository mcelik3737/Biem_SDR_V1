#pragma once

#include <array>
#include <cstdint>

namespace biem::dsp::dmr {

// Standard systematic (15,11) Hamming single-error-correcting code (the
// conventional textbook construction: parity at 1-indexed positions
// 1,2,4,8; data at the remaining 11 positions). This is plain, unambiguous
// textbook math - fully self-consistent and unit-tested (tests/test_fec.cpp)
// - independent of any DMR-specific ambiguity.
//
// Whether DMR's own BPTC(196,96) row code uses exactly this bit-position
// convention is a SEPARATE question - see docs/DMR_NOTES.md and the caveat
// in Bptc196x96.h. This class is correct in itself either way.
class Hamming1511 {
public:
    // 1-indexed positions (within the 15-bit row) of the 11 data bits and 4
    // parity bits, exposed publicly so callers building a larger structure
    // out of this code (e.g. Bptc196x96) share the exact same convention
    // instead of duplicating/risking drift.
    static constexpr std::array<int, 11> kDataPositions = {3, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15};
    static constexpr std::array<int, 4> kParityPositions = {1, 2, 4, 8};

    // data[0] is the first (most-significant) of the 11 data bits.
    // Returns codeword[0] = bit at 1-indexed position 1, ..., codeword[14] =
    // position 15, each entry 0 or 1.
    static std::array<uint8_t, 15> encode(const std::array<uint8_t, 11>& data);

    // Computes the syndrome, corrects a single-bit error in `codeword` in
    // place if the syndrome is non-zero, and extracts the 11 data bits into
    // `dataOut`. Returns true if a correction was applied.
    static bool decode(std::array<uint8_t, 15>& codeword, std::array<uint8_t, 11>& dataOut);
};

} // namespace biem::dsp::dmr
