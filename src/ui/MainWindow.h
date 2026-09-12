#pragma once

#include <QMainWindow>
#include <memory>

#include "../core/Database.h"

namespace biem::ui {

class CallLogWidget;
class ChannelListWidget;

// STATUS: written against Qt6 Widgets/Multimedia APIs but NOT built or run
// in this repo's development environment (no Qt6 installed there) - see
// docs/BUILD_WINDOWS.md for the first real build+test.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    std::unique_ptr<core::Database> db_;
    CallLogWidget* callLogWidget_ = nullptr;
    ChannelListWidget* channelListWidget_ = nullptr;
};

} // namespace biem::ui
