#include "Hamming1511.h"

namespace biem::dsp::dmr {

std::array<uint8_t, 15> Hamming1511::encode(const std::array<uint8_t, 11>& data) {
    std::array<uint8_t, 15> cw{};
    for (int i = 0; i < 11; ++i) {
        cw[kDataPositions[i] - 1] = data[i];
    }
    for (int p = 0; p < 4; ++p) {
        int mask = 1 << p;
        uint8_t parity = 0;
        for (int pos = 1; pos <= 15; ++pos) {
            if (pos == kParityPositions[p]) continue; // solving for this bit; exclude self
            if (pos & mask) parity ^= cw[pos - 1];
        }
        cw[kParityPositions[p] - 1] = parity;
    }
    return cw;
}

bool Hamming1511::decode(std::array<uint8_t, 15>& cw, std::array<uint8_t, 11>& dataOut) {
    int syndrome = 0;
    for (int p = 0; p < 4; ++p) {
        int mask = 1 << p;
        uint8_t parity = 0;
        for (int pos = 1; pos <= 15; ++pos) {
            if (pos & mask) parity ^= cw[pos - 1]; // includes the parity bit itself this time
        }
        if (parity) syndrome |= mask;
    }
    bool corrected = false;
    if (syndrome != 0 && syndrome <= 15) {
        cw[syndrome - 1] ^= 1;
        corrected = true;
    }
    for (int i = 0; i < 11; ++i) {
        dataOut[i] = cw[kDataPositions[i] - 1];
    }
    return corrected;
}

} // namespace biem::dsp::dmr
