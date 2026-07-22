#pragma once

#include "presentation/dialogs/DialogData.h"

#include <QDialog>

class QLineEdit;
class QPushButton;
class QTableWidget;

namespace novel::presentation {

class PersonManagementDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PersonManagementDialog(QWidget* parent = nullptr);

    void setPeople(const std::vector<PersonChoice>& people);
    PersonId selectedPerson() const noexcept;

signals:
    void addPersonRequested(const QString& name);
    void renamePersonRequested(PersonId id, const QString& name);
    void mergePeopleRequested(PersonId source, PersonId target);
    void deleteUnusedPersonRequested(PersonId id);

private slots:
    void rebuildTable();
    void requestAdd();
    void requestRename();
    void requestMerge();
    void requestDelete();
    void updateButtons();

private:
    static constexpr int kPersonIdRole = Qt::UserRole + 1;

    std::vector<PersonChoice> people_;
    QLineEdit* searchEdit_{};
    QTableWidget* table_{};
    QPushButton* renameButton_{};
    QPushButton* mergeButton_{};
    QPushButton* deleteButton_{};
};

}  // namespace novel::presentation
