#pragma once

#include <QWidget>

class QMediaPlayer;
class QAudioOutput;
class QSlider;
class QPushButton;
class QLabel;

namespace biem::ui {

// Minimal WAV playback: play/pause + a seek slider. Qt Multimedia
// (QMediaPlayer/QAudioOutput) handles PCM WAV natively, so this stays
// thin - no manual decoding needed.
class PlaybackWidget : public QWidget {
    Q_OBJECT
public:
    explicit PlaybackWidget(QWidget* parent = nullptr);

    void playFile(const QString& path);

private:
    QMediaPlayer* player_ = nullptr;
    QAudioOutput* audioOutput_ = nullptr;
    QPushButton* playPauseButton_ = nullptr;
    QSlider* positionSlider_ = nullptr;
    QLabel* timeLabel_ = nullptr;

    bool seeking_ = false;

    void togglePlayPause();
    void updatePosition(qint64 ms);
    void updateDuration(qint64 ms);
};

} // namespace biem::ui
