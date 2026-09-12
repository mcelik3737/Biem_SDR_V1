#include "RtlSdrSource.h"

#ifdef BIEM_HAVE_RTLSDR

#include <rtl-sdr.h>

#include <stdexcept>

namespace biem::dsp {

namespace {
// librtlsdr requires buf_len to be a multiple of 512; these are reasonable,
// explicit defaults rather than relying on undocumented zero-means-default
// behaviour that has varied across librtlsdr versions.
constexpr uint32_t kBufNum = 15;
constexpr uint32_t kBufLen = 16 * 16384;
} // namespace

RtlSdrSource::RtlSdrSource(int deviceIndex) : deviceIndex_(deviceIndex) {}

RtlSdrSource::~RtlSdrSource() {
    stop();
    close();
}

bool RtlSdrSource::open() {
    if (dev_) return true;
    rtlsdr_dev_t* devT = nullptr;
    if (rtlsdr_open(&devT, static_cast<uint32_t>(deviceIndex_)) != 0) {
        return false;
    }
    dev_ = devT;
    rtlsdr_set_sample_rate(dev_, static_cast<uint32_t>(sampleRateHz_));
    if (centerFrequencyHz_ > 0.0) {
        rtlsdr_set_center_freq(dev_, static_cast<uint32_t>(centerFrequencyHz_));
    }
    rtlsdr_set_tuner_gain_mode(dev_, 0); // auto gain by default; call setGainTenthDb() to override
    rtlsdr_reset_buffer(dev_);
    return true;
}

void RtlSdrSource::close() {
    if (dev_) {
        rtlsdr_close(dev_);
        dev_ = nullptr;
    }
}

bool RtlSdrSource::setCenterFrequencyHz(double hz) {
    centerFrequencyHz_ = hz;
    if (dev_) {
        return rtlsdr_set_center_freq(dev_, static_cast<uint32_t>(hz)) == 0;
    }
    return true;
}

bool RtlSdrSource::setSampleRateHz(double hz) {
    sampleRateHz_ = hz;
    if (dev_) {
        return rtlsdr_set_sample_rate(dev_, static_cast<uint32_t>(hz)) == 0;
    }
    return true;
}

bool RtlSdrSource::setGainTenthDb(int gainTenthDb) {
    if (!dev_) return false;
    if (gainTenthDb < 0) {
        return rtlsdr_set_tuner_gain_mode(dev_, 0) == 0;
    }
    if (rtlsdr_set_tuner_gain_mode(dev_, 1) != 0) return false;
    return rtlsdr_set_tuner_gain(dev_, gainTenthDb) == 0;
}

void RtlSdrSource::staticReadCallback(unsigned char* buf, uint32_t len, void* ctx) {
    reinterpret_cast<RtlSdrSource*>(ctx)->handleRawBuffer(buf, len);
}

void RtlSdrSource::handleRawBuffer(const unsigned char* buf, uint32_t len) {
    // RTL-SDR delivers unsigned 8-bit interleaved I/Q samples (0..255,
    // centered at 127.5) - normalize to roughly [-1, 1].
    size_t nSamples = len / 2;
    convertScratch_.resize(nSamples);
    for (size_t i = 0; i < nSamples; ++i) {
        float re = (static_cast<float>(buf[2 * i]) - 127.5f) / 127.5f;
        float im = (static_cast<float>(buf[2 * i + 1]) - 127.5f) / 127.5f;
        convertScratch_[i] = IqSample(re, im);
    }
    if (callback_) callback_(convertScratch_.data(), convertScratch_.size());
}

void RtlSdrSource::start(SampleCallback cb) {
    if (running_) return;
    if (!dev_ && !open()) {
        throw std::runtime_error("RtlSdrSource::start: failed to open RTL-SDR device");
    }
    callback_ = std::move(cb);
    running_ = true;
    workerThread_ = std::thread([this]() {
        rtlsdr_read_async(dev_, &RtlSdrSource::staticReadCallback, this, kBufNum, kBufLen);
        running_ = false;
    });
}

void RtlSdrSource::stop() {
    if (dev_ && running_) {
        rtlsdr_cancel_async(dev_);
    }
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    running_ = false;
}

} // namespace biem::dsp

#endif // BIEM_HAVE_RTLSDR
