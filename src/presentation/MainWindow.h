#pragma once

#include "application/ApplicationDtos.h"
#include "domain/model/ChapterTypes.h"
#include "domain/model/GraphTypes.h"
#include "presentation/interaction/IUserInteraction.h"

#include <QMainWindow>

#include <memory>
#include <optional>
#include <vector>

class QAction;
class QCloseEvent;
class QComboBox;
class QDockWidget;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTableView;
class QTextBrowser;

namespace novel::application {
class ProjectApplicationService;
struct ApplicationError;
}

namespace novel::presentation {

class ChapterTableModel;
class PersonTableModel;
struct PersonChoice;
class RelationTableModel;
class RelationGraphView;
class SearchSortProxyModel;
class TraversalTreeView;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(
        application::ProjectApplicationService& service,
        std::unique_ptr<IUserInteraction> interaction = {},
        QWidget* parent = nullptr);
    ~MainWindow() override;

    void refreshAll();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void importChapter();
    void editSelectedChapter();
    void reextractSelectedChapter();
    void deleteSelectedChapter();
    void showPersonManagement();
    void showAliasManagement();
    void showSelectedChapterDetail();
    void showSelectedPersonDetail();
    void showSelectedRelationDetail();
    void showPersonDetail(PersonId id);
    void showRelationDetail(EdgeId id, PersonId first, PersonId second);
    void updateGraphSnapshot();
    void focusGraphPerson();
    void runDepthFirst();
    void runBreadthFirst();
    void playTraversal();
    void stopTraversal();
    void showTraversalTree();
    void updateSelectionActions();

private:
    void buildMenus();
    void buildCentralUi();
    QWidget* buildChapterPanel();
    QWidget* buildCenterPanel();
    QWidget* buildDetailPanel();
    QWidget* buildTraversalBar();
    void buildLogDock();
    void buildStatusBar();
    void connectViewSignals();

    bool confirmDiscardOrSave();
    bool saveToCurrentPath();
    bool saveToChosenPath();
    ChapterId selectedChapterId() const noexcept;
    PersonId selectedPersonId() const noexcept;
    EdgeId selectedRelationId() const noexcept;
    PersonId traversalStartPerson() const noexcept;
    std::vector<PersonChoice> personChoices() const;
    void setTraversalResult(application::TraversalResultDto result);
    void resetDetailPanel();
    void updateStatusBar();
    void updatePersonSelectors();
    void appendLog(const QString& level, const QString& operation,
                   const QString& message);
    void reportError(const QString& operation,
                     const application::ApplicationError& error);
    void reportQueryError(const QString& operation,
                          const application::ApplicationError& error);

    application::ProjectApplicationService& service_;
    std::unique_ptr<IUserInteraction> interaction_;

    ChapterTableModel* chapterModel_{};
    PersonTableModel* personModel_{};
    RelationTableModel* relationModel_{};
    SearchSortProxyModel* chapterProxy_{};
    SearchSortProxyModel* personProxy_{};
    SearchSortProxyModel* relationProxy_{};

    QLineEdit* chapterSearchEdit_{};
    QTableView* chapterTable_{};
    QTableView* personTable_{};
    QTableView* relationTable_{};
    QTabWidget* centerTabs_{};
    RelationGraphView* graphView_{};
    QComboBox* graphPersonSearch_{};
    QDoubleSpinBox* thresholdSpin_{};
    QTextBrowser* detailBrowser_{};

    QComboBox* traversalStartCombo_{};
    QLineEdit* traversalSequenceEdit_{};
    QPushButton* playTraversalButton_{};
    QPushButton* stopTraversalButton_{};
    QPushButton* showTraversalTreeButton_{};
    std::optional<application::TraversalResultDto> traversalResult_;

    QDockWidget* logDock_{};
    QPlainTextEdit* logEdit_{};
    QLabel* pathStatusLabel_{};
    QLabel* personStatusLabel_{};
    QLabel* relationStatusLabel_{};
    QLabel* chapterStatusLabel_{};
    QLabel* dirtyStatusLabel_{};

    QAction* saveAction_{};
    QAction* editChapterAction_{};
    QAction* reextractChapterAction_{};
    QAction* deleteChapterAction_{};
};

}  // namespace novel::presentation
