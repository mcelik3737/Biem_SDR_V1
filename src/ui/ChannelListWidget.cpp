#include "ChannelListWidget.h"

#include <cstddef>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace biem::ui {

ChannelListWidget::ChannelListWidget(QWidget* parent) : QWidget(parent) {
    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({tr("Etiket"), tr("Frekans (Hz)"), tr("Aralik (Hz)"), tr("Mod")});
    table_->horizontalHeader()->setStretchLastSection(true);

    addButton_ = new QPushButton(tr("Kanal Ekle"), this);
    removeButton_ = new QPushButton(tr("Secileni Sil"), this);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addWidget(addButton_);
    buttonRow->addWidget(removeButton_);
    buttonRow->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(table_, 1);
    layout->addLayout(buttonRow);

    connect(addButton_, &QPushButton::clicked, this, &ChannelListWidget::addChannel);
    connect(removeButton_, &QPushButton::clicked, this, &ChannelListWidget::removeSelected);
}

void ChannelListWidget::addChannel() {
    core::ChannelConfig ch;
    ch.label = "Yeni Kanal";
    ch.frequencyHz = 446000000.0; // PMR446 band - a sane default starting point, not a claim about your channels
    ch.spacing = core::ChannelSpacing::Hz12500;
    ch.modulation = core::Modulation::AnalogFM;
    channels_.push_back(ch);
    refreshTable();
}

void ChannelListWidget::removeSelected() {
    int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(channels_.size())) return;
    channels_.erase(channels_.begin() + row);
    refreshTable();
}

void ChannelListWidget::refreshTable() {
    table_->setRowCount(static_cast<int>(channels_.size()));
    for (int row = 0; row < static_cast<int>(channels_.size()); ++row) {
        const auto& ch = channels_[static_cast<size_t>(row)];
        table_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(ch.label)));
        table_->setItem(row, 1, new QTableWidgetItem(QString::number(ch.frequencyHz, 'f', 0)));
        table_->setItem(row, 2, new QTableWidgetItem(QString::number(static_cast<int>(ch.spacing))));
        table_->setItem(
            row, 3, new QTableWidgetItem(ch.modulation == core::Modulation::DmrDigital ? "DMR" : "FM"));
    }
}

} // namespace biem::ui
