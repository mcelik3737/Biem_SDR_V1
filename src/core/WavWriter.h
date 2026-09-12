#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>

namespace biem::core {

// Minimal, dependency-free PCM16 WAV file writer. Writes a placeholder
// 44-byte canonical header up front, appends raw little-endian PCM16
// samples as they arrive, and patches the RIFF/data chunk sizes on
// finalize() (or in the destructor, if not called explicitly) so the file
// is always left as a valid, playable .wav even if the process is
// interrupted mid-call - only the *duration* would be short, not the file
// broken, since every writeSamples() call is a plain append.
class WavWriter {
public:
    WavWriter(const std::string& path, int sampleRateHz, int numChannels = 1);
    ~WavWriter();

    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    bool isOpen() const;

    // Appends `count` int16 samples (interleaved if numChannels > 1).
    void writeSamples(const int16_t* samples, size_t count);

    // Patches the RIFF/data chunk sizes. Safe to call multiple times; only
    // the first call after the last write has an effect until more samples
    // are written (there is no use case here for writing after finalize,
    // so a second finalize() is simply a no-op).
    void finalize();

    uint64_t samplesWritten() const { return samplesWritten_; }
    double durationSeconds() const;

private:
    std::fstream file_;
    int sampleRateHz_;
    int numChannels_;
    uint64_t samplesWritten_ = 0; // total samples across all channels
    bool finalized_ = false;

    void writeHeaderPlaceholder();
};

} // namespace biem::core
