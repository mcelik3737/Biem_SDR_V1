#pragma once

#include <array>
#include <cstdint>

namespace biem::dsp::dmr {

// One physical DMR burst: 264 bits = 33 bytes, MSB-first within each byte
// (bit 7 of byte 0 is the first bit transmitted). This packing matches the
// byte-indexed bit manipulation DmrSlotDecoder ports from a cited
// reference - see docs/DMR_NOTES.md.
//
// NOTE: producing an aligned sequence of these from a continuous
// demodulated bitstream (symbol timing recovery, then walking fixed
// 264-bit steps from a DmrFrameSync hit) is NOT implemented in this
// repo yet - see docs/ROADMAP.md.
using DmrBurstBytes = std::array<uint8_t, 33>;

// The 4-bit "Data Type" field carried in Slot Type, meaningful for
// data/control sync bursts (BsSourcedData / MsSourcedData - see
// DmrConstants::SyncType). Voice bursts (Frame A, sync = *SourcedVoice; and
// Frames B-F, no full sync match at all - they use embedded signalling
// instead) are identified via the SYNC TYPE, not this field - see
// DmrCallTracker. This specific 0-9 code table is the commonly-cited DMR
// Data Type enumeration; it was NOT independently re-derived/verified in
// this session's research pass (unlike the sync words and BPTC/Golay
// constants in DmrConstants.h) - treat it with the same caution.
enum class DmrDataType {
    PiHeader,
    VoiceLcHeader,
    Terminator,
    Csbk,
    MultiBlockControlHeader,
    MultiBlockControlContinuation,
    DataHeader,
    Rate12Data,
    Rate34Data,
    Idle,
    Reserved,
    Unknown
};

inline DmrDataType dataTypeFromCode(int code4bits) {
    switch (code4bits & 0x0F) {
        case 0: return DmrDataType::PiHeader;
        case 1: return DmrDataType::VoiceLcHeader;
        case 2: return DmrDataType::Terminator;
        case 3: return DmrDataType::Csbk;
        case 4: return DmrDataType::MultiBlockControlHeader;
        case 5: return DmrDataType::MultiBlockControlContinuation;
        case 6: return DmrDataType::DataHeader;
        case 7: return DmrDataType::Rate12Data;
        case 8: return DmrDataType::Rate34Data;
        case 9: return DmrDataType::Idle;
        default: return DmrDataType::Reserved;
    }
}

} // namespace biem::dsp::dmr
