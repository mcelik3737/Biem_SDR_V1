// End-to-end sanity test for the analog NBFM chain: generate a known
// FM-modulated tone entirely in memory (no hardware, no file), demodulate
// it, and confirm the recovered audio's frequency is roughly right via
// zero-crossing counting.
#include "test_util.h"

#include <vector>

#include "../src/dsp/NbfmDemodulator.h"
#include "../src/dsp/WavIqSource.h"

using namespace biem::dsp;

namespace {

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

} // namespace

int main() {
    testNbfmRecoversAudioTone();
    BIEM_TEST_MAIN_RETURN();
}
