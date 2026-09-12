#include "CallLogWidget.h"

#include <cstddef>

#include <QAbstractItemView>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "PlaybackWidget.h"

namespace biem::ui {

namespace {

QString modulationLabel(core::Modulation m) {
    switch (m) {
        case core::Modulation::AnalogFM:
            return QStringLiteral("FM");
        case core::Modulation::DmrDigital:
            return QStringLiteral("DMR");
        default:
            return QStringLiteral("?");
    }
}

QString sourceLabel(core::CallSource s) {
    return s == core::CallSource::NetworkRepeater ? QStringLiteral("Repeater") : QStringLiteral("SDR");
}

} // namespace

CallLogWidget::CallLogWidget(core::Database& db, QWidget* parent) : QWidget(parent), db_(db) {
    auto* filterRow = new QHBoxLayout();
    titleFilter_ = new QLineEdit(this);
    titleFilter_->setPlaceholderText(tr("Baslik icerir..."));
    talkgroupFilter_ = new QLineEdit(this);
    talkgroupFilter_->setPlaceholderText(tr("Grup/TG ID"));
    radioIdFilter_ = new QLineEdit(this);
    radioIdFilter_->setPlaceholderText(tr("Radio ID"));
    fromFilter_ = new QDateTimeEdit(QDateTime::currentDateTime().addDays(-7), this);
    fromFilter_->setCalendarPopup(true);
    toFilter_ = new QDateTimeEdit(QDateTime::currentDateTime(), this);
    toFilter_->setCalendarPopup(true);
    searchButton_ = new QPushButton(tr("Ara"), this);
    playButton_ = new QPushButton(tr("Dinle"), this);
    playButton_->setEnabled(false);

    filterRow->addWidget(new QLabel(tr("Baslik:"), this));
    filterRow->addWidget(titleFilter_);
    filterRow->addWidget(new QLabel(tr("Grup:"), this));
    filterRow->addWidget(talkgroupFilter_);
    filterRow->addWidget(new QLabel(tr("ID:"), this));
    filterRow->addWidget(radioIdFilter_);
    filterRow->addWidget(new QLabel(tr("Baslangic:"), this));
    filterRow->addWidget(fromFilter_);
    filterRow->addWidget(new QLabel(tr("Bitis:"), this));
    filterRow->addWidget(toFilter_);
    filterRow->addWidget(searchButton_);

    resultsTable_ = new QTableWidget(this);
    resultsTable_->setColumnCount(7);
    resultsTable_->setHorizontalHeaderLabels(
        {tr("Tarih"), tr("Baslik"), tr("Grup"), tr("ID"), tr("Slot"), tr("Mod"), tr("Kaynak")});
    resultsTable_->horizontalHeader()->setStretchLastSection(true);
    resultsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    playback_ = new PlaybackWidget(this);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(filterRow);
    mainLayout->addWidget(resultsTable_, 1);
    mainLayout->addWidget(playButton_);
    mainLayout->addWidget(playback_);

    connect(searchButton_, &QPushButton::clicked, this, &CallLogWidget::runSearch);
    connect(playButton_, &QPushButton::clicked, this, &CallLogWidget::playSelected);
    connect(resultsTable_, &QTableWidget::itemSelectionChanged, this,
            [this]() { playButton_->setEnabled(resultsTable_->currentRow() >= 0); });

    runSearch();
}

void CallLogWidget::runSearch() {
    core::Database::SearchFilter filter;
    if (!titleFilter_->text().isEmpty()) {
        filter.titleContains = titleFilter_->text().toStdString();
    }
    if (!talkgroupFilter_->text().isEmpty()) {
        bool ok = false;
        uint32_t v = talkgroupFilter_->text().toUInt(&ok);
        if (ok) filter.talkgroupId = v;
    }
    if (!radioIdFilter_->text().isEmpty()) {
        bool ok = false;
        uint32_t v = radioIdFilter_->text().toUInt(&ok);
        if (ok) filter.radioId = v;
    }
    filter.fromUnixTimeMs = fromFilter_->dateTime().toMSecsSinceEpoch();
    filter.toUnixTimeMs = toFilter_->dateTime().toMSecsSinceEpoch();

    currentResults_ = db_.search(filter);
    populateTable(currentResults_);
}

void CallLogWidget::populateTable(const std::vector<core::CallRecord>& results) {
    resultsTable_->setRowCount(static_cast<int>(results.size()));
    for (int row = 0; row < static_cast<int>(results.size()); ++row) {
        const auto& rec = results[static_cast<size_t>(row)];
        QString dateStr = QDateTime::fromMSecsSinceEpoch(rec.startUnixTimeMs).toString("yyyy-MM-dd HH:mm:ss");
        resultsTable_->setItem(row, 0, new QTableWidgetItem(dateStr));
        resultsTable_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(rec.title)));
        resultsTable_->setItem(
            row, 2, new QTableWidgetItem(rec.talkgroupId ? QString::number(*rec.talkgroupId) : QString()));
        resultsTable_->setItem(
            row, 3, new QTableWidgetItem(rec.radioId ? QString::number(*rec.radioId) : QString()));
        resultsTable_->setItem(row, 4,
                                new QTableWidgetItem(rec.slot ? QString::number(*rec.slot) : QString()));
        resultsTable_->setItem(row, 5, new QTableWidgetItem(modulationLabel(rec.modulation)));
        resultsTable_->setItem(row, 6, new QTableWidgetItem(sourceLabel(rec.source)));
    }
}

void CallLogWidget::playSelected() {
    int row = resultsTable_->currentRow();
    if (row < 0 || row >= static_cast<int>(currentResults_.size())) return;
    const auto& rec = currentResults_[static_cast<size_t>(row)];
    if (rec.audioFilePath.empty()) return;
    playback_->playFile(QString::fromStdString(rec.audioFilePath));
}

} // namespace biem::ui
