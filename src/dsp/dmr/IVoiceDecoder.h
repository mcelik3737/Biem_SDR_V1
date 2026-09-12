#pragma once

#include <cstddef>
#include <cstdint>

namespace biem::dsp::dmr {

// Abstract DMR voice (AMBE+2) decoder. AMBE+2 is a patented/licensed codec
// and this repo does NOT implement it - see docs/DMR_NOTES.md. This
// interface exists so the rest of the pipeline (DmrCallTracker) doesn't
// need to change once a real decoder is wired in, e.g. via an external
// library. NullVoiceDecoder is the default: a DMR call is still logged
// (metadata only - talkgroup/ID/slot/color code/duration) with no audio,
// rather than not logged at all.
class IVoiceDecoder {
public:
    virtual ~IVoiceDecoder() = default;

    // `frameBits` is one DMR voice frame's payload bits, post-FEC. Returns
    // true and fills `pcmOut` (up to `pcmCapacity` samples; `pcmProduced`
    // set to how many) if audio was produced.
    virtual bool decodeFrame(const uint8_t* frameBits, size_t bitCount, int16_t* pcmOut,
                              size_t pcmCapacity, size_t* pcmProduced) = 0;
};

class NullVoiceDecoder : public IVoiceDecoder {
public:
    bool decodeFrame(const uint8_t* /*frameBits*/, size_t /*bitCount*/, int16_t* /*pcmOut*/,
                      size_t /*pcmCapacity*/, size_t* pcmProduced) override {
        if (pcmProduced) *pcmProduced = 0;
        return false;
    }
};

} // namespace biem::dsp::dmr
