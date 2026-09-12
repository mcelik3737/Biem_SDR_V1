#pragma once

// Only declared/compiled when CMake found librtlsdr (BIEM_HAVE_RTLSDR) -
// see top-level CMakeLists.txt and src/CMakeLists.txt. There is
// deliberately no dummy/no-op fallback class under this name when the
// library is absent: code that needs live SDR hardware should be guarded
// with #ifdef BIEM_HAVE_RTLSDR at the call site, not by silently getting a
// class that compiles but never produces samples.
#ifdef BIEM_HAVE_RTLSDR

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "IqSource.h"

struct rtlsdr_dev; // opaque - matches librtlsdr's `struct rtlsdr_dev` (aka rtlsdr_dev_t)

namespace biem::dsp {

// Live RTL-SDR (Realtek RTL2832U-based USB dongle) hardware source, backed
// by librtlsdr. This is the same class of hardware already in use with
// SDRSharp (see AGENTS/task notes) - same Zadig/WinUSB driver setup applies.
//
// STATUS: written against the well-documented, long-stable librtlsdr C API,
// but NOT YET BUILT OR HARDWARE-TESTED in this repo's development
// environment (librtlsdr isn't installed there - see docs/BUILD_WINDOWS.md
// for the first real build+test against the actual dongle on Windows).
class RtlSdrSource : public IqSource {
public:
    explicit RtlSdrSource(int deviceIndex = 0);
    ~RtlSdrSource() override;

    RtlSdrSource(const RtlSdrSource&) = delete;
    RtlSdrSource& operator=(const RtlSdrSource&) = delete;

    bool open() override;
    void close() override;

    bool setCenterFrequencyHz(double hz) override;
    double centerFrequencyHz() const override { return centerFrequencyHz_; }

    bool setSampleRateHz(double hz) override;
    double sampleRateHz() const override { return sampleRateHz_; }

    // gainTenthDb < 0 => auto gain (rtlsdr_set_tuner_gain_mode(0)).
    // gainTenthDb >= 0 => manual gain in tenths of a dB, e.g. 400 = 40.0 dB,
    // as librtlsdr's rtlsdr_set_tuner_gain() expects.
    bool setGainTenthDb(int gainTenthDb);

    // Spawns a background thread running rtlsdr_read_async() and returns
    // immediately; samples arrive on `cb` from that background thread until
    // stop() is called.
    void start(SampleCallback cb) override;
    void stop() override;

    bool isRunning() const override { return running_; }

private:
    int deviceIndex_;
    double centerFrequencyHz_ = 0.0;
    double sampleRateHz_ = 240000.0;
    std::atomic<bool> running_{false};
    std::thread workerThread_;
    SampleCallback callback_;

    rtlsdr_dev* dev_ = nullptr;

    std::vector<IqSample> convertScratch_;

    static void staticReadCallback(unsigned char* buf, uint32_t len, void* ctx);
    void handleRawBuffer(const unsigned char* buf, uint32_t len);
};

} // namespace biem::dsp

#endif // BIEM_HAVE_RTLSDR
