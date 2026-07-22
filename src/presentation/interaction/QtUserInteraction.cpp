#include "presentation/interaction/QtUserInteraction.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QWidget>

namespace novel::presentation {

QString QtUserInteraction::chooseOpenProjectFile(QWidget* parent) {
    return QFileDialog::getOpenFileName(
        parent, QObject::tr("打开项目"), {},
        QObject::tr("小说人物关系项目 (*.nprg);;所有文件 (*)"));
}

QString QtUserInteraction::chooseSaveProjectFile(
    QWidget* parent, const QString& currentPath) {
    return QFileDialog::getSaveFileName(
        parent, QObject::tr("保存项目"), currentPath,
        QObject::tr("小说人物关系项目 (*.nprg)"));
}

QString QtUserInteraction::chooseChapterTextFile(QWidget* parent) {
    return QFileDialog::getOpenFileName(
        parent, QObject::tr("导入章节"), {},
        QObject::tr("UTF-8 文本文件 (*.txt);;所有文件 (*)"));
}

QString QtUserInteraction::choosePersonsDictionaryFile(QWidget* parent) {
    return QFileDialog::getOpenFileName(
        parent, QObject::tr("选择人物词典"), {},
        QObject::tr("UTF-8 文本文件 (*.txt);;所有文件 (*)"));
}

QString QtUserInteraction::chooseAliasesDictionaryFile(QWidget* parent) {
    return QFileDialog::getOpenFileName(
        parent, QObject::tr("选择别名词典"), {},
        QObject::tr("UTF-8 文本文件 (*.txt);;所有文件 (*)"));
}

UnsavedChangesChoice QtUserInteraction::confirmUnsavedChanges(
    QWidget* parent) {
    const auto answer = QMessageBox::warning(
        parent, QObject::tr("未保存的更改"),
        QObject::tr("当前项目存在未保存的更改。"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (answer == QMessageBox::Save) {
        return UnsavedChangesChoice::Save;
    }
    if (answer == QMessageBox::Discard) {
        return UnsavedChangesChoice::Discard;
    }
    return UnsavedChangesChoice::Cancel;
}

bool QtUserInteraction::confirmDestructiveAction(
    QWidget* parent, const QString& title, const QString& message) {
    return QMessageBox::question(parent, title, message, QMessageBox::Yes |
                                                        QMessageBox::No,
                                 QMessageBox::No) == QMessageBox::Yes;
}

void QtUserInteraction::showError(QWidget* parent, const QString& title,
                                  const QString& message) {
    QMessageBox::critical(parent, title, message);
}

void QtUserInteraction::showInformation(QWidget* parent, const QString& title,
                                        const QString& message) {
    QMessageBox::information(parent, title, message);
}

}  // namespace novel::presentation
