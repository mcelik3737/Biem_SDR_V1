#include "WavIqSource.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace biem::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr size_t kChunkSamples = 4096;
} // namespace

WavIqSource::WavIqSource() = default;

WavIqSource::WavIqSource(std::string path, double sampleRateHz)
    : path_(std::move(path)), sampleRateHz_(sampleRateHz) {}

WavIqSource WavIqSource::makeSyntheticFm(double sampleRateHz,
                                          double audioToneHz,
                                          double fmDeviationHz,
                                          double durationSeconds,
                                          double noiseAmplitude) {
    WavIqSource src;
    src.sampleRateHz_ = sampleRateHz;
    src.fromMemory_ = true;

    size_t n = static_cast<size_t>(durationSeconds * sampleRateHz);
    src.memorySamples_.reserve(n);

    double phase = 0.0;

    // Small deterministic PRNG (xorshift-ish LCG) for reproducible additive
    // "noise" in a test signal - not meant to model any real channel, just
    // enough to make sure the demodulator/squelch aren't relying on a
    // perfectly noiseless input.
    uint32_t rngState = 0x1234567u;
    auto nextNoise = [&]() -> double {
        rngState = rngState * 1664525u + 1013904223u;
        double u = static_cast<double>(rngState) / 4294967296.0; // [0,1)
        return (u * 2.0 - 1.0) * noiseAmplitude;
    };

    for (size_t i = 0; i < n; ++i) {
        double t = static_cast<double>(i) / sampleRateHz;
        double audio = std::sin(2.0 * kPi * audioToneHz * t);
        double instFreq = fmDeviationHz * audio;
        phase += 2.0 * kPi * instFreq / sampleRateHz;
        float re = static_cast<float>(std::cos(phase) + nextNoise());
        float im = static_cast<float>(std::sin(phase) + nextNoise());
        src.memorySamples_.emplace_back(re, im);
    }
    return src;
}

bool WavIqSource::open() {
    if (fromMemory_) return true;
    std::ifstream test(path_, std::ios::binary);
    return test.good();
}

void WavIqSource::close() {}

bool WavIqSource::setCenterFrequencyHz(double hz) {
    centerFrequencyHz_ = hz;
    return true;
}

bool WavIqSource::setSampleRateHz(double hz) {
    sampleRateHz_ = hz;
    return true;
}

void WavIqSource::start(SampleCallback cb) {
    running_ = true;

    if (fromMemory_) {
        for (size_t offset = 0; offset < memorySamples_.size() && running_; offset += kChunkSamples) {
            size_t n = std::min(kChunkSamples, memorySamples_.size() - offset);
            cb(memorySamples_.data() + offset, n);
        }
        running_ = false;
        return;
    }

    std::ifstream file(path_, std::ios::binary);
    if (!file.good()) {
        running_ = false;
        return;
    }
    std::vector<float> raw(kChunkSamples * 2);
    std::vector<IqSample> buf(kChunkSamples);
    while (running_ && file.good()) {
        file.read(reinterpret_cast<char*>(raw.data()),
                   static_cast<std::streamsize>(raw.size() * sizeof(float)));
        std::streamsize got = file.gcount();
        size_t gotSamples = static_cast<size_t>(got) / (2 * sizeof(float));
        if (gotSamples == 0) break;
        for (size_t i = 0; i < gotSamples; ++i) {
            buf[i] = IqSample(raw[2 * i], raw[2 * i + 1]);
        }
        cb(buf.data(), gotSamples);
    }
    running_ = false;
}

void WavIqSource::stop() {
    running_ = false;
}

} // namespace biem::dsp
