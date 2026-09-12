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
#include "net/UdpRawLogger.h"

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
        << "  search <db_yolu> [baslik_icerir]\n"
        << "      Veritabanindaki cagrilari listeler (opsiyonel baslik filtresi).\n"
        << "  alias-radio <db_yolu> <radio_id> <etiket>\n"
        << "  alias-tg <db_yolu> <tg_id> <etiket>\n"
        << "      Radio ID / Talkgroup icin okunabilir isim tanimlar - bkz.\n"
        << "      core/TitleBuilder.h.\n"
        << "  udp-capture <port> <log_klasoru>\n"
        << "      Verilen UDP portunu dinler, gelen her paketi hex+raw olarak\n"
        << "      kaydeder - bkz. docs/HYTERA_HR659.md. Durdurmak icin Enter'a basin.\n"
#if defined(BIEM_HAVE_RTLSDR)
        << "  live <frekans_hz> <db_yolu> <kayit_klasoru> [kazanc_onda_db]\n"
        << "      Gercek RTL-SDR donanimindan analog FM alir, kaydeder.\n"
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

#if defined(BIEM_HAVE_RTLSDR)
int runLive(double frequencyHz, const std::string& dbPath, const std::string& recDir, int gainTenthDb) {
    biem::core::Database db(dbPath);
    db.migrate();
    biem::core::CallRecorder recorder(db, recDir, 8000);

    biem::dsp::NbfmConfig cfg;
    cfg.iqSampleRateHz = 240000.0;
    cfg.audioSampleRateHz = 8000.0;
    biem::dsp::NbfmDemodulator demod(cfg);

    biem::core::CallRecord meta;
    meta.channelLabel = "Live";
    meta.modulation = biem::core::Modulation::AnalogFM;
    meta.frequencyHz = frequencyHz;

    demod.setSquelchCallback([&](bool open) {
        if (open) {
            recorder.beginCall(meta);
        } else if (recorder.hasActiveCall()) {
            recorder.endCall();
        }
    });
    demod.setAudioCallback([&](const int16_t* pcm, size_t count) { recorder.pushAudio(pcm, count); });

    biem::dsp::RtlSdrSource src;
    src.setSampleRateHz(cfg.iqSampleRateHz);
    src.setCenterFrequencyHz(frequencyHz);
    if (!src.open()) {
        std::cerr << "RTL-SDR acilamadi.\n";
        return 1;
    }
    if (gainTenthDb >= 0) src.setGainTenthDb(gainTenthDb);

    std::cerr << frequencyHz << " Hz dinleniyor. Durdurmak icin Enter'a basin.\n";
    src.start([&](const biem::dsp::IqSample* samples, size_t count) { demod.processSamples(samples, count); });
    std::cin.get();
    src.stop();
    if (recorder.hasActiveCall()) recorder.endCall();
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
#if defined(BIEM_HAVE_RTLSDR)
        if (cmd == "live" && argc >= 5) {
            int gain = argc >= 6 ? std::stoi(argv[5]) : -1;
            return runLive(std::stod(argv[2]), argv[3], argv[4], gain);
        }
#endif
    } catch (const std::exception& ex) {
        std::cerr << "Hata: " << ex.what() << "\n";
        return 1;
    }

    printUsage(argv[0]);
    return 1;
}
