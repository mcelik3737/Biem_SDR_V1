#include "NbfmDemodulator.h"

#include <algorithm>
#include <cmath>

namespace biem::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kSquelchPowerAvgAlpha = 0.001; // ~ms-scale exponential average at typical IQ rates
} // namespace

NbfmDemodulator::NbfmDemodulator(NbfmConfig config) : config_(config) {
    decimationRatio_ = config_.iqSampleRateHz / config_.audioSampleRateHz;

    double lpfCutoffHz = config_.audioSampleRateHz * 0.45; // just under the output Nyquist
    lpfAlpha_ = 1.0 - std::exp(-2.0 * kPi * lpfCutoffHz / config_.iqSampleRateHz);

    double tau = config_.deemphasisTimeConstantUs * 1e-6;
    double dt = 1.0 / config_.audioSampleRateHz;
    deemphasisAlpha_ = dt / (tau + dt);
}

double NbfmDemodulator::discriminate(const IqSample& s) {
    IqSample prod = s * std::conj(prevSample_);
    prevSample_ = s;
    return std::atan2(static_cast<double>(prod.imag()), static_cast<double>(prod.real()));
}

void NbfmDemodulator::processSamples(const IqSample* samples, size_t count) {
    pcmScratch_.clear();

    for (size_t i = 0; i < count; ++i) {
        const IqSample& s = samples[i];

        // --- squelch: exponential moving average of instantaneous power ---
        double instPower = static_cast<double>(s.real()) * s.real() + static_cast<double>(s.imag()) * s.imag();
        squelchPowerAvg_ += kSquelchPowerAvgAlpha * (instPower - squelchPowerAvg_);
        double powerDb = 10.0 * std::log10(std::max(squelchPowerAvg_, 1e-12));
        bool newSquelchOpen = powerDb > config_.squelchThresholdDb;
        if (newSquelchOpen != squelchOpen_) {
            squelchOpen_ = newSquelchOpen;
            if (squelchCb_) squelchCb_(squelchOpen_);
        }

        // --- FM discriminator (instantaneous frequency, normalized to ~[-1,1]) ---
        double freqRadPerSample = discriminate(s);
        double freqHz = freqRadPerSample * (config_.iqSampleRateHz / (2.0 * kPi));
        double audioSample = freqHz / config_.maxDeviationHz;

        // --- anti-alias low-pass ahead of decimation ---
        lpfState_ += lpfAlpha_ * (audioSample - lpfState_);

        // --- drop-sample decimation to audioSampleRateHz ---
        decimationAccumulator_ += 1.0;
        if (decimationAccumulator_ >= decimationRatio_) {
            decimationAccumulator_ -= decimationRatio_;

            // --- de-emphasis ---
            deemphasisState_ += deemphasisAlpha_ * (lpfState_ - deemphasisState_);

            double clamped = std::clamp(deemphasisState_, -1.0, 1.0);
            if (!squelchOpen_) clamped = 0.0; // mute while squelch is closed
            pcmScratch_.push_back(static_cast<int16_t>(clamped * 32767.0));
        }
    }

    if (!pcmScratch_.empty() && audioCb_) {
        audioCb_(pcmScratch_.data(), pcmScratch_.size());
    }
}

} // namespace biem::dsp
