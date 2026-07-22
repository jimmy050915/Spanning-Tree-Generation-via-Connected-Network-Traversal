#pragma once

#include "presentation/interaction/IUserInteraction.h"

namespace novel::presentation {

class QtUserInteraction final : public IUserInteraction {
public:
    QString chooseOpenProjectFile(QWidget* parent) override;
    QString chooseSaveProjectFile(QWidget* parent,
                                  const QString& currentPath) override;
    QString chooseChapterTextFile(QWidget* parent) override;
    QString choosePersonsDictionaryFile(QWidget* parent) override;
    QString chooseAliasesDictionaryFile(QWidget* parent) override;

    UnsavedChangesChoice confirmUnsavedChanges(QWidget* parent) override;
    bool confirmDestructiveAction(QWidget* parent, const QString& title,
                                  const QString& message) override;

    void showError(QWidget* parent, const QString& title,
                   const QString& message) override;
    void showInformation(QWidget* parent, const QString& title,
                         const QString& message) override;
};

}  // namespace novel::presentation
