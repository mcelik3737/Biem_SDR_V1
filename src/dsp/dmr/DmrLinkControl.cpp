#include "DmrLinkControl.h"

namespace biem::dsp::dmr {

namespace {
uint32_t bitsToByte(const std::array<uint8_t, 96>& bits, int startBit) {
    uint32_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 1) | static_cast<uint32_t>(bits[startBit + i] & 1u);
    return v;
}
} // namespace

LinkControlInfo DmrLinkControl::parse(const std::array<uint8_t, 96>& payloadBits) {
    LinkControlInfo info;

    uint32_t flcoByte = bitsToByte(payloadBits, 0);
    uint32_t flcoCode = flcoByte & 0x3Fu; // low 6 bits carry the FLCO value; top bits are PF/Reserved
    switch (flcoCode) {
        case 0x00: info.flco = Flco::GroupVoice; break;
        case 0x03: info.flco = Flco::PrivateVoice; break;
        default: info.flco = Flco::Other; break;
    }

    info.featureSetId = static_cast<uint8_t>(bitsToByte(payloadBits, 8));
    info.serviceOptions = static_cast<uint8_t>(bitsToByte(payloadBits, 16));

    info.groupOrDestAddress = (bitsToByte(payloadBits, 24) << 16) |
                              (bitsToByte(payloadBits, 32) << 8) |
                              bitsToByte(payloadBits, 40);

    info.sourceAddress = (bitsToByte(payloadBits, 48) << 16) |
                          (bitsToByte(payloadBits, 56) << 8) |
                          bitsToByte(payloadBits, 64);

    return info;
}

} // namespace biem::dsp::dmr
