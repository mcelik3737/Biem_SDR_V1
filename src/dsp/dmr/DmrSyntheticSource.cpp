#include "DmrSyntheticSource.h"

namespace biem::dsp::dmr {

std::vector<double> bitsToSymbolDeviationsHz(const std::vector<uint8_t>& bits) {
    std::vector<double> out;
    out.reserve(bits.size() / 2);
    for (size_t i = 0; i + 1 < bits.size(); i += 2) {
        uint8_t high = bits[i] & 1u;
        uint8_t low = bits[i + 1] & 1u;
        // Exact inverse of sliceToDibit() in DmrRfDemodulator.cpp - see
        // DmrConstants.h for the citation on this mapping.
        double dev;
        if (high == 0 && low == 1) {
            dev = -kDmrOuterDeviationHz; // dibit 01 - lowest level
        } else if (high == 0 && low == 0) {
            dev = -kDmrInnerDeviationHz; // dibit 00
        } else if (high == 1 && low == 0) {
            dev = kDmrInnerDeviationHz; // dibit 10
        } else {
            dev = kDmrOuterDeviationHz; // dibit 11 - highest level
        }
        out.push_back(dev);
    }
    return out;
}

std::vector<uint8_t> makeSyntheticBurstBits(SyncType type, uint8_t fillPattern) {
    std::vector<uint8_t> bits(static_cast<size_t>(kBurstTotalBits), 0);

    for (int i = 0; i < kBurstTotalBits; ++i) {
        int bitPos = 7 - (i % 8); // MSB-first within each repeated byte of fillPattern
        bits[static_cast<size_t>(i)] = static_cast<uint8_t>((fillPattern >> bitPos) & 1u);
    }

    uint64_t pattern = 0;
    for (const auto& entry : kSyncPatterns) {
        if (entry.type == type) {
            pattern = entry.pattern;
            break;
        }
    }
    for (int i = 0; i < kSyncPatternBits; ++i) {
        int shift = kSyncPatternBits - 1 - i; // MSB-first
        bits[static_cast<size_t>(kBurstSyncStartBit + i)] = static_cast<uint8_t>((pattern >> shift) & 1u);
    }

    return bits;
}

} // namespace biem::dsp::dmr
