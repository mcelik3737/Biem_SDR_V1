#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "CallRecord.h"

namespace biem::core {

class Database;
class WavWriter;

// Owns the "currently being recorded" call: takes PCM audio pushed to it by
// whichever demodulator/decoder is active (NbfmDemodulator, or a future DMR
// voice decoder), writes it to a WAV file, and on endCall() finalizes the
// file and inserts the CallRecord into the Database. Both the SDR path and
// the network/repeater path use the same CallRecorder type - the only
// difference is who calls beginCall()/pushAudio()/endCall() and what they
// fill into the CallRecord metadata.
class CallRecorder {
public:
    // `recordingsRootDir` is the base directory recordings are organized
    // under as <root>/<yyyy>/<mm>/<file>.wav. `sampleRateHz` is the fixed
    // output PCM sample rate all pushAudio() calls are expected to already
    // be at (resampling, if needed, happens upstream in the DSP chain).
    CallRecorder(Database& db, std::string recordingsRootDir, int sampleRateHz = 8000);
    ~CallRecorder();

    CallRecorder(const CallRecorder&) = delete;
    CallRecorder& operator=(const CallRecorder&) = delete;

    // Starts a new call. If one is already active, it is finalized first
    // (defensively - callers should normally call endCall() themselves).
    // `meta.startUnixTimeMs` is set to "now" if left at 0.
    void beginCall(CallRecord meta);

    void pushAudio(const int16_t* samples, size_t count);

    // Finalizes the WAV file, fills in duration/voiceDecoded, inserts the
    // record into the database, and returns it. Returns nullopt if no call
    // was active.
    std::optional<CallRecord> endCall();

    bool hasActiveCall() const { return active_.has_value(); }
    const std::optional<CallRecord>& activeCallMeta() const { return active_; }

private:
    Database& db_;
    std::string recordingsRootDir_;
    int sampleRateHz_;

    std::optional<CallRecord> active_;
    std::unique_ptr<WavWriter> writer_;

    std::string buildFilePath(const CallRecord& meta) const;
};

} // namespace biem::core
