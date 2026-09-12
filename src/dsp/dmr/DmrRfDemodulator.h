#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "../IqSample.h"
#include "DmrBurst.h"
#include "DmrConstants.h"

namespace biem::dsp::dmr {

struct DmrRfConfig {
    double iqSampleRateHz = 240000.0;
    // Same DC-spike-avoidance mechanism as NbfmDemodulator (see its header
    // comment for the full rationale - the short version: zero-IF tuners
    // like the E4000 put a DC/LO-leakage spike exactly at the tuned
    // frequency, so real RF reception must tune off-channel and shift
    // back in software). Defaults to 0/disabled for the same reason
    // NbfmConfig::mixerOffsetHz does: it's meaningless for a source that
    // already delivers true-baseband IQ (a synthetic test signal). The
    // live RTL-SDR path (biem_cli.cpp's runDmrLive()) must set this
    // explicitly.
    double mixerOffsetHz = 0.0;

    // Complex low-pass cutoff before the discriminator - same role as
    // NbfmConfig::channelHalfBandwidthHz. DMR's occupied bandwidth is
    // roughly the outer deviation (kDmrOuterDeviationHz = 1944 Hz) plus
    // symbol-rate rolloff; this default leaves headroom within a 12.5 kHz
    // channel without needing to be razor-tight.
    double channelHalfBandwidthHz = 6300.0;

    // Same role/caveat as NbfmConfig::squelchThresholdDb - needs
    // calibrating against real hardware/RF environment, no universal
    // correct default. See biem_cli.cpp's live level-callback pattern.
    double squelchThresholdDb = -50.0;
};

// Bridges raw RTL-SDR IQ to burst-aligned DmrBurstBytes + SyncType for
// DmrCallTracker to consume. This is the piece docs/ROADMAP.md described
// as missing: "Producing [an] aligned sequence [of DmrBurstBytes] from a
// continuous discriminator/4FSK-symbol stream (symbol timing recovery,
// then walking fixed 264-bit steps out from a DmrFrameSync hit) is a
// separate piece, not yet implemented."
//
// Pipeline: mixer (mixerOffsetHz) -> complex channel filter -> FM
// discriminator (IDENTICAL math to NbfmDemodulator::discriminate - 4FSK
// *is* frequency modulation, just 4 levels instead of a continuous analog
// deviation, so the same atan2-of-conjugate-product technique applies
// unchanged) -> slow center-tracking (absorbs small residual Tx/Rx
// frequency offset before slicing) -> per-symbol slicing into a dibit (see
// DmrConstants::kDmr*) -> bit-level DmrFrameSync -> once TWO sync words
// are found exactly 264 bits apart (see below - not just one), a full
// 264-bit burst is assembled and handed to the burst callback.
//
// Symbol timing acquisition: while not locked, tries every one of the
// samplesPerSymbol possible sample-phase offsets IN PARALLEL, cheaply -
// see the .cpp: because each raw IQ sample belongs to exactly one phase
// (sampleIndex % samplesPerSymbol), the total symbol-decision work across
// all phases combined equals the work of tracking a single phase, not
// samplesPerSymbol times as much.
//
// A single 48-bit sync match within tolerance is NOT trusted by itself -
// an earlier version of this class did trust it (after also waiting for
// the 108 bits a real burst would have following it, but WITHOUT
// validating those bits against anything), and tests/test_dmr_rf.cpp's
// pure-noise test caught it locking onto nothing: with ~50 parallel
// phases each seeing thousands of noise bits, a spurious 48-bit match
// happens often enough to matter, and nothing was checking the bits after
// it. The fix, in BurstAligner (.cpp): a phase is only trusted once TWO
// sync detections land exactly kBurstTotalBits (264) bits apart - a
// standard "acquire, then verify" pattern that cuts the false-lock
// probability to negligible. Cost: the first burst of any real
// transmission is consumed purely as the verification reference and is
// never itself reported (one burst, ~27.5 ms, of extra acquisition
// latency) - a normal, expected trade-off for this kind of synchronizer,
// not a bug. Once verified, that phase stays locked in indefinitely, and
// a sync landing somewhere other than its expected next position drops
// the lock and starts fresh verification.
//
// Symbol decisions only happen while a FAST internal power gate is open
// (see fastPowerAvg_ - NOT the same as the public, slow squelch below) -
// this is NOT just an efficiency skip, it's load-bearing: a DMR TDMA
// frame is 60ms (two 30ms slots) but a burst is only ~27.5ms, so a lone
// simplex radio using only slot 1 leaves slot 2's entire 30ms window
// truly RF-silent, and real consecutive bursts from it are a full 60ms
// (576 bits at this symbol rate) apart in absolute time - NOT the
// kBurstTotalBits (264) the two-sync check above requires. Skipping
// symbol decisions while the fast gate is closed is what reconciles
// this: the idle slot's dead time contributes zero bits to
// BurstAligner's count, so consecutive slot-1 bursts still land exactly
// 264 bits apart from its point of view no matter how much real silence
// separates them. A real repeater using both slots continuously never
// hits this gap at all. The gate needs to be FAST (not the same slow
// tracker the public squelch uses) because a single-slot gap is only
// ~7200 samples - the slow tracker's ~1000-sample time constant can't
// settle within that window and would stay "open" straight through it,
// which is exactly how an earlier version of this fix (gating on the
// slow squelch instead) failed. The fast gate reopening after a SHORT
// closed period (one idle-slot's worth) does NOT reset acquisition for
// the same reason - only reopening after a genuinely long silence does
// (see minSilenceForReacquireSamples_ in the .cpp), since a brief
// single-slot gap must not cost accumulated verification progress. The
// public squelch (squelchOpen()/setSquelchCallback()) is unaffected by
// any of this - it keeps its original slow, call-boundary-timescale
// behavior throughout.
//
// Known simplifications (same spirit as NbfmDemodulator's own list):
//  - Symbol decisions use the single raw sample at the recovered phase
//    instant, not a matched-filter/multi-sample average - no RRC pulse
//    shaping is undone on receive. A real DMR signal's raised-cosine
//    shaping means adjacent symbols bleed into each other somewhat; this
//    works because 4FSK holds each symbol's frequency for the whole
//    symbol period, so any sample not too close to a transition edge
//    reads correctly, but a matched filter would be more robust.
//  - No clock-drift tracking WITHIN a lock (beyond noticing a sync miss
//    its expected position and re-verifying) - real crystal oscillators
//    drift very little over the length of one reception session in
//    practice, but this has NOT been proven against real hardware/long
//    sessions.
//  - No slot 1 vs slot 2 disambiguation here - this class just reports
//    "a burst was found", in arrival order; deciding which physical TDMA
//    slot each one belongs to is a timing/CACH question handled one layer
//    up (see biem_cli.cpp's runDmrLive()), not something a single burst's
//    own bits self-report in what this class currently decodes.
// See docs/DMR_NOTES.md for the full confirmed-vs-assumed breakdown of the
// physical-layer constants this relies on (symbol rate, deviations, dibit
// mapping, burst layout).
class DmrRfDemodulator {
public:
    using BurstCallback = std::function<void(const DmrBurstBytes& burst, SyncType syncType)>;
    using SquelchCallback = std::function<void(bool open)>;
    using LevelCallback = std::function<void(double powerDb)>;

    explicit DmrRfDemodulator(DmrRfConfig config);
    ~DmrRfDemodulator();

    DmrRfDemodulator(const DmrRfDemodulator&) = delete;
    DmrRfDemodulator& operator=(const DmrRfDemodulator&) = delete;

    void setBurstCallback(BurstCallback cb) { burstCb_ = std::move(cb); }
    void setSquelchCallback(SquelchCallback cb) { squelchCb_ = std::move(cb); }

    // Same semantics as NbfmDemodulator::setLevelCallback - see there.
    void setLevelCallback(LevelCallback cb, uint64_t everyNSamples = 0);

    // Consumes IQ samples at config.iqSampleRateHz, same calling
    // convention as NbfmDemodulator::processSamples (call repeatedly with
    // consecutive chunks - all filter/mixer/squelch/acquisition state
    // persists across calls).
    void processSamples(const IqSample* samples, size_t count);

    bool squelchOpen() const { return squelchOpen_; }
    bool locked() const { return locked_; }

private:
    struct BurstAligner; // defined in DmrRfDemodulator.cpp

    DmrRfConfig config_;
    BurstCallback burstCb_;
    SquelchCallback squelchCb_;
    LevelCallback levelCb_;
    uint64_t levelReportInterval_ = 0;
    uint64_t levelSampleCounter_ = 0;

    // --- mixer / channel filter / discriminator (same technique as
    // NbfmDemodulator, kept as an independent implementation rather than
    // shared code - see class comment in NbfmDemodulator.h about not
    // risking the now-verified-on-real-hardware analog path) ---
    IqSample prevSample_{0.0f, 0.0f};
    double mixerPhaseRad_ = 0.0;
    double mixerPhaseIncRad_ = 0.0;
    double channelLpfAlpha_ = 1.0;
    IqSample channelLpfState_{0.0f, 0.0f};

    // Slow power tracker - unchanged meaning/purpose from the first
    // version of this class: drives the PUBLIC squelchOpen()/
    // setSquelchCallback() surface, on a call-boundary timescale (matches
    // NbfmDemodulator's squelch, and callers like biem_cli.cpp that
    // signal call start/end from it). Deliberately NOT used to gate
    // symbol-level processing below - see fastGateOpen_'s comment for why
    // that needs a much faster tracker instead.
    double squelchPowerAvg_ = 0.0;
    bool squelchOpen_ = false;

    // Fast power tracker - gates symbol-level processing (see
    // processSamples()). A real finding drove this split into two
    // trackers: a lone simplex DMR radio using only one TDMA slot leaves
    // the OTHER slot's ~30ms window RF-silent throughout an otherwise
    // continuous transmission, but the SLOW tracker above (tuned for
    // human-speech-timescale call boundaries, correct for that purpose)
    // has a time constant of roughly 1000 samples - it CANNOT settle
    // within a single ~7200-sample/30ms slot window, so it stays
    // "open" (or hovers ambiguously) straight through a real gap instead
    // of ever reading "closed". Gating symbol decisions on it therefore
    // let the idle slot's silence leak into BurstAligner as real bits,
    // corrupting the exact bit-spacing two-sync verification depends on.
    // This tracker is fast enough (~50-sample time constant, 100+ time
    // constants within one 30ms slot) to track the true on/off envelope
    // of a single-slot signal accurately.
    double fastPowerAvg_ = 0.0;
    bool fastGateOpen_ = false;

    // Consecutive samples the FAST gate has been closed - see
    // processSamples(): acquisition only resets on reopen after a
    // genuinely long silence (minSilenceForReacquireSamples_), not on
    // every brief single-slot gap (see fastGateOpen_'s comment).
    uint64_t closedSampleCount_ = 0;
    uint64_t minSilenceForReacquireSamples_ = 0; // set in constructor from iqSampleRateHz

    // Slow-tracked center of the (boxcar-smoothed) discriminator output,
    // subtracted before slicing - absorbs small residual Tx/Rx frequency
    // offset (a real concern here: the four decision levels are only
    // ~648 Hz apart near zero, and crystal tolerances alone can plausibly
    // be a meaningful fraction of that). Assumes real DMR traffic's
    // average deviation is close to zero over the ~20-symbol time
    // constant this tracks at (true for properly Golay/BPTC/CRC-encoded
    // content, which is designed to be balanced) - a synthetic test
    // signal built from a hand-picked, unrealistically DC-biased repeating
    // byte pattern instead can make this legitimately lag after a content
    // change; see tests/test_dmr_rf.cpp's fill-pattern comments for a
    // worked example of this actually happening during development.
    double centerEma_ = 0.0;

    // Trailing boxcar average of the raw (pre-centering) discriminator
    // output - see the constructor comment in the .cpp: a modest,
    // still-worthwhile smoothing of sample-level noise, not a fix for the
    // centerEma_ lag described above (a different issue, with a different
    // fix - see that comment).
    std::vector<double> boxcarBuf_;
    size_t boxcarPos_ = 0;
    double boxcarSum_ = 0.0;
    int boxcarWindow_ = 1;

    int samplesPerSymbol_ = 1;
    uint64_t sampleIndex_ = 0;

    bool locked_ = false;
    int lockedPhase_ = -1;
    std::vector<std::unique_ptr<BurstAligner>> acquisitionTracks_;
    std::unique_ptr<BurstAligner> lockedTrack_;

    void resetAcquisition();
    double discriminate(const IqSample& s);
};

} // namespace biem::dsp::dmr
