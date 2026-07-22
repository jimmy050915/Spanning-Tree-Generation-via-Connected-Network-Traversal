#include "application/ProjectApplicationService.h"
#include "presentation/MainWindow.h"
#include "presentation/dialogs/ChapterEditorDialog.h"
#include "presentation/dialogs/NewProjectDialog.h"
#include "presentation/graphics/RelationGraphView.h"
#include "presentation/graphics/RelationGraphSceneController.h"
#include "presentation/graphics/RelationEdgeItem.h"
#include "presentation/graphics/TraversalTreeView.h"
#include "presentation/interaction/IUserInteraction.h"
#include "presentation/models/ChapterTableModel.h"
#include "presentation/models/PersonTableModel.h"
#include "presentation/models/RelationTableModel.h"
#include "presentation/models/SearchSortProxyModel.h"

#include <QComboBox>
#include <QApplication>
#include <QCursor>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <algorithm>
#include <memory>

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
    int errorCount{};
    int informationCount{};
    QString lastError;

    QString chooseOpenProjectFile(QWidget*) override {
        return openProjectFile;
    }
    QString chooseSaveProjectFile(QWidget*, const QString&) override {
        return saveProjectFile;
    }
    QString chooseChapterTextFile(QWidget*) override { return chapterTextFile; }
    QString choosePersonsDictionaryFile(QWidget*) override {
        return personsDictionaryFile;
    }
    QString chooseAliasesDictionaryFile(QWidget*) override {
        return aliasesDictionaryFile;
    }
    UnsavedChangesChoice confirmUnsavedChanges(QWidget*) override {
        return unsavedChoice;
    }
    bool confirmDestructiveAction(QWidget*, const QString&,
                                  const QString&) override {
        return false;
    }
    void showError(QWidget*, const QString&, const QString& message) override {
        ++errorCount;
        lastError = message;
    }
    void showInformation(QWidget*, const QString&, const QString&) override {
        ++informationCount;
    }
};

template <typename T, typename Comparator>
void compareDtoVectors(const std::vector<T>& actual,
                       const std::vector<T>& expected,
                       Comparator compare) {
    QCOMPARE(actual.size(), expected.size());
    const auto count = std::min(actual.size(), expected.size());
    for (std::size_t index = 0; index < count; ++index) {
        compare(actual[index], expected[index]);
    }
}

void compareChapterRow(const application::ChapterRowDto& actual,
                       const application::ChapterRowDto& expected) {
    QCOMPARE(actual.id, expected.id);
    QCOMPARE(actual.key, expected.key);
    QCOMPARE(actual.title, expected.title);
    QCOMPARE(actual.sourceFile, expected.sourceFile);
    QCOMPARE(actual.personCount, expected.personCount);
    QCOMPARE(static_cast<int>(actual.status), static_cast<int>(expected.status));
}

void comparePersonRow(const application::PersonRowDto& actual,
                      const application::PersonRowDto& expected) {
    QCOMPARE(actual.id, expected.id);
    QCOMPARE(actual.name, expected.name);
    QCOMPARE(actual.chapterCount, expected.chapterCount);
    QCOMPARE(actual.degree, expected.degree);
    QCOMPARE(actual.strongestPerson.has_value(),
             expected.strongestPerson.has_value());
    if (actual.strongestPerson && expected.strongestPerson) {
        QCOMPARE(*actual.strongestPerson, *expected.strongestPerson);
    }
    QCOMPARE(actual.strongestPersonName, expected.strongestPersonName);
    QCOMPARE(actual.strongestJaccard, expected.strongestJaccard);
}

void compareRelationRow(const application::RelationRowDto& actual,
                        const application::RelationRowDto& expected) {
    QCOMPARE(actual.id, expected.id);
    QCOMPARE(actual.personA, expected.personA);
    QCOMPARE(actual.personB, expected.personB);
    QCOMPARE(actual.personAName, expected.personAName);
    QCOMPARE(actual.personBName, expected.personBName);
    QCOMPARE(actual.coChapterCount, expected.coChapterCount);
    QCOMPARE(actual.jaccard, expected.jaccard);
}

void compareAliasRow(const application::AliasRowDto& actual,
                     const application::AliasRowDto& expected) {
    QCOMPARE(actual.alias, expected.alias);
    QCOMPARE(actual.targetPerson, expected.targetPerson);
    QCOMPARE(actual.targetName, expected.targetName);
}

void compareGraphSnapshot(const application::GraphSnapshotDto& actual,
                          const application::GraphSnapshotDto& expected) {
    QCOMPARE(actual.minimumJaccard, expected.minimumJaccard);
    compareDtoVectors(actual.nodes, expected.nodes,
                      [](const auto& left, const auto& right) {
                          QCOMPARE(left.id, right.id);
                          QCOMPARE(left.canonicalName, right.canonicalName);
                          QCOMPARE(left.chapterCount, right.chapterCount);
                      });
    compareDtoVectors(actual.edges, expected.edges,
                      [](const auto& left, const auto& right) {
                          QCOMPARE(left.id, right.id);
                          QCOMPARE(left.endpointA, right.endpointA);
                          QCOMPARE(left.endpointB, right.endpointB);
                          QCOMPARE(left.endpointAName, right.endpointAName);
                          QCOMPARE(left.endpointBName, right.endpointBName);
                          QCOMPARE(left.coChapterCount, right.coChapterCount);
                          QCOMPARE(left.jaccard, right.jaccard);
                      });
}

void compareChapterDetail(const application::ChapterDetailDto& actual,
                          const application::ChapterDetailDto& expected) {
    compareChapterRow(actual.chapter, expected.chapter);
    QCOMPARE(actual.body, expected.body);
    compareDtoVectors(actual.persons, expected.persons, comparePersonRow);
}

void comparePersonDetail(const application::PersonDetailDto& actual,
                         const application::PersonDetailDto& expected) {
    comparePersonRow(actual.person, expected.person);
    QCOMPARE(actual.aliases, expected.aliases);
    compareDtoVectors(actual.chapters, expected.chapters, compareChapterRow);
    compareDtoVectors(actual.relations, expected.relations, compareRelationRow);
    QCOMPARE(actual.strongestRelation.has_value(),
             expected.strongestRelation.has_value());
    if (actual.strongestRelation && expected.strongestRelation) {
        compareRelationRow(*actual.strongestRelation,
                           *expected.strongestRelation);
    }
}

void compareRelationDetail(const application::RelationDetailDto& actual,
                           const application::RelationDetailDto& expected) {
    compareRelationRow(actual.relation, expected.relation);
    compareDtoVectors(actual.commonChapters, expected.commonChapters,
                      compareChapterRow);
}

void compareTraversal(const application::TraversalResultDto& actual,
                      const application::TraversalResultDto& expected) {
    QCOMPARE(static_cast<int>(actual.kind), static_cast<int>(expected.kind));
    QCOMPARE(actual.start, expected.start);
    QCOMPARE(actual.order, expected.order);
    QCOMPARE(actual.orderNames, expected.orderNames);
    compareDtoVectors(actual.nodes, expected.nodes,
                      [](const auto& left, const auto& right) {
                          QCOMPARE(left.id, right.id);
                          QCOMPARE(left.canonicalName, right.canonicalName);
                          QCOMPARE(left.virtualRoot, right.virtualRoot);
                      });
    compareDtoVectors(actual.treeEdges, expected.treeEdges,
                      [](const auto& left, const auto& right) {
                          QCOMPARE(left.parent, right.parent);
                          QCOMPARE(left.child, right.child);
                      });
}

}  // namespace

class UiPhaseFiveTests final : public QObject {
    Q_OBJECT

private slots:
    void tableModelsExposeStableRowsAndIds();
    void searchProxyFiltersAcrossEveryColumn();
    void chapterEditorPreservesManualSelection();
    void ordinaryChapterEditShowsMatchesWithoutReplacingManualSelection();
    void newProjectDialogStartsWithOptionalFilesEmpty();
    void mainWindowContainsDocumentedRegions();
    void editableGraphSearchHonorsTypedTextAndDropdownSelection();
    void endToEndWorkflowRestoresEquivalentDtos();
    void populatedWindowRendersForVisualQa();
};

void UiPhaseFiveTests::tableModelsExposeStableRowsAndIds() {
    PersonTableModel people;
    people.setRows({{7, QStringLiteral("林黛玉"), 5, 3, 4,
                     QStringLiteral("贾宝玉"), 0.6},
                    {4, QStringLiteral("贾宝玉"), 8, 6, 7,
                     QStringLiteral("林黛玉"), 0.6}});
    QCOMPARE(people.rowCount(), 2);
    QCOMPARE(people.index(0, PersonTableModel::NameColumn).data().toString(),
             QStringLiteral("林黛玉"));
    QCOMPARE(people.index(0, 0).data(PersonTableModel::PersonIdRole)
                 .value<PersonId>(),
             PersonId{7});
    QCOMPARE(people.rowForPerson(4), 1);
    QCOMPARE(people.index(0, PersonTableModel::StrongestPersonColumn)
                 .data().toString(),
             QStringLiteral("贾宝玉"));
    QCOMPARE(people.index(0, PersonTableModel::StrongestJaccardColumn)
                 .data().toString(),
             QStringLiteral("0.6000"));
    QCOMPARE(people.headerData(PersonTableModel::StrongestPersonColumn,
                               Qt::Horizontal).toString(),
             QStringLiteral("最高关联人物"));

    RelationTableModel relations;
    relations.setRows({{9, 7, 4, QStringLiteral("林黛玉"),
                        QStringLiteral("贾宝玉"), 3, 0.6}});
    QCOMPARE(relations.rowCount(), 1);
    QCOMPARE(relations.index(0, RelationTableModel::JaccardColumn)
                 .data()
                 .toString(),
             QStringLiteral("0.6000"));
    QCOMPARE(relations.rowForRelation(4, 7), 0);

    ChapterTableModel chapters;
    chapters.setRows({{12, QStringLiteral("第一回"),
                       QStringLiteral("用假语村言"),
                       QStringLiteral("chapter-01.txt"), 2}});
    QCOMPARE(chapters.index(0, 0).data(ChapterTableModel::ChapterIdRole)
                 .value<ChapterId>(),
             ChapterId{12});
    QCOMPARE(chapters.index(0, ChapterTableModel::SourceFileColumn)
                 .data().toString(),
             QStringLiteral("chapter-01.txt"));
    QCOMPARE(chapters.index(0, ChapterTableModel::StatusColumn)
                 .data().toString(),
             QStringLiteral("正常"));
}

void UiPhaseFiveTests::searchProxyFiltersAcrossEveryColumn() {
    ChapterTableModel chapters;
    chapters.setRows({{1, QStringLiteral("第一回"), QStringLiteral("梦幻"),
                       QStringLiteral("one.txt"), 2},
                      {2, QStringLiteral("第二回"), QStringLiteral("金陵"),
                       QStringLiteral("two.txt"), 4}});
    SearchSortProxyModel proxy;
    proxy.setSourceModel(&chapters);
    proxy.setFilterFixedString(QStringLiteral("金陵"));
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, ChapterTableModel::KeyColumn).data().toString(),
             QStringLiteral("第二回"));
}

void UiPhaseFiveTests::chapterEditorPreservesManualSelection() {
    FakeInteraction interaction;
    ChapterEditorDialog dialog(ChapterEditorDialog::Mode::Import, nullptr,
                               &interaction);
    ChapterEditorData data;
    data.filePath = QStringLiteral("chapter.txt");
    data.chapterKey = QStringLiteral("第一章");
    data.title = QStringLiteral("开篇");
    data.content = QStringLiteral("甲与乙相遇。");
    data.availablePeople = {{1, QStringLiteral("甲"), 0},
                            {2, QStringLiteral("乙"), 0}};
    data.selectedPeople = {2};
    data.matches = {{QStringLiteral("乙"), false, 2,
                     QStringLiteral("乙")}};
    dialog.setData(data);

    auto* selected =
        dialog.findChild<QListWidget*>(QStringLiteral("selectedPeopleList"));
    QVERIFY(selected);
    QCOMPARE(selected->count(), 1);
    QCOMPARE(selected->item(0)->text(), QStringLiteral("乙"));
    QCOMPARE(dialog.result().selectedPeople, std::vector<PersonId>{2});

    dialog.findChild<QLineEdit*>(QStringLiteral("chapterKeyEdit"))->clear();
    dialog.findChild<QDialogButtonBox*>(
              QStringLiteral("chapterDialogButtons"))
        ->button(QDialogButtonBox::Ok)
        ->click();
    QCOMPARE(interaction.errorCount, 1);
    QVERIFY(interaction.lastError.contains(QStringLiteral("不能为空")));
}

void UiPhaseFiveTests::
    ordinaryChapterEditShowsMatchesWithoutReplacingManualSelection() {
    application::ProjectApplicationService service;
    const auto recognizedPerson = service.addPerson("甲");
    const auto manuallySelectedPerson = service.addPerson("乙");
    QVERIFY(recognizedPerson && manuallySelectedPerson);
    QVERIFY(service.addAlias("阿甲", recognizedPerson.value()));

    application::ImportChapterCommand command;
    command.expectedRevision = service.status().revision;
    command.sourcePath = std::filesystem::u8path("第一章.txt");
    command.key = "001";
    command.title = "人工修正";
    command.body = "阿甲独行。";
    command.selectedPersonIds = {manuallySelectedPerson.value()};
    QVERIFY(service.importChapter(command));

    MainWindow window(service, std::make_unique<FakeInteraction>());
    auto* chapterTable =
        window.findChild<QTableView*>(QStringLiteral("chapterTable"));
    QVERIFY(chapterTable);
    chapterTable->selectRow(0);

    bool dialogInspected = false;
    int matchCount = -1;
    QStringList matchCells;
    QStringList selectedNames;
    std::vector<PersonId> dialogSelection;
    QTimer dialogTimer;
    connect(&dialogTimer, &QTimer::timeout, &window, [&] {
        auto* dialog = qobject_cast<ChapterEditorDialog*>(
            QApplication::activeModalWidget());
        if (dialog == nullptr) {
            return;
        }

        auto* matches = dialog->findChild<QTableWidget*>(
            QStringLiteral("recognizedPeopleTable"));
        auto* selected = dialog->findChild<QListWidget*>(
            QStringLiteral("selectedPeopleList"));
        if (matches != nullptr && selected != nullptr) {
            matchCount = matches->rowCount();
            if (matchCount > 0) {
                for (int column = 0; column < matches->columnCount();
                     ++column) {
                    const auto* cell = matches->item(0, column);
                    matchCells.push_back(cell == nullptr ? QString{}
                                                         : cell->text());
                }
            }
            for (int row = 0; row < selected->count(); ++row) {
                selectedNames.push_back(selected->item(row)->text());
            }
            dialogSelection = dialog->result().selectedPeople;
            dialogInspected = true;
        }
        dialogTimer.stop();
        dialog->reject();
    });
    dialogTimer.start(5);
    QVERIFY(QMetaObject::invokeMethod(&window, "editSelectedChapter",
                                      Qt::DirectConnection));
    dialogTimer.stop();
    QVERIFY(dialogInspected);
    QCOMPARE(matchCount, 1);
    QCOMPARE(matchCells,
             QStringList({QStringLiteral("阿甲"), QStringLiteral("别名"),
                          QStringLiteral("甲")}));
    QCOMPARE(selectedNames, QStringList({QStringLiteral("乙")}));
    QCOMPARE(dialogSelection,
             std::vector<PersonId>{manuallySelectedPerson.value()});

    const auto detail = service.chapterDetail(service.chapters().front().id);
    QVERIFY(detail);
    QCOMPARE(detail.value().persons.size(), std::size_t{1});
    QCOMPARE(detail.value().persons.front().id,
             manuallySelectedPerson.value());
}

void UiPhaseFiveTests::newProjectDialogStartsWithOptionalFilesEmpty() {
    NewProjectDialog dialog;
    QVERIFY(dialog.personsFile().isEmpty());
    QVERIFY(dialog.aliasesFile().isEmpty());
    auto* aliasButton = dialog.findChild<QPushButton*>(
        QStringLiteral("chooseAliasesDictionary"));
    QVERIFY(aliasButton);
    QVERIFY(!aliasButton->isEnabled());
}

void UiPhaseFiveTests::mainWindowContainsDocumentedRegions() {
    application::ProjectApplicationService service;
    QVERIFY(service.addPerson("甲"));
    MainWindow window(service, std::make_unique<FakeInteraction>());

    auto* splitter = window.findChild<QSplitter*>(
        QStringLiteral("mainThreeColumnSplitter"));
    QVERIFY(splitter);
    QCOMPARE(splitter->count(), 3);
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("analysisTabs"));
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 3);
    QVERIFY(window.findChild<QTableView*>(QStringLiteral("chapterTable")));
    auto* personTable =
        window.findChild<QTableView*>(QStringLiteral("personTable"));
    QVERIFY(personTable);
    QVERIFY(window.findChild<QTableView*>(QStringLiteral("relationTable")));
    QVERIFY(window.findChild<QTextBrowser*>(QStringLiteral("detailBrowser")));
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("traversalBar")));
    QVERIFY(window.findChild<QDockWidget*>(QStringLiteral("operationLogDock")));

    auto* locate = window.findChild<QPushButton*>(
        QStringLiteral("locateGraphPersonButton"));
    QVERIFY(locate);
    locate->click();
    QCoreApplication::processEvents();
    QVERIFY(window.findChild<QTextBrowser*>(QStringLiteral("detailBrowser"))
                ->toPlainText()
                .contains(QStringLiteral("甲")));

    personTable->selectRow(0);
    QCoreApplication::processEvents();
    QVERIFY(window.findChild<QTextBrowser*>(QStringLiteral("detailBrowser"))
                ->toPlainText()
                .contains(QStringLiteral("甲")));

    auto* dfs =
        window.findChild<QPushButton*>(QStringLiteral("depthFirstButton"));
    auto* play =
        window.findChild<QPushButton*>(QStringLiteral("playTraversalButton"));
    auto* stop =
        window.findChild<QPushButton*>(QStringLiteral("stopTraversalButton"));
    auto* sequence =
        window.findChild<QLineEdit*>(QStringLiteral("traversalSequence"));
    QVERIFY(dfs && play && stop && sequence);
    dfs->click();
    QCOMPARE(sequence->text(), QStringLiteral("甲"));
    QVERIFY(play->isEnabled());
    play->click();
    QVERIFY(!play->isEnabled());
    QVERIFY(stop->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(play->isEnabled() && !stop->isEnabled(), 1000);

    QVERIFY(service.addPerson("乙"));
    window.refreshAll();
    QVERIFY(sequence->text().isEmpty());
    QVERIFY(!play->isEnabled());
}

void UiPhaseFiveTests::editableGraphSearchHonorsTypedTextAndDropdownSelection() {
    application::ProjectApplicationService service;
    const auto first = service.addPerson("甲角色");
    const auto second = service.addPerson("乙角色");
    QVERIFY(first && second);

    MainWindow window(service, std::make_unique<FakeInteraction>());
    auto* search =
        window.findChild<QComboBox*>(QStringLiteral("graphPersonSearch"));
    auto* locate = window.findChild<QPushButton*>(
        QStringLiteral("locateGraphPersonButton"));
    auto* detail =
        window.findChild<QTextBrowser*>(QStringLiteral("detailBrowser"));
    QVERIFY(search && search->lineEdit() && locate && detail);

    QCOMPARE(search->currentData().value<PersonId>(), first.value());
    search->lineEdit()->setText(QStringLiteral("乙"));
    QCOMPARE(search->currentData().value<PersonId>(), first.value());
    locate->click();
    QCoreApplication::processEvents();
    QVERIFY(detail->toPlainText().contains(QStringLiteral("乙角色")));

    const int secondIndex =
        search->findData(QVariant::fromValue(second.value()));
    const int firstIndex = search->findData(QVariant::fromValue(first.value()));
    QVERIFY(secondIndex >= 0 && firstIndex >= 0);
    search->setCurrentIndex(secondIndex);
    search->setCurrentIndex(firstIndex);
    QCOMPARE(search->currentText(), search->itemText(firstIndex));
    locate->click();
    QCoreApplication::processEvents();
    QVERIFY(detail->toPlainText().contains(QStringLiteral("甲角色")));
}

void UiPhaseFiveTests::endToEndWorkflowRestoresEquivalentDtos() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString personsPath = temporary.filePath(QStringLiteral("人物.txt"));
    const QString aliasesPath = temporary.filePath(QStringLiteral("别名.txt"));
    const QString chapterPath = temporary.filePath(QStringLiteral("第一章.txt"));
    const QString projectPath = temporary.filePath(QStringLiteral("端到端.nprg"));
    {
        QFile persons(personsPath);
        QVERIFY(persons.open(QIODevice::WriteOnly));
        QCOMPARE(persons.write(QStringLiteral("甲\n乙\n丙\n").toUtf8()),
                 qint64{12});
    }
    {
        QFile aliases(aliasesPath);
        QVERIFY(aliases.open(QIODevice::WriteOnly));
        const auto content = QStringLiteral("小甲\t甲\n").toUtf8();
        QCOMPARE(aliases.write(content), static_cast<qint64>(content.size()));
    }
    {
        QFile chapter(chapterPath);
        QVERIFY(chapter.open(QIODevice::WriteOnly));
        const auto content =
            QStringLiteral("@chapter=001\n@title=相遇\n\n甲与乙同行。")
                .toUtf8();
        QCOMPARE(chapter.write(content), static_cast<qint64>(content.size()));
    }

    application::ProjectApplicationService service;
    auto fake = std::make_unique<FakeInteraction>();
    auto* interaction = fake.get();
    interaction->personsDictionaryFile = personsPath;
    interaction->aliasesDictionaryFile = aliasesPath;
    interaction->chapterTextFile = chapterPath;
    interaction->saveProjectFile = projectPath;
    interaction->openProjectFile = projectPath;
    interaction->unsavedChoice = UnsavedChangesChoice::Discard;
    MainWindow window(service, std::move(fake));

    bool newDialogHandled = false;
    QTimer newDialogTimer;
    connect(&newDialogTimer, &QTimer::timeout, &window, [&] {
        auto* dialog = qobject_cast<NewProjectDialog*>(
            QApplication::activeModalWidget());
        if (dialog == nullptr) {
            return;
        }
        dialog->findChild<QPushButton*>(
                  QStringLiteral("choosePersonsDictionary"))
            ->click();
        dialog->findChild<QPushButton*>(
                  QStringLiteral("chooseAliasesDictionary"))
            ->click();
        newDialogHandled = true;
        newDialogTimer.stop();
        dialog->accept();
    });
    newDialogTimer.start(5);
    QVERIFY(QMetaObject::invokeMethod(&window, "newProject",
                                      Qt::DirectConnection));
    newDialogTimer.stop();
    QVERIFY(newDialogHandled);
    QCOMPARE(service.status().personCount, std::size_t{3});

    bool chapterDialogHandled = false;
    QTimer chapterDialogTimer;
    connect(&chapterDialogTimer, &QTimer::timeout, &window, [&] {
        auto* dialog = qobject_cast<ChapterEditorDialog*>(
            QApplication::activeModalWidget());
        if (dialog == nullptr) {
            return;
        }
        auto* selected = dialog->findChild<QListWidget*>(
            QStringLiteral("selectedPeopleList"));
        auto* available = dialog->findChild<QListWidget*>(
            QStringLiteral("availablePeopleList"));
        for (int row = 0; row < selected->count(); ++row) {
            if (selected->item(row)->text() == QStringLiteral("乙")) {
                selected->setCurrentRow(row);
                break;
            }
        }
        dialog->findChild<QPushButton*>(
                  QStringLiteral("removeSelectedPersonButton"))
            ->click();
        for (int row = 0; row < available->count(); ++row) {
            if (available->item(row)->data(Qt::UserRole).toString() ==
                QStringLiteral("丙")) {
                available->setCurrentRow(row);
                break;
            }
        }
        dialog->findChild<QPushButton*>(
                  QStringLiteral("addSelectedPersonButton"))
            ->click();
        chapterDialogHandled = true;
        chapterDialogTimer.stop();
        dialog->findChild<QDialogButtonBox*>(
                  QStringLiteral("chapterDialogButtons"))
            ->button(QDialogButtonBox::Ok)
            ->click();
    });
    chapterDialogTimer.start(5);
    QVERIFY(QMetaObject::invokeMethod(&window, "importChapter",
                                      Qt::DirectConnection));
    chapterDialogTimer.stop();
    QVERIFY(chapterDialogHandled);
    QCOMPARE(service.status().chapterCount, std::size_t{1});
    QCOMPARE(service.status().relationCount, std::size_t{1});
    const auto importedDetail =
        service.chapterDetail(service.chapters().front().id);
    QVERIFY(importedDetail);
    QCOMPARE(importedDetail.value().persons.size(), std::size_t{2});
    QCOMPARE(QString::fromUtf8(
                 importedDetail.value().persons[0].name.c_str()),
             QStringLiteral("甲"));
    QCOMPARE(QString::fromUtf8(
                 importedDetail.value().persons[1].name.c_str()),
             QStringLiteral("丙"));

    auto* relationTable =
        window.findChild<QTableView*>(QStringLiteral("relationTable"));
    QCOMPARE(relationTable->model()->rowCount(), 1);
    relationTable->selectRow(0);
    QCoreApplication::processEvents();
    const auto detailText =
        window.findChild<QTextBrowser*>(QStringLiteral("detailBrowser"))
            ->toPlainText();
    QVERIFY(detailText.contains(QStringLiteral("甲")) &&
            detailText.contains(QStringLiteral("丙")));

    auto* dfs =
        window.findChild<QPushButton*>(QStringLiteral("depthFirstButton"));
    auto* bfs =
        window.findChild<QPushButton*>(QStringLiteral("breadthFirstButton"));
    auto* sequence =
        window.findChild<QLineEdit*>(QStringLiteral("traversalSequence"));
    dfs->click();
    QVERIFY(sequence->text().contains(QStringLiteral("甲")));
    QVERIFY(sequence->text().contains(QStringLiteral("乙")));
    QVERIFY(sequence->text().contains(QStringLiteral("丙")));
    bfs->click();
    QVERIFY(sequence->text().contains(QStringLiteral("甲")));

    const auto statusBeforeSave = service.status();
    const auto peopleBeforeSave = service.people();
    const auto aliasesBeforeSave = service.aliases();
    const auto chaptersBeforeSave = service.chapters();
    const auto relationsBeforeSave = service.relations();
    const auto graphBeforeSaveResult = service.graphSnapshot(0.0);
    QVERIFY(graphBeforeSaveResult);
    const auto graphBeforeSave = graphBeforeSaveResult.value();
    QCOMPARE(aliasesBeforeSave.size(), std::size_t{1});
    QVERIFY(!peopleBeforeSave.empty() && !chaptersBeforeSave.empty() &&
            !relationsBeforeSave.empty());

    const auto chapterDetailBeforeSaveResult =
        service.chapterDetail(chaptersBeforeSave.front().id);
    QVERIFY(chapterDetailBeforeSaveResult);
    const auto chapterDetailBeforeSave =
        chapterDetailBeforeSaveResult.value();

    std::vector<application::PersonDetailDto> personDetailsBeforeSave;
    personDetailsBeforeSave.reserve(peopleBeforeSave.size());
    for (const auto& person : peopleBeforeSave) {
        const auto detail = service.personDetail(person.id);
        QVERIFY(detail);
        personDetailsBeforeSave.push_back(detail.value());
    }

    std::vector<application::RelationDetailDto> relationDetailsBeforeSave;
    relationDetailsBeforeSave.reserve(relationsBeforeSave.size());
    for (const auto& relation : relationsBeforeSave) {
        const auto detail = service.relationDetail(relation.id);
        QVERIFY(detail);
        relationDetailsBeforeSave.push_back(detail.value());
    }

    const PersonId traversalStart = peopleBeforeSave.front().id;
    const auto dfsBeforeSaveResult = service.depthFirst(traversalStart);
    const auto bfsBeforeSaveResult = service.breadthFirst(traversalStart);
    QVERIFY(dfsBeforeSaveResult && bfsBeforeSaveResult);
    const auto dfsBeforeSave = dfsBeforeSaveResult.value();
    const auto bfsBeforeSave = bfsBeforeSaveResult.value();

    QVERIFY(QMetaObject::invokeMethod(&window, "saveProjectAs",
                                      Qt::DirectConnection));
    QVERIFY(QFile::exists(projectPath));
    QVERIFY(!service.status().dirty);

    QVERIFY(service.addPerson("临时人物"));
    window.refreshAll();
    QVERIFY(QMetaObject::invokeMethod(&window, "openProject",
                                      Qt::DirectConnection));
    const auto statusAfterOpen = service.status();
    QVERIFY(!statusAfterOpen.dirty && !statusAfterOpen.filePath.empty());
    QCOMPARE(statusAfterOpen.personCount, statusBeforeSave.personCount);
    QCOMPARE(statusAfterOpen.relationCount, statusBeforeSave.relationCount);
    QCOMPARE(statusAfterOpen.chapterCount, statusBeforeSave.chapterCount);
    QVERIFY(statusAfterOpen.revision > statusBeforeSave.revision);

    const auto peopleAfterOpen = service.people();
    const auto aliasesAfterOpen = service.aliases();
    const auto chaptersAfterOpen = service.chapters();
    const auto relationsAfterOpen = service.relations();
    compareDtoVectors(peopleAfterOpen, peopleBeforeSave, comparePersonRow);
    compareDtoVectors(aliasesAfterOpen, aliasesBeforeSave, compareAliasRow);
    compareDtoVectors(chaptersAfterOpen, chaptersBeforeSave, compareChapterRow);
    compareDtoVectors(relationsAfterOpen, relationsBeforeSave,
                      compareRelationRow);

    const auto graphAfterOpenResult = service.graphSnapshot(0.0);
    QVERIFY(graphAfterOpenResult);
    const auto& graphAfterOpen = graphAfterOpenResult.value();
    QCOMPARE(graphAfterOpen.revision, statusAfterOpen.revision);
    QVERIFY(graphAfterOpen.revision > graphBeforeSave.revision);
    compareGraphSnapshot(graphAfterOpen, graphBeforeSave);

    const auto chapterDetailAfterOpen =
        service.chapterDetail(chaptersBeforeSave.front().id);
    QVERIFY(chapterDetailAfterOpen);
    compareChapterDetail(chapterDetailAfterOpen.value(),
                         chapterDetailBeforeSave);

    for (std::size_t index = 0; index < peopleBeforeSave.size(); ++index) {
        const auto detail = service.personDetail(peopleBeforeSave[index].id);
        QVERIFY(detail);
        comparePersonDetail(detail.value(), personDetailsBeforeSave[index]);
    }
    for (std::size_t index = 0; index < relationsBeforeSave.size(); ++index) {
        const auto detail =
            service.relationDetail(relationsBeforeSave[index].id);
        QVERIFY(detail);
        compareRelationDetail(detail.value(),
                              relationDetailsBeforeSave[index]);
    }

    const auto dfsAfterOpen = service.depthFirst(traversalStart);
    const auto bfsAfterOpen = service.breadthFirst(traversalStart);
    QVERIFY(dfsAfterOpen && bfsAfterOpen);
    compareTraversal(dfsAfterOpen.value(), dfsBeforeSave);
    compareTraversal(bfsAfterOpen.value(), bfsBeforeSave);
    QCOMPARE(interaction->errorCount, 0);
}

void UiPhaseFiveTests::populatedWindowRendersForVisualQa() {
    application::ProjectApplicationService service;
    application::NewProjectCommand project;
    project.canonicalNames = {"林黛玉", "贾宝玉", "薛宝钗",
                              "王熙凤", "贾母", "史湘云"};
    project.aliases = {{"宝二爷", "贾宝玉"},
                       {"凤姐", "王熙凤"},
                       {"老祖宗", "贾母"}};
    QVERIFY(service.newProject(project));

    const auto people = service.people();
    const auto personId = [&people](const std::string& name) {
        const auto found = std::find_if(
            people.begin(), people.end(), [&name](const auto& person) {
                return person.name == name;
            });
        return found == people.end() ? PersonId{} : found->id;
    };
    const PersonId lin = personId("林黛玉");
    const PersonId jia = personId("贾宝玉");
    const PersonId xue = personId("薛宝钗");
    const PersonId wang = personId("王熙凤");
    const PersonId grandmother = personId("贾母");
    const PersonId shi = personId("史湘云");
    QVERIFY(lin && jia && xue && wang && grandmother && shi);

    application::ImportChapterCommand first;
    first.sourcePath = std::filesystem::u8path("chapter-001.txt");
    first.key = "001";
    first.title = "黛玉进府";
    first.body = "林黛玉初进贾府，贾母、王熙凤和贾宝玉相迎。";
    first.selectedPersonIds = {lin, jia, wang, grandmother};
    QVERIFY(service.importChapter(first));

    application::ImportChapterCommand second;
    second.sourcePath = std::filesystem::u8path("chapter-002.txt");
    second.key = "002";
    second.title = "大观园小聚";
    second.body = "贾宝玉、林黛玉、薛宝钗、史湘云和王熙凤小聚。";
    second.selectedPersonIds = {lin, jia, xue, wang, shi};
    QVERIFY(service.importChapter(second));

    application::ImportChapterCommand third;
    third.sourcePath = std::filesystem::u8path("chapter-003.txt");
    third.key = "003";
    third.title = "宝钗贺寿";
    third.body = "薛宝钗和贾宝玉向贾母贺寿，王熙凤料理宴席。";
    third.selectedPersonIds = {jia, xue, wang, grandmother};
    QVERIFY(service.importChapter(third));

    MainWindow window(service, std::make_unique<FakeInteraction>());
    window.resize(1400, 860);
    window.show();
    if (QGuiApplication::platformName() == QStringLiteral("windows")) {
        window.raise();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
    }
    QTest::qWait(40);
    auto* graph = window.findChild<RelationGraphView*>(
        QStringLiteral("relationGraphView"));
    QVERIFY(graph && graph->focusPerson(jia));
    QCoreApplication::processEvents();

    const QString outputDirectory =
        qEnvironmentVariable("PHASE5_QA_OUTPUT");
    const QString suffix = qEnvironmentVariable("PHASE5_QA_SUFFIX", "default");
    const auto saveCapture = [&](QWidget& widget, const QString& name) {
        const auto capture = widget.grab();
        QVERIFY(!capture.isNull());
        QVERIFY(capture.width() >= widget.width());
        if (!outputDirectory.isEmpty()) {
            QVERIFY(QDir().mkpath(outputDirectory));
            QVERIFY(capture.save(
                QDir(outputDirectory).filePath(name + '-' + suffix + ".png")));
        }
    };
    const auto movePointer = [graph](const QPoint& viewportPosition) {
        if (QGuiApplication::platformName() == QStringLiteral("windows")) {
            QCursor::setPos(graph->viewport()->mapToGlobal(viewportPosition));
        } else {
            QTest::mouseMove(graph->viewport(), viewportPosition);
        }
        QCoreApplication::processEvents();
    };
    movePointer(QPoint{4, 4});
    QCoreApplication::processEvents();
    saveCapture(window, QStringLiteral("main-graph"));

    const auto graphSnapshot = service.graphSnapshot(0.0);
    QVERIFY(graphSnapshot && !graphSnapshot.value().edges.empty());
    auto* hoverEdge = graph->sceneController()->edgeItem(
        graphSnapshot.value().edges.front().id);
    QVERIFY(hoverEdge);
    movePointer(graph->mapFromScene(hoverEdge->boundingRect().center()));
    QTRY_VERIFY_WITH_TIMEOUT(hoverEdge->isUnderMouse(), 500);
    saveCapture(window, QStringLiteral("edge-hover"));

    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("analysisTabs"));
    tabs->setCurrentIndex(1);
    QCoreApplication::processEvents();
    saveCapture(window, QStringLiteral("person-table"));

    tabs->setCurrentIndex(2);
    auto* relations =
        window.findChild<QTableView*>(QStringLiteral("relationTable"));
    relations->selectRow(0);
    QCoreApplication::processEvents();
    saveCapture(window, QStringLiteral("relation-detail"));

    tabs->setCurrentIndex(0);
    window.findChild<QPushButton*>(QStringLiteral("depthFirstButton"))
        ->click();
    window.findChild<QPushButton*>(QStringLiteral("playTraversalButton"))
        ->click();
    QCoreApplication::processEvents();
    saveCapture(window, QStringLiteral("traversal-highlight"));
    graph->stopTraversal();

    const auto traversal = service.depthFirst(jia);
    QVERIFY(traversal);
    TraversalTreeView tree;
    tree.resize(900, 600);
    tree.setTraversal(traversal.value());
    tree.show();
    QTest::qWait(20);
    saveCapture(tree, QStringLiteral("traversal-tree"));
}

}  // namespace novel::presentation

QTEST_MAIN(novel::presentation::UiPhaseFiveTests)
#include "test_ui_phase_five.moc"
