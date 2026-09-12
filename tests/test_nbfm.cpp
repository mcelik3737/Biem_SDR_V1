// End-to-end sanity test for the analog NBFM chain: generate a known
// FM-modulated tone entirely in memory (no hardware, no file), demodulate
// it, and confirm the recovered audio's frequency is roughly right via
// zero-crossing counting.
#include "test_util.h"

#include <cmath>
#include <vector>

#include "../src/dsp/NbfmDemodulator.h"
#include "../src/dsp/WavIqSource.h"

using namespace biem::dsp;

namespace {

constexpr double kTestPi = 3.14159265358979323846;

// Normalized correlation of `pcm[start:]` against a pure sine at `toneHz`,
// maximized over phase (projects onto both sin and cos references). 1.0
// for a clean, undistorted tone at exactly toneHz; much lower for noise,
// a wrong-frequency signal, or a badly distorted waveform - a far
// stricter and more honest measure of "is this actually the right tone"
// than counting zero crossings, which tolerates surprisingly severe
// corruption without ever leaving its pass tolerance (see the DC-spike
// regression test below, which zero-crossing counting failed to catch).
double toneCorrelation(const std::vector<int16_t>& pcm, size_t start, double toneHz, double sampleRateHz) {
    double sumSin = 0.0, sumCos = 0.0, sumSq = 0.0;
    size_t n = pcm.size() - start;
    for (size_t i = start; i < pcm.size(); ++i) {
        double t = static_cast<double>(i - start) / sampleRateHz;
        double x = static_cast<double>(pcm[i]);
        sumSin += x * std::sin(2.0 * kTestPi * toneHz * t);
        sumCos += x * std::cos(2.0 * kTestPi * toneHz * t);
        sumSq += x * x;
    }
    if (sumSq < 1e-9 || n == 0) return 0.0;
    double numerator = std::sqrt(sumSin * sumSin + sumCos * sumCos);
    double denominator = std::sqrt(static_cast<double>(n) / 2.0) * std::sqrt(sumSq);
    return numerator / denominator;
}

void testNbfmRecoversAudioTone() {
    const double iqRate = 48000.0;
    const double audioRate = 8000.0;
    const double toneHz = 1000.0;
    const double devHz = 2500.0;

    NbfmConfig cfg;
    cfg.iqSampleRateHz = iqRate;
    cfg.audioSampleRateHz = audioRate;
    cfg.maxDeviationHz = devHz;
    cfg.squelchThresholdDb = -60.0; // low threshold - the synthetic signal is "always on"
    NbfmDemodulator demod(cfg);

    std::vector<int16_t> allPcm;
    bool squelchWasOpen = false;
    demod.setSquelchCallback([&](bool open) { squelchWasOpen = squelchWasOpen || open; });
    demod.setAudioCallback(
        [&](const int16_t* pcm, size_t count) { allPcm.insert(allPcm.end(), pcm, pcm + count); });

    auto src = WavIqSource::makeSyntheticFm(iqRate, toneHz, devHz, /*durationSeconds=*/1.0,
                                             /*noiseAmplitude=*/0.0);
    src.start([&](const IqSample* samples, size_t count) { demod.processSamples(samples, count); });

    BIEM_CHECK(squelchWasOpen);
    BIEM_CHECK(allPcm.size() > static_cast<size_t>(audioRate * 0.5));

    // Zero-crossing frequency estimate over the back half of the buffer
    // (skips filter/de-emphasis settling time at the start).
    size_t start = allPcm.size() / 2;
    int crossings = 0;
    for (size_t i = start + 1; i < allPcm.size(); ++i) {
        bool prevPos = allPcm[i - 1] >= 0;
        bool curPos = allPcm[i] >= 0;
        if (prevPos != curPos) ++crossings;
    }
    double seconds = static_cast<double>(allPcm.size() - start) / audioRate;
    double estimatedHz = (static_cast<double>(crossings) / 2.0) / seconds;

    BIEM_CHECK(estimatedHz > toneHz * 0.7);
    BIEM_CHECK(estimatedHz < toneHz * 1.3);
}

// Same recovery test but with substantial wideband noise added to the raw
// IQ (comparable in amplitude to the unit-amplitude carrier itself) -
// this is exactly the scenario that used to produce "clicking" instead of
// recognizable audio before the pre-discriminator channel filter existed
// (see NbfmDemodulator.h's channelHalfBandwidthHz). Looser tolerance than
// the clean-signal test: the point is "still recognizably ~1 kHz despite
// heavy noise", not precision.
void testNbfmRecoversAudioToneWithWidebandNoise() {
    const double iqRate = 48000.0;
    const double audioRate = 8000.0;
    const double toneHz = 1000.0;
    const double devHz = 2500.0;

    NbfmConfig cfg;
    cfg.iqSampleRateHz = iqRate;
    cfg.audioSampleRateHz = audioRate;
    cfg.maxDeviationHz = devHz;
    cfg.squelchThresholdDb = -60.0;
    NbfmDemodulator demod(cfg);

    std::vector<int16_t> allPcm;
    demod.setAudioCallback(
        [&](const int16_t* pcm, size_t count) { allPcm.insert(allPcm.end(), pcm, pcm + count); });

    auto src = WavIqSource::makeSyntheticFm(iqRate, toneHz, devHz, /*durationSeconds=*/1.0,
                                             /*noiseAmplitude=*/0.3);
    src.start([&](const IqSample* samples, size_t count) { demod.processSamples(samples, count); });

    BIEM_CHECK(allPcm.size() > static_cast<size_t>(audioRate * 0.5));

    size_t start = allPcm.size() / 2;
    int crossings = 0;
    for (size_t i = start + 1; i < allPcm.size(); ++i) {
        bool prevPos = allPcm[i - 1] >= 0;
        bool curPos = allPcm[i] >= 0;
        if (prevPos != curPos) ++crossings;
    }
    double seconds = static_cast<double>(allPcm.size() - start) / audioRate;
    double estimatedHz = (static_cast<double>(crossings) / 2.0) / seconds;

    BIEM_CHECK(estimatedHz > toneHz * 0.5);
    BIEM_CHECK(estimatedHz < toneHz * 1.5);
}

// Regression test for the "tik tik" (clicking, no voice) field report:
// models a zero-IF tuner's own DC/LO-leakage spike (see
// NbfmConfig::mixerOffsetHz) as a strong constant complex bias sitting at
// raw 0 Hz, with the actual wanted FM signal placed +50 kHz away - i.e.
// exactly what you get when the hardware is deliberately tuned 50 kHz
// below the channel, which is what biem_cli.cpp's runLive() now does
// instead of tuning exactly on-channel. Confirms that configuring
// mixerOffsetHz to match still recovers a clean, recognizable tone despite
// a spike stronger than the wanted signal itself sitting right where the
// channel filter used to be centered - the failure mode this exists to
// prevent (see the class comment on mixerOffsetHz for why on-channel
// tuning can't be fixed after the fact: there, the spike and the wanted
// signal are the same frequency, and no filter can separate that).
void testNbfmRecoversAudioDespiteDcSpikeWithMixerOffset() {
    const double iqRate = 240000.0; // matches biem_cli.cpp runLive()'s IQ rate
    const double audioRate = 8000.0;
    const double toneHz = 1000.0;
    const double devHz = 2500.0;
    const double offsetHz = 50000.0;

    NbfmConfig cfg;
    cfg.iqSampleRateHz = iqRate;
    cfg.audioSampleRateHz = audioRate;
    cfg.maxDeviationHz = devHz;
    cfg.squelchThresholdDb = -60.0;
    cfg.mixerOffsetHz = offsetHz;
    NbfmDemodulator demod(cfg);

    std::vector<int16_t> allPcm;
    demod.setAudioCallback(
        [&](const int16_t* pcm, size_t count) { allPcm.insert(allPcm.end(), pcm, pcm + count); });

    // DC spike amplitude (2.0) deliberately larger than the unit-amplitude
    // wanted carrier - real LO leakage is not necessarily weak relative to
    // a modest received signal.
    auto src = WavIqSource::makeSyntheticFm(iqRate, toneHz, devHz, /*durationSeconds=*/1.0,
                                             /*noiseAmplitude=*/0.02, /*carrierOffsetHz=*/offsetHz,
                                             /*dcSpikeAmplitude=*/2.0);
    src.start([&](const IqSample* samples, size_t count) { demod.processSamples(samples, count); });

    BIEM_CHECK(allPcm.size() > static_cast<size_t>(audioRate * 0.5));

    // Tone-correlation check (see toneCorrelation() above) instead of
    // zero-crossing counting: with the DC spike in play, a badly corrupted
    // discriminator output can still happen to cross zero at roughly the
    // right rate (zero-crossing counting tried this first and could not
    // tell the mixer-enabled and mixer-disabled cases apart), so this
    // needs a metric that actually measures waveform fidelity.
    size_t start = allPcm.size() / 2;
    double corr = toneCorrelation(allPcm, start, toneHz, audioRate);
    // Empirically: ~0.98 with the mixer correctly configured (this test),
    // ~0.77 with mixerOffsetHz left at 0 despite the offset signal (i.e.
    // the bug this guards against) - 0.85 sits well clear of both.
    BIEM_CHECK(corr > 0.85);
}

} // namespace

int main() {
    testNbfmRecoversAudioTone();
    testNbfmRecoversAudioToneWithWidebandNoise();
    testNbfmRecoversAudioDespiteDcSpikeWithMixerOffset();
    BIEM_TEST_MAIN_RETURN();
}
