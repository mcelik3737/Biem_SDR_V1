#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "IqSample.h"

namespace biem::dsp {

struct NbfmConfig {
    double iqSampleRateHz = 240000.0;         // IQ input sample rate from the SDR
    double audioSampleRateHz = 8000.0;        // PCM output sample rate
    double maxDeviationHz = 5000.0;           // NBFM peak deviation (~2.5 kHz @ 12.5 kHz spacing narrow, ~5 kHz @ 25 kHz - tune per channel)
    double squelchThresholdDb = -50.0;        // relative to the IQ source's own power scale - needs calibrating per setup/gain, see class comment
    double deemphasisTimeConstantUs = 300.0;  // common land-mobile NBFM value; adjust if the target radios use a different standard
};

// Classic quadrature FM discriminator for narrowband analog FM - the mode
// used by simplex/PMR UHF/VHF radios at 6.25/12.5/25 kHz channel spacing.
// Feed it IQ samples via processSamples(); it decimates to
// audioSampleRateHz, applies a single-pole anti-alias low-pass + de-emphasis
// filter, a simple RMS-power squelch, and delivers PCM16 audio (muted while
// squelch is closed) plus squelch open/close edge events via the callbacks.
//
// Known simplifications (documented, not hidden):
//  - Decimation is single-pole-IIR-filtered drop-sample, not a proper
//    polyphase/FIR decimator. Fine for voice bandwidth NBFM; revisit if
//    audio quality on real signals isn't good enough.
//  - Squelch threshold is in the same arbitrary power units as whatever
//    IqSource feeds it (RtlSdrSource normalizes to roughly [-1,1] per
//    sample) - it WILL need calibrating against your actual dongle/gain
//    settings and RF environment; there's no universal correct default.
class NbfmDemodulator {
public:
    using AudioCallback = std::function<void(const int16_t* pcm, size_t count)>;
    using SquelchCallback = std::function<void(bool open)>;

    explicit NbfmDemodulator(NbfmConfig config);

    void setAudioCallback(AudioCallback cb) { audioCb_ = std::move(cb); }
    void setSquelchCallback(SquelchCallback cb) { squelchCb_ = std::move(cb); }

    // Consumes IQ samples at config.iqSampleRateHz. Call repeatedly with
    // consecutive chunks - all filter/decimator/squelch state persists
    // across calls. audioCb_ is invoked at most once per call, with however
    // many PCM samples this chunk produced (may be zero, in which case it's
    // not invoked at all).
    void processSamples(const IqSample* samples, size_t count);

    bool squelchOpen() const { return squelchOpen_; }

private:
    NbfmConfig config_;
    AudioCallback audioCb_;
    SquelchCallback squelchCb_;

    IqSample prevSample_{0.0f, 0.0f};

    double decimationRatio_ = 1.0;
    double decimationAccumulator_ = 0.0;
    double lpfAlpha_ = 1.0;
    double lpfState_ = 0.0;
    double deemphasisAlpha_ = 1.0;
    double deemphasisState_ = 0.0;

    double squelchPowerAvg_ = 0.0;
    bool squelchOpen_ = false;

    std::vector<int16_t> pcmScratch_;

    double discriminate(const IqSample& s);
};

} // namespace biem::dsp
