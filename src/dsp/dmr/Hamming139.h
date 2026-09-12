#pragma once

#include <array>
#include <cstdint>

namespace biem::dsp::dmr {

// Standard systematic (13,9) Hamming single-error-correcting code - same
// construction as Hamming1511 (parity at 1-indexed positions 1,2,4,8),
// truncated to 13 positions. See Hamming1511.h for the same provenance
// caveat regarding DMR-specific bit-position conventions.
class Hamming139 {
public:
    static constexpr std::array<int, 9> kDataPositions = {3, 5, 6, 7, 9, 10, 11, 12, 13};
    static constexpr std::array<int, 4> kParityPositions = {1, 2, 4, 8};

    static std::array<uint8_t, 13> encode(const std::array<uint8_t, 9>& data);
    static bool decode(std::array<uint8_t, 13>& codeword, std::array<uint8_t, 9>& dataOut);
};

} // namespace biem::dsp::dmr
