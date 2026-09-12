#include "WavWriter.h"

namespace biem::core {

namespace {

void writeU32LE(std::ostream& os, uint32_t v) {
    char b[4] = {
        static_cast<char>(v & 0xFF),
        static_cast<char>((v >> 8) & 0xFF),
        static_cast<char>((v >> 16) & 0xFF),
        static_cast<char>((v >> 24) & 0xFF),
    };
    os.write(b, 4);
}

void writeU16LE(std::ostream& os, uint16_t v) {
    char b[2] = {
        static_cast<char>(v & 0xFF),
        static_cast<char>((v >> 8) & 0xFF),
    };
    os.write(b, 2);
}

} // namespace

WavWriter::WavWriter(const std::string& path, int sampleRateHz, int numChannels)
    : sampleRateHz_(sampleRateHz), numChannels_(numChannels) {
    // Create/truncate first, then reopen for read+write so finalize() can
    // seek back and patch the header - some standard library implementations
    // don't reliably support trunc combined with in|out in a single open().
    {
        std::ofstream create(path, std::ios::binary | std::ios::trunc);
    }
    file_.open(path, std::ios::binary | std::ios::in | std::ios::out);
    if (file_.is_open()) {
        writeHeaderPlaceholder();
    }
}

WavWriter::~WavWriter() {
    finalize();
}

bool WavWriter::isOpen() const {
    return file_.is_open();
}

void WavWriter::writeHeaderPlaceholder() {
    file_.seekp(0);
    file_.write("RIFF", 4);
    writeU32LE(file_, 0); // chunk size, patched in finalize()
    file_.write("WAVE", 4);

    file_.write("fmt ", 4);
    writeU32LE(file_, 16); // PCM subchunk1 size
    writeU16LE(file_, 1);  // audio format = PCM
    writeU16LE(file_, static_cast<uint16_t>(numChannels_));
    writeU32LE(file_, static_cast<uint32_t>(sampleRateHz_));
    uint32_t byteRate = static_cast<uint32_t>(sampleRateHz_) * static_cast<uint32_t>(numChannels_) * 2u;
    writeU32LE(file_, byteRate);
    writeU16LE(file_, static_cast<uint16_t>(numChannels_ * 2)); // block align
    writeU16LE(file_, 16); // bits per sample

    file_.write("data", 4);
    writeU32LE(file_, 0); // data size, patched in finalize()
    file_.flush();
}

void WavWriter::writeSamples(const int16_t* samples, size_t count) {
    if (!file_.is_open()) return;
    file_.seekp(0, std::ios::end);
    for (size_t idx = 0; idx < count; ++idx) {
        writeU16LE(file_, static_cast<uint16_t>(samples[idx]));
    }
    samplesWritten_ += count;
    finalized_ = false; // header sizes are now stale again
}

void WavWriter::finalize() {
    if (finalized_ || !file_.is_open()) return;
    uint32_t dataBytes = static_cast<uint32_t>(samplesWritten_ * sizeof(int16_t));
    uint32_t riffSize = 36 + dataBytes;

    file_.seekp(4);
    writeU32LE(file_, riffSize);
    file_.seekp(40);
    writeU32LE(file_, dataBytes);
    file_.flush();
    finalized_ = true;
}

double WavWriter::durationSeconds() const {
    if (sampleRateHz_ <= 0 || numChannels_ <= 0) return 0.0;
    return static_cast<double>(samplesWritten_) / static_cast<double>(sampleRateHz_ * numChannels_);
}

} // namespace biem::core
