#include "DmrRfDemodulator.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "DmrFrameSync.h"

namespace biem::dsp::dmr {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kSquelchPowerAvgAlpha = 0.001; // same as NbfmDemodulator - ~ms-scale exponential average
constexpr double kCenterEmaAlpha = 0.001;       // slow - tracks residual freq offset, not symbol content
// Fast gate for symbol-level processing - see fastPowerAvg_'s header
// comment for why this needs to be much faster than kSquelchPowerAvgAlpha.
// Needs to be fast enough that the lag between a real signal actually
// ending and this noticing (a 60dB EMA transition takes roughly
// 13.8 time constants) stays well under one symbol period (50 samples at
// the live iqSampleRateHz) - a slower gate leaks a few trailing "garbage"
// bits from the decaying tail into BurstAligner right after a real
// burst, which is exactly the kind of small bit-position slop
// BurstAligner's kSyncPositionToleranceBits (DmrConstants.h) exists to
// absorb, but there's no reason to lean on that tolerance more than
// necessary. Time constant 1/0.25 = 4 samples -> 60dB settles in ~55
// samples, close to one symbol period.
constexpr double kFastGatePowerAvgAlpha = 0.25;

// Packs exactly kBurstTotalBits (264) 0/1 values, oldest-first, into a
// 33-byte DmrBurstBytes, MSB-first within each byte - matches the
// convention documented in DmrBurst.h ("bit 7 of byte 0 is the first bit
// transmitted").
void packBits(const std::vector<uint8_t>& bits, DmrBurstBytes& out) {
    for (size_t byteIdx = 0; byteIdx < out.size(); ++byteIdx) {
        uint8_t b = 0;
        for (int bitIdx = 0; bitIdx < 8; ++bitIdx) {
            b = static_cast<uint8_t>((b << 1) | (bits[byteIdx * 8 + static_cast<size_t>(bitIdx)] & 1u));
        }
        out[byteIdx] = b;
    }
}

// Slices a centered discriminator value (Hz, DC-offset already removed by
// the caller) into a dibit, per DmrConstants::kDmrSliceThresholdHz - see
// that header for the full citation. Returns {highBit, lowBit} in
// transmission order (high bit first, matching how the dibit->level table
// was documented: "0xC0 (dibit 11) -> highest level", i.e. the first-read
// bit is the more significant one).
std::pair<uint8_t, uint8_t> sliceToDibit(double centeredHz) {
    if (centeredHz < -kDmrSliceThresholdHz) return {0, 1}; // dibit 01 - lowest level
    if (centeredHz < 0.0) return {0, 0};                   // dibit 00
    if (centeredHz < kDmrSliceThresholdHz) return {1, 0};  // dibit 10
    return {1, 1};                                         // dibit 11 - highest level
}

} // namespace

// One candidate symbol-timing phase: owns its own DmrFrameSync + rolling
// bit history, and knows how to assemble a full burst once a sync word is
// found and the bits following it arrive. Used both as one of many
// parallel candidates during acquisition and as the sole active tracker
// once locked (see DmrRfDemodulator::processSamples).
struct DmrRfDemodulator::BurstAligner {
    DmrFrameSync frameSync;
    std::vector<uint8_t> history; // 0/1 values, oldest-first, capped at kBurstTotalBits
    int64_t bitCounter = 0;       // total bits pushed so far, monotonic
    int64_t lastSyncBitPos = -1;  // bitCounter-1 at the last (unconfirmed-or-verified) sync seen; -1 = none yet
    int64_t nextExpectedSyncBitPos = -1; // once verified: where the NEXT sync must land to stay locked
    bool verified = false;        // true once two syncs exactly kBurstTotalBits apart have been seen
    int pendingCountdown = -1;    // bits still needed before the CONFIRMED burst is complete; -1 = idle
    SyncType pendingType = SyncType::Unknown;
    bool syncFiredThisBit = false;
    SyncType firedType = SyncType::Unknown;

    BurstAligner() {
        history.reserve(static_cast<size_t>(kBurstTotalBits));
        frameSync.setSyncCallback([this](SyncDetection det) {
            syncFiredThisBit = true;
            firedType = det.type;
        });
    }

    // Returns true (and fills outBurst/outType) exactly when this call
    // completes a CONFIRMED 264-bit burst.
    //
    // A single 48-bit sync match (even within DmrConstants::
    // kSyncMaxHammingDistance) is NOT, by itself, trustworthy enough to
    // lock onto - earlier code here treated "sync found, then 108 more
    // bits arrive" as sufficient, but those 108 bits were never validated
    // against anything, so a single spurious match (which, across the ~50
    // parallel acquisition phases and thousands of bits each, is not
    // nearly as rare as it sounds) would deterministically produce a fake
    // burst. This was caught by tests/test_dmr_rf.cpp's pure-noise test
    // actually locking on nothing.
    //
    // Fix: require TWO sync detections exactly kBurstTotalBits (264) bits
    // apart before trusting the phase - a standard "acquire, then verify"
    // pattern. This cuts the false-lock probability from "one spurious
    // 48-bit match" to "two independent spurious matches at one specific
    // required bit spacing", which is negligible (see the commit message
    // for the actual numbers). The cost: the FIRST burst of any real
    // transmission is consumed purely as the verification reference and
    // is never itself reported - only the second and later bursts are
    // (one burst, ~27.5 ms, of acquisition latency). A completely
    // standard trade-off for frame synchronizers, not something to "fix"
    // away.
    bool pushBit(uint8_t bit, DmrBurstBytes& outBurst, SyncType& outType) {
        history.push_back(bit & 1u);
        if (history.size() > static_cast<size_t>(kBurstTotalBits)) {
            history.erase(history.begin());
        }
        ++bitCounter;

        // Decrement using the countdown's value from BEFORE this call's
        // sync handling below can refresh it - a sync's own last bit must
        // not count as one of the kBurstBitsAfterSync bits that follow it
        // (same reasoning as the single-sync version this replaced).
        bool completedNow = false;
        if (pendingCountdown > 0) {
            --pendingCountdown;
            if (pendingCountdown == 0) completedNow = true;
        }

        syncFiredThisBit = false;
        frameSync.pushBit(bit);

        if (syncFiredThisBit) {
            int64_t thisSyncPos = bitCounter - 1; // position of the bit just pushed
            // Both comparisons below allow a small slop
            // (kSyncPositionToleranceBits - see DmrConstants.h for why:
            // short of it, extraction still anchors to thisSyncPos - the
            // ACTUAL detected position - so a real signal is unaffected
            // by its own past slop, only this check needed the room).
            if (verified && std::abs(thisSyncPos - nextExpectedSyncBitPos) <= kSyncPositionToleranceBits) {
                // Still locked: this burst's sync landed where expected
                // (within tolerance). Arm extraction of THIS burst and
                // schedule where the next one must land, based on where
                // THIS one actually was (self-correcting, not drifting
                // from a purely theoretical fixed grid).
                nextExpectedSyncBitPos = thisSyncPos + kBurstTotalBits;
                pendingCountdown = kBurstBitsAfterSync;
                pendingType = firedType;
            } else if (!verified && lastSyncBitPos >= 0 &&
                       std::abs(thisSyncPos - lastSyncBitPos - kBurstTotalBits) <= kSyncPositionToleranceBits) {
                // Two syncs one burst-length apart (within tolerance):
                // confirmed. The burst that just confirmed (not the
                // earlier candidate, which has already scrolled out of
                // history) is the first one actually extracted.
                verified = true;
                nextExpectedSyncBitPos = thisSyncPos + kBurstTotalBits;
                pendingCountdown = kBurstBitsAfterSync;
                pendingType = firedType;
            } else {
                // Either the first sync ever seen, or a verified lock
                // just missed its expected position (lost sync - noise,
                // fading, or a real gap in transmission) - either way,
                // fall back to treating this as a fresh unconfirmed
                // candidate rather than silently keeping a stale lock.
                verified = false;
                lastSyncBitPos = thisSyncPos;
                pendingCountdown = -1;
            }
        }

        if (completedNow && verified && history.size() == static_cast<size_t>(kBurstTotalBits)) {
            packBits(history, outBurst);
            outType = pendingType;
            return true;
        }
        return false;
    }
};

DmrRfDemodulator::DmrRfDemodulator(DmrRfConfig config) : config_(config) {
    mixerPhaseIncRad_ = -2.0 * kPi * config_.mixerOffsetHz / config_.iqSampleRateHz;
    channelLpfAlpha_ = 1.0 - std::exp(-2.0 * kPi * config_.channelHalfBandwidthHz / config_.iqSampleRateHz);
    samplesPerSymbol_ = std::max(1, static_cast<int>(std::lround(config_.iqSampleRateHz / kDmrSymbolRateHz)));
    // Trailing boxcar average of the raw discriminator output, used only
    // to pick the symbol-decision value (see processSamples) - a crude,
    // cheap stand-in for a real matched filter, smoothing out single-
    // sample noise/glitches right at a decision instant. Window = half a
    // symbol period: long enough to help, short enough not to smear
    // across a neighboring symbol.
    //
    // NOTE on what this does NOT fix: a real bit-perfect round-trip test
    // failure was chased during development and initially blamed on
    // channel-filter transient settling (hence this boxcar) - but adding
    // it barely moved the error count. The actual cause turned out to be
    // centerEma_ below legitimately lagging behind a hand-picked test
    // fill pattern with a strong, unrealistic average DC bias (repeating
    // raw bytes, not real Golay/BPTC/CRC-scrambled content, which is
    // naturally close to balanced) - fixed in the test data, not here
    // (see tests/test_dmr_rf.cpp and the commit message for the full
    // story). Kept boxcar averaging anyway since it's a real, if modest,
    // improvement for actual sample-level noise - just not what fixed
    // that particular bug.
    boxcarWindow_ = std::max(1, samplesPerSymbol_ / 2);
    boxcarBuf_.assign(static_cast<size_t>(boxcarWindow_), 0.0);
    // 150 ms: comfortably longer than one TDMA slot's ~30ms silent half
    // (see processSamples()'s squelch-reopen comment) but much shorter
    // than a real gap between separate transmissions.
    minSilenceForReacquireSamples_ = static_cast<uint64_t>(0.15 * config_.iqSampleRateHz);
    resetAcquisition();
}

DmrRfDemodulator::~DmrRfDemodulator() = default;

void DmrRfDemodulator::resetAcquisition() {
    locked_ = false;
    lockedPhase_ = -1;
    lockedTrack_.reset();
    acquisitionTracks_.clear();
    acquisitionTracks_.reserve(static_cast<size_t>(samplesPerSymbol_));
    for (int i = 0; i < samplesPerSymbol_; ++i) {
        acquisitionTracks_.push_back(std::make_unique<BurstAligner>());
    }
}

void DmrRfDemodulator::setLevelCallback(LevelCallback cb, uint64_t everyNSamples) {
    levelCb_ = std::move(cb);
    levelReportInterval_ = everyNSamples != 0 ? everyNSamples : static_cast<uint64_t>(config_.iqSampleRateHz);
}

double DmrRfDemodulator::discriminate(const IqSample& s) {
    IqSample prod = s * std::conj(prevSample_);
    prevSample_ = s;
    return std::atan2(static_cast<double>(prod.imag()), static_cast<double>(prod.real()));
}

void DmrRfDemodulator::processSamples(const IqSample* samples, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const IqSample& s = samples[i];

        // --- mixer (see mixerOffsetHz) ---
        IqSample mixed = s;
        if (mixerPhaseIncRad_ != 0.0) {
            IqSample mixer(static_cast<float>(std::cos(mixerPhaseRad_)),
                           static_cast<float>(std::sin(mixerPhaseRad_)));
            mixed = s * mixer;
            mixerPhaseRad_ += mixerPhaseIncRad_;
            if (mixerPhaseRad_ > kPi) {
                mixerPhaseRad_ -= 2.0 * kPi;
            } else if (mixerPhaseRad_ < -kPi) {
                mixerPhaseRad_ += 2.0 * kPi;
            }
        }

        // --- channel-select filter ---
        channelLpfState_ = IqSample(
            channelLpfState_.real() +
                static_cast<float>(channelLpfAlpha_) * (mixed.real() - channelLpfState_.real()),
            channelLpfState_.imag() +
                static_cast<float>(channelLpfAlpha_) * (mixed.imag() - channelLpfState_.imag()));
        const IqSample& filtered = channelLpfState_;

        // --- squelch (SLOW - public callback / call-boundary semantics
        // only, identical approach to NbfmDemodulator; deliberately does
        // NOT gate anything below - see fastPowerAvg_'s header comment) ---
        double instPower = static_cast<double>(filtered.real()) * filtered.real() +
                            static_cast<double>(filtered.imag()) * filtered.imag();
        squelchPowerAvg_ += kSquelchPowerAvgAlpha * (instPower - squelchPowerAvg_);
        double powerDb = 10.0 * std::log10(std::max(squelchPowerAvg_, 1e-12));
        bool newSquelchOpen = powerDb > config_.squelchThresholdDb;
        if (newSquelchOpen != squelchOpen_) {
            squelchOpen_ = newSquelchOpen;
            if (squelchCb_) squelchCb_(squelchOpen_);
        }

        // --- fast gate (internal only - see fastPowerAvg_'s header
        // comment for why symbol processing needs this instead of the
        // slow squelch above) ---
        fastPowerAvg_ += kFastGatePowerAvgAlpha * (instPower - fastPowerAvg_);
        double fastPowerDb = 10.0 * std::log10(std::max(fastPowerAvg_, 1e-12));
        bool newFastGateOpen = fastPowerDb > config_.squelchThresholdDb;
        if (newFastGateOpen != fastGateOpen_) {
            fastGateOpen_ = newFastGateOpen;
            if (fastGateOpen_) {
                // Only re-acquire if the RF was genuinely gone for a
                // while (closedSampleCount_ past
                // minSilenceForReacquireSamples_) - NOT on every reopen.
                // A lone simplex DMR radio transmitting on only one TDMA
                // slot leaves the OTHER slot's ~30ms window truly
                // RF-silent throughout an otherwise continuous PTT hold,
                // so this fast gate legitimately closes every ~30ms
                // during perfectly normal reception (that's the whole
                // point of tracking it fast enough to notice) - resetting
                // acquisition on every one of those flaps discarded
                // verification progress almost as fast as it was made,
                // since two-sync confirmation needs the SAME phase's
                // history to survive across one full burst (see
                // BurstAligner). A real new transmission's gap is far
                // longer than one slot period, so this threshold still
                // resets for that case.
                if (closedSampleCount_ >= minSilenceForReacquireSamples_) {
                    resetAcquisition();
                }
            }
            closedSampleCount_ = 0;
        }
        if (!fastGateOpen_) ++closedSampleCount_;

        if (levelCb_ && levelReportInterval_ != 0) {
            if (++levelSampleCounter_ >= levelReportInterval_) {
                levelSampleCounter_ = 0;
                levelCb_(powerDb);
            }
        }

        // --- FM discriminator (same math as NbfmDemodulator - 4FSK is FM
        // with 4 levels instead of a continuous analog deviation) ---
        double freqRadPerSample = discriminate(filtered);
        double freqHz = freqRadPerSample * (config_.iqSampleRateHz / (2.0 * kPi));

        // --- trailing boxcar smoothing (see boxcarWindow_ comment) ---
        boxcarSum_ -= boxcarBuf_[boxcarPos_];
        boxcarBuf_[boxcarPos_] = freqHz;
        boxcarSum_ += freqHz;
        boxcarPos_ = (boxcarPos_ + 1) % boxcarBuf_.size();
        double smoothedFreqHz = boxcarSum_ / static_cast<double>(boxcarBuf_.size());

        centerEma_ += kCenterEmaAlpha * (smoothedFreqHz - centerEma_);

        uint64_t idx = sampleIndex_++;
        // Gating on the FAST gate here is INTENTIONAL and load-bearing,
        // not just an efficiency skip - see fastPowerAvg_'s header
        // comment for the real-world case this interacts with (and for
        // why the slow, public squelch above is deliberately NOT used
        // for this). Short version: a DMR TDMA frame is 60ms (two 30ms
        // slots) but a burst is only ~27.5ms; a lone simplex radio using
        // only slot 1 leaves slot 2's entire 30ms silent, so consecutive
        // REAL bursts are a full 60ms (576 bits at this symbol rate)
        // apart in absolute time, NOT the 264 bits BurstAligner's
        // two-sync verification requires. Skipping symbol decisions while
        // the fast gate is closed is what makes this work anyway: it
        // means the idle slot's dead time contributes ZERO bits to
        // BurstAligner's count, so consecutive slot-1 bursts still land
        // exactly kBurstTotalBits apart from the aligner's point of view,
        // regardless of how much real wall-clock silence separates them.
        // (Two earlier versions of this fix got this wrong in opposite
        // ways: one removed gating entirely - reasoning that processing
        // through silence is safe against false locks, which is true but
        // misses that this gate isn't about false locks, it's about not
        // corrupting real bursts' bit-spacing with dead-time bits: the
        // other kept gating but on the SLOW squelch tracker, whose
        // ~1000-sample time constant can't settle within one 7200-sample/
        // 30ms slot, so it never actually reads "closed" for a
        // single-slot gap and the dead time leaked through anyway.)
        if (!fastGateOpen_) continue;

        int phase = static_cast<int>(idx % static_cast<uint64_t>(samplesPerSymbol_));
        double centered = smoothedFreqHz - centerEma_;

        if (locked_) {
            if (phase != lockedPhase_) continue;
            auto [highBit, lowBit] = sliceToDibit(centered);
            DmrBurstBytes burst{};
            SyncType type = SyncType::Unknown;
            bool done = lockedTrack_->pushBit(highBit, burst, type);
            if (!done) done = lockedTrack_->pushBit(lowBit, burst, type);
            if (done && burstCb_) burstCb_(burst, type);
            continue;
        }

        // --- acquisition: exactly one track is "due" for this raw sample
        // (the one whose phase equals sampleIndex % samplesPerSymbol), so
        // this is O(1) work per sample in aggregate across all tracks, not
        // O(samplesPerSymbol) - see class comment. ---
        BurstAligner& track = *acquisitionTracks_[static_cast<size_t>(phase)];
        auto [highBit, lowBit] = sliceToDibit(centered);
        DmrBurstBytes burst{};
        SyncType type = SyncType::Unknown;
        bool done = track.pushBit(highBit, burst, type);
        if (!done) done = track.pushBit(lowBit, burst, type);
        if (done) {
            // This phase just assembled a complete, valid burst - far
            // stronger evidence of correct timing than a bare sync match
            // (which could happen spuriously on noise). Lock onto it.
            lockedTrack_ = std::move(acquisitionTracks_[static_cast<size_t>(phase)]);
            acquisitionTracks_.clear();
            locked_ = true;
            lockedPhase_ = phase;
            if (burstCb_) burstCb_(burst, type);
        }
    }
}

} // namespace biem::dsp::dmr
