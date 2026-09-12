#include "CallRecorder.h"

#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include "Database.h"
#include "TitleBuilder.h"
#include "WavWriter.h"

namespace biem::core {

namespace fs = std::filesystem;

namespace {

std::string sanitizeForFilename(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
            out += c;
        } else if (c == ' ') {
            out += '_';
        }
        // anything else (path separators, punctuation, etc.) is dropped
    }
    if (out.empty()) out = "cagri";
    return out;
}

} // namespace

CallRecorder::CallRecorder(Database& db, std::string recordingsRootDir, int sampleRateHz)
    : db_(db), recordingsRootDir_(std::move(recordingsRootDir)), sampleRateHz_(sampleRateHz) {}

CallRecorder::~CallRecorder() {
    if (active_) {
        endCall();
    }
}

std::string CallRecorder::buildFilePath(const CallRecord& meta) const {
    std::time_t timeT = static_cast<std::time_t>(meta.startUnixTimeMs / 1000);
    std::tm tmBuf{};
#if defined(_WIN32)
    localtime_s(&tmBuf, &timeT);
#else
    localtime_r(&timeT, &tmBuf);
#endif

    std::ostringstream dirStream;
    dirStream << recordingsRootDir_ << "/" << std::put_time(&tmBuf, "%Y/%m");
    fs::path dir(dirStream.str());
    std::error_code ec;
    fs::create_directories(dir, ec);
    // If create_directories failed, WavWriter::isOpen() will simply come
    // back false and CallRecorder::beginCall() records that in audioFilePath
    // - callers can check hasActiveCall()/activeCallMeta() if they need to
    // surface that to an operator.

    std::ostringstream nameStream;
    nameStream << std::put_time(&tmBuf, "%Y%m%d_%H%M%S");
    std::string title = meta.title.empty() ? buildCallTitle(meta, nullptr) : meta.title;
    nameStream << "_" << sanitizeForFilename(title);
    if (meta.slot) nameStream << "_slot" << *meta.slot;
    nameStream << ".wav";

    return (dir / nameStream.str()).string();
}

void CallRecorder::beginCall(CallRecord meta) {
    if (active_) {
        endCall();
    }
    if (meta.startUnixTimeMs == 0) {
        meta.startUnixTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
    }

    std::string path = buildFilePath(meta);
    writer_ = std::make_unique<WavWriter>(path, sampleRateHz_, 1);
    meta.audioFilePath = writer_->isOpen() ? path : std::string();
    active_ = std::move(meta);
}

void CallRecorder::pushAudio(const int16_t* samples, size_t count) {
    if (!active_ || !writer_) return;
    writer_->writeSamples(samples, count);
}

std::optional<CallRecord> CallRecorder::endCall() {
    if (!active_) return std::nullopt;

    CallRecord rec = *active_;
    if (writer_) {
        writer_->finalize();
        rec.durationMs = static_cast<int64_t>(writer_->durationSeconds() * 1000.0);
        rec.voiceDecoded = writer_->samplesWritten() > 0;
        writer_.reset();
    }
    active_.reset();

    db_.insertCall(rec);
    return rec;
}

} // namespace biem::core
