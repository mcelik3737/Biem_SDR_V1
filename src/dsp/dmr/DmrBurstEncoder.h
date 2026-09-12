#pragma once

#include <cstdint>

#include "DmrBurst.h"
#include "DmrConstants.h"
#include "DmrLinkControl.h"

namespace biem::dsp::dmr {

// Builds a fully valid (Golay-encoded Slot Type + BPTC-encoded Link
// Control + a real 48-bit sync word) 264-bit DMR burst - for tests/the
// "dmr-demo" CLI command, NOT used by the live receive path. This is the
// exact bit-for-bit INVERSE of DmrSlotDecoder::decodeSlotType/
// extractInfoBitsForBptc (see DmrBurstEncoder.cpp - every mask/shift here
// has that function's corresponding line quoted right next to it), built
// specifically so a demo/test signal can drive Golay2087::encode and
// Bptc196x96::encode through a REAL burst layout - independently
// corroborating that those FEC primitives are wired correctly end to end
// through actual byte offsets, not just correct in the abstract
// 19-bit/96-bit domain tests/test_fec.cpp already checks.
DmrBurstBytes encodeVoiceLcHeaderBurst(SyncType syncType, int colorCode, Flco flco,
                                        uint32_t groupOrDestAddress, uint32_t sourceAddress);

// Same LC payload convention, dataType = Terminator instead of
// VoiceLcHeader - what DmrCallTracker::handleDataBurst ends a call on.
DmrBurstBytes encodeTerminatorBurst(SyncType syncType, int colorCode, Flco flco,
                                     uint32_t groupOrDestAddress, uint32_t sourceAddress);

} // namespace biem::dsp::dmr
