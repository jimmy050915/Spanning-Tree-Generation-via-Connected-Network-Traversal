#include "presentation/dialogs/PersonManagementDialog.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace novel::presentation {

PersonManagementDialog::PersonManagementDialog(QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("personManagementDialog"));
    setWindowTitle(tr("人物管理"));
    resize(640, 480);

    auto* root = new QVBoxLayout(this);
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setObjectName(QStringLiteral("managePersonSearchEdit"));
    searchEdit_->setPlaceholderText(tr("搜索人物…"));
    root->addWidget(searchEdit_);

    table_ = new QTableWidget(this);
    table_->setObjectName(QStringLiteral("managePeopleTable"));
    table_->setColumnCount(2);
    table_->setHorizontalHeaderLabels({tr("标准人物名"),
                                       tr("出现章节数")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(table_, 1);

    auto* actions = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("新增…"), this);
    addButton->setObjectName(QStringLiteral("addPersonButton"));
    renameButton_ = new QPushButton(tr("重命名…"), this);
    renameButton_->setObjectName(QStringLiteral("renamePersonButton"));
    mergeButton_ = new QPushButton(tr("合并到…"), this);
    mergeButton_->setObjectName(QStringLiteral("mergePersonButton"));
    deleteButton_ = new QPushButton(tr("删除未使用人物"), this);
    deleteButton_->setObjectName(QStringLiteral("deleteUnusedPersonButton"));
    actions->addWidget(addButton);
    actions->addWidget(renameButton_);
    actions->addWidget(mergeButton_);
    actions->addWidget(deleteButton_);
    actions->addStretch();
    root->addLayout(actions);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->setObjectName(QStringLiteral("personManagementButtons"));
    root->addWidget(buttons);

    connect(searchEdit_, &QLineEdit::textChanged, this,
            &PersonManagementDialog::rebuildTable);
    connect(table_, &QTableWidget::itemSelectionChanged, this,
            &PersonManagementDialog::updateButtons);
    connect(addButton, &QPushButton::clicked, this,
            &PersonManagementDialog::requestAdd);
    connect(renameButton_, &QPushButton::clicked, this,
            &PersonManagementDialog::requestRename);
    connect(mergeButton_, &QPushButton::clicked, this,
            &PersonManagementDialog::requestMerge);
    connect(deleteButton_, &QPushButton::clicked, this,
            &PersonManagementDialog::requestDelete);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateButtons();
}

void PersonManagementDialog::setPeople(
    const std::vector<PersonChoice>& people) {
    const auto selected = selectedPerson();
    people_ = people;
    rebuildTable();
    if (selected != 0) {
        for (int row = 0; row < table_->rowCount(); ++row) {
            if (table_->item(row, 0)->data(kPersonIdRole).value<PersonId>() ==
                selected) {
                table_->selectRow(row);
                break;
            }
        }
    }
}

PersonId PersonManagementDialog::selectedPerson() const noexcept {
    const auto selected = table_->selectedItems();
    return selected.isEmpty()
               ? 0
               : selected.front()->data(kPersonIdRole).value<PersonId>();
}

void PersonManagementDialog::rebuildTable() {
    const auto needle = searchEdit_->text().trimmed();
    table_->setRowCount(0);
    for (const auto& person : people_) {
        if (!needle.isEmpty() &&
            !person.name.contains(needle, Qt::CaseInsensitive)) {
            continue;
        }
        const int row = table_->rowCount();
        table_->insertRow(row);
        auto* name = new QTableWidgetItem(person.name);
        name->setData(kPersonIdRole, QVariant::fromValue(person.id));
        auto* count = new QTableWidgetItem;
        count->setData(Qt::DisplayRole, person.chapterCount);
        count->setData(kPersonIdRole, QVariant::fromValue(person.id));
        table_->setItem(row, 0, name);
        table_->setItem(row, 1, count);
    }
    updateButtons();
}

void PersonManagementDialog::requestAdd() {
    bool accepted = false;
    const auto name = QInputDialog::getText(
                          this, tr("新增人物"), tr("标准人物名："),
                          QLineEdit::Normal, {}, &accepted)
                          .trimmed();
    if (accepted && !name.isEmpty()) {
        emit addPersonRequested(name);
    }
}

void PersonManagementDialog::requestRename() {
    const auto id = selectedPerson();
    if (id == 0) {
        return;
    }
    const auto found = std::find_if(people_.begin(), people_.end(),
                                    [id](const PersonChoice& person) {
                                        return person.id == id;
                                    });
    if (found == people_.end()) {
        return;
    }
    bool accepted = false;
    const auto name = QInputDialog::getText(
                          this, tr("重命名人物"), tr("新标准人物名："),
                          QLineEdit::Normal, found->name, &accepted)
                          .trimmed();
    if (accepted && !name.isEmpty() && name != found->name) {
        emit renamePersonRequested(id, name);
    }
}

void PersonManagementDialog::requestMerge() {
    const auto source = selectedPerson();
    if (source == 0 || people_.size() < 2) {
        return;
    }
    QStringList names;
    std::vector<PersonId> ids;
    for (const auto& person : people_) {
        if (person.id != source) {
            names.push_back(person.name);
            ids.push_back(person.id);
        }
    }
    bool accepted = false;
    const auto targetName = QInputDialog::getItem(
        this, tr("合并人物"), tr("将当前人物合并到："), names, 0,
        false, &accepted);
    if (!accepted) {
        return;
    }
    const auto index = names.indexOf(targetName);
    if (index >= 0) {
        emit mergePeopleRequested(source,
                                  ids[static_cast<std::size_t>(index)]);
    }
}

void PersonManagementDialog::requestDelete() {
    const auto id = selectedPerson();
    if (id != 0) {
        emit deleteUnusedPersonRequested(id);
    }
}

void PersonManagementDialog::updateButtons() {
    const bool selected = selectedPerson() != 0;
    renameButton_->setEnabled(selected);
    mergeButton_->setEnabled(selected && people_.size() > 1);
    deleteButton_->setEnabled(selected);
}

}  // namespace novel::presentation
