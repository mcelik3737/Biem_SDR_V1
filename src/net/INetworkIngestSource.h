#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "../core/CallRecord.h"

namespace biem::net {

// A source of calls arriving over a repeater's network feed rather than
// demodulated locally from RF. Whatever implements this (currently only
// net/hytera/HyteraHR659Source, a stub) feeds the exact same
// biem::core::CallRecorder / Database / search UI pipeline the SDR/DMR
// path does - the rest of the app doesn't need to know which source a
// call came from.
class INetworkIngestSource {
public:
    using CallStartCallback = std::function<void(core::CallRecord meta)>;
    using AudioCallback = std::function<void(const int16_t* pcm, size_t count)>;
    using CallEndCallback = std::function<void()>;

    virtual ~INetworkIngestSource() = default;

    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;

    virtual void setCallStartCallback(CallStartCallback cb) = 0;
    virtual void setAudioCallback(AudioCallback cb) = 0;
    virtual void setCallEndCallback(CallEndCallback cb) = 0;
};

} // namespace biem::net
