#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "Types.h"

namespace biem::core {

// A single recorded call/transmission, regardless of whether it arrived via
// SDR (RF + local demodulation) or via a repeater's network feed. This is
// the one shared record type the whole app (recording, database, search UI)
// operates on.
struct CallRecord {
    int64_t id = 0;                 // DB primary key; 0 = not yet persisted
    int64_t startUnixTimeMs = 0;
    int64_t durationMs = 0;
    double frequencyHz = 0.0;       // 0 for pure network-sourced calls with no RF component
    Modulation modulation = Modulation::Unknown;
    CallSource source = CallSource::Sdr;

    // DMR / trunking metadata - empty for plain analog FM.
    std::optional<int> colorCode;
    std::optional<int> slot;              // 1 or 2 (DMR TDMA slot)
    std::optional<uint32_t> talkgroupId;  // "Grup"
    std::optional<uint32_t> radioId;      // "ID" (source radio)
    std::optional<uint32_t> destRadioId;  // private-call destination, if applicable
    std::optional<GpsFix> gps;
    std::optional<std::string> sdsText;   // short data service text payload, if any

    std::string channelLabel;   // e.g. "Saha-1 UHF" (configured channel name)
    std::string title;          // computed "Call Title" - see core/TitleBuilder.h
    std::string audioFilePath;  // WAV file on disk (empty if not yet finalized)

    // false when only metadata was captured and no voice decoder produced
    // audio (e.g. DMR without an AMBE+2 decoder wired in yet - see
    // docs/DMR_NOTES.md). The row still exists so the call isn't lost from
    // the log even though there's nothing to play back.
    bool voiceDecoded = false;
};

} // namespace biem::core
