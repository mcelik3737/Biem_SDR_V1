#pragma once

#include <memory>
#include <optional>
#include <string>

#include "DmrBurst.h"
#include "DmrConstants.h"
#include "DmrSlotDecoder.h"
#include "IVoiceDecoder.h"

namespace biem::core {
class CallRecorder;
}

namespace biem::dsp::dmr {

// Ties DmrSlotDecoder + Bptc196x96 + DmrLinkControl + an IVoiceDecoder
// together into a per-slot call state machine feeding a shared
// biem::core::CallRecorder. One instance covers one TDMA slot of one DMR
// channel; a full repeater channel needs two (slot 1 and slot 2).
//
// GAP not addressed by this class (see docs/ROADMAP.md): it expects
// already slot-aligned 264-bit bursts as input, paired with the SyncType
// that was seen at that position (or nullopt if none matched - meaning
// this is presumed to be a Frame B-F voice burst, which uses embedded
// signalling instead of a full sync). Producing that aligned sequence from
// a continuous discriminator/4FSK-symbol stream (symbol timing recovery,
// then walking fixed 264-bit steps out from a DmrFrameSync hit) is a
// separate piece, not yet implemented.
class DmrCallTracker {
public:
    DmrCallTracker(core::CallRecorder& recorder, int slotNumber, double frequencyHz,
                   std::string channelLabel, std::unique_ptr<IVoiceDecoder> voiceDecoder = nullptr);

    // `syncType` is what DmrFrameSync reported for this burst's position,
    // or nullopt if nothing matched within tolerance (presumed Frame B-F).
    void onBurst(const DmrBurstBytes& burst, std::optional<SyncType> syncType);

    bool callActive() const { return callActive_; }

private:
    core::CallRecorder& recorder_;
    int slotNumber_;
    double frequencyHz_;
    std::string channelLabel_;
    std::unique_ptr<IVoiceDecoder> voiceDecoder_;

    DmrSlotDecoder slotDecoder_;
    bool callActive_ = false;

    void handleDataBurst(const DmrBurstBytes& burst);
};

} // namespace biem::dsp::dmr
