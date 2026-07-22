#pragma once

#include <QDialog>

class QLineEdit;
class QPushButton;

namespace novel::presentation {

class IUserInteraction;

class NewProjectDialog final : public QDialog {
    Q_OBJECT

public:
    explicit NewProjectDialog(IUserInteraction* interaction = nullptr,
                              QWidget* parent = nullptr);

    QString personsFile() const;
    QString aliasesFile() const;

private slots:
    void choosePersonsFile();
    void chooseAliasesFile();
    void clearPersonsFile();
    void clearAliasesFile();
    void updateAliasControls();

private:
    IUserInteraction* interaction_{};  // Non-owning.
    QLineEdit* personsEdit_{};
    QLineEdit* aliasesEdit_{};
    QPushButton* chooseAliasesButton_{};
    QPushButton* clearAliasesButton_{};
};

}  // namespace novel::presentation
