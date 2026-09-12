#pragma once

#include <cstdint>
#include <vector>

#include "DmrConstants.h"

namespace biem::dsp::dmr {

// Test/demo-only helpers that generate synthetic DMR 4FSK bit sequences -
// see tests/test_dmr_rf.cpp (round-trip verification of DmrRfDemodulator)
// and biem_cli.cpp's "dmr-demo" command (hardware-free end-to-end demo,
// mirroring what "demo" already does for analog FM). Combine with
// WavIqSource::makeSyntheticFsk to get actual IQ samples.

// Converts a flat MSB-first bit sequence (consumed two at a time as
// dibits) into one frequency deviation (Hz) per symbol, using the cited
// dibit<->level mapping in DmrConstants.h. `bits.size()` must be even;
// an odd trailing bit is ignored.
std::vector<double> bitsToSymbolDeviationsHz(const std::vector<uint8_t>& bits);

// Builds one complete, self-consistent 264-bit DMR burst (as individual
// 0/1 values, MSB-first - feed straight into bitsToSymbolDeviationsHz, or
// concatenate several for a multi-burst stream) with a real sync word
// (see DmrConstants::kSyncPatterns) at the correct position (see
// DmrConstants::kBurstSyncStartBit). Every other bit (Info1/SlotType1/
// SlotType2/Info2 - 216 bits) is filled with `fillPattern`'s bits,
// repeated MSB-first - NOT real Golay/BPTC-encoded content (this exists
// to exercise burst timing/alignment recovery, not content decoding), but
// fully deterministic, so a decoded burst can be compared byte-for-byte
// against this function's own output.
std::vector<uint8_t> makeSyntheticBurstBits(SyncType type, uint8_t fillPattern = 0xA5);

} // namespace biem::dsp::dmr
