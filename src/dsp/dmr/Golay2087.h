#pragma once

#include <array>
#include <cstdint>

namespace biem::dsp::dmr {

// DMR Slot Type FEC ("Golay (20,8)" in the standard's naming; the
// systematic codeword this implementation works with is 19 bits - 8 data
// bits in the high bits, 11 parity bits in the low bits - see
// DmrConstants.h for why, and docs/DMR_NOTES.md for full provenance).
//
// Implementation approach: rather than copying g4klx/DMRGateway's
// precomputed 256-entry encoding table and 2048-entry decoding table
// (whose exact contents this repo's research pass could not obtain - see
// docs/DMR_NOTES.md), this class derives the SAME systematic cyclic code
// from first principles using the generator polynomial that research DID
// extract with reasonable confidence (kGolaySlotTypeGenPoly), via plain
// GF(2) polynomial division. The syndrome->error-pattern correction table
// is built at construction time by brute-force enumeration of all 1- and
// 2-bit error patterns over the 19-bit codeword (see kMaxCorrectableWeight)
// - this is a standard, provably-correct coset-leader decoding technique
// for the generator polynomial as reconstructed. It is only as correct
// against real DMR traffic as that generator polynomial reconstruction is;
// see docs/DMR_NOTES.md before relying on this against real air-interface
// data.
class Golay2087 {
public:
    Golay2087();

    // Returns the 19-bit systematic codeword: data8 in bits 18..11, 11-bit
    // parity in bits 10..0.
    uint32_t encode(uint8_t data8) const;

    // Attempts to correct up to kMaxCorrectableWeight bit errors in a
    // 19-bit codeword (only the low 19 bits of `codeword19` are used).
    // If `correctedBits` is non-null: 0 = already valid, >0 = that many
    // bits were flipped to correct it, -1 = syndrome not recognized
    // (uncorrectable within kMaxCorrectableWeight).
    uint8_t decode(uint32_t codeword19, int* correctedBits = nullptr) const;

    static constexpr int kCodewordBits = 19;
    static constexpr int kMaxCorrectableWeight = 2;

private:
    static uint32_t polyMod(uint32_t value);

    std::array<uint32_t, 1u << 11> syndromeToError_{};
    std::array<bool, 1u << 11> known_{};
};

} // namespace biem::dsp::dmr
