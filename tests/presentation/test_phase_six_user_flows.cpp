#include "application/ProjectApplicationService.h"
#include "presentation/MainWindow.h"
#include "presentation/dialogs/ChapterEditorDialog.h"
#include "presentation/graphics/PersonNodeItem.h"
#include "presentation/graphics/RelationEdgeItem.h"
#include "presentation/graphics/RelationGraphSceneController.h"
#include "presentation/graphics/RelationGraphView.h"
#include "presentation/graphics/TraversalTreeSceneController.h"
#include "presentation/graphics/TraversalTreeView.h"
#include "presentation/interaction/IUserInteraction.h"
#include "presentation/models/ChapterTableModel.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QGraphicsScene>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStatusBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTimer>
#include <QtTest>

#include <memory>
#include <utility>
#include <vector>

namespace novel::presentation {
namespace {

class FakeInteraction final : public IUserInteraction {
public:
    QString openProjectFile;
    QString saveProjectFile;
    QString chapterTextFile;
    QString personsDictionaryFile;
    QString aliasesDictionaryFile;
    UnsavedChangesChoice unsavedChoice{UnsavedChangesChoice::Cancel};
    bool destructiveConfirmation{};
    int destructiveConfirmationCount{};
    int errorCount{};
    int informationCount{};
    QString lastConfirmationTitle;
    QString lastConfirmationMessage;
    QString lastErrorTitle;
    QString lastError;

    QString chooseOpenProjectFile(QWidget*) override {
        return openProjectFile;
    }

    QString chooseSaveProjectFile(QWidget*, const QString&) override {
        return saveProjectFile;
    }

    QString chooseChapterTextFile(QWidget*) override {
        return chapterTextFile;
    }

    QString choosePersonsDictionaryFile(QWidget*) override {
        return personsDictionaryFile;
    }

    QString chooseAliasesDictionaryFile(QWidget*) override {
        return aliasesDictionaryFile;
    }

    UnsavedChangesChoice confirmUnsavedChanges(QWidget*) override {
        return unsavedChoice;
    }

    bool confirmDestructiveAction(QWidget*, const QString& title,
                                  const QString& message) override {
        ++destructiveConfirmationCount;
        lastConfirmationTitle = title;
        lastConfirmationMessage = message;
        return destructiveConfirmation;
    }

    void showError(QWidget*, const QString& title,
                   const QString& message) override {
        ++errorCount;
        lastErrorTitle = title;
        lastError = message;
    }

    void showInformation(QWidget*, const QString&, const QString&) override {
        ++informationCount;
    }
};

struct FixtureIds {
    PersonId a{};
    PersonId b{};
    PersonId c{};
    ChapterId abc{};
    ChapterId ab{};
    ChapterId ac{};
};

bool populateThreePersonFixture(
    application::ProjectApplicationService& service, FixtureIds& ids) {
    const auto a = service.addPerson("甲");
    const auto b = service.addPerson("乙");
    const auto c = service.addPerson("丙");
    if (!a || !b || !c) {
        return false;
    }
    ids.a = a.value();
    ids.b = b.value();
    ids.c = c.value();

    const auto import = [&service](const char* key, const char* title,
                                   const char* body,
                                   std::vector<PersonId> people,
                                   ChapterId& resultId) {
        application::ImportChapterCommand command;
        command.expectedRevision = service.status().revision;
        command.sourcePath =
            std::filesystem::u8path(std::string{"chapter-"} + key + ".txt");
        command.key = key;
        command.title = title;
        command.body = body;
        command.selectedPersonIds = std::move(people);
        const auto result = service.importChapter(command);
        if (!result) {
            return false;
        }
        resultId = result.value();
        return true;
    };

    // A appears in all three chapters. B and C each appear twice and share
    // only the first chapter, so AB/AC = 2/3 while BC = 1/3.
    return import("001", "三人初见", "甲乙丙初见。", {ids.a, ids.b, ids.c},
                  ids.abc) &&
           import("002", "甲乙同行", "甲与乙同行。", {ids.a, ids.b}, ids.ab) &&
           import("003", "甲丙同行", "甲与丙同行。", {ids.a, ids.c}, ids.ac);
}

EdgeId relationId(const application::ProjectApplicationService& service,
                  PersonId first, PersonId second) {
    for (const auto& relation : service.relations()) {
        if ((relation.personA == first && relation.personB == second) ||
            (relation.personA == second && relation.personB == first)) {
            return relation.id;
        }
    }
    return {};
}

void showForOffscreen(MainWindow& window) {
    window.resize(1180, 760);
    window.show();
    QCoreApplication::processEvents();
    QTest::qWait(20);
    QCoreApplication::processEvents();
}

bool statusBarContains(const MainWindow& window, const QString& expected) {
    const auto labels = window.statusBar()->findChildren<QLabel*>();
    for (const auto* label : labels) {
        if (label->text() == expected) {
            return true;
        }
    }
    return false;
}

}  // namespace

class PhaseSixUserFlowTests final : public QObject {
    Q_OBJECT

private slots:
    void duplicateImportFromButtonIsRejectedAtomically();
    void confirmedDeleteRefreshesTablesStatisticsAndGraph();
    void minimumJaccardSpinRefreshesRenderedEdges();
    void graphItemClicksSynchronizeSelectionAndDetails();
    void traversalTreeOpensAfterDepthFirstAndClosesFromTimer();
};

void PhaseSixUserFlowTests::duplicateImportFromButtonIsRejectedAtomically() {
    application::ProjectApplicationService service;
    FixtureIds fixture;
    QVERIFY(populateThreePersonFixture(service, fixture));

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto duplicatePath = temporary.filePath(QStringLiteral("duplicate.txt"));
    QFile duplicateFile(duplicatePath);
    QVERIFY(duplicateFile.open(QIODevice::WriteOnly));
    const auto duplicateText =
        QStringLiteral("@chapter=001\n@title=重复章节\n\n甲、乙、丙再次相遇。")
            .toUtf8();
    QCOMPARE(duplicateFile.write(duplicateText),
             static_cast<qint64>(duplicateText.size()));
    duplicateFile.close();

    auto interaction = std::make_unique<FakeInteraction>();
    auto* fake = interaction.get();
    fake->chapterTextFile = duplicatePath;
    MainWindow window(service, std::move(interaction));
    showForOffscreen(window);

    auto* importButton = window.findChild<QPushButton*>(
        QStringLiteral("importChapterButton"));
    auto* chapterTable =
        window.findChild<QTableView*>(QStringLiteral("chapterTable"));
    auto* relationTable =
        window.findChild<QTableView*>(QStringLiteral("relationTable"));
    auto* graph = window.findChild<RelationGraphView*>(
        QStringLiteral("relationGraphView"));
    QVERIFY(importButton && chapterTable && relationTable && graph);

    const auto before = service.status();
    const auto chapterCountBefore = service.chapters().size();
    const auto relationCountBefore = service.relations().size();
    const auto chapterRowsBefore = chapterTable->model()->rowCount();
    const auto relationRowsBefore = relationTable->model()->rowCount();
    const auto graphEdgesBefore = graph->sceneController()->edgeCount();

    bool previewDialogSeen = false;
    QString previewDialogTitle;
    QString previewKey;
    int previewSelectedPeople = -1;
    QTimer dialogTimer;
    connect(&dialogTimer, &QTimer::timeout, &window, [&] {
        auto* dialog = window.findChild<ChapterEditorDialog*>();
        if (dialog == nullptr) {
            return;
        }
        auto* keyEdit = dialog->findChild<QLineEdit*>(
            QStringLiteral("chapterKeyEdit"));
        auto* selectedPeople = dialog->findChild<QListWidget*>(
            QStringLiteral("selectedPeopleList"));
        auto* buttons = dialog->findChild<QDialogButtonBox*>(
            QStringLiteral("chapterDialogButtons"));
        if (keyEdit == nullptr || selectedPeople == nullptr ||
            buttons == nullptr) {
            return;
        }
        auto* confirmButton = buttons->button(QDialogButtonBox::Ok);
        if (confirmButton == nullptr) {
            return;
        }

        previewDialogSeen = true;
        previewDialogTitle = dialog->windowTitle();
        previewKey = keyEdit->text();
        previewSelectedPeople = selectedPeople->count();
        dialogTimer.stop();
        QTest::mouseClick(confirmButton, Qt::LeftButton);
    });
    dialogTimer.start(5);
    QTest::mouseClick(importButton, Qt::LeftButton);
    dialogTimer.stop();
    QCoreApplication::processEvents();

    QVERIFY(previewDialogSeen);
    QVERIFY(previewDialogTitle.contains(QStringLiteral("确认导入")));
    QCOMPARE(previewKey, QStringLiteral("001"));
    QCOMPARE(previewSelectedPeople, 3);
    QCOMPARE(fake->errorCount, 1);
    QCOMPARE(fake->lastErrorTitle, QStringLiteral("导入章节"));
    QVERIFY(fake->lastError.contains(QStringLiteral("章节键已存在")));

    const auto after = service.status();
    QCOMPARE(after.revision, before.revision);
    QCOMPARE(after.dirty, before.dirty);
    QCOMPARE(after.personCount, before.personCount);
    QCOMPARE(after.chapterCount, before.chapterCount);
    QCOMPARE(after.relationCount, before.relationCount);
    QVERIFY(after.filePath == before.filePath);
    QCOMPARE(service.chapters().size(), chapterCountBefore);
    QCOMPARE(service.relations().size(), relationCountBefore);
    QCOMPARE(chapterTable->model()->rowCount(), chapterRowsBefore);
    QCOMPARE(relationTable->model()->rowCount(), relationRowsBefore);
    QCOMPARE(graph->sceneController()->edgeCount(), graphEdgesBefore);
}

void PhaseSixUserFlowTests::
    confirmedDeleteRefreshesTablesStatisticsAndGraph() {
    application::ProjectApplicationService service;
    FixtureIds fixture;
    QVERIFY(populateThreePersonFixture(service, fixture));
    const auto bcRelation = relationId(service, fixture.b, fixture.c);
    QVERIFY(bcRelation != 0);

    auto interaction = std::make_unique<FakeInteraction>();
    auto* fake = interaction.get();
    fake->destructiveConfirmation = true;
    MainWindow window(service, std::move(interaction));
    showForOffscreen(window);

    auto* chapterTable =
        window.findChild<QTableView*>(QStringLiteral("chapterTable"));
    auto* relationTable =
        window.findChild<QTableView*>(QStringLiteral("relationTable"));
    auto* deleteButton = window.findChild<QPushButton*>(
        QStringLiteral("deleteChapterButton"));
    auto* graph = window.findChild<RelationGraphView*>(
        QStringLiteral("relationGraphView"));
    QVERIFY(chapterTable && relationTable && deleteButton && graph);
    QCOMPARE(chapterTable->model()
                 ->index(0, ChapterTableModel::KeyColumn)
                 .data()
                 .toString(),
             QStringLiteral("001"));
    QCOMPARE(graph->sceneController()->edgeCount(), qsizetype{3});

    const auto revisionBefore = service.status().revision;
    chapterTable->selectRow(0);
    QCoreApplication::processEvents();
    QVERIFY(deleteButton->isEnabled());
    QTest::mouseClick(deleteButton, Qt::LeftButton);
    QCoreApplication::processEvents();

    QCOMPARE(fake->destructiveConfirmationCount, 1);
    QCOMPARE(fake->lastConfirmationTitle, QStringLiteral("删除章节"));
    QVERIFY(fake->lastConfirmationMessage.contains(QStringLiteral("001")));
    QCOMPARE(fake->errorCount, 0);

    const auto status = service.status();
    QCOMPARE(status.revision, revisionBefore + 1U);
    QCOMPARE(status.personCount, std::size_t{3});
    QCOMPARE(status.chapterCount, std::size_t{2});
    QCOMPARE(status.relationCount, std::size_t{2});
    QCOMPARE(chapterTable->model()->rowCount(), 2);
    QCOMPARE(chapterTable->model()
                 ->index(0, ChapterTableModel::KeyColumn)
                 .data()
                 .toString(),
             QStringLiteral("002"));
    QCOMPARE(relationTable->model()->rowCount(), 2);
    QCOMPARE(graph->sceneController()->nodeCount(), qsizetype{3});
    QCOMPARE(graph->sceneController()->edgeCount(), qsizetype{2});
    QVERIFY(graph->sceneController()->edgeItem(bcRelation) == nullptr);
    QVERIFY(statusBarContains(window, QStringLiteral("人物：3")));
    QVERIFY(statusBarContains(window, QStringLiteral("边：2")));
    QVERIFY(statusBarContains(window, QStringLiteral("章节：2")));
}

void PhaseSixUserFlowTests::minimumJaccardSpinRefreshesRenderedEdges() {
    application::ProjectApplicationService service;
    FixtureIds fixture;
    QVERIFY(populateThreePersonFixture(service, fixture));

    MainWindow window(service, std::make_unique<FakeInteraction>());
    showForOffscreen(window);
    auto* threshold = window.findChild<QDoubleSpinBox*>(
        QStringLiteral("minimumJaccardSpin"));
    auto* graph = window.findChild<RelationGraphView*>(
        QStringLiteral("relationGraphView"));
    QVERIFY(threshold && graph);
    auto* controller = graph->sceneController();
    QCOMPARE(controller->edgeCount(), qsizetype{3});

    threshold->setValue(0.34);
    QCoreApplication::processEvents();
    QCOMPARE(controller->minimumJaccard(), threshold->value());
    QCOMPARE(controller->edgeCount(), qsizetype{2});

    threshold->setValue(0.67);
    QCoreApplication::processEvents();
    QCOMPARE(controller->minimumJaccard(), threshold->value());
    QCOMPARE(controller->edgeCount(), qsizetype{0});

    threshold->setValue(0.0);
    QCoreApplication::processEvents();
    QCOMPARE(controller->edgeCount(), qsizetype{3});
}

void PhaseSixUserFlowTests::
    graphItemClicksSynchronizeSelectionAndDetails() {
    application::ProjectApplicationService service;
    FixtureIds fixture;
    QVERIFY(populateThreePersonFixture(service, fixture));
    const auto bcRelation = relationId(service, fixture.b, fixture.c);
    QVERIFY(bcRelation != 0);

    MainWindow window(service, std::make_unique<FakeInteraction>());
    showForOffscreen(window);
    auto* graph = window.findChild<RelationGraphView*>(
        QStringLiteral("relationGraphView"));
    auto* details =
        window.findChild<QTextBrowser*>(QStringLiteral("detailBrowser"));
    QVERIFY(graph && details);
    auto* controller = graph->sceneController();

    auto* person = controller->nodeItem(fixture.a);
    QVERIFY(person);
    const auto personPoint = graph->mapFromScene(person->scenePos());
    QVERIFY(graph->viewport()->rect().contains(personPoint));
    QVERIFY(graph->itemAt(personPoint) == person);
    QTest::mouseClick(graph->viewport(), Qt::LeftButton, Qt::NoModifier,
                      personPoint);
    QTRY_VERIFY(person->isSelected());
    QTRY_VERIFY(details->toPlainText().contains(QStringLiteral("甲")));
    QCOMPARE(person->visualState(), PersonNodeItem::VisualState::Selected);
    QCOMPARE(controller->scene()->selectedItems().size(), 1);

    auto* edge = controller->edgeItem(bcRelation);
    auto* endpointB = controller->nodeItem(fixture.b);
    auto* endpointC = controller->nodeItem(fixture.c);
    QVERIFY(edge && endpointB && endpointC);
    const auto edgeScenePoint =
        (endpointB->scenePos() + endpointC->scenePos()) / 2.0;
    const auto edgePoint = graph->mapFromScene(edgeScenePoint);
    QVERIFY(graph->viewport()->rect().contains(edgePoint));
    QVERIFY(graph->itemAt(edgePoint) == edge);
    QTest::mouseClick(graph->viewport(), Qt::LeftButton, Qt::NoModifier,
                      edgePoint);
    QTRY_VERIFY(edge->isSelected());
    QTRY_VERIFY(details->toPlainText().contains(QStringLiteral("乙")) &&
                details->toPlainText().contains(QStringLiteral("丙")) &&
                details->toPlainText().contains(QStringLiteral("Jaccard")));
    QCOMPARE(edge->visualState(), RelationEdgeItem::VisualState::Selected);
    QCOMPARE(endpointB->visualState(), PersonNodeItem::VisualState::Selected);
    QCOMPARE(endpointC->visualState(), PersonNodeItem::VisualState::Selected);
    QVERIFY(!person->isSelected());
    QCOMPARE(controller->scene()->selectedItems().size(), 1);
}

void PhaseSixUserFlowTests::
    traversalTreeOpensAfterDepthFirstAndClosesFromTimer() {
    application::ProjectApplicationService service;
    FixtureIds fixture;
    QVERIFY(populateThreePersonFixture(service, fixture));

    MainWindow window(service, std::make_unique<FakeInteraction>());
    showForOffscreen(window);
    auto* start = window.findChild<QComboBox*>(
        QStringLiteral("traversalStartCombo"));
    auto* dfs =
        window.findChild<QPushButton*>(QStringLiteral("depthFirstButton"));
    auto* sequence =
        window.findChild<QLineEdit*>(QStringLiteral("traversalSequence"));
    auto* showTree = window.findChild<QPushButton*>(
        QStringLiteral("showTraversalTreeButton"));
    QVERIFY(start && dfs && sequence && showTree);
    QVERIFY(start->currentData().value<PersonId>() != 0);

    QTest::mouseClick(dfs, Qt::LeftButton);
    QCoreApplication::processEvents();
    QVERIFY(!sequence->text().isEmpty());
    QVERIFY(sequence->text().contains(QStringLiteral("甲")));
    QVERIFY(sequence->text().contains(QStringLiteral("乙")));
    QVERIFY(sequence->text().contains(QStringLiteral("丙")));
    QVERIFY(showTree->isEnabled());

    bool dialogSeen = false;
    bool viewSeen = false;
    bool dialogWasVisible = false;
    bool viewWasVisible = false;
    bool hasVirtualRoot = false;
    qsizetype treeNodeCount = -1;
    qsizetype treeEdgeCount = -1;
    QTimer dialogTimer;
    connect(&dialogTimer, &QTimer::timeout, &window, [&] {
        auto* dialog = window.findChild<QDialog*>(
            QStringLiteral("traversalTreeDialog"));
        if (dialog == nullptr) {
            return;
        }
        dialogSeen = true;
        dialogWasVisible = dialog->isVisible();
        auto* view = dialog->findChild<TraversalTreeView*>(
            QStringLiteral("traversalTreeView"));
        if (view == nullptr) {
            return;
        }
        viewSeen = true;
        viewWasVisible = view->isVisible();
        treeNodeCount = view->sceneController()->nodeCount();
        treeEdgeCount = view->sceneController()->edgeCount();
        hasVirtualRoot = view->sceneController()->hasVirtualRoot();
        dialogTimer.stop();
        dialog->accept();
    });
    dialogTimer.start(5);
    QTest::mouseClick(showTree, Qt::LeftButton);
    dialogTimer.stop();
    QCoreApplication::processEvents();

    QVERIFY(dialogSeen);
    QVERIFY(viewSeen);
    QVERIFY(dialogWasVisible);
    QVERIFY(viewWasVisible);
    QVERIFY(hasVirtualRoot);
    QCOMPARE(treeNodeCount, qsizetype{4});
    QCOMPARE(treeEdgeCount, qsizetype{3});
    QVERIFY(window.findChild<QDialog*>(
                QStringLiteral("traversalTreeDialog")) == nullptr);
}

}  // namespace novel::presentation

QTEST_MAIN(novel::presentation::PhaseSixUserFlowTests)
#include "test_phase_six_user_flows.moc"
