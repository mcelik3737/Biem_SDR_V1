#pragma once

#include <array>
#include <cstdint>

// DMR (ETSI TS 102 361) air-interface constants.
//
// Provenance / confidence level for each group below is documented inline -
// see also docs/DMR_NOTES.md for the full writeup. In short:
//   - kSyncPatterns: confirmed via web search against ETSI TS 102 361 refs.
//   - kBptcInterleaveMultiplier / kGolaySlotTypeGenPoly: extracted from
//     g4klx's open-source, real-world-deployed MMDVMHost/DMRGateway source
//     via an AI-summarized fetch (not a byte-exact diff of the file) - high
//     but not absolute confidence; see citations below.
//   - Row/column bit-position convention used by Bptc196x96: OUR OWN
//     best-effort default (textbook Hamming 1,2,4,8 parity positions), NOT
//     confirmed to match DMR's actual convention - see docs/DMR_NOTES.md
//     for a concrete discrepancy found during research that motivates this
//     caveat. Do not trust Bptc196x96 against real air-interface data
//     without validating this first.

namespace biem::dsp::dmr {

enum class SyncType {
    BsSourcedVoice,
    BsSourcedData,
    MsSourcedVoice,
    MsSourcedData,
    Unknown
};

// 48-bit DMR sync patterns. Source (confirmed by web search, 2026-09,
// against ETSI TS 102 361 references - see docs/DMR_NOTES.md for the
// exact search results/citations):
//   BS Voice: 755FD7DF75F7   BS Data: DFF57D75DF5D
//   MS Voice: 7F7D5DD57DFD   MS Data: D5D7F77FD757
// (RC Sync and the two Direct Mode sync patterns exist in the standard too
// but aren't needed for a repeater/base-station-centric receiver and were
// not chased down here.)
struct SyncEntry {
    SyncType type;
    uint64_t pattern; // low 48 bits significant
};

inline constexpr std::array<SyncEntry, 4> kSyncPatterns = {{
    {SyncType::BsSourcedVoice, 0x755FD7DF75F7ULL},
    {SyncType::BsSourcedData,  0xDFF57D75DF5DULL},
    {SyncType::MsSourcedVoice, 0x7F7D5DD57DFDULL},
    {SyncType::MsSourcedData,  0xD5D7F77FD757ULL},
}};

inline constexpr int kSyncPatternBits = 48;

// A received sync word is accepted if its Hamming distance to one of the
// patterns above is <= this threshold. Not spec-derived - a conservative
// starting point (roughly 10% of 48 bits) to tune once tested against real
// signals; too high risks false locks, too low risks missing real bursts
// with a few bit errors.
inline constexpr int kSyncMaxHammingDistance = 4;

// --- BPTC(196,96) --------------------------------------------------------
// Interleave permutation, source: g4klx/MMDVMHost BPTC19696.cpp (fetched
// 2026-09; see docs/DMR_NOTES.md): interleavedPosition = (a * 181) % 196.
// This is a genuine bijection over [0,196) (181 and 196 are coprime), which
// is a necessary property for a real interleaver - passing that sanity
// check is why this constant is trusted more than the row/column bit
// layout below.
inline constexpr int kBptcTotalBits = 196;
inline constexpr int kBptcPayloadBits = 96;
inline constexpr int kBptcInterleaveMultiplier = 181;

// --- Golay (20,8) / DMR Slot Type -----------------------------------------
// Generator polynomial, source: g4klx/DMRGateway Golay2087.cpp `GENPOL`
// constant (fetched 2026-09; see docs/DMR_NOTES.md). Degree 11 (highest set
// bit is bit 11), giving a systematic 8-data + 11-parity = 19-bit code
// (packed as data in the high 8 bits, parity in the low 11 - inferred from
// that file's decode() bit-packing, see Golay2087.cpp in this codebase for
// the full derivation).
inline constexpr uint32_t kGolaySlotTypeGenPoly = 0x00000C75u;
inline constexpr int kGolaySlotTypeGenDegree = 11;

} // namespace biem::dsp::dmr
