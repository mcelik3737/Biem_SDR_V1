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
    double squelchThresholdDb = -50.0;        // relative to the filtered-channel power scale - needs calibrating per setup/gain, see class comment
    double deemphasisTimeConstantUs = 300.0;  // common land-mobile NBFM value; adjust if the target radios use a different standard

    // Complex low-pass cutoff applied to the raw IQ samples BEFORE the FM
    // discriminator, to reject energy outside the channel of interest
    // (adjacent signals, wideband noise, the RTL-SDR's own DC spike at the
    // tuned frequency). Without this, the discriminator sees the full
    // capture bandwidth (iqSampleRateHz) and turns out-of-channel content
    // into audio-band "click"/"crackle" artifacts - a well-known FM-demod
    // failure mode, not a subtle one. Default matches half of a 12.5 kHz
    // channel; halve it for 6.25 kHz spacing, double it for 25 kHz.
    double channelHalfBandwidthHz = 6250.0;
};

// Classic quadrature FM discriminator for narrowband analog FM - the mode
// used by simplex/PMR UHF/VHF radios at 6.25/12.5/25 kHz channel spacing.
// Feed it IQ samples via processSamples(); it decimates to
// audioSampleRateHz, applies a single-pole anti-alias low-pass + de-emphasis
// filter, a simple RMS-power squelch, and delivers PCM16 audio (muted while
// squelch is closed) plus squelch open/close edge events via the callbacks.
//
// Known simplifications (documented, not hidden):
//  - The pre-discriminator channel filter and the post-discriminator
//    decimation filter are both single-pole IIR, not a proper
//    polyphase/FIR design. This is a real step up from having no
//    pre-filter at all (see channelHalfBandwidthHz above), but a sharper
//    filter may still be worth it if audio quality on real signals isn't
//    good enough yet.
//  - Squelch threshold is measured on the CHANNEL-FILTERED signal (as of
//    the channelHalfBandwidthHz fix), in the same arbitrary power units as
//    whatever IqSource feeds it (RtlSdrSource normalizes to roughly
//    [-1,1] per sample) - it WILL need calibrating against your actual
//    dongle/gain settings and RF environment; there's no universal
//    correct default, and a threshold calibrated before this fix will
//    read differently now (the filtered signal excludes out-of-channel
//    power, so the noise floor reading should actually be lower/cleaner).
class NbfmDemodulator {
public:
    using AudioCallback = std::function<void(const int16_t* pcm, size_t count)>;
    using SquelchCallback = std::function<void(bool open)>;
    using LevelCallback = std::function<void(double powerDb)>;

    explicit NbfmDemodulator(NbfmConfig config);

    void setAudioCallback(AudioCallback cb) { audioCb_ = std::move(cb); }
    void setSquelchCallback(SquelchCallback cb) { squelchCb_ = std::move(cb); }

    // Fires roughly every `everyNSamples` IQ samples (default: about once a
    // second at typical RTL-SDR rates) with the current smoothed power in
    // dB - the same number the squelch threshold is compared against. This
    // exists so a live tool can print real numbers for the operator to
    // calibrate squelchThresholdDb against, instead of guessing blind (see
    // the class-level caveat: there's no universal correct default).
    void setLevelCallback(LevelCallback cb, uint64_t everyNSamples = 0);

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
    LevelCallback levelCb_;
    uint64_t levelReportInterval_ = 0;
    uint64_t levelSampleCounter_ = 0;

    IqSample prevSample_{0.0f, 0.0f};

    // Pre-discriminator channel-select filter state (see
    // channelHalfBandwidthHz).
    double channelLpfAlpha_ = 1.0;
    IqSample channelLpfState_{0.0f, 0.0f};

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
