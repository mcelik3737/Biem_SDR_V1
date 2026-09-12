#pragma once

#include <cstdint>
#include <functional>

#include "DmrConstants.h"

namespace biem::dsp::dmr {

struct SyncDetection {
    SyncType type = SyncType::Unknown;
    int hammingDistance = 0;
};

// Slides a 48-bit window over an incoming stream of hard bits (one 0/1
// value per pushBit() call) looking for a match (within
// kSyncMaxHammingDistance) against one of DmrConstants::kSyncPatterns.
// Does NOT know the surrounding burst structure - it just reports "a sync
// pattern of this type was seen ending at the bit just pushed". Combining
// that with the fixed 264-bit burst length to walk out a full aligned
// burst sequence is a separate piece, not yet implemented (see
// docs/ROADMAP.md).
class DmrFrameSync {
public:
    using SyncCallback = std::function<void(SyncDetection)>;

    void setSyncCallback(SyncCallback cb) { cb_ = std::move(cb); }

    void pushBit(uint8_t bit);

private:
    uint64_t shiftReg_ = 0;
    int bitsLoaded_ = 0;
    SyncCallback cb_;
};

} // namespace biem::dsp::dmr
