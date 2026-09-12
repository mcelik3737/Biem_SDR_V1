#include "Hamming139.h"

namespace biem::dsp::dmr {

std::array<uint8_t, 13> Hamming139::encode(const std::array<uint8_t, 9>& data) {
    std::array<uint8_t, 13> cw{};
    for (int i = 0; i < 9; ++i) {
        cw[kDataPositions[i] - 1] = data[i];
    }
    for (int p = 0; p < 4; ++p) {
        int mask = 1 << p;
        uint8_t parity = 0;
        for (int pos = 1; pos <= 13; ++pos) {
            if (pos == kParityPositions[p]) continue;
            if (pos & mask) parity ^= cw[pos - 1];
        }
        cw[kParityPositions[p] - 1] = parity;
    }
    return cw;
}

bool Hamming139::decode(std::array<uint8_t, 13>& cw, std::array<uint8_t, 9>& dataOut) {
    int syndrome = 0;
    for (int p = 0; p < 4; ++p) {
        int mask = 1 << p;
        uint8_t parity = 0;
        for (int pos = 1; pos <= 13; ++pos) {
            if (pos & mask) parity ^= cw[pos - 1];
        }
        if (parity) syndrome |= mask;
    }
    bool corrected = false;
    if (syndrome != 0 && syndrome <= 13) {
        cw[syndrome - 1] ^= 1;
        corrected = true;
    }
    for (int i = 0; i < 9; ++i) {
        dataOut[i] = cw[kDataPositions[i] - 1];
    }
    return corrected;
}

} // namespace biem::dsp::dmr
