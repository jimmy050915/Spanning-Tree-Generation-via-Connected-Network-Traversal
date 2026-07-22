#include "presentation/MainWindow.h"

#include "application/ApplicationError.h"
#include "application/ProjectApplicationService.h"
#include "infrastructure/text/DictionaryTextParser.h"
#include "presentation/dialogs/AliasManagementDialog.h"
#include "presentation/dialogs/ChapterEditorDialog.h"
#include "presentation/dialogs/NewProjectDialog.h"
#include "presentation/dialogs/PersonManagementDialog.h"
#include "presentation/graphics/RelationGraphView.h"
#include "presentation/graphics/TraversalTreeView.h"
#include "presentation/interaction/IUserInteraction.h"
#include "presentation/interaction/QtUserInteraction.h"
#include "presentation/models/ChapterTableModel.h"
#include "presentation/models/PersonTableModel.h"
#include "presentation/models/RelationTableModel.h"
#include "presentation/models/SearchSortProxyModel.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <utility>

namespace novel::presentation {
namespace {

QString fromUtf8(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string toUtf8(const QString& text) {
    const auto bytes = text.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

QString fromPath(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::filesystem::path toPath(const QString& text) {
    const auto bytes = text.toUtf8();
    return std::filesystem::u8path(bytes.constData(),
                                   bytes.constData() + bytes.size());
}

QString htmlList(const QStringList& values) {
    if (values.isEmpty()) {
        return QObject::tr("<p><i>无</i></p>");
    }
    QString html = QStringLiteral("<ul>");
    for (const auto& value : values) {
        html += QStringLiteral("<li>%1</li>").arg(value.toHtmlEscaped());
    }
    return html + QStringLiteral("</ul>");
}

class BusyIndicator final {
public:
    BusyIndicator(QWidget* parent, const QString& message)
        : progress_(message, {}, 0, 0, parent) {
        progress_.setWindowTitle(QObject::tr("请稍候"));
        progress_.setWindowModality(Qt::WindowModal);
        progress_.setCancelButton(nullptr);
        progress_.setMinimumDuration(0);
        progress_.show();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

private:
    QProgressDialog progress_;
};

ChapterEditorData editorData(
    const application::ChapterPreviewDto& preview,
    const std::vector<PersonChoice>& choices) {
    ChapterEditorData data;
    data.filePath = fromPath(preview.path);
    data.chapterKey = fromUtf8(preview.key);
    data.title = fromUtf8(preview.title);
    data.content = fromUtf8(preview.body);
    data.selectedPeople = preview.selectedPersonIds;
    data.availablePeople = choices;
    data.matches.reserve(preview.matches.size());
    for (const auto& match : preview.matches) {
        data.matches.push_back({fromUtf8(match.matchedText), match.isAlias,
                                match.person,
                                fromUtf8(match.canonicalName)});
    }
    return data;
}

std::vector<application::StagedAliasCommand> stagedAliases(
    const ChapterEditorResult& result,
    const std::vector<PersonChoice>& people) {
    std::vector<application::StagedAliasCommand> commands;
    commands.reserve(result.newAliases.size());
    for (const auto& alias : result.newAliases) {
        QString target = alias.targetNewPersonName;
        if (target.isEmpty()) {
            const auto found = std::find_if(
                people.begin(), people.end(), [&alias](const PersonChoice& p) {
                    return p.id == alias.targetPerson;
                });
            if (found != people.end()) {
                target = found->name;
            }
        }
        if (!target.isEmpty()) {
            commands.push_back({toUtf8(alias.alias), toUtf8(target)});
        }
    }
    return commands;
}

std::vector<std::string> newPersonNames(const ChapterEditorResult& result) {
    std::vector<std::string> names;
    names.reserve(static_cast<std::size_t>(result.selectedNewPeople.size()));
    for (const auto& name : result.selectedNewPeople) {
        names.push_back(toUtf8(name));
    }
    return names;
}

}  // namespace

MainWindow::MainWindow(application::ProjectApplicationService& service,
                       std::unique_ptr<IUserInteraction> interaction,
                       QWidget* parent)
    : QMainWindow(parent),
      service_(service),
      interaction_(interaction ? std::move(interaction)
                               : std::make_unique<QtUserInteraction>()) {
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(tr("小说人物关系分析系统[*]"));
    resize(1400, 860);
    setMinimumSize(1000, 650);

    buildCentralUi();
    buildLogDock();
    buildStatusBar();
    buildMenus();
    connectViewSignals();
    refreshAll();
    appendLog(tr("信息"), tr("启动"), tr("应用程序已就绪。"));
}

MainWindow::~MainWindow() = default;

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    auto* newAction = fileMenu->addAction(tr("新建项目(&N)"));
    newAction->setShortcut(QKeySequence::New);
    auto* openAction = fileMenu->addAction(tr("打开项目(&O)…"));
    openAction->setShortcut(QKeySequence::Open);
    saveAction_ = fileMenu->addAction(tr("保存(&S)"));
    saveAction_->setShortcut(QKeySequence::Save);
    auto* saveAsAction = fileMenu->addAction(tr("另存为(&A)…"));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    fileMenu->addSeparator();
    auto* exitAction = fileMenu->addAction(tr("退出(&X)"));

    auto* chapterMenu = menuBar()->addMenu(tr("章节(&C)"));
    auto* importAction = chapterMenu->addAction(tr("导入章节…"));
    editChapterAction_ = chapterMenu->addAction(tr("编辑所选章节…"));
    reextractChapterAction_ =
        chapterMenu->addAction(tr("重新提取所选章节…"));
    deleteChapterAction_ = chapterMenu->addAction(tr("删除所选章节"));

    auto* personMenu = menuBar()->addMenu(tr("人物(&P)"));
    auto* peopleAction = personMenu->addAction(tr("人物管理…"));
    auto* aliasesAction = personMenu->addAction(tr("别名管理…"));

    auto* relationMenu = menuBar()->addMenu(tr("关系(&R)"));
    auto* refreshGraphAction = relationMenu->addAction(tr("刷新关系图"));

    auto* traversalMenu = menuBar()->addMenu(tr("遍历(&T)"));
    auto* dfsAction = traversalMenu->addAction(tr("深度优先遍历"));
    auto* bfsAction = traversalMenu->addAction(tr("广度优先遍历"));
    auto* treeAction = traversalMenu->addAction(tr("显示遍历树"));

    auto* toolsMenu = menuBar()->addMenu(tr("工具(&L)"));
    toolsMenu->addAction(logDock_->toggleViewAction());

    auto* helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    auto* aboutAction = helpMenu->addAction(tr("关于"));

    connect(newAction, &QAction::triggered, this, &MainWindow::newProject);
    connect(openAction, &QAction::triggered, this, &MainWindow::openProject);
    connect(saveAction_, &QAction::triggered, this, &MainWindow::saveProject);
    connect(saveAsAction, &QAction::triggered, this,
            &MainWindow::saveProjectAs);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    connect(importAction, &QAction::triggered, this,
            &MainWindow::importChapter);
    connect(editChapterAction_, &QAction::triggered, this,
            &MainWindow::editSelectedChapter);
    connect(reextractChapterAction_, &QAction::triggered, this,
            &MainWindow::reextractSelectedChapter);
    connect(deleteChapterAction_, &QAction::triggered, this,
            &MainWindow::deleteSelectedChapter);
    connect(peopleAction, &QAction::triggered, this,
            &MainWindow::showPersonManagement);
    connect(aliasesAction, &QAction::triggered, this,
            &MainWindow::showAliasManagement);
    connect(refreshGraphAction, &QAction::triggered, this,
            &MainWindow::updateGraphSnapshot);
    connect(dfsAction, &QAction::triggered, this,
            &MainWindow::runDepthFirst);
    connect(bfsAction, &QAction::triggered, this,
            &MainWindow::runBreadthFirst);
    connect(treeAction, &QAction::triggered, this,
            &MainWindow::showTraversalTree);
    connect(aboutAction, &QAction::triggered, this, [this] {
        interaction_->showInformation(
            this, tr("关于"),
            tr("基于邻接多重表与 Jaccard 系数的小说人物关联分析系统。"));
    });
}

void MainWindow::buildCentralUi() {
    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("mainCentralWidget"));
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(6, 6, 6, 6);
    auto* splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setObjectName(QStringLiteral("mainThreeColumnSplitter"));
    splitter->addWidget(buildChapterPanel());
    splitter->addWidget(buildCenterPanel());
    splitter->addWidget(buildDetailPanel());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({420, 640, 340});
    layout->addWidget(splitter, 1);
    layout->addWidget(buildTraversalBar());
    setCentralWidget(central);
}

QWidget* MainWindow::buildChapterPanel() {
    auto* panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("chapterPanel"));
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(tr("章节管理"), panel);
    title->setObjectName(QStringLiteral("chapterPanelTitle"));
    layout->addWidget(title);
    chapterSearchEdit_ = new QLineEdit(panel);
    chapterSearchEdit_->setObjectName(QStringLiteral("chapterSearchEdit"));
    chapterSearchEdit_->setPlaceholderText(tr("搜索章节编号或标题…"));
    layout->addWidget(chapterSearchEdit_);

    chapterModel_ = new ChapterTableModel(this);
    chapterProxy_ = new SearchSortProxyModel(this);
    chapterProxy_->setSourceModel(chapterModel_);
    chapterTable_ = new QTableView(panel);
    chapterTable_->setObjectName(QStringLiteral("chapterTable"));
    chapterTable_->setModel(chapterProxy_);
    chapterTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    chapterTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    chapterTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    chapterTable_->setSortingEnabled(true);
    chapterTable_->horizontalHeader()->setStretchLastSection(false);
    chapterTable_->sortByColumn(ChapterTableModel::KeyColumn,
                                Qt::AscendingOrder);
    chapterTable_->verticalHeader()->hide();
    layout->addWidget(chapterTable_, 1);

    auto* importButton = new QPushButton(tr("导入章节…"), panel);
    importButton->setObjectName(QStringLiteral("importChapterButton"));
    auto* editButton = new QPushButton(tr("修改"), panel);
    editButton->setObjectName(QStringLiteral("editChapterButton"));
    auto* reextractButton = new QPushButton(tr("重新提取"), panel);
    reextractButton->setObjectName(QStringLiteral("reextractChapterButton"));
    auto* deleteButton = new QPushButton(tr("删除"), panel);
    deleteButton->setObjectName(QStringLiteral("deleteChapterButton"));
    auto* row = new QHBoxLayout;
    row->addWidget(editButton);
    row->addWidget(reextractButton);
    row->addWidget(deleteButton);
    layout->addWidget(importButton);
    layout->addLayout(row);

    connect(chapterSearchEdit_, &QLineEdit::textChanged, chapterProxy_,
            &QSortFilterProxyModel::setFilterFixedString);
    connect(importButton, &QPushButton::clicked, this,
            &MainWindow::importChapter);
    connect(editButton, &QPushButton::clicked, this,
            &MainWindow::editSelectedChapter);
    connect(reextractButton, &QPushButton::clicked, this,
            &MainWindow::reextractSelectedChapter);
    connect(deleteButton, &QPushButton::clicked, this,
            &MainWindow::deleteSelectedChapter);
    connect(chapterTable_, &QTableView::doubleClicked, this,
            &MainWindow::editSelectedChapter);
    return panel;
}

QWidget* MainWindow::buildCenterPanel() {
    centerTabs_ = new QTabWidget(this);
    centerTabs_->setObjectName(QStringLiteral("analysisTabs"));

    auto* graphPage = new QWidget(centerTabs_);
    auto* graphLayout = new QVBoxLayout(graphPage);
    graphLayout->setContentsMargins(0, 0, 0, 0);
    auto* graphControls = new QHBoxLayout;
    graphPersonSearch_ = new QComboBox(graphPage);
    graphPersonSearch_->setObjectName(QStringLiteral("graphPersonSearch"));
    graphPersonSearch_->setEditable(true);
    graphPersonSearch_->setInsertPolicy(QComboBox::NoInsert);
    graphPersonSearch_->lineEdit()->setPlaceholderText(tr("搜索并定位人物…"));
    auto* locateButton = new QPushButton(tr("定位"), graphPage);
    locateButton->setObjectName(QStringLiteral("locateGraphPersonButton"));
    thresholdSpin_ = new QDoubleSpinBox(graphPage);
    thresholdSpin_->setObjectName(QStringLiteral("minimumJaccardSpin"));
    thresholdSpin_->setRange(0.0, 1.0);
    thresholdSpin_->setSingleStep(0.05);
    thresholdSpin_->setDecimals(2);
    thresholdSpin_->setValue(0.0);
    thresholdSpin_->setPrefix(tr("最低关联度 "));
    graphControls->addWidget(graphPersonSearch_, 1);
    graphControls->addWidget(locateButton);
    graphControls->addSpacing(12);
    graphControls->addWidget(thresholdSpin_);
    graphLayout->addLayout(graphControls);
    graphView_ = new RelationGraphView(graphPage);
    graphView_->setObjectName(QStringLiteral("relationGraphView"));
    graphLayout->addWidget(graphView_, 1);
    centerTabs_->addTab(graphPage, tr("人物关系图"));
    connect(locateButton, &QPushButton::clicked, this,
            &MainWindow::focusGraphPerson);

    auto* personPage = new QWidget(centerTabs_);
    auto* personLayout = new QVBoxLayout(personPage);
    personLayout->setContentsMargins(0, 0, 0, 0);
    auto* personSearch = new QLineEdit(personPage);
    personSearch->setObjectName(QStringLiteral("personTableSearchEdit"));
    personSearch->setPlaceholderText(tr("搜索人物…"));
    personModel_ = new PersonTableModel(this);
    personProxy_ = new SearchSortProxyModel(this);
    personProxy_->setSourceModel(personModel_);
    personTable_ = new QTableView(personPage);
    personTable_->setObjectName(QStringLiteral("personTable"));
    personTable_->setModel(personProxy_);
    personTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    personTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    personTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    personTable_->setSortingEnabled(true);
    personTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    personTable_->sortByColumn(PersonTableModel::IdColumn,
                               Qt::AscendingOrder);
    personLayout->addWidget(personSearch);
    personLayout->addWidget(personTable_, 1);
    centerTabs_->addTab(personPage, tr("人物表"));
    connect(personSearch, &QLineEdit::textChanged, personProxy_,
            &QSortFilterProxyModel::setFilterFixedString);

    auto* relationPage = new QWidget(centerTabs_);
    auto* relationLayout = new QVBoxLayout(relationPage);
    relationLayout->setContentsMargins(0, 0, 0, 0);
    auto* relationSearch = new QLineEdit(relationPage);
    relationSearch->setObjectName(QStringLiteral("relationTableSearchEdit"));
    relationSearch->setPlaceholderText(tr("搜索人物关系…"));
    relationModel_ = new RelationTableModel(this);
    relationProxy_ = new SearchSortProxyModel(this);
    relationProxy_->setSourceModel(relationModel_);
    relationTable_ = new QTableView(relationPage);
    relationTable_->setObjectName(QStringLiteral("relationTable"));
    relationTable_->setModel(relationProxy_);
    relationTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    relationTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    relationTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    relationTable_->setSortingEnabled(true);
    relationTable_->horizontalHeader()->setStretchLastSection(true);
    relationTable_->sortByColumn(RelationTableModel::FirstPersonColumn,
                                 Qt::AscendingOrder);
    relationLayout->addWidget(relationSearch);
    relationLayout->addWidget(relationTable_, 1);
    centerTabs_->addTab(relationPage, tr("关系表"));
    connect(relationSearch, &QLineEdit::textChanged, relationProxy_,
            &QSortFilterProxyModel::setFilterFixedString);
    return centerTabs_;
}

QWidget* MainWindow::buildDetailPanel() {
    auto* panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("detailPanel"));
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel(tr("人物 / 关系 / 章节详情"), panel));
    detailBrowser_ = new QTextBrowser(panel);
    detailBrowser_->setObjectName(QStringLiteral("detailBrowser"));
    detailBrowser_->setOpenExternalLinks(false);
    layout->addWidget(detailBrowser_, 1);
    resetDetailPanel();
    return panel;
}

QWidget* MainWindow::buildTraversalBar() {
    auto* box = new QGroupBox(tr("图遍历"), this);
    box->setObjectName(QStringLiteral("traversalBar"));
    auto* layout = new QHBoxLayout(box);
    traversalStartCombo_ = new QComboBox(box);
    traversalStartCombo_->setObjectName(QStringLiteral("traversalStartCombo"));
    traversalStartCombo_->setMinimumWidth(150);
    auto* dfsButton = new QPushButton(tr("DFS"), box);
    dfsButton->setObjectName(QStringLiteral("depthFirstButton"));
    auto* bfsButton = new QPushButton(tr("BFS"), box);
    bfsButton->setObjectName(QStringLiteral("breadthFirstButton"));
    playTraversalButton_ = new QPushButton(tr("播放"), box);
    playTraversalButton_->setObjectName(QStringLiteral("playTraversalButton"));
    stopTraversalButton_ = new QPushButton(tr("停止"), box);
    stopTraversalButton_->setObjectName(QStringLiteral("stopTraversalButton"));
    showTraversalTreeButton_ = new QPushButton(tr("显示遍历树"), box);
    showTraversalTreeButton_->setObjectName(
        QStringLiteral("showTraversalTreeButton"));
    traversalSequenceEdit_ = new QLineEdit(box);
    traversalSequenceEdit_->setObjectName(QStringLiteral("traversalSequence"));
    traversalSequenceEdit_->setReadOnly(true);
    traversalSequenceEdit_->setPlaceholderText(tr("遍历序列将显示在这里"));
    layout->addWidget(new QLabel(tr("起点："), box));
    layout->addWidget(traversalStartCombo_);
    layout->addWidget(dfsButton);
    layout->addWidget(bfsButton);
    layout->addWidget(playTraversalButton_);
    layout->addWidget(stopTraversalButton_);
    layout->addWidget(showTraversalTreeButton_);
    layout->addWidget(traversalSequenceEdit_, 1);
    connect(dfsButton, &QPushButton::clicked, this,
            &MainWindow::runDepthFirst);
    connect(bfsButton, &QPushButton::clicked, this,
            &MainWindow::runBreadthFirst);
    connect(playTraversalButton_, &QPushButton::clicked, this,
            &MainWindow::playTraversal);
    connect(stopTraversalButton_, &QPushButton::clicked, this,
            &MainWindow::stopTraversal);
    connect(showTraversalTreeButton_, &QPushButton::clicked, this,
            &MainWindow::showTraversalTree);
    playTraversalButton_->setEnabled(false);
    stopTraversalButton_->setEnabled(false);
    showTraversalTreeButton_->setEnabled(false);
    return box;
}

void MainWindow::buildLogDock() {
    logDock_ = new QDockWidget(tr("操作日志"), this);
    logDock_->setObjectName(QStringLiteral("operationLogDock"));
    logDock_->setAllowedAreas(Qt::BottomDockWidgetArea);
    logEdit_ = new QPlainTextEdit(logDock_);
    logEdit_->setObjectName(QStringLiteral("operationLog"));
    logEdit_->setReadOnly(true);
    logEdit_->setMaximumBlockCount(2000);
    logDock_->setWidget(logEdit_);
    addDockWidget(Qt::BottomDockWidgetArea, logDock_);
    resizeDocks({logDock_}, {150}, Qt::Vertical);
}

void MainWindow::buildStatusBar() {
    pathStatusLabel_ = new QLabel(this);
    pathStatusLabel_->setObjectName(QStringLiteral("projectPathStatus"));
    personStatusLabel_ = new QLabel(this);
    relationStatusLabel_ = new QLabel(this);
    chapterStatusLabel_ = new QLabel(this);
    dirtyStatusLabel_ = new QLabel(this);
    statusBar()->addWidget(pathStatusLabel_, 1);
    statusBar()->addPermanentWidget(personStatusLabel_);
    statusBar()->addPermanentWidget(relationStatusLabel_);
    statusBar()->addPermanentWidget(chapterStatusLabel_);
    statusBar()->addPermanentWidget(dirtyStatusLabel_);
}

void MainWindow::connectViewSignals() {
    connect(chapterTable_->selectionModel(),
            &QItemSelectionModel::selectionChanged, this,
            [this] { showSelectedChapterDetail(); });
    connect(personTable_->selectionModel(),
            &QItemSelectionModel::selectionChanged, this,
            [this] { showSelectedPersonDetail(); });
    connect(relationTable_->selectionModel(),
            &QItemSelectionModel::selectionChanged, this,
            [this] { showSelectedRelationDetail(); });
    connect(graphView_, &RelationGraphView::personSelected, this,
            &MainWindow::showPersonDetail);
    connect(graphView_, &RelationGraphView::relationSelected, this,
            &MainWindow::showRelationDetail);
    connect(graphView_, &RelationGraphView::selectionCleared, this,
            &MainWindow::resetDetailPanel);
    connect(graphView_, &RelationGraphView::traversalFinished, this, [this] {
        playTraversalButton_->setEnabled(traversalResult_.has_value());
        stopTraversalButton_->setEnabled(false);
        appendLog(tr("信息"), tr("遍历动画"), tr("播放完成。"));
    });
    connect(graphView_, &RelationGraphView::traversalStopped, this, [this] {
        playTraversalButton_->setEnabled(traversalResult_.has_value());
        stopTraversalButton_->setEnabled(false);
    });
    connect(graphPersonSearch_->lineEdit(), &QLineEdit::returnPressed, this,
            &MainWindow::focusGraphPerson);
    connect(thresholdSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double) { updateGraphSnapshot(); });
    connect(chapterTable_->selectionModel(),
            &QItemSelectionModel::selectionChanged, this,
            &MainWindow::updateSelectionActions);
}

void MainWindow::refreshAll() {
    graphView_->stopTraversal();
    traversalResult_.reset();
    traversalSequenceEdit_->clear();
    playTraversalButton_->setEnabled(false);
    stopTraversalButton_->setEnabled(false);
    showTraversalTreeButton_->setEnabled(false);

    std::vector<ChapterRow> chapters;
    for (const auto& dto : service_.chapters()) {
        chapters.push_back({dto.id, fromUtf8(dto.key), fromUtf8(dto.title),
                            fromUtf8(dto.sourceFile),
                            static_cast<quint32>(dto.personCount),
                            dto.status ==
                                application::ChapterStatus::NeedsReview});
    }
    chapterModel_->setRows(std::move(chapters));
    chapterTable_->resizeColumnsToContents();

    std::vector<PersonRow> people;
    for (const auto& dto : service_.people()) {
        people.push_back({dto.id, fromUtf8(dto.name), dto.chapterCount,
                          static_cast<quint32>(dto.degree),
                          dto.strongestPerson.value_or(PersonId{}),
                          fromUtf8(dto.strongestPersonName),
                          dto.strongestJaccard});
    }
    personModel_->setRows(std::move(people));

    std::vector<RelationRow> relations;
    for (const auto& dto : service_.relations()) {
        relations.push_back(
            {dto.id, dto.personA, dto.personB, fromUtf8(dto.personAName),
             fromUtf8(dto.personBName), dto.coChapterCount, dto.jaccard});
    }
    relationModel_->setRows(std::move(relations));

    updatePersonSelectors();
    updateGraphSnapshot();
    updateStatusBar();
    updateSelectionActions();
    resetDetailPanel();
}

void MainWindow::newProject() {
    if (!confirmDiscardOrSave()) {
        return;
    }
    NewProjectDialog dialog(interaction_.get(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    application::NewProjectFileOptions options;
    if (!dialog.personsFile().isEmpty()) {
        options.personsFile = toPath(dialog.personsFile());
    }
    if (!dialog.aliasesFile().isEmpty()) {
        options.aliasesFile = toPath(dialog.aliasesFile());
    }
    BusyIndicator busy(this, tr("正在创建新项目…"));
    const auto outcome = service_.newProjectFromFiles(options);
    if (!outcome) {
        reportError(tr("新建项目"), outcome.error());
        return;
    }
    traversalResult_.reset();
    refreshAll();
    appendLog(tr("信息"), tr("新建项目"), tr("项目已创建。"));
}

void MainWindow::openProject() {
    if (!confirmDiscardOrSave()) {
        return;
    }
    const auto file = interaction_->chooseOpenProjectFile(this);
    if (file.isEmpty()) {
        return;
    }
    BusyIndicator busy(this, tr("正在打开项目…"));
    const auto outcome = service_.openProject(toPath(file));
    if (!outcome) {
        reportError(tr("打开项目"), outcome.error());
        return;
    }
    traversalResult_.reset();
    refreshAll();
    appendLog(tr("信息"), tr("打开项目"),
              tr("已打开：%1").arg(file));
}

void MainWindow::saveProject() {
    saveToCurrentPath();
}

void MainWindow::saveProjectAs() {
    saveToChosenPath();
}

bool MainWindow::saveToCurrentPath() {
    if (service_.status().filePath.empty()) {
        return saveToChosenPath();
    }
    BusyIndicator busy(this, tr("正在保存项目…"));
    const auto outcome = service_.save();
    if (!outcome) {
        reportError(tr("保存项目"), outcome.error());
        return false;
    }
    updateStatusBar();
    appendLog(tr("信息"), tr("保存项目"),
              tr("项目已保存。"));
    return true;
}

bool MainWindow::saveToChosenPath() {
    auto initial = fromPath(service_.status().filePath);
    const auto file = interaction_->chooseSaveProjectFile(this, initial);
    if (file.isEmpty()) {
        return false;
    }
    auto chosen = file;
    if (QFileInfo(chosen).suffix().isEmpty()) {
        chosen += QStringLiteral(".nprg");
    }
    BusyIndicator busy(this, tr("正在保存项目…"));
    const auto outcome = service_.saveAs(toPath(chosen));
    if (!outcome) {
        reportError(tr("另存项目"), outcome.error());
        return false;
    }
    updateStatusBar();
    appendLog(tr("信息"), tr("另存项目"),
              tr("已保存至：%1").arg(chosen));
    return true;
}

bool MainWindow::confirmDiscardOrSave() {
    if (!service_.status().dirty) {
        return true;
    }
    switch (interaction_->confirmUnsavedChanges(this)) {
        case UnsavedChangesChoice::Save:
            return saveToCurrentPath();
        case UnsavedChangesChoice::Discard:
            return true;
        case UnsavedChangesChoice::Cancel:
            return false;
    }
    return false;
}

void MainWindow::importChapter() {
    const auto file = interaction_->chooseChapterTextFile(this);
    if (file.isEmpty()) {
        return;
    }
    application::Result<application::ChapterPreviewDto> preview = [&] {
        BusyIndicator busy(this, tr("正在解析章节文件…"));
        return service_.previewChapterFile(toPath(file));
    }();
    if (!preview) {
        reportError(tr("导入章节"), preview.error());
        return;
    }
    const auto choices = personChoices();
    ChapterEditorDialog dialog(ChapterEditorDialog::Mode::Import, this,
                               interaction_.get());
    dialog.setData(editorData(preview.value(), choices));
    if (dialog.exec() != QDialog::Accepted) {
        appendLog(tr("信息"), tr("导入章节"), tr("用户取消导入。"));
        return;
    }
    const auto edited = dialog.result();
    application::ImportChapterCommand command;
    command.expectedRevision = preview.value().revision;
    command.sourcePath = preview.value().path;
    command.key = toUtf8(edited.chapterKey);
    command.title = toUtf8(edited.title);
    command.body = toUtf8(edited.content);
    command.selectedPersonIds = edited.selectedPeople;
    command.newCanonicalNames = newPersonNames(edited);
    command.newAliases = stagedAliases(edited, choices);
    const auto outcome = [&] {
        BusyIndicator busy(this, tr("正在更新人物关系统计…"));
        return service_.importChapter(command);
    }();
    if (!outcome) {
        reportError(tr("导入章节"), outcome.error());
        return;
    }
    refreshAll();
    appendLog(tr("信息"), tr("导入章节"),
              tr("章节 %1 导入成功。").arg(edited.chapterKey));
}

void MainWindow::editSelectedChapter() {
    const auto id = selectedChapterId();
    if (id == 0) {
        return;
    }
    const auto detail = service_.chapterDetail(id);
    if (!detail) {
        reportError(tr("编辑章节"), detail.error());
        return;
    }
    const auto preview = service_.previewChapterReextraction(id);
    if (!preview) {
        reportError(tr("编辑章节"), preview.error());
        return;
    }
    const auto choices = personChoices();
    auto data = editorData(preview.value(), choices);
    // Ordinary editing displays fresh recognition details, but the chapter's
    // manually corrected people remain authoritative until the user changes
    // them explicitly. Re-extraction has a separate command for adopting the
    // automatic suggestion.
    data.selectedPeople.clear();
    for (const auto& person : detail.value().persons) {
        data.selectedPeople.push_back(person.id);
    }
    ChapterEditorDialog dialog(ChapterEditorDialog::Mode::Edit, this,
                               interaction_.get());
    dialog.setData(data);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto edited = dialog.result();
    application::ModifyChapterCommand command;
    command.expectedRevision = preview.value().revision;
    command.id = id;
    command.sourcePath = preview.value().path;
    command.key = toUtf8(edited.chapterKey);
    command.title = toUtf8(edited.title);
    command.body = toUtf8(edited.content);
    command.selectedPersonIds = edited.selectedPeople;
    command.newCanonicalNames = newPersonNames(edited);
    command.newAliases = stagedAliases(edited, choices);
    const auto outcome = service_.modifyChapter(command);
    if (!outcome) {
        reportError(tr("编辑章节"), outcome.error());
        return;
    }
    refreshAll();
    appendLog(tr("信息"), tr("编辑章节"),
              tr("章节 %1 已更新。").arg(edited.chapterKey));
}

void MainWindow::reextractSelectedChapter() {
    const auto id = selectedChapterId();
    if (id == 0) {
        return;
    }
    const auto preview = service_.previewChapterReextraction(id);
    if (!preview) {
        reportError(tr("重新提取章节"), preview.error());
        return;
    }
    const auto choices = personChoices();
    ChapterEditorDialog dialog(ChapterEditorDialog::Mode::Edit, this,
                               interaction_.get());
    dialog.setWindowTitle(tr("确认重新提取结果"));
    dialog.setData(editorData(preview.value(), choices));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto edited = dialog.result();
    application::ModifyChapterCommand command;
    command.expectedRevision = preview.value().revision;
    command.id = id;
    command.sourcePath = preview.value().path;
    command.key = toUtf8(edited.chapterKey);
    command.title = toUtf8(edited.title);
    command.body = toUtf8(edited.content);
    command.selectedPersonIds = edited.selectedPeople;
    command.newCanonicalNames = newPersonNames(edited);
    command.newAliases = stagedAliases(edited, choices);
    const auto outcome = service_.modifyChapter(command);
    if (!outcome) {
        reportError(tr("重新提取章节"), outcome.error());
        return;
    }
    refreshAll();
    appendLog(tr("信息"), tr("重新提取章节"),
              tr("章节 %1 的人物集合已更新。")
                  .arg(edited.chapterKey));
}

void MainWindow::deleteSelectedChapter() {
    const auto id = selectedChapterId();
    if (id == 0) {
        return;
    }
    const auto* row = chapterModel_->rowAt(
        chapterProxy_->mapToSource(chapterTable_->currentIndex()).row());
    const auto label = row ? row->chapterKey : QString::number(id);
    if (!interaction_->confirmDestructiveAction(
            this, tr("删除章节"),
            tr("确定删除章节“%1”吗？人物关系将重新计算。")
                .arg(label))) {
        return;
    }
    const auto outcome = service_.deleteChapter(id);
    if (!outcome) {
        reportError(tr("删除章节"), outcome.error());
        return;
    }
    refreshAll();
    appendLog(tr("信息"), tr("删除章节"),
              tr("章节 %1 已删除。").arg(label));
}

void MainWindow::showPersonManagement() {
    PersonManagementDialog dialog(this);
    dialog.setPeople(personChoices());
    connect(&dialog, &PersonManagementDialog::addPersonRequested, this,
            [this, &dialog](const QString& name) {
                const auto outcome = service_.addPerson(toUtf8(name));
                if (!outcome) {
                    reportError(tr("新增人物"), outcome.error());
                    return;
                }
                refreshAll();
                dialog.setPeople(personChoices());
                appendLog(tr("信息"), tr("新增人物"),
                          tr("已新增人物：%1").arg(name));
            });
    connect(&dialog, &PersonManagementDialog::renamePersonRequested, this,
            [this, &dialog](PersonId id, const QString& name) {
                const auto outcome = service_.renamePerson(id, toUtf8(name));
                if (!outcome) {
                    reportError(tr("重命名人物"), outcome.error());
                    return;
                }
                refreshAll();
                dialog.setPeople(personChoices());
                appendLog(tr("信息"), tr("重命名人物"),
                          tr("人物已重命名为：%1").arg(name));
            });
    connect(&dialog, &PersonManagementDialog::mergePeopleRequested, this,
            [this, &dialog](PersonId source, PersonId target) {
                if (!interaction_->confirmDestructiveAction(
                        this, tr("合并人物"),
                        tr("确定合并这两个人物吗？来源人物的章节和别名将转移到目标人物。"))) {
                    return;
                }
                const auto outcome = service_.mergePersons(source, target);
                if (!outcome) {
                    reportError(tr("合并人物"), outcome.error());
                    return;
                }
                refreshAll();
                dialog.setPeople(personChoices());
                appendLog(tr("信息"), tr("合并人物"),
                          tr("人物合并完成。"));
            });
    connect(&dialog,
            &PersonManagementDialog::deleteUnusedPersonRequested, this,
            [this, &dialog](PersonId id) {
                if (!interaction_->confirmDestructiveAction(
                        this, tr("删除人物"),
                        tr("确定删除所选的未使用人物及其别名吗？"))) {
                    return;
                }
                const auto outcome = service_.deleteUnusedPerson(id);
                if (!outcome) {
                    reportError(tr("删除人物"), outcome.error());
                    return;
                }
                refreshAll();
                dialog.setPeople(personChoices());
                appendLog(tr("信息"), tr("删除人物"),
                          tr("未使用人物已删除。"));
            });
    dialog.exec();
}

void MainWindow::showAliasManagement() {
    AliasManagementDialog dialog(this);
    const auto reload = [this, &dialog] {
        std::vector<AliasRow> aliases;
        for (const auto& dto : service_.aliases()) {
            aliases.push_back({fromUtf8(dto.alias), dto.targetPerson,
                               fromUtf8(dto.targetName)});
        }
        dialog.setData(aliases, personChoices());
    };
    reload();
    connect(&dialog, &AliasManagementDialog::addAliasRequested, this,
            [this, &reload](const QString& alias, PersonId target) {
                const auto outcome = service_.addAlias(toUtf8(alias), target);
                if (!outcome) {
                    reportError(tr("添加别名"), outcome.error());
                    return;
                }
                refreshAll();
                reload();
                appendLog(tr("信息"), tr("添加别名"),
                          tr("已添加别名：%1").arg(alias));
            });
    connect(&dialog, &AliasManagementDialog::removeAliasRequested, this,
            [this, &reload](const QString& alias) {
                if (!interaction_->confirmDestructiveAction(
                        this, tr("删除别名"),
                        tr("确定删除别名“%1”吗？").arg(alias))) {
                    return;
                }
                const auto outcome = service_.removeAlias(toUtf8(alias));
                if (!outcome) {
                    reportError(tr("删除别名"), outcome.error());
                    return;
                }
                refreshAll();
                reload();
                appendLog(tr("信息"), tr("删除别名"),
                          tr("已删除别名：%1").arg(alias));
            });
    dialog.exec();
}

void MainWindow::showSelectedChapterDetail() {
    updateSelectionActions();
    const auto id = selectedChapterId();
    if (id == 0) {
        resetDetailPanel();
        return;
    }
    const auto detail = service_.chapterDetail(id);
    if (!detail) {
        reportQueryError(tr("查看章节详情"), detail.error());
        return;
    }
    QStringList people;
    for (const auto& person : detail.value().persons) {
        people.push_back(fromUtf8(person.name));
    }
    detailBrowser_->setHtml(
        tr("<h2>%1</h2><p><b>章节编号：</b>%2</p>"
           "<p><b>来源文件：</b>%3</p><p><b>人物：</b></p>%4"
           "<hr><h3>正文</h3><p style='white-space:pre-wrap'>%5</p>")
            .arg(fromUtf8(detail.value().chapter.title).toHtmlEscaped(),
                 fromUtf8(detail.value().chapter.key).toHtmlEscaped(),
                 fromUtf8(detail.value().chapter.sourceFile).toHtmlEscaped(),
                 htmlList(people),
                 fromUtf8(detail.value().body).toHtmlEscaped()));
}

void MainWindow::showSelectedPersonDetail() {
    const auto id = selectedPersonId();
    if (id != 0) {
        {
            const QSignalBlocker blocker(graphView_);
            (void)graphView_->focusPerson(id);
        }
        showPersonDetail(id);
    }
}

void MainWindow::showSelectedRelationDetail() {
    const auto id = selectedRelationId();
    if (id == 0) {
        return;
    }
    const auto index = relationTable_->currentIndex();
    showRelationDetail(
        id, index.data(RelationTableModel::FirstPersonIdRole).value<PersonId>(),
        index.data(RelationTableModel::SecondPersonIdRole).value<PersonId>());
}

void MainWindow::showPersonDetail(PersonId id) {
    const auto detail = service_.personDetail(id);
    if (!detail) {
        reportQueryError(tr("查看人物详情"), detail.error());
        return;
    }
    QStringList aliases;
    for (const auto& alias : detail.value().aliases) {
        aliases.push_back(fromUtf8(alias));
    }
    QStringList chapters;
    for (const auto& chapter : detail.value().chapters) {
        chapters.push_back(
            tr("%1 - %2").arg(fromUtf8(chapter.key), fromUtf8(chapter.title)));
    }
    QStringList relations;
    for (const auto& relation : detail.value().relations) {
        const auto other = relation.personA == id ? relation.personBName
                                                  : relation.personAName;
        relations.push_back(
            tr("%1：Jaccard %2，共同 %3 章")
                .arg(fromUtf8(other))
                .arg(relation.jaccard, 0, 'f', 4)
                .arg(relation.coChapterCount));
    }
    QString strongest = tr("无");
    if (detail.value().strongestRelation) {
        const auto& edge = *detail.value().strongestRelation;
        strongest = fromUtf8(edge.personA == id ? edge.personBName
                                                : edge.personAName);
    }
    detailBrowser_->setHtml(
        tr("<h2>%1</h2><p><b>出现章节数：</b>%2</p>"
           "<p><b>度数：</b>%3</p><p><b>最高关联人物：</b>%4</p>"
           "<h3>别名</h3>%5<h3>出现章节</h3>%6<h3>关联人物</h3>%7")
            .arg(fromUtf8(detail.value().person.name).toHtmlEscaped())
            .arg(detail.value().person.chapterCount)
            .arg(detail.value().person.degree)
            .arg(strongest.toHtmlEscaped(), htmlList(aliases),
                 htmlList(chapters), htmlList(relations)));
}

void MainWindow::showRelationDetail(EdgeId id, PersonId, PersonId) {
    const auto detail = service_.relationDetail(id);
    if (!detail) {
        reportQueryError(tr("查看关系详情"), detail.error());
        return;
    }
    QStringList chapters;
    for (const auto& chapter : detail.value().commonChapters) {
        chapters.push_back(
            tr("%1 - %2").arg(fromUtf8(chapter.key), fromUtf8(chapter.title)));
    }
    const auto& relation = detail.value().relation;
    detailBrowser_->setHtml(
        tr("<h2>%1 — %2</h2><p><b>共同章节数：</b>%3</p>"
           "<p><b>Jaccard 关联度：</b>%4</p><h3>共同出现章节</h3>%5")
            .arg(fromUtf8(relation.personAName).toHtmlEscaped(),
                 fromUtf8(relation.personBName).toHtmlEscaped())
            .arg(relation.coChapterCount)
            .arg(relation.jaccard, 0, 'f', 6)
            .arg(htmlList(chapters)));
}

void MainWindow::updateGraphSnapshot() {
    graphView_->stopTraversal();
    const auto snapshot = service_.graphSnapshot(thresholdSpin_->value());
    if (!snapshot) {
        graphView_->clear();
        reportQueryError(tr("刷新关系图"), snapshot.error());
        return;
    }
    graphView_->setSnapshot(snapshot.value());
}

void MainWindow::focusGraphPerson() {
    bool found = false;
    const auto query = graphPersonSearch_->currentText();
    const int currentIndex = graphPersonSearch_->currentIndex();
    const bool unchangedDropDownChoice =
        currentIndex >= 0 && query == graphPersonSearch_->itemText(currentIndex);
    if (unchangedDropDownChoice) {
        const auto id =
            graphPersonSearch_->itemData(currentIndex).value<PersonId>();
        if (id != 0) {
            found = graphView_->focusPerson(id);
        }
    }
    if (!found) {
        found = graphView_->focusPerson(query);
    }
    if (!found) {
        statusBar()->showMessage(tr("当前关系图中没有该人物。"), 3000);
    }
}

void MainWindow::runDepthFirst() {
    const auto start = traversalStartPerson();
    if (start == 0) {
        statusBar()->showMessage(tr("请先选择遍历起点。"), 3000);
        return;
    }
    const auto outcome = service_.depthFirst(start);
    if (!outcome) {
        reportError(tr("DFS 遍历"), outcome.error());
        return;
    }
    setTraversalResult(outcome.value());
    appendLog(tr("信息"), tr("DFS 遍历"),
              tr("已生成包含 %1 个人物的完整遍历。")
                  .arg(outcome.value().order.size()));
}

void MainWindow::runBreadthFirst() {
    const auto start = traversalStartPerson();
    if (start == 0) {
        statusBar()->showMessage(tr("请先选择遍历起点。"), 3000);
        return;
    }
    const auto outcome = service_.breadthFirst(start);
    if (!outcome) {
        reportError(tr("BFS 遍历"), outcome.error());
        return;
    }
    setTraversalResult(outcome.value());
    appendLog(tr("信息"), tr("BFS 遍历"),
              tr("已生成包含 %1 个人物的完整遍历。")
                  .arg(outcome.value().order.size()));
}

void MainWindow::setTraversalResult(application::TraversalResultDto result) {
    traversalResult_ = std::move(result);
    QStringList names;
    for (const auto& name : traversalResult_->orderNames) {
        names.push_back(fromUtf8(name));
    }
    traversalSequenceEdit_->setText(names.join(tr(" → ")));
    traversalSequenceEdit_->setCursorPosition(0);
    playTraversalButton_->setEnabled(!traversalResult_->order.empty());
    stopTraversalButton_->setEnabled(false);
    showTraversalTreeButton_->setEnabled(!traversalResult_->nodes.empty());
}

void MainWindow::playTraversal() {
    if (!traversalResult_ || traversalResult_->order.empty()) {
        return;
    }
    centerTabs_->setCurrentIndex(0);
    playTraversalButton_->setEnabled(false);
    stopTraversalButton_->setEnabled(true);
    graphView_->playTraversal(*traversalResult_, 400);
    appendLog(tr("信息"), tr("遍历动画"), tr("开始播放。"));
}

void MainWindow::stopTraversal() {
    graphView_->stopTraversal();
    playTraversalButton_->setEnabled(traversalResult_.has_value());
    stopTraversalButton_->setEnabled(false);
}

void MainWindow::showTraversalTree() {
    if (!traversalResult_) {
        statusBar()->showMessage(tr("请先执行 DFS 或 BFS。"), 3000);
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("traversalTreeDialog"));
    dialog.setWindowTitle(
        traversalResult_->kind == application::TraversalKind::DepthFirst
            ? tr("DFS 遍历树")
            : tr("BFS 遍历树"));
    dialog.resize(900, 650);
    auto* layout = new QVBoxLayout(&dialog);
    auto* view = new TraversalTreeView(&dialog);
    view->setObjectName(QStringLiteral("traversalTreeView"));
    view->setTraversal(*traversalResult_);
    layout->addWidget(view, 1);
    auto* closeButton = new QPushButton(tr("关闭"), &dialog);
    layout->addWidget(closeButton, 0, Qt::AlignRight);
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(view, &TraversalTreeView::personSelected, this,
            [this](PersonId id) {
                centerTabs_->setCurrentIndex(0);
                {
                    const QSignalBlocker blocker(graphView_);
                    (void)graphView_->focusPerson(id);
                }
                showPersonDetail(id);
            });
    dialog.exec();
}

ChapterId MainWindow::selectedChapterId() const noexcept {
    if (!chapterTable_ || !chapterTable_->currentIndex().isValid()) {
        return 0;
    }
    return chapterTable_->currentIndex()
        .data(ChapterTableModel::ChapterIdRole)
        .value<ChapterId>();
}

PersonId MainWindow::selectedPersonId() const noexcept {
    if (!personTable_ || !personTable_->currentIndex().isValid()) {
        return 0;
    }
    return personTable_->currentIndex()
        .data(PersonTableModel::PersonIdRole)
        .value<PersonId>();
}

EdgeId MainWindow::selectedRelationId() const noexcept {
    if (!relationTable_ || !relationTable_->currentIndex().isValid()) {
        return 0;
    }
    return relationTable_->currentIndex()
        .data(RelationTableModel::EdgeIdRole)
        .value<EdgeId>();
}

PersonId MainWindow::traversalStartPerson() const noexcept {
    return traversalStartCombo_->currentData().value<PersonId>();
}

std::vector<PersonChoice> MainWindow::personChoices() const {
    std::vector<PersonChoice> choices;
    for (const auto& person : service_.people()) {
        choices.push_back(
            {person.id, fromUtf8(person.name), person.chapterCount});
    }
    return choices;
}

void MainWindow::updatePersonSelectors() {
    const auto oldTraversal = traversalStartPerson();
    const auto oldGraph = graphPersonSearch_->currentData().value<PersonId>();
    traversalStartCombo_->clear();
    graphPersonSearch_->clear();
    for (const auto& person : service_.people()) {
        const auto name = fromUtf8(person.name);
        traversalStartCombo_->addItem(name, QVariant::fromValue(person.id));
        graphPersonSearch_->addItem(name, QVariant::fromValue(person.id));
    }
    const auto restore = [](QComboBox* combo, PersonId id) {
        const auto index = combo->findData(QVariant::fromValue(id));
        if (index >= 0) {
            combo->setCurrentIndex(index);
        }
    };
    restore(traversalStartCombo_, oldTraversal);
    restore(graphPersonSearch_, oldGraph);
}

void MainWindow::updateStatusBar() {
    const auto status = service_.status();
    const auto path = status.filePath.empty() ? tr("未保存项目")
                                              : fromPath(status.filePath);
    pathStatusLabel_->setText(tr("路径：%1").arg(path));
    pathStatusLabel_->setToolTip(path);
    personStatusLabel_->setText(tr("人物：%1").arg(status.personCount));
    relationStatusLabel_->setText(tr("边：%1").arg(status.relationCount));
    chapterStatusLabel_->setText(tr("章节：%1").arg(status.chapterCount));
    dirtyStatusLabel_->setText(status.dirty ? tr("已修改") : tr("已保存"));
    setWindowModified(status.dirty);
    saveAction_->setEnabled(status.dirty);
}

void MainWindow::updateSelectionActions() {
    const bool hasChapter = selectedChapterId() != 0;
    if (editChapterAction_) {
        editChapterAction_->setEnabled(hasChapter);
        reextractChapterAction_->setEnabled(hasChapter);
        deleteChapterAction_->setEnabled(hasChapter);
    }
    for (const auto* name : {"editChapterButton", "reextractChapterButton",
                             "deleteChapterButton"}) {
        if (auto* button = findChild<QPushButton*>(QString::fromLatin1(name))) {
            button->setEnabled(hasChapter);
        }
    }
}

void MainWindow::resetDetailPanel() {
    detailBrowser_->setHtml(
        tr("<h2>详情</h2><p>从章节、人物或关系表中选择一项，"
           "也可在关系图中点击结点或边。</p>"));
}

void MainWindow::appendLog(const QString& level, const QString& operation,
                           const QString& message) {
    logEdit_->appendPlainText(
        tr("[%1] [%2] %3：%4")
            .arg(QDateTime::currentDateTime().toString(
                     QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                 level, operation, message));
}

void MainWindow::reportError(
    const QString& operation,
    const application::ApplicationError& error) {
    const auto message = fromUtf8(error.message);
    appendLog(tr("错误"), operation, message);
    interaction_->showError(this, operation, message);
    updateStatusBar();
}

void MainWindow::reportQueryError(
    const QString& operation,
    const application::ApplicationError& error) {
    const auto message = fromUtf8(error.message);
    appendLog(tr("错误"), operation, message);
    statusBar()->showMessage(tr("%1：%2").arg(operation, message), 5000);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (confirmDiscardOrSave()) {
        event->accept();
    } else {
        event->ignore();
    }
}

}  // namespace novel::presentation
