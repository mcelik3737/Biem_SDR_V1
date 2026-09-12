#include "DmrFrameSync.h"

#include <bit>

namespace biem::dsp::dmr {

void DmrFrameSync::pushBit(uint8_t bit) {
    constexpr uint64_t kMask = (kSyncPatternBits >= 64) ? ~0ULL : ((1ULL << kSyncPatternBits) - 1);
    shiftReg_ = ((shiftReg_ << 1) | (bit & 1u)) & kMask;

    // Count up to a full window, then check on the SAME push that completes
    // it (not one push later - an earlier version of this function
    // returned before checking on that push, which misaligned the window
    // by one bit against real continuous data and could cause a real sync
    // word to never line up with any known pattern at all).
    if (bitsLoaded_ < kSyncPatternBits) ++bitsLoaded_;
    if (bitsLoaded_ < kSyncPatternBits) return;

    if (!cb_) return;

    for (const auto& entry : kSyncPatterns) {
        uint64_t diff = (shiftReg_ ^ entry.pattern) & kMask;
        int dist = std::popcount(diff);
        if (dist <= kSyncMaxHammingDistance) {
            cb_(SyncDetection{entry.type, dist});
            return; // report at most one match per bit position
        }
    }
}

} // namespace biem::dsp::dmr
