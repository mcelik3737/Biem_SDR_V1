#pragma once

#include <QObject>
#include <QWidget>

#include <atomic>
#include <memory>
#include <string>

#include "../core/CallRecord.h"
#include "../dsp/NbfmDemodulator.h"
#include "../dsp/dmr/DmrCallTracker.h"
#include "../dsp/dmr/DmrRfDemodulator.h"
#include "../dsp/dmr/DmrSlotDecoder.h"

class QComboBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QProgressBar;
class QPlainTextEdit;

namespace biem::core {
class Database;
class CallRecorder;
}  // namespace biem::core

namespace biem::dsp {
class IqSource;
}  // namespace biem::dsp

namespace biem::ui {

enum class LiveMode { AnalogFm, Dmr };

struct LiveSessionConfig {
    LiveMode mode = LiveMode::AnalogFm;
    double frequencyHz = 446000000.0;
    double squelchThresholdDb = -50.0;
    int gainTenthDb = -1;  // < 0 = auto (rtlsdr_set_tuner_gain_mode(0))
    std::string dbPath;
    std::string recordingsDir;
};

// Owns one live SDR receive session - RtlSdrSource + demodulator (FM or
// DMR) + CallRecorder(s), mirroring biem_cli.cpp's runLive()/runDmrLive()
// (see docs/ROADMAP.md) but reporting through Qt signals instead of stderr.
//
// THREADING - read this before touching this class: RtlSdrSource::start()
// spawns its OWN background std::thread and delivers every sample buffer
// (and therefore every demodulator callback below - squelch/level/burst)
// from THAT thread, never from the thread that called start(). Every slot
// above simply does `emit somethingChanged(...)` from inside those
// callbacks - it does NOT touch any QWidget directly. That is deliberate:
// a Qt signal emitted from a thread other than the receiving QObject's own
// thread affinity is automatically delivered as a queued event on the
// receiver's event loop (Qt::AutoConnection, the default - see Qt's
// "Signals and Slots Across Threads" docs), which is exactly what makes it
// safe for LiveMonitorWidget's slots to update widgets in response. This
// only holds as long as:
//   - this object is constructed on the GUI thread (LiveMonitorWidget does
//     this, so its thread() affinity is the GUI thread);
//   - nobody ever connects a signal here with Qt::DirectConnection;
//   - start()/stop() are only ever called from the GUI thread, and never
//     while a previous session on this same object is still running.
//
// Also owns its OWN core::Database connection rather than sharing
// MainWindow's: core::Database is documented as not safe to touch from more
// than one thread, and this object's writes (CallRecorder::endCall() ->
// Database::insertCall()) happen on RtlSdrSource's background thread while
// MainWindow/CallLogWidget's reads happen on the GUI thread. Two separate
// sqlite3 connections to the same file are safe here because Database
// already runs in WAL mode (see Database.cpp's constructor) - a reader on
// one connection is never blocked by a writer on another.
class LiveSession : public QObject {
    Q_OBJECT
public:
    explicit LiveSession(QObject* parent = nullptr);
    ~LiveSession() override;

    LiveSession(const LiveSession&) = delete;
    LiveSession& operator=(const LiveSession&) = delete;

    // Returns an empty string on success, or a user-facing error message on
    // failure (nothing is left running/open when this returns non-empty -
    // e.g. "RTL-SDR acilamadi").
    QString start(const LiveSessionConfig& config);

    // Idempotent - safe to call even if start() was never called or already
    // failed. Blocks briefly (RtlSdrSource::stop() joins its background
    // thread) - same tradeoff biem_cli's blocking src.stop() call makes.
    void stop();

    bool running() const { return running_; }

signals:
    void levelChanged(double powerDb);
    void squelchChanged(bool open);
    void lockChanged(bool locked);  // DMR only - never emitted in FM mode
    void callActiveChanged(bool active);
    void callFinished();  // a call row was just inserted into the database
    void logLine(QString text);

private:
    std::atomic<bool> running_{false};
    LiveMode mode_ = LiveMode::AnalogFm;

    std::unique_ptr<core::Database> db_;
    std::unique_ptr<core::CallRecorder> recorder_;
    std::unique_ptr<core::CallRecorder> recorderSlot2_;  // DMR only
    std::unique_ptr<dsp::dmr::DmrCallTracker> dmrTrackerSlot1_;
    std::unique_ptr<dsp::dmr::DmrCallTracker> dmrTrackerSlot2_;
    std::unique_ptr<dsp::IqSource> source_;
    std::unique_ptr<dsp::NbfmDemodulator> fmDemod_;
    std::unique_ptr<dsp::dmr::DmrRfDemodulator> dmrDemod_;
    dsp::dmr::DmrSlotDecoder dmrSlotDecoderForDiag_;  // display-only, see biem_cli.cpp's runDmrLive()
    bool nextIsSlot1_ = true;
};

// Live receive tab: pick a frequency/mode, press Baslat, watch power level /
// squelch / (DMR) lock / active-call state update in real time, and see
// each event logged - the GUI counterpart of `biem_cli live`/`dmr-live`
// (see docs/ROADMAP.md). A finished call shows up in the "Cagri Kayitlari"
// tab automatically (see callFinished()).
class LiveMonitorWidget : public QWidget {
    Q_OBJECT
public:
    // dbPath/recordingsDir are plain filesystem paths (not a shared
    // Database&) because each Start click creates a fresh LiveSession with
    // its own Database connection - see LiveSession's class comment.
    LiveMonitorWidget(std::string dbPath, std::string recordingsDir, QWidget* parent = nullptr);
    ~LiveMonitorWidget() override;

signals:
    // Forwarded from the active LiveSession's callFinished() - MainWindow
    // connects this to CallLogWidget's search refresh.
    void callFinished();

private slots:
    void onStartStopClicked();
    void onModeChanged(int index);

private:
    std::string dbPath_;
    std::string recordingsDir_;

    QComboBox* modeCombo_ = nullptr;
    QDoubleSpinBox* frequencyMhzSpin_ = nullptr;
    QDoubleSpinBox* squelchSpin_ = nullptr;
    QCheckBox* manualGainCheck_ = nullptr;
    QDoubleSpinBox* gainSpin_ = nullptr;
    QPushButton* startStopButton_ = nullptr;

    QLabel* squelchIndicator_ = nullptr;
    QLabel* lockIndicator_ = nullptr;
    QLabel* callIndicator_ = nullptr;
    QProgressBar* levelMeter_ = nullptr;
    QPlainTextEdit* log_ = nullptr;

    std::unique_ptr<LiveSession> session_;

    void appendLog(const QString& text);
    void setRunningUiState(bool running);
};

}  // namespace biem::ui
