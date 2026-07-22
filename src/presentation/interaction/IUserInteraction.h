#pragma once

#include <QString>

class QWidget;

namespace novel::presentation {

enum class UnsavedChangesChoice { Save, Discard, Cancel };

class IUserInteraction {
public:
    virtual ~IUserInteraction() = default;

    virtual QString chooseOpenProjectFile(QWidget* parent) = 0;
    virtual QString chooseSaveProjectFile(QWidget* parent,
                                          const QString& currentPath) = 0;
    virtual QString chooseChapterTextFile(QWidget* parent) = 0;
    virtual QString choosePersonsDictionaryFile(QWidget* parent) = 0;
    virtual QString chooseAliasesDictionaryFile(QWidget* parent) = 0;

    virtual UnsavedChangesChoice confirmUnsavedChanges(QWidget* parent) = 0;
    virtual bool confirmDestructiveAction(QWidget* parent,
                                          const QString& title,
                                          const QString& message) = 0;

    virtual void showError(QWidget* parent, const QString& title,
                           const QString& message) = 0;
    virtual void showInformation(QWidget* parent, const QString& title,
                                 const QString& message) = 0;
};

}  // namespace novel::presentation
