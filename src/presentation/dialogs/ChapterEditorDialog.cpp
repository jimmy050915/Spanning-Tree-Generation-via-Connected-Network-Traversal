#include "presentation/dialogs/ChapterEditorDialog.h"

#include "presentation/interaction/IUserInteraction.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace novel::presentation {

ChapterEditorDialog::ChapterEditorDialog(Mode mode, QWidget* parent,
                                         IUserInteraction* interaction)
    : QDialog(parent), mode_(mode), interaction_(interaction) {
    buildUi();
}

void ChapterEditorDialog::buildUi() {
    setObjectName(QStringLiteral("chapterEditorDialog"));
    setWindowTitle(mode_ == Mode::Import ? tr("确认导入章节")
                                         : tr("编辑章节"));
    resize(920, 700);

    auto* root = new QVBoxLayout(this);
    auto* metadata = new QFormLayout;
    pathEdit_ = new QLineEdit(this);
    pathEdit_->setObjectName(QStringLiteral("chapterPathEdit"));
    pathEdit_->setReadOnly(true);
    keyEdit_ = new QLineEdit(this);
    keyEdit_->setObjectName(QStringLiteral("chapterKeyEdit"));
    titleEdit_ = new QLineEdit(this);
    titleEdit_->setObjectName(QStringLiteral("chapterTitleEdit"));
    metadata->addRow(tr("文件路径："), pathEdit_);
    metadata->addRow(tr("章节编号："), keyEdit_);
    metadata->addRow(tr("章节标题："), titleEdit_);
    root->addLayout(metadata);

    auto* split = new QSplitter(Qt::Vertical, this);
    auto* contentPanel = new QWidget(split);
    auto* contentLayout = new QVBoxLayout(contentPanel);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->addWidget(new QLabel(tr("正文预览"), contentPanel));
    contentEdit_ = new QPlainTextEdit(contentPanel);
    contentEdit_->setObjectName(QStringLiteral("chapterContentPreview"));
    contentEdit_->setReadOnly(true);
    contentEdit_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    contentLayout->addWidget(contentEdit_);

    auto* peoplePanel = new QWidget(split);
    auto* peopleLayout = new QVBoxLayout(peoplePanel);
    peopleLayout->setContentsMargins(0, 0, 0, 0);
    peopleLayout->addWidget(new QLabel(tr("自动识别结果"), peoplePanel));
    recognizedTable_ = new QTableWidget(peoplePanel);
    recognizedTable_->setObjectName(QStringLiteral("recognizedPeopleTable"));
    recognizedTable_->setColumnCount(3);
    recognizedTable_->setHorizontalHeaderLabels(
        {tr("匹配文本"), tr("类型"), tr("标准人物")});
    recognizedTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    recognizedTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    recognizedTable_->horizontalHeader()->setStretchLastSection(true);
    peopleLayout->addWidget(recognizedTable_, 1);

    auto* chooseLayout = new QHBoxLayout;
    auto* availableLayout = new QVBoxLayout;
    personSearchEdit_ = new QLineEdit(peoplePanel);
    personSearchEdit_->setObjectName(QStringLiteral("personSearchEdit"));
    personSearchEdit_->setPlaceholderText(tr("搜索可添加人物…"));
    availablePeopleList_ = new QListWidget(peoplePanel);
    availablePeopleList_->setObjectName(QStringLiteral("availablePeopleList"));
    availableLayout->addWidget(personSearchEdit_);
    availableLayout->addWidget(availablePeopleList_, 1);
    chooseLayout->addLayout(availableLayout, 1);

    auto* controls = new QVBoxLayout;
    addPersonButton_ = new QPushButton(tr("添加 →"), peoplePanel);
    addPersonButton_->setObjectName(QStringLiteral("addSelectedPersonButton"));
    removePersonButton_ = new QPushButton(tr("← 移除"), peoplePanel);
    removePersonButton_->setObjectName(
        QStringLiteral("removeSelectedPersonButton"));
    newPersonButton_ = new QPushButton(tr("新建标准人物…"), peoplePanel);
    newPersonButton_->setObjectName(QStringLiteral("newPersonButton"));
    newAliasButton_ = new QPushButton(tr("添加别名…"), peoplePanel);
    newAliasButton_->setObjectName(QStringLiteral("newAliasButton"));
    controls->addStretch();
    controls->addWidget(addPersonButton_);
    controls->addWidget(removePersonButton_);
    controls->addWidget(newPersonButton_);
    controls->addWidget(newAliasButton_);
    controls->addStretch();
    chooseLayout->addLayout(controls);

    auto* selectedLayout = new QVBoxLayout;
    selectedLayout->addWidget(new QLabel(tr("已选人物"), peoplePanel));
    selectedPeopleList_ = new QListWidget(peoplePanel);
    selectedPeopleList_->setObjectName(QStringLiteral("selectedPeopleList"));
    selectedLayout->addWidget(selectedPeopleList_, 1);
    chooseLayout->addLayout(selectedLayout, 1);
    peopleLayout->addLayout(chooseLayout, 2);

    split->addWidget(contentPanel);
    split->addWidget(peoplePanel);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    root->addWidget(split, 1);

    buttonBox_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox_->setObjectName(QStringLiteral("chapterDialogButtons"));
    buttonBox_->button(QDialogButtonBox::Ok)
        ->setText(mode_ == Mode::Import ? tr("确认导入") : tr("保存修改"));
    root->addWidget(buttonBox_);

    connect(personSearchEdit_, &QLineEdit::textChanged, this,
            &ChapterEditorDialog::updateAvailablePeople);
    connect(addPersonButton_, &QPushButton::clicked, this,
            &ChapterEditorDialog::addSelectedPerson);
    connect(removePersonButton_, &QPushButton::clicked, this,
            &ChapterEditorDialog::removeSelectedPerson);
    connect(newPersonButton_, &QPushButton::clicked, this,
            &ChapterEditorDialog::createPerson);
    connect(newAliasButton_, &QPushButton::clicked, this,
            &ChapterEditorDialog::createAlias);
    connect(availablePeopleList_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { addSelectedPerson(); });
    connect(buttonBox_, &QDialogButtonBox::accepted, this,
            &ChapterEditorDialog::validateAndAccept);
    connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ChapterEditorDialog::setData(const ChapterEditorData& data) {
    data_ = data;
    pendingAliases_.clear();
    pathEdit_->setText(data.filePath);
    keyEdit_->setText(data.chapterKey);
    titleEdit_->setText(data.title);
    contentEdit_->setPlainText(data.content);
    rebuildRecognizedMatches();
    rebuildSelectedPeople();
    updateAvailablePeople();
}

ChapterEditorResult ChapterEditorDialog::result() const {
    ChapterEditorResult result;
    result.chapterKey = keyEdit_->text().trimmed();
    result.title = titleEdit_->text().trimmed();
    result.content = contentEdit_->toPlainText();
    result.newAliases = pendingAliases_;
    for (int row = 0; row < selectedPeopleList_->count(); ++row) {
        const auto* item = selectedPeopleList_->item(row);
        if (item->data(kIsNewPersonRole).toBool()) {
            result.selectedNewPeople.push_back(item->text());
        } else {
            result.selectedPeople.push_back(
                item->data(kPersonIdRole).value<PersonId>());
        }
    }
    return result;
}

void ChapterEditorDialog::rebuildRecognizedMatches() {
    recognizedTable_->setRowCount(static_cast<int>(data_.matches.size()));
    for (int row = 0; row < static_cast<int>(data_.matches.size()); ++row) {
        const auto& match = data_.matches[static_cast<std::size_t>(row)];
        recognizedTable_->setItem(row, 0,
                                  new QTableWidgetItem(match.matchedText));
        recognizedTable_->setItem(
            row, 1,
            new QTableWidgetItem(match.isAlias ? tr("别名") : tr("标准名")));
        recognizedTable_->setItem(
            row, 2, new QTableWidgetItem(match.canonicalName));
    }
    recognizedTable_->resizeColumnsToContents();
}

void ChapterEditorDialog::rebuildSelectedPeople() {
    selectedPeopleList_->clear();
    for (const auto id : data_.selectedPeople) {
        const auto found = std::find_if(
            data_.availablePeople.begin(), data_.availablePeople.end(),
            [id](const PersonChoice& person) { return person.id == id; });
        if (found != data_.availablePeople.end()) {
            appendSelectedPerson(found->id, found->name, false);
        }
    }
}

void ChapterEditorDialog::updateAvailablePeople() {
    const auto needle = personSearchEdit_->text().trimmed();
    availablePeopleList_->clear();
    for (const auto& person : data_.availablePeople) {
        if (!needle.isEmpty() &&
            !person.name.contains(needle, Qt::CaseInsensitive)) {
            continue;
        }
        auto* item = new QListWidgetItem(
            tr("%1（%2 章）").arg(person.name).arg(person.chapterCount),
            availablePeopleList_);
        item->setData(kPersonIdRole, QVariant::fromValue(person.id));
        item->setData(Qt::UserRole, person.name);
        if (containsSelectedPerson(person.id)) {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        }
    }
}

void ChapterEditorDialog::addSelectedPerson() {
    const auto* source = availablePeopleList_->currentItem();
    if (!source) {
        return;
    }
    const auto id = source->data(kPersonIdRole).value<PersonId>();
    if (containsSelectedPerson(id)) {
        return;
    }
    appendSelectedPerson(id, source->data(Qt::UserRole).toString(), false);
    updateAvailablePeople();
}

void ChapterEditorDialog::removeSelectedPerson() {
    auto* removed =
        selectedPeopleList_->takeItem(selectedPeopleList_->currentRow());
    if (!removed) {
        return;
    }
    if (removed->data(kIsNewPersonRole).toBool()) {
        const auto name = removed->text();
        pendingAliases_.erase(
            std::remove_if(pendingAliases_.begin(), pendingAliases_.end(),
                           [&name](const PendingAlias& alias) {
                               return alias.targetNewPersonName == name;
                           }),
            pendingAliases_.end());
    }
    delete removed;
    updateAvailablePeople();
}

void ChapterEditorDialog::createPerson() {
    bool accepted = false;
    const auto name = QInputDialog::getText(
                          this, tr("新建标准人物"), tr("人物名："),
                          QLineEdit::Normal, {}, &accepted)
                          .trimmed();
    if (!accepted || name.isEmpty()) {
        return;
    }
    const auto existing = std::any_of(
        data_.availablePeople.begin(), data_.availablePeople.end(),
        [&name](const PersonChoice& person) { return person.name == name; });
    if (existing || containsSelectedName(name)) {
        showError(tr("人物重复"), tr("人物“%1”已存在。").arg(name));
        return;
    }
    appendSelectedPerson(0, name, true);
}

void ChapterEditorDialog::createAlias() {
    if (selectedPeopleList_->count() == 0) {
        showInformation(tr("添加别名"),
                        tr("请先在已选人物中选择目标人物。"));
        return;
    }
    bool accepted = false;
    const auto alias = QInputDialog::getText(
                           this, tr("添加别名"), tr("别名："),
                           QLineEdit::Normal, {}, &accepted)
                           .trimmed();
    if (!accepted || alias.isEmpty()) {
        return;
    }
    auto* target = selectedPeopleList_->currentItem();
    if (!target) {
        target = selectedPeopleList_->item(0);
    }
    PendingAlias pending;
    pending.alias = alias;
    if (target->data(kIsNewPersonRole).toBool()) {
        pending.targetNewPersonName = target->text();
    } else {
        pending.targetPerson = target->data(kPersonIdRole).value<PersonId>();
    }
    pendingAliases_.push_back(std::move(pending));
    newAliasButton_->setToolTip(
        tr("已暂存 %1 个别名，将随章节一并提交。")
            .arg(pendingAliases_.size()));
}

void ChapterEditorDialog::validateAndAccept() {
    if (keyEdit_->text().trimmed().isEmpty()) {
        showError(tr("章节信息不完整"), tr("章节编号不能为空。"));
        keyEdit_->setFocus();
        return;
    }
    accept();
}

bool ChapterEditorDialog::containsSelectedPerson(PersonId id) const {
    for (int row = 0; row < selectedPeopleList_->count(); ++row) {
        const auto* item = selectedPeopleList_->item(row);
        if (!item->data(kIsNewPersonRole).toBool() &&
            item->data(kPersonIdRole).value<PersonId>() == id) {
            return true;
        }
    }
    return false;
}

bool ChapterEditorDialog::containsSelectedName(const QString& name) const {
    for (int row = 0; row < selectedPeopleList_->count(); ++row) {
        if (selectedPeopleList_->item(row)->text().compare(
                name, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

void ChapterEditorDialog::appendSelectedPerson(PersonId id,
                                               const QString& name,
                                               bool isNew) {
    auto* item = new QListWidgetItem(name, selectedPeopleList_);
    item->setData(kPersonIdRole, QVariant::fromValue(id));
    item->setData(kIsNewPersonRole, isNew);
    if (isNew) {
        item->setToolTip(tr("将在确认章节时新建该标准人物。"));
    }
}

void ChapterEditorDialog::showError(const QString& title,
                                    const QString& message) {
    if (interaction_ != nullptr) {
        interaction_->showError(this, title, message);
        return;
    }
    QMessageBox::warning(this, title, message);
}

void ChapterEditorDialog::showInformation(const QString& title,
                                          const QString& message) {
    if (interaction_ != nullptr) {
        interaction_->showInformation(this, title, message);
        return;
    }
    QMessageBox::information(this, title, message);
}

}  // namespace novel::presentation
