#pragma once

#include "presentation/dialogs/DialogData.h"

#include <QDialog>

class QLineEdit;
class QPushButton;
class QTableWidget;

namespace novel::presentation {

class AliasManagementDialog final : public QDialog {
    Q_OBJECT

public:
    explicit AliasManagementDialog(QWidget* parent = nullptr);

    void setData(const std::vector<AliasRow>& aliases,
                 const std::vector<PersonChoice>& people);

signals:
    void addAliasRequested(const QString& alias, PersonId target);
    void removeAliasRequested(const QString& alias);

private slots:
    void rebuildTable();
    void requestAdd();
    void requestRemove();

private:
    std::vector<AliasRow> aliases_;
    std::vector<PersonChoice> people_;
    QLineEdit* searchEdit_{};
    QTableWidget* table_{};
    QPushButton* removeButton_{};
};

}  // namespace novel::presentation
