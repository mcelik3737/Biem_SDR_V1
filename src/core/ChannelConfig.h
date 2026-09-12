#pragma once

#include <string>

#include "Types.h"

namespace biem::core {

// A configured monitoring channel (one entry in the "channel list" the user
// manages in the UI / a config file). Independent of the DMR/network
// ingest path, which uses its own configuration (see net/hytera/HyteraHR659Source.h).
struct ChannelConfig {
    std::string label;                    // display name, e.g. "Saha-1 UHF"
    double frequencyHz = 0.0;
    ChannelSpacing spacing = ChannelSpacing::Hz12500;
    Modulation modulation = Modulation::AnalogFM;
    bool enabled = true;

    // Analog FM only: squelch threshold in dBFS-ish units used by
    // NbfmDemodulator's simple level squelch. Ignored for DMR.
    double squelchThresholdDb = -50.0;
};

} // namespace biem::core
