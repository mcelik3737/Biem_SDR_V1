// biem_cli - headless command-line entry point. Exists so the core
// pipeline (demod/DMR/recording/database/network-capture) can be built and
// exercised without Qt6 - this is what this repo's development sandbox
// (no Qt6 installed) actually built and ran; see docs/ROADMAP.md.
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

#include "core/CallRecorder.h"
#include "core/Database.h"
#include "dsp/NbfmDemodulator.h"
#include "dsp/WavIqSource.h"
#include "dsp/dmr/DmrBurstEncoder.h"
#include "dsp/dmr/DmrCallTracker.h"
#include "dsp/dmr/DmrRfDemodulator.h"
#include "dsp/dmr/DmrSlotDecoder.h"
#include "dsp/dmr/DmrSyntheticSource.h"
#include "net/UdpRawLogger.h"
#include "net/hytera/HyteraHR659Source.h"

#if defined(BIEM_HAVE_RTLSDR)
#include "dsp/RtlSdrSource.h"
#endif

namespace {

void printUsage(const char* argv0) {
    std::cerr
        << "Kullanim: " << argv0 << " <komut> [secenekler]\n\n"
        << "Komutlar:\n"
        << "  demo <db_yolu> <kayit_klasoru>\n"
        << "      Sentetik FM sinyali uretir, demodule eder, kaydeder ve\n"
        << "      veritabanina yazar - donanim olmadan uctan uca test.\n"
        << "  dmr-demo <db_yolu> <kayit_klasoru>\n"
        << "      demo'nun DMR karsiligi: sentetik 4FSK sinyali uretir,\n"
        << "      DmrRfDemodulator + DmrCallTracker ile cozer, kaydeder. Icerik\n"
        << "      cozme guvenilirligi notu icin docs/ROADMAP.md ve\n"
        << "      tests/test_dmr_rf.cpp'ye bakin.\n"
        << "  search <db_yolu> [baslik_icerir]\n"
        << "      Veritabanindaki cagrilari listeler (opsiyonel baslik filtresi).\n"
        << "  alias-radio <db_yolu> <radio_id> <etiket>\n"
        << "  alias-tg <db_yolu> <tg_id> <etiket>\n"
        << "      Radio ID / Talkgroup icin okunabilir isim tanimlar - bkz.\n"
        << "      core/TitleBuilder.h.\n"
        << "  udp-capture <port> <log_klasoru>\n"
        << "      Verilen UDP portunu dinler, gelen her paketi hex+raw olarak\n"
        << "      kaydeder - bkz. docs/HYTERA_HR659.md. Durdurmak icin Enter'a basin.\n"
        << "  hytera-live <port> <db_yolu> <kayit_klasoru> <kanal_etiketi>\n"
        << "      Hytera HR659 icin VARSAYIMSAL/DOGRULANMAMIS paket ayristirici -\n"
        << "      bkz. docs/HYTERA_HR659.md. Her paket ham olarak da kaydedilir;\n"
        << "      cikan [hytera-hr659] tanilama satirlarini gercek trafikle\n"
        << "      karsilastirip alan konumlarini dogrulayin/duzeltin.\n"
#if defined(BIEM_HAVE_RTLSDR)
        << "  live <frekans_hz> <db_yolu> <kayit_klasoru> [squelch_db] [kazanc_onda_db]\n"
        << "      Gercek RTL-SDR donanimindan analog FM alir, kaydeder. Her ~1\n"
        << "      saniyede bir anlik guc seviyesini (dB) ve squelch acik/kapali\n"
        << "      durumunu ekrana yazar - squelch_db'yi bu okumalara gore\n"
        << "      ayarlayin (bos kanal seviyesinin biraz uzerinde secin).\n"
        << "      squelch_db verilmezse varsayilan -50 kullanilir.\n"
        << "  dmr-live <frekans_hz> <db_yolu> <kayit_klasoru> [squelch_db] [kazanc_onda_db]\n"
        << "      live'in DMR karsiligi. Slot 1/2 ayrimi VARSAYIMSAL (sirayla\n"
        << "      degisen, gercek CACH/zamanlama takibi degil) - bkz. kaynak\n"
        << "      kodundaki yorum ve docs/ROADMAP.md.\n"
#endif
        ;
}

int runDemo(const std::string& dbPath, const std::string& recDir) {
    biem::core::Database db(dbPath);
    db.migrate();
    biem::core::CallRecorder recorder(db, recDir, 8000);

    biem::dsp::NbfmConfig cfg;
    cfg.iqSampleRateHz = 48000.0;
    cfg.audioSampleRateHz = 8000.0;
    cfg.maxDeviationHz = 3000.0;
    cfg.squelchThresholdDb = -30.0;
    biem::dsp::NbfmDemodulator demod(cfg);

    biem::core::CallRecord meta;
    meta.channelLabel = "Demo Kanal";
    meta.modulation = biem::core::Modulation::AnalogFM;
    meta.source = biem::core::CallSource::Sdr;
    meta.frequencyHz = 446006250.0; // PMR446 kanal 1

    demod.setSquelchCallback([&](bool open) {
        std::cerr << "[demo] squelch " << (open ? "acik" : "kapali") << "\n";
        if (open) {
            recorder.beginCall(meta);
        } else if (recorder.hasActiveCall()) {
            auto rec = recorder.endCall();
            if (rec) {
                std::cerr << "[demo] cagri kaydedildi: " << rec->audioFilePath << " (" << rec->durationMs
                          << " ms)\n";
            }
        }
    });
    demod.setAudioCallback([&](const int16_t* pcm, size_t count) { recorder.pushAudio(pcm, count); });

    auto src = biem::dsp::WavIqSource::makeSyntheticFm(cfg.iqSampleRateHz, /*audioToneHz=*/1000.0,
                                                        /*fmDeviationHz=*/2500.0, /*durationSeconds=*/3.0,
                                                        /*noiseAmplitude=*/0.02);
    src.start([&](const biem::dsp::IqSample* samples, size_t count) { demod.processSamples(samples, count); });

    if (recorder.hasActiveCall()) recorder.endCall();

    std::cerr << "[demo] tamamlandi.\n";
    return 0;
}

int runDmrDemo(const std::string& dbPath, const std::string& recDir) {
    // DMR counterpart to "demo" above: no hardware, entirely synthetic,
    // but this time exercising DMR's own RF chain end to end -
    // DmrRfDemodulator (4FSK demod + symbol timing acquisition + burst
    // assembly - see docs/ROADMAP.md, this used to not exist at all) ->
    // DmrCallTracker (Slot Type/BPTC/LC decode, already existed) ->
    // CallRecorder -> Database, the same destination "demo" and "live"
    // use for the analog path.
    //
    // Radio ID 123123 is used here (not an arbitrary-looking value like
    // 555555) because it's a KNOWN-GOOD value for this exact scenario -
    // see tests/test_dmr_rf.cpp's testDmrRfFullStackReliabilityIsBoundedAndNonZero:
    // DmrRfDemodulator's lack of a real matched filter (documented, known
    // simplification) gives it a residual bit error rate that BPTC's
    // correction capacity does not ALWAYS overcome for arbitrary content
    // yet - most random IDs currently do NOT decode cleanly through the
    // full stack. This demo is meant to show the pipeline working, not
    // to overstate today's reliability - see that test and
    // docs/ROADMAP.md for the honest, quantified state of this gap.
    biem::core::Database db(dbPath);
    db.migrate();
    biem::core::CallRecorder recorder(db, recDir, 8000);

    const int colorCode = 5;
    const uint32_t talkgroupId = 100;
    const uint32_t radioId = 123123;

    biem::dsp::dmr::DmrCallTracker tracker(recorder, /*slotNumber=*/1, /*frequencyHz=*/446006250.0,
                                            "Demo DMR Kanal");

    biem::dsp::dmr::DmrRfConfig cfg;
    cfg.iqSampleRateHz = 240000.0;
    cfg.squelchThresholdDb = -60.0; // permissive - the synthetic signal is "always on"
    biem::dsp::dmr::DmrRfDemodulator demod(cfg);
    demod.setBurstCallback([&](const biem::dsp::dmr::DmrBurstBytes& b, biem::dsp::dmr::SyncType t) {
        tracker.onBurst(b, t);
    });

    // Preamble (0xDD - see tests/test_dmr_rf.cpp for why a balanced,
    // zero-average-deviation pattern matters here) + burst1 (sacrificial
    // acquisition-verification reference - see DmrRfDemodulator.h's class
    // comment on why the first burst of any transmission is never itself
    // reported) + burst2 (VoiceLcHeader carrying the real TG/Radio ID,
    // starts the call) + burst3 (Terminator, ends it).
    std::vector<uint8_t> bits;
    for (int i = 0; i < 400; ++i) {
        int bitPos = 7 - (i % 8);
        bits.push_back(static_cast<uint8_t>((0xDD >> bitPos) & 1u));
    }
    auto appendBurst = [&](const biem::dsp::dmr::DmrBurstBytes& burst) {
        for (uint8_t byte : burst) {
            for (int bi = 7; bi >= 0; --bi) bits.push_back(static_cast<uint8_t>((byte >> bi) & 1u));
        }
    };
    appendBurst(biem::dsp::dmr::encodeVoiceLcHeaderBurst(biem::dsp::dmr::SyncType::BsSourcedData, colorCode,
                                                          biem::dsp::dmr::Flco::GroupVoice, 1, 2));
    appendBurst(biem::dsp::dmr::encodeVoiceLcHeaderBurst(biem::dsp::dmr::SyncType::BsSourcedData, colorCode,
                                                          biem::dsp::dmr::Flco::GroupVoice, talkgroupId, radioId));
    appendBurst(biem::dsp::dmr::encodeTerminatorBurst(biem::dsp::dmr::SyncType::BsSourcedData, colorCode,
                                                       biem::dsp::dmr::Flco::GroupVoice, talkgroupId, radioId));

    auto devs = biem::dsp::dmr::bitsToSymbolDeviationsHz(bits);
    auto src = biem::dsp::WavIqSource::makeSyntheticFsk(devs, biem::dsp::dmr::kDmrSymbolRateHz,
                                                         cfg.iqSampleRateHz, /*carrierOffsetHz=*/0.0,
                                                         /*noiseAmplitude=*/0.02);
    src.start([&](const biem::dsp::IqSample* samples, size_t count) { demod.processSamples(samples, count); });

    if (recorder.hasActiveCall()) recorder.endCall();

    std::cerr << "[dmr-demo] tamamlandi (locked=" << demod.locked() << ").\n";
    return 0;
}

int runSearch(const std::string& dbPath, const std::string& titleFilter) {
    biem::core::Database db(dbPath);
    db.migrate();
    biem::core::Database::SearchFilter filter;
    if (!titleFilter.empty()) filter.titleContains = titleFilter;
    auto results = db.search(filter);
    for (const auto& rec : results) {
        std::cout << rec.id << "\t" << rec.startUnixTimeMs << "\t" << rec.title << "\t" << rec.durationMs
                  << "ms\t" << rec.audioFilePath << "\n";
    }
    std::cerr << results.size() << " kayit bulundu.\n";
    return 0;
}

int runAliasRadio(const std::string& dbPath, uint32_t id, const std::string& label) {
    biem::core::Database db(dbPath);
    db.migrate();
    db.setRadioLabel(id, label);
    std::cerr << "Radio ID " << id << " -> \"" << label << "\"\n";
    return 0;
}

int runAliasTg(const std::string& dbPath, uint32_t id, const std::string& label) {
    biem::core::Database db(dbPath);
    db.migrate();
    db.setTalkgroupLabel(id, label);
    std::cerr << "Talkgroup " << id << " -> \"" << label << "\"\n";
    return 0;
}

int runUdpCapture(uint16_t port, const std::string& logDir) {
    biem::net::UdpRawLogger logger(port, logDir);
    logger.setPacketCallback([](const biem::net::RawPacket& pkt) {
        std::cerr << "[udp-capture] " << pkt.sourceIp << ":" << pkt.sourcePort << " len=" << pkt.length
                  << "\n";
    });
    if (!logger.start()) {
        std::cerr << "UDP port " << port << " dinlenemedi.\n";
        return 1;
    }
    std::cerr << "UDP port " << port << " dinleniyor, " << logDir
              << " altina kaydediliyor. Durdurmak icin Enter'a basin.\n";
    std::cin.get();
    logger.stop();
    return 0;
}

int runHyteraLive(uint16_t port, const std::string& dbPath, const std::string& recDir,
                    const std::string& channelLabel) {
    // See src/net/hytera/HyteraHR659Source.h and docs/HYTERA_HR659.md:
    // this parses against an ASSUMED, UNCONFIRMED packet layout (no real
    // HR659 capture existed when it was written). Every packet is still
    // captured to <recDir>/../hytera_raw (hex + raw) regardless of whether
    // parsing succeeds, so nothing is lost if the layout turns out wrong -
    // and the per-packet [hytera-hr659] diagnostic below is exactly what's
    // needed to correct it once real traffic arrives.
    biem::core::Database db(dbPath);
    db.migrate();
    biem::core::CallRecorder recorder(db, recDir, 8000);

    biem::net::hytera::HyteraHR659Source src(port, recDir + "/hytera_raw", channelLabel);
    src.setCallStartCallback([&](biem::core::CallRecord meta) { recorder.beginCall(std::move(meta)); });
    src.setCallEndCallback([&]() {
        if (recorder.hasActiveCall()) {
            auto rec = recorder.endCall();
            if (rec) {
                std::cerr << "[hytera-live] cagri kaydedildi (sadece metadata - ses yok, bkz. "
                             "AMBE+2/IVoiceDecoder): "
                          << rec->title << " (" << rec->durationMs << " ms)\n";
            }
        }
    });

    if (!src.start()) {
        std::cerr << "UDP port " << port << " dinlenemedi.\n";
        return 1;
    }
    std::cerr << "Hytera HR659 (varsayimsal parser - dogrulanmadi) UDP port " << port
              << " dinleniyor. Durdurmak icin Enter'a basin.\n";
    std::cin.get();
    src.stop();
    return 0;
}

#if defined(BIEM_HAVE_RTLSDR)
int runLive(double frequencyHz, const std::string& dbPath, const std::string& recDir, double squelchDb,
            int gainTenthDb) {
    biem::core::Database db(dbPath);
    db.migrate();
    biem::core::CallRecorder recorder(db, recDir, 8000);

    biem::dsp::NbfmConfig cfg;
    cfg.iqSampleRateHz = 240000.0;
    cfg.audioSampleRateHz = 8000.0;
    cfg.squelchThresholdDb = squelchDb;
    // Tune the dongle mixerOffsetHz BELOW the wanted channel and shift back
    // in software (see NbfmConfig::mixerOffsetHz, which defaults this to 0
    // /disabled - live RTL-SDR reception is the one case that must opt in)
    // instead of tuning exactly on-channel: on zero-IF tuners like the
    // E4000, tuning on-channel parks the tuner's own DC/LO-leakage spike
    // directly on top of the wanted signal, which is indistinguishable
    // from a click/no-audio fault downstream. tunedFrequencyHz below must
    // stay in sync with this.
    cfg.mixerOffsetHz = 50000.0;
    double tunedFrequencyHz = frequencyHz - cfg.mixerOffsetHz;
    biem::dsp::NbfmDemodulator demod(cfg);

    biem::core::CallRecord meta;
    meta.channelLabel = "Live";
    meta.modulation = biem::core::Modulation::AnalogFM;
    meta.frequencyHz = frequencyHz;

    demod.setSquelchCallback([&](bool open) {
        std::cerr << "[live] squelch " << (open ? "ACIK (kayit basliyor)" : "kapali") << "\n";
        if (open) {
            recorder.beginCall(meta);
        } else if (recorder.hasActiveCall()) {
            auto rec = recorder.endCall();
            if (rec) {
                std::cerr << "[live] cagri kaydedildi: " << rec->audioFilePath << " (" << rec->durationMs
                          << " ms)\n";
            }
        }
    });
    demod.setAudioCallback([&](const int16_t* pcm, size_t count) { recorder.pushAudio(pcm, count); });
    demod.setLevelCallback([&](double powerDb) {
        std::cerr << "[live] guc seviyesi: " << powerDb << " dB (squelch esigi: " << squelchDb << " dB)\n";
    });

    biem::dsp::RtlSdrSource src;
    src.setSampleRateHz(cfg.iqSampleRateHz);
    src.setCenterFrequencyHz(tunedFrequencyHz);
    if (!src.open()) {
        std::cerr << "RTL-SDR acilamadi - baska bir program (SDR#, baska bir biem_cli) cihazi kullaniyor "
                     "olabilir; once onu kapatin.\n";
        return 1;
    }
    if (gainTenthDb >= 0) src.setGainTenthDb(gainTenthDb);

    std::cerr << frequencyHz << " Hz dinleniyor (donanim " << tunedFrequencyHz
              << " Hz'e ayarli, DC spike'i kanaldan uzak tutmak icin - bkz. NbfmConfig::mixerOffsetHz), "
                 "squelch esigi "
              << squelchDb << " dB. Durdurmak icin Enter'a basin.\n";
    src.start([&](const biem::dsp::IqSample* samples, size_t count) { demod.processSamples(samples, count); });
    std::cin.get();
    src.stop();
    if (recorder.hasActiveCall()) recorder.endCall();
    return 0;
}

int runDmrLive(double frequencyHz, const std::string& dbPath, const std::string& recDir, double squelchDb,
               int gainTenthDb) {
    // DMR counterpart to runLive() above - same RtlSdrSource, same
    // mixerOffsetHz DC-spike-avoidance tuning trick, but DmrRfDemodulator
    // (4FSK) instead of NbfmDemodulator, feeding two DmrCallTracker
    // instances instead of one CallRecorder-facing squelch callback.
    //
    // Slot 1 vs slot 2: DmrRfDemodulator reports bursts in ARRIVAL ORDER
    // only (see its header - it does not itself determine which physical
    // TDMA slot a burst belongs to, that needs either timing-grid
    // tracking or CACH decode, neither implemented yet). This ASSUMES a
    // busy repeater alternates slot 1/slot 2/slot 1/... in strict
    // lockstep and demuxes purely by counting - correct for a repeater
    // actively using both slots back to back, WRONG the moment only one
    // slot is active (everything would incorrectly bounce between both
    // trackers) or a burst is ever missed (permanently flips slot
    // labeling from then on). Flagged here, in the class comment, and in
    // docs/ROADMAP.md - fixing it needs real slot-timing/CACH work, not
    // attempted in this pass.
    biem::core::Database db(dbPath);
    db.migrate();
    biem::core::CallRecorder recorderSlot1(db, recDir, 8000);
    biem::core::CallRecorder recorderSlot2(db, recDir, 8000);
    biem::dsp::dmr::DmrCallTracker trackerSlot1(recorderSlot1, 1, frequencyHz, "Live DMR");
    biem::dsp::dmr::DmrCallTracker trackerSlot2(recorderSlot2, 2, frequencyHz, "Live DMR");

    biem::dsp::dmr::DmrRfConfig cfg;
    cfg.iqSampleRateHz = 240000.0;
    cfg.squelchThresholdDb = squelchDb;
    cfg.mixerOffsetHz = 50000.0; // see NbfmConfig::mixerOffsetHz / runLive() above for why
    double tunedFrequencyHz = frequencyHz - cfg.mixerOffsetHz;
    biem::dsp::dmr::DmrRfDemodulator demod(cfg);

    demod.setSquelchCallback([&](bool open) {
        std::cerr << "[dmr-live] squelch " << (open ? "ACIK" : "kapali") << "\n";
    });

    biem::dsp::dmr::DmrSlotDecoder slotDecoderForDiag; // diagnostic-only - DmrCallTracker has its own internally

    bool nextIsSlot1 = true;
    demod.setBurstCallback([&](const biem::dsp::dmr::DmrBurstBytes& b, biem::dsp::dmr::SyncType t) {
        biem::core::CallRecorder& recorder = nextIsSlot1 ? recorderSlot1 : recorderSlot2;
        int slotLabel = nextIsSlot1 ? 1 : 2;
        biem::dsp::dmr::DmrCallTracker& tracker = nextIsSlot1 ? trackerSlot1 : trackerSlot2;

        // Slot Type diagnostic (see DmrSlotDecoder/DmrConstants) - ONLY for
        // display, so it's easy to see WHY a burst did or didn't start a
        // call (dataType != VoiceLcHeader is completely normal - most
        // bursts in a real call are Voice Frame A/B-F, not the header).
        biem::dsp::dmr::SlotTypeInfo slotInfo = slotDecoderForDiag.decodeSlotType(b);
        std::cerr << "[dmr-live] burst alindi: sync-tur=" << biem::dsp::dmr::toString(t) << " slot=" << slotLabel
                  << " (varsayim) renk-kodu=" << slotInfo.colorCode
                  << " veri-tipi=" << biem::dsp::dmr::toString(slotInfo.dataType)
                  << " golay-duzeltilen-bit=" << slotInfo.correctedBits << "\n";

        bool wasActive = recorder.hasActiveCall();
        tracker.onBurst(b, t);
        bool isActive = recorder.hasActiveCall();

        if (!wasActive && isActive) {
            const auto& meta = recorder.activeCallMeta();
            std::cerr << "[dmr-live] cagri basladi (slot " << slotLabel << "): "
                      << (meta && meta->radioId ? std::to_string(*meta->radioId) : std::string("?")) << " -> "
                      << (meta && meta->talkgroupId ? "TG " + std::to_string(*meta->talkgroupId)
                          : (meta && meta->destRadioId ? "ID " + std::to_string(*meta->destRadioId)
                                                        : std::string("?"))) << "\n";
        } else if (wasActive && !isActive) {
            std::cerr << "[dmr-live] cagri bitti (slot " << slotLabel << ").\n";
        }

        nextIsSlot1 = !nextIsSlot1;
    });
    demod.setLevelCallback([&](double powerDb) {
        std::cerr << "[dmr-live] guc seviyesi: " << powerDb << " dB (squelch esigi: " << squelchDb
                  << " dB, kilit: " << (demod.locked() ? "VAR" : "yok") << ")\n";
    });

    biem::dsp::RtlSdrSource src;
    src.setSampleRateHz(cfg.iqSampleRateHz);
    src.setCenterFrequencyHz(tunedFrequencyHz);
    if (!src.open()) {
        std::cerr << "RTL-SDR acilamadi - baska bir program (SDR#, baska bir biem_cli) cihazi kullaniyor "
                     "olabilir; once onu kapatin.\n";
        return 1;
    }
    if (gainTenthDb >= 0) src.setGainTenthDb(gainTenthDb);

    std::cerr << frequencyHz << " Hz DMR dinleniyor (donanim " << tunedFrequencyHz
              << " Hz'e ayarli), squelch esigi " << squelchDb
              << " dB. Slot 1/2 ayrimi VARSAYIMSAL (bkz. kaynak yorumu). Durdurmak icin Enter'a basin.\n";
    src.start([&](const biem::dsp::IqSample* samples, size_t count) { demod.processSamples(samples, count); });
    std::cin.get();
    src.stop();
    return 0;
}
#endif

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }
    std::string cmd = argv[1];

    try {
        if (cmd == "demo" && argc >= 4) {
            return runDemo(argv[2], argv[3]);
        }
        if (cmd == "dmr-demo" && argc >= 4) {
            return runDmrDemo(argv[2], argv[3]);
        }
        if (cmd == "search" && argc >= 3) {
            return runSearch(argv[2], argc >= 4 ? argv[3] : "");
        }
        if (cmd == "alias-radio" && argc >= 5) {
            return runAliasRadio(argv[2], static_cast<uint32_t>(std::stoul(argv[3])), argv[4]);
        }
        if (cmd == "alias-tg" && argc >= 5) {
            return runAliasTg(argv[2], static_cast<uint32_t>(std::stoul(argv[3])), argv[4]);
        }
        if (cmd == "udp-capture" && argc >= 4) {
            return runUdpCapture(static_cast<uint16_t>(std::stoul(argv[2])), argv[3]);
        }
        if (cmd == "hytera-live" && argc >= 6) {
            return runHyteraLive(static_cast<uint16_t>(std::stoul(argv[2])), argv[3], argv[4], argv[5]);
        }
#if defined(BIEM_HAVE_RTLSDR)
        if (cmd == "live" && argc >= 5) {
            double squelch = argc >= 6 ? std::stod(argv[5]) : -50.0;
            int gain = argc >= 7 ? std::stoi(argv[6]) : -1;
            return runLive(std::stod(argv[2]), argv[3], argv[4], squelch, gain);
        }
        if (cmd == "dmr-live" && argc >= 5) {
            double squelch = argc >= 6 ? std::stod(argv[5]) : -50.0;
            int gain = argc >= 7 ? std::stoi(argv[6]) : -1;
            return runDmrLive(std::stod(argv[2]), argv[3], argv[4], squelch, gain);
        }
#endif
    } catch (const std::exception& ex) {
        std::cerr << "Hata: " << ex.what() << "\n";
        return 1;
    }

    printUsage(argv[0]);
    return 1;
}
