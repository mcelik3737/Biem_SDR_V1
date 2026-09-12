#pragma once

#include <cstddef>
#include <functional>

#include "IqSample.h"

namespace biem::dsp {

// Abstract source of complex baseband I/Q samples at a fixed sample rate,
// tuned to a fixed center frequency. Two implementations exist:
//   - WavIqSource:  file/synthetic, always built (no hardware dependency)
//   - RtlSdrSource: live RTL-SDR USB dongle, only built if CMake found
//                   librtlsdr (see top-level CMakeLists.txt)
// Callers that want to work regardless of which backend is available
// (e.g. biem_cli) should code against this interface, not the concrete
// classes, and check at runtime/compile-time (BIEM_HAVE_RTLSDR) whether the
// hardware backend exists at all.
class IqSource {
public:
    using SampleCallback = std::function<void(const IqSample* samples, size_t count)>;

    virtual ~IqSource() = default;

    virtual bool open() = 0;
    virtual void close() = 0;

    virtual bool setCenterFrequencyHz(double hz) = 0;
    virtual double centerFrequencyHz() const = 0;

    virtual bool setSampleRateHz(double hz) = 0;
    virtual double sampleRateHz() const = 0;

    // Starts delivering samples to `cb`. Hardware sources run this from a
    // background thread and return immediately; call stop() to end it.
    // File/synthetic sources may instead run synchronously and return only
    // once exhausted - see the concrete class's own documentation.
    virtual void start(SampleCallback cb) = 0;
    virtual void stop() = 0;

    virtual bool isRunning() const = 0;
};

} // namespace biem::dsp
