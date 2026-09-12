#include "PlaybackWidget.h"

#include <QAudioOutput>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaPlayer>
#include <QPushButton>
#include <QSlider>
#include <QUrl>

namespace biem::ui {

namespace {
QString formatMs(qint64 ms) {
    qint64 totalSeconds = ms / 1000;
    return QString("%1:%2").arg(totalSeconds / 60).arg(totalSeconds % 60, 2, 10, QChar('0'));
}
} // namespace

PlaybackWidget::PlaybackWidget(QWidget* parent) : QWidget(parent) {
    player_ = new QMediaPlayer(this);
    audioOutput_ = new QAudioOutput(this);
    player_->setAudioOutput(audioOutput_);

    playPauseButton_ = new QPushButton(tr("Oynat"), this);
    positionSlider_ = new QSlider(Qt::Horizontal, this);
    timeLabel_ = new QLabel("0:00 / 0:00", this);

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(playPauseButton_);
    layout->addWidget(positionSlider_, 1);
    layout->addWidget(timeLabel_);

    connect(playPauseButton_, &QPushButton::clicked, this, &PlaybackWidget::togglePlayPause);
    connect(player_, &QMediaPlayer::positionChanged, this, &PlaybackWidget::updatePosition);
    connect(player_, &QMediaPlayer::durationChanged, this, &PlaybackWidget::updateDuration);
    connect(positionSlider_, &QSlider::sliderPressed, this, [this]() { seeking_ = true; });
    connect(positionSlider_, &QSlider::sliderReleased, this, [this]() {
        seeking_ = false;
        player_->setPosition(positionSlider_->value());
    });
}

void PlaybackWidget::playFile(const QString& path) {
    player_->setSource(QUrl::fromLocalFile(path));
    player_->play();
    playPauseButton_->setText(tr("Duraklat"));
}

void PlaybackWidget::togglePlayPause() {
    if (player_->playbackState() == QMediaPlayer::PlayingState) {
        player_->pause();
        playPauseButton_->setText(tr("Oynat"));
    } else {
        player_->play();
        playPauseButton_->setText(tr("Duraklat"));
    }
}

void PlaybackWidget::updatePosition(qint64 ms) {
    if (!seeking_) positionSlider_->setValue(static_cast<int>(ms));
    timeLabel_->setText(formatMs(ms) + " / " + formatMs(player_->duration()));
}

void PlaybackWidget::updateDuration(qint64 ms) {
    positionSlider_->setRange(0, static_cast<int>(ms));
}

} // namespace biem::ui
