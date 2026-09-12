#pragma once

#include <QWidget>
#include <vector>

#include "../core/Database.h"

class QTableWidget;
class QLineEdit;
class QDateTimeEdit;
class QPushButton;

namespace biem::ui {

class PlaybackWidget;

// Search + browse the call log: filter by date range / title / talkgroup /
// radio ID, and play back the selected recording. Works identically
// whether a row came from the SDR/DMR path or the network/repeater path -
// see docs/ARCHITECTURE.md.
class CallLogWidget : public QWidget {
    Q_OBJECT
public:
    explicit CallLogWidget(core::Database& db, QWidget* parent = nullptr);

private slots:
    void runSearch();
    void playSelected();

private:
    core::Database& db_;

    QLineEdit* titleFilter_ = nullptr;
    QLineEdit* talkgroupFilter_ = nullptr;
    QLineEdit* radioIdFilter_ = nullptr;
    QDateTimeEdit* fromFilter_ = nullptr;
    QDateTimeEdit* toFilter_ = nullptr;
    QPushButton* searchButton_ = nullptr;
    QPushButton* playButton_ = nullptr;
    QTableWidget* resultsTable_ = nullptr;
    PlaybackWidget* playback_ = nullptr;

    std::vector<core::CallRecord> currentResults_;

    void populateTable(const std::vector<core::CallRecord>& results);
};

} // namespace biem::ui
