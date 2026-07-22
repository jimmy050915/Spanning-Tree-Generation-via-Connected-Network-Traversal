#include "presentation/dialogs/AliasManagementDialog.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace novel::presentation {

AliasManagementDialog::AliasManagementDialog(QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("aliasManagementDialog"));
    setWindowTitle(tr("别名管理"));
    resize(560, 420);

    auto* root = new QVBoxLayout(this);
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setObjectName(QStringLiteral("aliasSearchEdit"));
    searchEdit_->setPlaceholderText(tr("搜索别名或标准人物…"));
    root->addWidget(searchEdit_);

    table_ = new QTableWidget(this);
    table_->setObjectName(QStringLiteral("aliasesTable"));
    table_->setColumnCount(2);
    table_->setHorizontalHeaderLabels({tr("别名"), tr("标准人物")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(table_, 1);

    auto* actionLayout = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("添加别名…"), this);
    addButton->setObjectName(QStringLiteral("addAliasButton"));
    removeButton_ = new QPushButton(tr("删除别名"), this);
    removeButton_->setObjectName(QStringLiteral("removeAliasButton"));
    actionLayout->addWidget(addButton);
    actionLayout->addWidget(removeButton_);
    actionLayout->addStretch();
    root->addLayout(actionLayout);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    root->addWidget(buttons);

    connect(searchEdit_, &QLineEdit::textChanged, this,
            &AliasManagementDialog::rebuildTable);
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        removeButton_->setEnabled(!table_->selectedItems().isEmpty());
    });
    connect(addButton, &QPushButton::clicked, this,
            &AliasManagementDialog::requestAdd);
    connect(removeButton_, &QPushButton::clicked, this,
            &AliasManagementDialog::requestRemove);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    removeButton_->setEnabled(false);
}

void AliasManagementDialog::setData(
    const std::vector<AliasRow>& aliases,
    const std::vector<PersonChoice>& people) {
    aliases_ = aliases;
    people_ = people;
    rebuildTable();
}

void AliasManagementDialog::rebuildTable() {
    const auto needle = searchEdit_->text().trimmed();
    table_->setRowCount(0);
    for (const auto& alias : aliases_) {
        if (!needle.isEmpty() &&
            !alias.alias.contains(needle, Qt::CaseInsensitive) &&
            !alias.targetName.contains(needle, Qt::CaseInsensitive)) {
            continue;
        }
        const int row = table_->rowCount();
        table_->insertRow(row);
        table_->setItem(row, 0, new QTableWidgetItem(alias.alias));
        table_->setItem(row, 1, new QTableWidgetItem(alias.targetName));
    }
    removeButton_->setEnabled(false);
}

void AliasManagementDialog::requestAdd() {
    if (people_.empty()) {
        return;
    }
    bool aliasAccepted = false;
    const auto alias = QInputDialog::getText(
                           this, tr("添加别名"), tr("别名："),
                           QLineEdit::Normal, {}, &aliasAccepted)
                           .trimmed();
    if (!aliasAccepted || alias.isEmpty()) {
        return;
    }
    QStringList names;
    for (const auto& person : people_) {
        names.push_back(person.name);
    }
    bool targetAccepted = false;
    const auto targetName = QInputDialog::getItem(
        this, tr("添加别名"), tr("标准人物："), names, 0, false,
        &targetAccepted);
    const auto index = names.indexOf(targetName);
    if (targetAccepted && index >= 0) {
        emit addAliasRequested(
            alias, people_[static_cast<std::size_t>(index)].id);
    }
}

void AliasManagementDialog::requestRemove() {
    const auto selected = table_->selectedItems();
    if (!selected.isEmpty()) {
        emit removeAliasRequested(table_->item(selected.front()->row(), 0)
                                      ->text());
    }
}

}  // namespace novel::presentation
