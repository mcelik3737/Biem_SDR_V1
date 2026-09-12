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

    // Complex low-pass cutoff applied to the (mixer-shifted, see
    // mixerOffsetHz) IQ samples BEFORE the FM discriminator, to reject
    // energy outside the channel of interest. Default matches half of a
    // 12.5 kHz channel; halve it for 6.25 kHz spacing, double it for 25 kHz.
    double channelHalfBandwidthHz = 6250.0;

    // If the hardware is tuned `mixerOffsetHz` BELOW the actual channel
    // frequency (the caller's job - see RtlSdrSource::setCenterFrequencyHz
    // usage in biem_cli.cpp's runLive()), the wanted channel arrives at
    // +mixerOffsetHz in the raw baseband spectrum instead of at 0 Hz. This
    // digitally mixes it back down to true baseband before filtering.
    //
    // WHY THIS MATTERS FOR LIVE RTL-SDR RECEPTION (this is not a
    // nice-to-have there): RTL-SDR dongles built around zero-IF tuners (the
    // Elonics E4000 among them) have a DC spike/LO-leakage artifact sitting
    // exactly AT the tuned frequency. If you tune directly on the channel
    // you want, that spike lands exactly on top of your signal - no filter
    // can separate them because they occupy the same frequency. Tuning
    // off-channel and shifting back in software moves the spike to
    // -mixerOffsetHz, where channelHalfBandwidthHz above can reject it
    // while passing the (now correctly centered) wanted signal.
    //
    // Defaults to 0 (mixer disabled) because this field is meaningless for
    // any source that already delivers true-baseband IQ - synthetic test
    // signals (see test_nbfm.cpp), WavIqSource file playback of a capture
    // that was itself recorded on-channel, and biem_cli's "demo" command
    // all fall in that category, and would have a bogus offset mixed in by
    // mistake if this defaulted to nonzero. Only the live RTL-SDR path
    // (biem_cli.cpp's runLive()) should set this explicitly.
    double mixerOffsetHz = 0.0;
};

// Classic quadrature FM discriminator for narrowband analog FM - the mode
// used by simplex/PMR UHF/VHF radios at 6.25/12.5/25 kHz channel spacing.
// Feed it IQ samples via processSamples(); it digitally re-centers the
// signal (see mixerOffsetHz), channel-filters, discriminates, then
// DC-blocks + low-pass filters + de-emphasizes + decimates to
// audioSampleRateHz, with a simple RMS-power squelch, delivering PCM16
// audio (muted while squelch is closed) plus squelch open/close edge
// events via the callbacks.
//
// Known simplifications (documented, not hidden):
//  - The channel-select, decimation, and DC-blocking filters are all
//    single-pole/single-zero IIR, not a proper polyphase/FIR design. This
//    is a real step up from having neither a channel filter nor a mixer
//    offset at all (see mixerOffsetHz and channelHalfBandwidthHz above),
//    but a sharper filter may still be worth it if audio quality on real
//    signals isn't good enough yet.
//  - Squelch threshold is measured on the CHANNEL-FILTERED (post-mixer)
//    signal, in the same arbitrary power units as whatever IqSource feeds
//    it (RtlSdrSource normalizes to roughly [-1,1] per sample) - it WILL
//    need calibrating against your actual dongle/gain settings and RF
//    environment; there's no universal correct default.
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
    // consecutive chunks - all mixer/filter/decimator/squelch state
    // persists across calls (the mixer phase in particular must stay
    // continuous across chunk boundaries, or every call would re-introduce
    // a phase discontinuity indistinguishable from a click). audioCb_ is
    // invoked at most once per call, with however many PCM samples this
    // chunk produced (may be zero, in which case it's not invoked at all).
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

    // Digital mixer (NCO) state - see mixerOffsetHz.
    double mixerPhaseRad_ = 0.0;
    double mixerPhaseIncRad_ = 0.0;

    // Pre-discriminator channel-select filter state (see
    // channelHalfBandwidthHz). Applied AFTER the mixer shift.
    double channelLpfAlpha_ = 1.0;
    IqSample channelLpfState_{0.0f, 0.0f};

    // Post-discriminator DC blocker (removes residual DC/near-DC leakage
    // before the low-pass+decimation stage - a one-pole/one-zero classic
    // DC blocker, y[n] = x[n] - x[n-1] + r*y[n-1]).
    double dcBlockR_ = 0.999;
    double dcBlockPrevIn_ = 0.0;
    double dcBlockPrevOut_ = 0.0;

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
