#include "DmrCallTracker.h"

#include "../../core/CallRecorder.h"
#include "Bptc196x96.h"
#include "DmrLinkControl.h"

namespace biem::dsp::dmr {

DmrCallTracker::DmrCallTracker(core::CallRecorder& recorder, int slotNumber, double frequencyHz,
                                std::string channelLabel, std::unique_ptr<IVoiceDecoder> voiceDecoder)
    : recorder_(recorder),
      slotNumber_(slotNumber),
      frequencyHz_(frequencyHz),
      channelLabel_(std::move(channelLabel)),
      voiceDecoder_(voiceDecoder ? std::move(voiceDecoder) : std::make_unique<NullVoiceDecoder>()) {}

void DmrCallTracker::handleDataBurst(const DmrBurstBytes& burst) {
    SlotTypeInfo slotType = slotDecoder_.decodeSlotType(burst);

    if (slotType.dataType == DmrDataType::VoiceLcHeader) {
        auto infoBits = slotDecoder_.extractInfoBitsForBptc(burst);
        std::array<uint8_t, 96> payload{};
        if (Bptc196x96::decode(infoBits, payload)) {
            LinkControlInfo lc = DmrLinkControl::parse(payload);

            core::CallRecord meta;
            meta.frequencyHz = frequencyHz_;
            meta.modulation = core::Modulation::DmrDigital;
            meta.source = core::CallSource::Sdr;
            meta.colorCode = slotType.colorCode;
            meta.slot = slotNumber_;
            meta.channelLabel = channelLabel_;
            if (lc.flco == Flco::GroupVoice) {
                meta.talkgroupId = lc.groupOrDestAddress;
            } else if (lc.flco == Flco::PrivateVoice) {
                meta.destRadioId = lc.groupOrDestAddress;
            }
            meta.radioId = lc.sourceAddress;

            recorder_.beginCall(std::move(meta));
            callActive_ = true;
        }
        return;
    }

    if (slotType.dataType == DmrDataType::Terminator) {
        if (callActive_) {
            recorder_.endCall();
            callActive_ = false;
        }
        return;
    }

    // Other data-type bursts (CSBK, data header, rate 1/2 or 3/4 data,
    // idle, ...) are not yet handled - CSBK in particular is where SDS
    // (short data) and GPS-revert data typically ride; see
    // docs/ROADMAP.md.
}

void DmrCallTracker::onBurst(const DmrBurstBytes& burst, std::optional<SyncType> syncType) {
    if (!syncType) {
        // No sync matched at this position: presumed Voice Frame B-F
        // (uses embedded signalling, not a full sync word). Voice decode
        // is not implemented (NullVoiceDecoder) - see docs/DMR_NOTES.md.
        // Once a real IVoiceDecoder exists, extract the AMBE frame bits
        // here and call recorder_.pushAudio() with its output while
        // callActive_ is true.
        return;
    }

    switch (*syncType) {
        case SyncType::BsSourcedData:
        case SyncType::MsSourcedData:
            handleDataBurst(burst);
            break;
        case SyncType::BsSourcedVoice:
        case SyncType::MsSourcedVoice:
            // Voice Frame A: carries a Slot Type field like data bursts do,
            // but the payload is voice, not BPTC-coded LC/CSBK data - the
            // LC for the call already arrived via a preceding
            // VoiceLcHeader data burst. Nothing to do here until voice
            // decode exists (see above).
            break;
        case SyncType::Unknown:
        default:
            break;
    }
}

} // namespace biem::dsp::dmr
