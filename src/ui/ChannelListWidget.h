#pragma once

#include <QWidget>
#include <vector>

#include "../core/ChannelConfig.h"

class QTableWidget;
class QPushButton;

namespace biem::ui {

// In-memory channel list editor. Persistence to a config file, and wiring
// this list to actually drive RtlSdrSource/NbfmDemodulator instances, are
// Faz-2 items - see docs/ROADMAP.md. This exists so the shape of "manage a
// list of monitored channels" is in place.
class ChannelListWidget : public QWidget {
    Q_OBJECT
public:
    explicit ChannelListWidget(QWidget* parent = nullptr);

    const std::vector<core::ChannelConfig>& channels() const { return channels_; }

private slots:
    void addChannel();
    void removeSelected();

private:
    std::vector<core::ChannelConfig> channels_;
    QTableWidget* table_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;

    void refreshTable();
};

} // namespace biem::ui
