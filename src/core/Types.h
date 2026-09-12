#pragma once

#include <cstdint>
#include <optional>

namespace biem::core {

enum class Modulation {
    AnalogFM,
    DmrDigital,
    Unknown
};

enum class CallSource {
    Sdr,             // received over the air via RTL-SDR and demodulated locally
    NetworkRepeater  // received from a repeater's own network/UDP feed
};

enum class ChannelSpacing {
    Hz6250  = 6250,
    Hz12500 = 12500,
    Hz25000 = 25000
};

struct GpsFix {
    double latitude = 0.0;
    double longitude = 0.0;
    std::optional<double> altitudeMeters;
    int64_t unixTimeMs = 0;
};

inline const char* toString(Modulation m) {
    switch (m) {
        case Modulation::AnalogFM:   return "AnalogFM";
        case Modulation::DmrDigital: return "DmrDigital";
        default:                     return "Unknown";
    }
}

inline const char* toString(CallSource s) {
    switch (s) {
        case CallSource::Sdr:             return "Sdr";
        case CallSource::NetworkRepeater: return "NetworkRepeater";
    }
    return "Sdr";
}

} // namespace biem::core
