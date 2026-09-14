#include "MainWindow.h"

#include <QDir>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>

#include "CallLogWidget.h"
#include "ChannelListWidget.h"
#include "LiveMonitorWidget.h"

namespace biem::ui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("BIEM Radia Dispatcher"));

    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    std::string dbPath = (dataDir + "/biem_radia.sqlite3").toStdString();
    QString recDirQ = dataDir + "/recordings";
    QDir().mkpath(recDirQ);
    std::string recDir = recDirQ.toStdString();

    db_ = std::make_unique<core::Database>(dbPath);
    db_->migrate();

    auto* tabs = new QTabWidget(this);
    callLogWidget_ = new CallLogWidget(*db_, this);
    channelListWidget_ = new ChannelListWidget(this);
    // Own Database connection, deliberately not db_ above - see
    // LiveSession's class comment (LiveMonitorWidget.h) for why.
    liveMonitorWidget_ = new LiveMonitorWidget(dbPath, recDir, this);

    tabs->addTab(liveMonitorWidget_, tr("Canli Dinleme"));
    tabs->addTab(callLogWidget_, tr("Cagri Kayitlari"));
    tabs->addTab(channelListWidget_, tr("Kanallar"));

    // A call recorded live should show up without the user having to click
    // "Ara" themselves.
    connect(liveMonitorWidget_, &LiveMonitorWidget::callFinished, callLogWidget_, &CallLogWidget::runSearch);

    setCentralWidget(tabs);
    statusBar()->showMessage(tr("Hazir"));
}

MainWindow::~MainWindow() = default;

} // namespace biem::ui
