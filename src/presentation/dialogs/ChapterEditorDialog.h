#pragma once

#include "presentation/dialogs/DialogData.h"

#include <QDialog>

class QDialogButtonBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace novel::presentation {

class IUserInteraction;

class ChapterEditorDialog final : public QDialog {
    Q_OBJECT

public:
    enum class Mode { Import, Edit };

    explicit ChapterEditorDialog(Mode mode, QWidget* parent = nullptr,
                                 IUserInteraction* interaction = nullptr);

    void setData(const ChapterEditorData& data);
    ChapterEditorResult result() const;

private slots:
    void updateAvailablePeople();
    void addSelectedPerson();
    void removeSelectedPerson();
    void createPerson();
    void createAlias();
    void validateAndAccept();

private:
    static constexpr int kPersonIdRole = Qt::UserRole + 1;
    static constexpr int kIsNewPersonRole = Qt::UserRole + 2;

    void buildUi();
    void rebuildRecognizedMatches();
    void rebuildSelectedPeople();
    bool containsSelectedPerson(PersonId id) const;
    bool containsSelectedName(const QString& name) const;
    void appendSelectedPerson(PersonId id, const QString& name,
                              bool isNew);
    void showError(const QString& title, const QString& message);
    void showInformation(const QString& title, const QString& message);

    Mode mode_;
    IUserInteraction* interaction_{};  // Non-owning.
    ChapterEditorData data_;
    std::vector<PendingAlias> pendingAliases_;

    QLineEdit* pathEdit_{};
    QLineEdit* keyEdit_{};
    QLineEdit* titleEdit_{};
    QPlainTextEdit* contentEdit_{};
    QTableWidget* recognizedTable_{};
    QLineEdit* personSearchEdit_{};
    QListWidget* availablePeopleList_{};
    QListWidget* selectedPeopleList_{};
    QPushButton* addPersonButton_{};
    QPushButton* removePersonButton_{};
    QPushButton* newPersonButton_{};
    QPushButton* newAliasButton_{};
    QDialogButtonBox* buttonBox_{};
};

}  // namespace novel::presentation
