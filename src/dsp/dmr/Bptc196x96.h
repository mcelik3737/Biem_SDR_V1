#pragma once

#include <array>
#include <cstdint>

namespace biem::dsp::dmr {

// BPTC(196,96): 196 raw bits <-> 96 payload bits (DMR's Block Product Turbo
// Code, used to protect Voice LC / CSBK / data headers etc.).
//
// Provenance / confidence - see docs/DMR_NOTES.md for the full writeup:
//   - interleave()/deinterleave(): grounded in a citable source
//     (g4klx/MMDVMHost) and independently sanity-checked (the permutation
//     is a genuine bijection over 196 elements) - solid.
//   - Row/column Hamming(15,11)/(13,9) product-code structure: standard,
//     provably-correct linear product-code construction GIVEN that the
//     row/column bit-position convention below matches DMR's actual one -
//     that specific convention is OUR best-effort default (see
//     Hamming1511.h), not confirmed against the real spec/reference.
//   - Which 3 of the 99 row-data x column-data intersection cells are
//     "reserved" rather than payload (99 - 96 = 3): placed as the FIRST 3
//     candidate cells in row-major order, chosen because it reproduces the
//     8-bits-in-the-first-data-row / 11-bits-per-subsequent-row pattern a
//     source fetch (see docs/DMR_NOTES.md) suggested - plausible, NOT
//     independently confirmed.
//
// Net effect: encode()/decode() are internally consistent (round-trip
// tested in tests/test_fec.cpp) and structurally the right SHAPE of
// algorithm, but should not be trusted to correctly decode a real DMR
// burst until validated per docs/DMR_NOTES.md.
class Bptc196x96 {
public:
    // `raw196` is 196 bits (0/1 per element) as received, in already-
    // deinterleaved-INPUT order - i.e. this function itself calls
    // deinterleave() first. Runs one row-correction pass then one
    // column-correction pass (standard iterative product-code decoding
    // would repeat this a few times for noisier input - see
    // docs/DMR_NOTES.md; one pass is what's implemented here), then
    // extracts the 96 payload bits. Returns false only if the row/column
    // data-position tables are internally inconsistent (a programming
    // error, not a real-world "bad frame" condition - bad frames just
    // produce wrong/corrected bits, decode() doesn't attempt to detect
    // "too corrupted to trust" itself; that's left to the caller, e.g. via
    // a CRC check elsewhere in the DMR message it decoded).
    static bool decode(const std::array<uint8_t, 196>& raw196, std::array<uint8_t, 96>& payloadOut);

    // Inverse of decode(): places the 96 payload bits (plus 3 always-zero
    // placeholder "reserved" bits - see class comment) into the grid,
    // computes row parity for the 9 data rows, then column parity for all
    // 15 columns (this order matters - see .cpp - it's what makes the
    // corner/parity-of-parity cells well-defined), and interleaves the
    // result. Exists so tests/test_fec.cpp can round-trip through this
    // class using only its own convention (proves internal consistency,
    // not spec conformance - see class comment above).
    static std::array<uint8_t, 196> encode(const std::array<uint8_t, 96>& payload);

    // The (a * 181) % 196 permutation (DmrConstants::kBptcInterleaveMultiplier).
    static std::array<uint8_t, 196> interleave(const std::array<uint8_t, 196>& input);
    static std::array<uint8_t, 196> deinterleave(const std::array<uint8_t, 196>& input);
};

} // namespace biem::dsp::dmr
