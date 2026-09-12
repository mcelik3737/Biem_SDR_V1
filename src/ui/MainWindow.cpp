#include "MainWindow.h"

#include <QDir>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>

#include "CallLogWidget.h"
#include "ChannelListWidget.h"

namespace biem::ui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("BIEM Radia Dispatcher"));

    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    std::string dbPath = (dataDir + "/biem_radia.sqlite3").toStdString();

    db_ = std::make_unique<core::Database>(dbPath);
    db_->migrate();

    auto* tabs = new QTabWidget(this);
    callLogWidget_ = new CallLogWidget(*db_, this);
    channelListWidget_ = new ChannelListWidget(this);

    tabs->addTab(callLogWidget_, tr("Cagri Kayitlari"));
    tabs->addTab(channelListWidget_, tr("Kanallar"));

    setCentralWidget(tabs);
    statusBar()->showMessage(tr("Hazir"));
}

MainWindow::~MainWindow() = default;

} // namespace biem::ui
