#include "presentation/dialogs/NewProjectDialog.h"

#include "presentation/interaction/IUserInteraction.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace novel::presentation {

NewProjectDialog::NewProjectDialog(IUserInteraction* interaction,
                                   QWidget* parent)
    : QDialog(parent), interaction_(interaction) {
    setObjectName(QStringLiteral("newProjectDialog"));
    setWindowTitle(tr("新建项目"));
    resize(680, 220);
    auto* root = new QVBoxLayout(this);
    root->addWidget(new QLabel(
        tr("可选择 UTF-8 人物词典和别名词典；两者均留空时创建空项目。"),
        this));
    auto* form = new QFormLayout;

    personsEdit_ = new QLineEdit(this);
    personsEdit_->setObjectName(QStringLiteral("personsDictionaryPath"));
    personsEdit_->setReadOnly(true);
    auto* choosePersons = new QPushButton(tr("选择…"), this);
    choosePersons->setObjectName(QStringLiteral("choosePersonsDictionary"));
    auto* clearPersons = new QPushButton(tr("清除"), this);
    auto* personsRow = new QHBoxLayout;
    personsRow->addWidget(personsEdit_, 1);
    personsRow->addWidget(choosePersons);
    personsRow->addWidget(clearPersons);
    form->addRow(tr("人物词典："), personsRow);

    aliasesEdit_ = new QLineEdit(this);
    aliasesEdit_->setObjectName(QStringLiteral("aliasesDictionaryPath"));
    aliasesEdit_->setReadOnly(true);
    chooseAliasesButton_ = new QPushButton(tr("选择…"), this);
    chooseAliasesButton_->setObjectName(
        QStringLiteral("chooseAliasesDictionary"));
    clearAliasesButton_ = new QPushButton(tr("清除"), this);
    auto* aliasesRow = new QHBoxLayout;
    aliasesRow->addWidget(aliasesEdit_, 1);
    aliasesRow->addWidget(chooseAliasesButton_);
    aliasesRow->addWidget(clearAliasesButton_);
    form->addRow(tr("别名词典："), aliasesRow);
    root->addLayout(form);
    root->addStretch();

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("newProjectButtons"));
    buttons->button(QDialogButtonBox::Ok)->setText(tr("创建"));
    root->addWidget(buttons);

    connect(choosePersons, &QPushButton::clicked, this,
            &NewProjectDialog::choosePersonsFile);
    connect(clearPersons, &QPushButton::clicked, this,
            &NewProjectDialog::clearPersonsFile);
    connect(chooseAliasesButton_, &QPushButton::clicked, this,
            &NewProjectDialog::chooseAliasesFile);
    connect(clearAliasesButton_, &QPushButton::clicked, this,
            &NewProjectDialog::clearAliasesFile);
    connect(personsEdit_, &QLineEdit::textChanged, this,
            &NewProjectDialog::updateAliasControls);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateAliasControls();
}

QString NewProjectDialog::personsFile() const {
    return personsEdit_->text();
}

QString NewProjectDialog::aliasesFile() const {
    return aliasesEdit_->text();
}

void NewProjectDialog::choosePersonsFile() {
    const auto path = interaction_
                          ? interaction_->choosePersonsDictionaryFile(this)
                          : QFileDialog::getOpenFileName(
                                this, tr("选择人物词典"), {},
                                tr("UTF-8 文本文件 (*.txt)"));
    if (!path.isEmpty()) {
        personsEdit_->setText(path);
    }
}

void NewProjectDialog::chooseAliasesFile() {
    const auto path = interaction_
                          ? interaction_->chooseAliasesDictionaryFile(this)
                          : QFileDialog::getOpenFileName(
                                this, tr("选择别名词典"), {},
                                tr("UTF-8 文本文件 (*.txt)"));
    if (!path.isEmpty()) {
        aliasesEdit_->setText(path);
    }
}

void NewProjectDialog::clearPersonsFile() {
    personsEdit_->clear();
    aliasesEdit_->clear();
}

void NewProjectDialog::clearAliasesFile() {
    aliasesEdit_->clear();
}

void NewProjectDialog::updateAliasControls() {
    const bool enabled = !personsEdit_->text().isEmpty();
    chooseAliasesButton_->setEnabled(enabled);
    clearAliasesButton_->setEnabled(enabled && !aliasesEdit_->text().isEmpty());
    if (!enabled) {
        aliasesEdit_->clear();
    }
}

}  // namespace novel::presentation
