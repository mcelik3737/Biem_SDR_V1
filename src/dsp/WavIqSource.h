#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "IqSource.h"

namespace biem::dsp {

// Offline / synthetic IqSource - no hardware, no external library, always
// built. Two ways to get one:
//
//  1. WavIqSource(path, sampleRateHz) - reads raw interleaved float32 I,Q
//     samples ("cf32", the same layout GNU Radio's file source/sink uses)
//     from disk. Useful for replaying a capture taken elsewhere.
//
//  2. WavIqSource::makeSyntheticFm(...) - generates an FM-modulated test
//     signal entirely in memory; no file involved. This is what
//     tests/test_nbfm.cpp uses to exercise NbfmDemodulator end-to-end
//     without any real hardware or captured file.
//
// start() delivers samples synchronously from the calling thread, in fixed
// chunks, as fast as it can - it does NOT pace itself to real time. That's
// correct for offline analysis/testing; nothing here is meant to look like
// a live feed.
class WavIqSource : public IqSource {
public:
    WavIqSource(std::string path, double sampleRateHz);

    static WavIqSource makeSyntheticFm(double sampleRateHz,
                                        double audioToneHz,
                                        double fmDeviationHz,
                                        double durationSeconds,
                                        double noiseAmplitude = 0.0);

    bool open() override;
    void close() override;

    bool setCenterFrequencyHz(double hz) override;
    double centerFrequencyHz() const override { return centerFrequencyHz_; }

    bool setSampleRateHz(double hz) override;
    double sampleRateHz() const override { return sampleRateHz_; }

    void start(SampleCallback cb) override;
    void stop() override;

    bool isRunning() const override { return running_; }

private:
    WavIqSource(); // used internally by makeSyntheticFm

    std::string path_;
    double sampleRateHz_ = 48000.0;
    double centerFrequencyHz_ = 0.0;
    bool running_ = false;
    bool fromMemory_ = false;
    std::vector<IqSample> memorySamples_;
};

} // namespace biem::dsp
