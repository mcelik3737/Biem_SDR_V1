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

inline const char* toString(SyncType t) {
    switch (t) {
        case SyncType::BsSourcedVoice: return "BsSourcedVoice";
        case SyncType::BsSourcedData:  return "BsSourcedData";
        case SyncType::MsSourcedVoice: return "MsSourcedVoice";
        case SyncType::MsSourcedData:  return "MsSourcedData";
        default:                       return "Unknown";
    }
}

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

// --- 264-bit burst layout (all bit counts, derived not guessed) -----------
// Cross-checked two independent ways: (1) walking the exact byte/bit
// offsets DMRSlotType.cpp uses (see docs/DMR_NOTES.md item 4, ported
// verbatim into DmrSlotDecoder::decodeSlotType) shows Slot Type's 20-bit
// Golay codeword is split 10 bits before the sync word and 10 bits after
// it, sandwiched as Info1(98) + SlotType1(10) + Sync(48) + SlotType2(10) +
// Info2(98) = 264; (2) independently, lyonscomputer.com.au's DMR signal
// processing notes describe the same burst as "108-bit payload + 48-bit
// SYNC + 108-bit payload" (108 = 98+10, i.e. the same split at a coarser
// granularity) - both agree.
inline constexpr int kBurstTotalBits = 264;
inline constexpr int kBurstSyncStartBit = 108;  // 0-indexed bit position where SYNC begins
inline constexpr int kBurstBitsAfterSync = kBurstTotalBits - kBurstSyncStartBit - kSyncPatternBits; // 108

// How many bits DmrRfDemodulator's BurstAligner tolerates a sync landing
// away from its exact expected next position (start-of-transmission
// verification and staying-locked position checks both use this - see
// DmrRfDemodulator.cpp) before treating it as a mismatch. Not spec-derived
// - a real, small effect this needs to absorb: any real-world power gate
// used to skip an idle TDMA slot's dead time (see DmrRfDemodulator.h's
// fastPowerAvg_) has SOME decay lag between the real signal ending and
// the gate noticing, during which a handful of trailing bits leak through
// as if real. Burst extraction itself is unaffected (it always anchors
// to wherever the sync was actually found, not a theoretical exact
// position), so this tolerance only needs to cover verification, not
// content alignment. Kept small relative to kBurstTotalBits (264) so it
// stays far tighter than chance - even combined with
// kSyncMaxHammingDistance's own tolerance on the sync match itself, this
// is nowhere near loose enough to meaningfully raise the false-lock risk
// tests/test_dmr_rf.cpp's noise test guards against.
inline constexpr int kSyncPositionToleranceBits = 8;

// --- 4FSK physical layer (symbol rate, deviation, slicing threshold) -----
// Symbol rate: CONFIRMED by general DMR reference (lyonscomputer.com.au
// DMR-Signal-Processing-Notes): 4800 symbols/sec, 2 bits/symbol.
inline constexpr double kDmrSymbolRateHz = 4800.0;
// Deviation levels: CONFIRMED two independent ways - the same reference
// states raw FSK frequencies of +1944/+648/-648/-1944 Hz, and g4klx/MMDVM's
// real firmware (DMRDMOTX.cpp, fetched 2026-09) uses internal levels
// +1362/+454/-454/-1362 for the same four symbols with ratio 1362:454 =
// 3.0003 - the same 3:1 ratio as 1944:648 - independently corroborating
// the same four-level structure.
inline constexpr double kDmrInnerDeviationHz = 648.0;
inline constexpr double kDmrOuterDeviationHz = 1944.0;
// Dibit<->level mapping: CONFIRMED from g4klx/MMDVM DMRDMOTX.cpp (TX table)
// cross-checked against DMRDMORX.cpp (RX slicer thresholds) - both fetched
// 2026-09, and mutually consistent (RX's "sample < -threshold -> 01" lines
// up exactly with TX's "01 -> lowest level" entry, and so on for all four):
//   dibit 11 -> +kDmrOuterDeviationHz (highest)
//   dibit 10 -> +kDmrInnerDeviationHz
//   dibit 00 -> -kDmrInnerDeviationHz
//   dibit 01 -> -kDmrOuterDeviationHz (lowest)
// (a Gray code across the four levels: each adjacent pair differs by
// exactly one bit, as expected of a real, working 4FSK design - another
// point in favor of this mapping being correct, not just plausible-looking).
// Slicing threshold: g4klx/MMDVM's RX code uses 0.6 * (max level) as the
// boundary between the inner and outer pair on each side of zero; applied
// to our real-Hz deviation values rather than their internal fixed-point
// scale, since this demodulator works in Hz throughout (see
// DmrRfDemodulator.cpp / NbfmDemodulator.cpp's discriminator).
inline constexpr double kDmrSliceThresholdHz = 0.6 * kDmrOuterDeviationHz; // 1166.4 Hz

} // namespace biem::dsp::dmr
