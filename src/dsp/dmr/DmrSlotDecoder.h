#pragma once

#include <array>
#include <cstdint>

#include "DmrBurst.h"
#include "Golay2087.h"

namespace biem::dsp::dmr {

struct SlotTypeInfo {
    int colorCode = 0;
    DmrDataType dataType = DmrDataType::Unknown;
    int correctedBits = -1; // see Golay2087::decode's doc comment for the meaning of this value
};

// Extracts and Golay-decodes the Slot Type field from a raw 264-bit/33-byte
// DMR burst, and separately extracts the two Info fields for BPTC decode.
class DmrSlotDecoder {
public:
    DmrSlotDecoder();

    // Byte-offset bit manipulation here is a direct port (see
    // docs/DMR_NOTES.md) of g4klx/MMDVMHost's DMRSlotType.cpp extraction -
    // the one piece of burst-layout-specific code in this module with a
    // real citation behind its exact bit offsets, not just structural
    // plausibility. Feeds the extracted 19-bit field into Golay2087.
    SlotTypeInfo decodeSlotType(const DmrBurstBytes& burst) const;

    // Best-effort extraction of the two ~98-bit Info fields surrounding
    // the Slot Type/Sync fields, packed into a 196-bit array ready for
    // Bptc196x96::decode(). *** NOT independently verified *** - see
    // docs/DMR_NOTES.md and Bptc196x96.h.
    std::array<uint8_t, 196> extractInfoBitsForBptc(const DmrBurstBytes& burst) const;

private:
    Golay2087 golay_; // owns the syndrome table (built once, not per call)
};

} // namespace biem::dsp::dmr
