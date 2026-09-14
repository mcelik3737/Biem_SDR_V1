#include "LiveMonitorWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTime>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "../core/CallRecorder.h"
#include "../core/Database.h"
#include "../dsp/IqSample.h"
#include "../dsp/IqSource.h"

#if defined(BIEM_HAVE_RTLSDR)
#include "../dsp/RtlSdrSource.h"
#endif

namespace biem::ui {

// --- LiveSession -----------------------------------------------------------

LiveSession::LiveSession(QObject* parent) : QObject(parent) {}

LiveSession::~LiveSession() { stop(); }

QString LiveSession::start(const LiveSessionConfig& config) {
    if (running_) return QStringLiteral("Zaten calisiyor.");

#if !defined(BIEM_HAVE_RTLSDR)
    (void)config;
    return QStringLiteral(
        "Bu derlemede RTL-SDR destegi yok (librtlsdr bulunamadi) - donanimdan canli alim "
        "mumkun degil. bkz. docs/BUILD_WINDOWS.md.");
#else
    try {
        db_ = std::make_unique<core::Database>(config.dbPath);
        db_->migrate();
    } catch (const std::exception& e) {
        db_.reset();
        return QStringLiteral("Veritabani acilamadi: %1").arg(QString::fromLocal8Bit(e.what()));
    }

    mode_ = config.mode;

    // Same E4000 DC-spike-avoidance tuning trick as biem_cli.cpp's
    // runLive()/runDmrLive() - see NbfmConfig::mixerOffsetHz for the full
    // explanation. Not exposed as a UI control: it's a fixed hardware
    // workaround, not a tuning parameter an operator should need to touch.
    const double mixerOffsetHz = 50000.0;
    const double iqSampleRateHz = 240000.0;
    const double tunedFrequencyHz = config.frequencyHz - mixerOffsetHz;

    auto rtl = std::make_unique<dsp::RtlSdrSource>();
    rtl->setSampleRateHz(iqSampleRateHz);
    rtl->setCenterFrequencyHz(tunedFrequencyHz);
    if (!rtl->open()) {
        db_.reset();
        return QStringLiteral(
            "RTL-SDR acilamadi - baska bir program (SDR#, baska bir biem_cli/biem_gui) cihazi "
            "kullaniyor olabilir; once onu kapatin.");
    }
    if (config.gainTenthDb >= 0) rtl->setGainTenthDb(config.gainTenthDb);

    if (mode_ == LiveMode::AnalogFm) {
        recorder_ = std::make_unique<core::CallRecorder>(*db_, config.recordingsDir, 8000);

        dsp::NbfmConfig cfg;
        cfg.iqSampleRateHz = iqSampleRateHz;
        cfg.squelchThresholdDb = config.squelchThresholdDb;
        cfg.mixerOffsetHz = mixerOffsetHz;
        fmDemod_ = std::make_unique<dsp::NbfmDemodulator>(cfg);

        core::CallRecord meta;
        meta.channelLabel = "Canli";
        meta.modulation = core::Modulation::AnalogFM;
        meta.source = core::CallSource::Sdr;
        meta.frequencyHz = config.frequencyHz;

        fmDemod_->setSquelchCallback([this, meta](bool open) {
            emit squelchChanged(open);
            emit logLine(open ? QStringLiteral("squelch ACIK (kayit basliyor)") : QStringLiteral("squelch kapali"));
            if (open) {
                recorder_->beginCall(meta);
                emit callActiveChanged(true);
            } else if (recorder_->hasActiveCall()) {
                auto rec = recorder_->endCall();
                emit callActiveChanged(false);
                if (rec) {
                    emit logLine(QStringLiteral("cagri kaydedildi: %1 (%2 ms)")
                                     .arg(QString::fromStdString(rec->audioFilePath))
                                     .arg(rec->durationMs));
                    emit callFinished();
                }
            }
        });
        fmDemod_->setAudioCallback(
            [this](const int16_t* pcm, size_t count) { recorder_->pushAudio(pcm, count); });
        fmDemod_->setLevelCallback([this](double powerDb) { emit levelChanged(powerDb); });
    } else {
        recorder_ = std::make_unique<core::CallRecorder>(*db_, config.recordingsDir, 8000);
        recorderSlot2_ = std::make_unique<core::CallRecorder>(*db_, config.recordingsDir, 8000);
        dmrTrackerSlot1_ =
            std::make_unique<dsp::dmr::DmrCallTracker>(*recorder_, 1, config.frequencyHz, "Canli DMR");
        dmrTrackerSlot2_ =
            std::make_unique<dsp::dmr::DmrCallTracker>(*recorderSlot2_, 2, config.frequencyHz, "Canli DMR");

        dsp::dmr::DmrRfConfig cfg;
        cfg.iqSampleRateHz = iqSampleRateHz;
        cfg.squelchThresholdDb = config.squelchThresholdDb;
        cfg.mixerOffsetHz = mixerOffsetHz;
        dmrDemod_ = std::make_unique<dsp::dmr::DmrRfDemodulator>(cfg);

        dmrDemod_->setSquelchCallback([this](bool open) {
            emit squelchChanged(open);
            emit logLine(open ? QStringLiteral("squelch ACIK") : QStringLiteral("squelch kapali"));
        });

        // Slot 1/2 demux by arrival order ONLY - same documented assumption
        // as biem_cli.cpp's runDmrLive() (correct for a busy repeater
        // alternating slots in lockstep, wrong for a single active slot or
        // a missed burst - see docs/ROADMAP.md, not solved here either).
        nextIsSlot1_ = true;
        dmrDemod_->setBurstCallback([this](const dsp::dmr::DmrBurstBytes& b, dsp::dmr::SyncType t) {
            core::CallRecorder& rec = nextIsSlot1_ ? *recorder_ : *recorderSlot2_;
            dsp::dmr::DmrCallTracker& tracker = nextIsSlot1_ ? *dmrTrackerSlot1_ : *dmrTrackerSlot2_;
            const int slotLabel = nextIsSlot1_ ? 1 : 2;

            dsp::dmr::SlotTypeInfo info = dmrSlotDecoderForDiag_.decodeSlotType(b);
            emit logLine(QStringLiteral("burst alindi: sync-tur=%1 slot=%2 renk-kodu=%3 veri-tipi=%4 golay=%5")
                             .arg(dsp::dmr::toString(t))
                             .arg(slotLabel)
                             .arg(info.colorCode)
                             .arg(dsp::dmr::toString(info.dataType))
                             .arg(info.correctedBits));

            const bool wasActive = rec.hasActiveCall();
            tracker.onBurst(b, t);
            const bool isActive = rec.hasActiveCall();
            if (!wasActive && isActive) {
                emit callActiveChanged(true);
                emit logLine(QStringLiteral("cagri basladi (slot %1)").arg(slotLabel));
            } else if (wasActive && !isActive) {
                emit callActiveChanged(false);
                emit logLine(QStringLiteral("cagri bitti (slot %1)").arg(slotLabel));
                emit callFinished();
            }
            nextIsSlot1_ = !nextIsSlot1_;
        });
        dmrDemod_->setLevelCallback([this](double powerDb) {
            emit levelChanged(powerDb);
            emit lockChanged(dmrDemod_->locked());
        });
    }

    source_ = std::move(rtl);
    running_ = true;
    if (mode_ == LiveMode::AnalogFm) {
        source_->start(
            [this](const dsp::IqSample* samples, size_t count) { fmDemod_->processSamples(samples, count); });
    } else {
        source_->start(
            [this](const dsp::IqSample* samples, size_t count) { dmrDemod_->processSamples(samples, count); });
    }
    emit logLine(QStringLiteral("dinleniyor: %1 Hz (donanim %2 Hz'e ayarli)")
                     .arg(config.frequencyHz, 0, 'f', 0)
                     .arg(tunedFrequencyHz, 0, 'f', 0));
    return QString();
#endif
}

void LiveSession::stop() {
    if (!running_) return;
    if (source_) source_->stop();  // blocks until RtlSdrSource's background thread has fully joined

    // Past this point nothing can call into this object concurrently
    // anymore (see class comment), so plain member access below is safe.
#if defined(BIEM_HAVE_RTLSDR)
    if (mode_ == LiveMode::AnalogFm && recorder_ && recorder_->hasActiveCall()) {
        // Matches runLive()'s own end-of-session flush - runDmrLive() has no
        // DMR equivalent today (an in-progress DMR call is simply left
        // unflushed on stop, same as the CLI), not attempted here either.
        auto rec = recorder_->endCall();
        emit callActiveChanged(false);
        if (rec) emit callFinished();
    }
#endif
    source_.reset();
    fmDemod_.reset();
    dmrDemod_.reset();
    dmrTrackerSlot1_.reset();
    dmrTrackerSlot2_.reset();
    recorder_.reset();
    recorderSlot2_.reset();
    db_.reset();
    running_ = false;
    emit logLine(QStringLiteral("durduruldu."));
}

// --- LiveMonitorWidget -------------------------------------------------------

LiveMonitorWidget::LiveMonitorWidget(std::string dbPath, std::string recordingsDir, QWidget* parent)
    : QWidget(parent), dbPath_(std::move(dbPath)), recordingsDir_(std::move(recordingsDir)) {
    modeCombo_ = new QComboBox(this);
    modeCombo_->addItem(tr("Analog FM"));
    modeCombo_->addItem(tr("DMR (deneysel)"));

    frequencyMhzSpin_ = new QDoubleSpinBox(this);
    frequencyMhzSpin_->setDecimals(6);
    frequencyMhzSpin_->setRange(24.0, 1750.0);
    frequencyMhzSpin_->setSingleStep(0.00625);  // one 12.5 kHz channel step
    frequencyMhzSpin_->setValue(446.0);
    frequencyMhzSpin_->setSuffix(tr(" MHz"));

    squelchSpin_ = new QDoubleSpinBox(this);
    squelchSpin_->setRange(-100.0, 0.0);
    squelchSpin_->setSingleStep(1.0);
    squelchSpin_->setValue(-50.0);
    squelchSpin_->setSuffix(tr(" dB"));

    manualGainCheck_ = new QCheckBox(tr("Manuel kazanc"), this);
    gainSpin_ = new QDoubleSpinBox(this);
    gainSpin_->setRange(0.0, 50.0);
    gainSpin_->setSingleStep(0.5);
    gainSpin_->setValue(30.0);
    gainSpin_->setSuffix(tr(" dB"));
    gainSpin_->setEnabled(false);
    connect(manualGainCheck_, &QCheckBox::toggled, gainSpin_, &QDoubleSpinBox::setEnabled);

    startStopButton_ = new QPushButton(tr("Baslat"), this);

    auto* form = new QFormLayout();
    form->addRow(tr("Mod:"), modeCombo_);
    form->addRow(tr("Frekans:"), frequencyMhzSpin_);
    form->addRow(tr("Squelch esigi:"), squelchSpin_);
    auto* gainRow = new QHBoxLayout();
    gainRow->addWidget(manualGainCheck_);
    gainRow->addWidget(gainSpin_);
    gainRow->addStretch(1);
    form->addRow(tr("Kazanc:"), gainRow);

    auto* controlsBox = new QGroupBox(tr("Ayarlar"), this);
    auto* controlsLayout = new QVBoxLayout(controlsBox);
    controlsLayout->addLayout(form);
    controlsLayout->addWidget(startStopButton_);

    squelchIndicator_ = new QLabel(tr("Squelch: kapali"), this);
    lockIndicator_ = new QLabel(tr("Kilit: yok"), this);
    callIndicator_ = new QLabel(tr("● Bekleniyor"), this);
    callIndicator_->setStyleSheet("font-weight: bold; color: gray;");
    levelMeter_ = new QProgressBar(this);
    levelMeter_->setRange(-80, 0);
    levelMeter_->setValue(-80);
    levelMeter_->setFormat(tr("%v dB"));
    levelMeter_->setTextVisible(true);

    auto* statusRow = new QHBoxLayout();
    statusRow->addWidget(squelchIndicator_);
    statusRow->addWidget(lockIndicator_);
    statusRow->addWidget(callIndicator_);
    statusRow->addStretch(1);

    auto* statusBox = new QGroupBox(tr("Canli Durum"), this);
    auto* statusLayout = new QVBoxLayout(statusBox);
    statusLayout->addLayout(statusRow);
    statusLayout->addWidget(new QLabel(tr("Guc seviyesi:"), this));
    statusLayout->addWidget(levelMeter_);

    log_ = new QPlainTextEdit(this);
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(2000);  // bounded scrollback - a long PTT session shouldn't grow this unbounded

    auto* topRow = new QHBoxLayout();
    topRow->addWidget(controlsBox);
    topRow->addWidget(statusBox, 1);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(topRow);
    mainLayout->addWidget(new QLabel(tr("Gunluk:"), this));
    mainLayout->addWidget(log_, 1);

    connect(startStopButton_, &QPushButton::clicked, this, &LiveMonitorWidget::onStartStopClicked);
    connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &LiveMonitorWidget::onModeChanged);

    onModeChanged(modeCombo_->currentIndex());

#if !defined(BIEM_HAVE_RTLSDR)
    startStopButton_->setEnabled(false);
    appendLog(tr("Bu derlemede RTL-SDR destegi yok (librtlsdr bulunamadi) - canli alim devre disi. "
                 "bkz. docs/BUILD_WINDOWS.md."));
#endif
}

LiveMonitorWidget::~LiveMonitorWidget() {
    if (session_) session_->stop();
}

void LiveMonitorWidget::onModeChanged(int index) {
    const bool isDmr = (index == 1);
    lockIndicator_->setVisible(isDmr);
}

void LiveMonitorWidget::onStartStopClicked() {
    if (session_ && session_->running()) {
        startStopButton_->setEnabled(false);
        startStopButton_->setText(tr("Durduruluyor..."));
        session_->stop();
        session_.reset();
        setRunningUiState(false);
        startStopButton_->setEnabled(true);
        return;
    }

    LiveSessionConfig cfg;
    cfg.mode = (modeCombo_->currentIndex() == 1) ? LiveMode::Dmr : LiveMode::AnalogFm;
    cfg.frequencyHz = frequencyMhzSpin_->value() * 1.0e6;
    cfg.squelchThresholdDb = squelchSpin_->value();
    cfg.gainTenthDb = manualGainCheck_->isChecked() ? static_cast<int>(std::lround(gainSpin_->value() * 10.0)) : -1;
    cfg.dbPath = dbPath_;
    cfg.recordingsDir = recordingsDir_;

    session_ = std::make_unique<LiveSession>(this);
    connect(session_.get(), &LiveSession::levelChanged, this, [this](double db) {
        levelMeter_->setValue(static_cast<int>(std::lround(std::clamp(db, -80.0, 0.0))));
    });
    connect(session_.get(), &LiveSession::squelchChanged, this, [this](bool open) {
        squelchIndicator_->setText(open ? tr("Squelch: ACIK") : tr("Squelch: kapali"));
        squelchIndicator_->setStyleSheet(open ? "font-weight: bold; color: green;" : "color: gray;");
    });
    connect(session_.get(), &LiveSession::lockChanged, this, [this](bool locked) {
        lockIndicator_->setText(locked ? tr("Kilit: VAR") : tr("Kilit: yok"));
        lockIndicator_->setStyleSheet(locked ? "font-weight: bold; color: green;" : "color: gray;");
    });
    connect(session_.get(), &LiveSession::callActiveChanged, this, [this](bool active) {
        callIndicator_->setText(active ? tr("● AKTIF CAGRI") : tr("● Bekleniyor"));
        callIndicator_->setStyleSheet(active ? "font-weight: bold; color: red;" : "font-weight: bold; color: gray;");
    });
    connect(session_.get(), &LiveSession::callFinished, this, &LiveMonitorWidget::callFinished);
    connect(session_.get(), &LiveSession::logLine, this, &LiveMonitorWidget::appendLog);

    QString error = session_->start(cfg);
    if (!error.isEmpty()) {
        appendLog(tr("HATA: %1").arg(error));
        QMessageBox::warning(this, tr("Canli alim baslatilamadi"), error);
        session_.reset();
        return;
    }

    setRunningUiState(true);
}

void LiveMonitorWidget::appendLog(const QString& text) {
    log_->appendPlainText(QStringLiteral("[%1] %2")
                               .arg(QTime::currentTime().toString("HH:mm:ss"), text));
}

void LiveMonitorWidget::setRunningUiState(bool running) {
    modeCombo_->setEnabled(!running);
    frequencyMhzSpin_->setEnabled(!running);
    squelchSpin_->setEnabled(!running);
    manualGainCheck_->setEnabled(!running);
    gainSpin_->setEnabled(!running && manualGainCheck_->isChecked());
    startStopButton_->setText(running ? tr("Durdur") : tr("Baslat"));
    if (!running) {
        squelchIndicator_->setText(tr("Squelch: kapali"));
        squelchIndicator_->setStyleSheet("color: gray;");
        lockIndicator_->setText(tr("Kilit: yok"));
        lockIndicator_->setStyleSheet("color: gray;");
        callIndicator_->setText(tr("● Bekleniyor"));
        callIndicator_->setStyleSheet("font-weight: bold; color: gray;");
        levelMeter_->setValue(-80);
    }
}

}  // namespace biem::ui
