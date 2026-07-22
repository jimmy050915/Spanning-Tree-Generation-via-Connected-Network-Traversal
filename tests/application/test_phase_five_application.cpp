#include "application/ProjectApplicationService.h"
#include "domain/project/NovelRelationProject.h"
#include "infrastructure/text/DictionaryNameExtractor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using novel::ChapterDraft;
using novel::DictionaryNameExtractor;
using novel::PersonId;
using novel::application::ApplicationErrorCode;
using novel::application::ChapterDetailDto;
using novel::application::GraphSnapshotDto;
using novel::application::ImportChapterCommand;
using novel::application::ModifyChapterCommand;
using novel::application::NewProjectCommand;
using novel::application::NewProjectFileOptions;
using novel::application::ProjectApplicationService;
using novel::application::StagedAliasCommand;

struct TestContext {
    int failures{};

    void check(bool condition, const std::string& message) {
        if (!condition) {
            ++failures;
            std::cerr << "失败：" << message << '\n';
        }
    }
};

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto stamp = static_cast<unsigned long long>(
            std::chrono::high_resolution_clock::now()
                .time_since_epoch()
                .count());
        path_ = std::filesystem::temp_directory_path() /
                ("novel-relation-phase-five-app-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    std::filesystem::path file(const std::string& name) const {
        return path_ / std::filesystem::u8path(name);
    }

private:
    std::filesystem::path path_;
};

template <typename Action>
void runCase(TestContext& context,
             const std::string& name,
             Action&& action) {
    try {
        action();
    } catch (const std::exception& error) {
        context.check(false, name + "抛出意外异常：" + error.what());
    } catch (...) {
        context.check(false, name + "抛出未知异常");
    }
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error("无法写入测试文件");
    }
}

std::string graphSignature(const GraphSnapshotDto& graph) {
    std::string result = std::to_string(graph.revision) + "|";
    for (const auto& node : graph.nodes) {
        result += std::to_string(node.id) + ":" + node.canonicalName + ":" +
                  std::to_string(node.chapterCount) + ";";
    }
    result += "|";
    for (const auto& edge : graph.edges) {
        result += std::to_string(edge.id) + ":" +
                  std::to_string(edge.endpointA) + ":" +
                  std::to_string(edge.endpointB) + ":" +
                  std::to_string(edge.coChapterCount) + ":" +
                  std::to_string(edge.jaccard) + ";";
    }
    return result;
}

void testDetailedExtraction(TestContext& context) {
    novel::NovelRelationProject project;
    const PersonId monkey = project.addPerson("孙悟空");
    project.addPerson("悟空");
    project.addAlias("大圣", monkey);
    const DictionaryNameExtractor extractor(project);

    const auto matches = extractor.extractDetailed("孙悟空与大圣，再见孙悟空");
    context.check(matches.size() == 3U,
                  "详细提取应返回每一个非重叠匹配，而非只返回人物集合");
    context.check(matches.size() >= 2U &&
                      matches[0].matchedText == "孙悟空" &&
                      !matches[0].isAlias && matches[0].person == monkey &&
                      matches[1].matchedText == "大圣" &&
                      matches[1].isAlias &&
                      matches[1].canonicalName == "孙悟空",
                  "详细提取应区分标准名和别名并给出标准人物");
    context.check(matches.size() >= 3U &&
                      matches[0].byteOffset == 0U &&
                      matches[0].byteLength == std::string("孙悟空").size() &&
                      matches[2].byteOffset > matches[1].byteOffset,
                  "详细提取应按原文顺序提供 UTF-8 字节范围");
    context.check(extractor.extract("大圣和孙悟空") ==
                      std::vector<PersonId>({monkey}),
                  "旧 extract API 应继续返回排序去重的人物编号");
}

void testNewProjectDictionaryAndStalePreview(TestContext& context) {
    ProjectApplicationService service;

    const auto initialPreview = service.previewChapterText(
        std::filesystem::u8path("初始预览.txt"),
        "@chapter=initial\n@title=初始预览\n\n暂无人物。");
    context.check(initialPreview && initialPreview.value().revision != 0U,
                  "初始项目预览也应获得非零修订号");
    context.check(service.addPerson("预览后新增").hasValue(),
                  "初始预览后的词典变更应成功");
    ImportChapterCommand initialStale;
    initialStale.expectedRevision = initialPreview.value().revision;
    initialStale.sourcePath = initialPreview.value().path;
    initialStale.key = initialPreview.value().key;
    initialStale.title = initialPreview.value().title;
    initialStale.body = initialPreview.value().body;
    const auto initialStaleResult = service.importChapter(initialStale);
    context.check(!initialStaleResult &&
                      initialStaleResult.error().code ==
                          ApplicationErrorCode::Conflict &&
                      service.status().chapterCount == 0U,
                  "初始修订号上产生的旧预览不得绕过冲突检查");

    NewProjectCommand create;
    create.canonicalNames = {"林黛玉", "贾宝玉"};
    create.aliases = {{"宝二爷", "贾宝玉"}};
    const auto created = service.newProject(create);
    context.check(created && created.value().personCount == 2U &&
                      service.aliases().size() == 1U &&
                      created.value().dirty,
                  "新建项目应原子初始化字典并标记未保存变更");

    const auto preview = service.previewChapterText(
        std::filesystem::u8path("红楼梦.txt"),
        "@chapter=001\n@title=初见\n\n宝二爷初见林黛玉。");
    context.check(preview && preview.value().revision == service.status().revision,
                  "章节预览应携带当前项目修订号");
    context.check(service.addPerson("薛宝钗").hasValue(),
                  "制造预览后词典变更应成功");
    ImportChapterCommand stale;
    stale.expectedRevision = preview.value().revision;
    stale.sourcePath = preview.value().path;
    stale.key = preview.value().key;
    stale.title = preview.value().title;
    stale.body = preview.value().body;
    stale.selectedPersonIds = preview.value().selectedPersonIds;
    const auto staleResult = service.importChapter(stale);
    context.check(!staleResult &&
                      staleResult.error().code ==
                          ApplicationErrorCode::Conflict &&
                      service.status().chapterCount == 0U,
                  "词典变化后提交旧预览应被拒绝且不修改章节");
}

void testNewProjectFromDictionaryFiles(TestContext& context,
                                       const TemporaryDirectory& temporary) {
    const auto personsFile = temporary.file("人物.txt");
    const auto aliasesFile = temporary.file("别名.txt");
    writeText(personsFile, "孙悟空\n唐僧\n");
    writeText(aliasesFile, "悟空\t孙悟空\n三藏\t唐僧\n");

    ProjectApplicationService service;
    const auto created = service.newProjectFromFiles(
        NewProjectFileOptions{personsFile, aliasesFile});
    context.check(created && created.value().personCount == 2U &&
                      service.aliases().size() == 2U &&
                      created.value().dirty,
                  "应用服务应解析字典文件并标记新项目未保存");

    const auto revisionBeforeFailure = service.status().revision;
    const auto peopleBeforeFailure = service.people();
    const auto aliasOnly = service.newProjectFromFiles(
        NewProjectFileOptions{{}, aliasesFile});
    context.check(!aliasOnly &&
                      aliasOnly.error().code ==
                          ApplicationErrorCode::TextFileFailure &&
                      service.status().revision == revisionBeforeFailure &&
                      service.people().size() == peopleBeforeFailure.size(),
                  "只有别名文件且无标准目标时应失败并保留当前项目");

    const auto empty = service.newProjectFromFiles(NewProjectFileOptions{});
    context.check(empty && empty.value().personCount == 0U &&
                      empty.value().chapterCount == 0U,
                  "两个可选字典路径都为空时应创建空项目");
}

struct Fixture {
    ProjectApplicationService service;
    PersonId monkey{};
    PersonId master{};
    PersonId pig{};

    explicit Fixture(TestContext& context) {
        const auto reset = service.newProject();
        context.check(reset.hasValue() && reset.value().dirty,
                      "显式新建项目应标记未保存变更");
        const auto monkeyResult = service.addPerson("孙悟空");
        const auto masterResult = service.addPerson("唐僧");
        const auto pigResult = service.addPerson("猪八戒");
        context.check(monkeyResult && masterResult && pigResult,
                      "人物应能通过应用服务创建");
        if (monkeyResult && masterResult && pigResult) {
            monkey = monkeyResult.value();
            master = masterResult.value();
            pig = pigResult.value();
        }
        context.check(service.addAlias("悟空", monkey).hasValue() &&
                          service.addAlias("三藏", master).hasValue(),
                      "别名应能通过应用服务创建");
    }
};

void testPreviewImportAndQueries(TestContext& context) {
    Fixture fixture(context);
    auto& service = fixture.service;

    const auto preview = service.previewChapterText(
        std::filesystem::u8path("第一章.txt"),
        "@chapter=001\n@title=初遇\n\n悟空遇见唐僧，孙悟空行礼。");
    context.check(preview && preview.value().key == "001" &&
                      preview.value().title == "初遇" &&
                      preview.value().matches.size() == 3U &&
                      preview.value().selectedPersonIds ==
                          std::vector<PersonId>({fixture.monkey,
                                                 fixture.master}),
                  "章节预览应解析元数据、详细匹配并去重建议人物");
    const auto statusBeforePreview = service.status();
    context.check(statusBeforePreview.personCount == 3U &&
                      statusBeforePreview.chapterCount == 0U,
                  "章节预览和取消不得修改项目");

    ImportChapterCommand first;
    first.sourcePath = std::filesystem::u8path("第一章.txt");
    first.key = "001";
    first.title = "初遇";
    first.body = "悟空遇见唐僧，孙悟空行礼。";
    first.selectedPersonIds = {fixture.monkey, fixture.master};
    const auto firstId = service.confirmChapterImport(first);
    context.check(firstId && service.status().chapterCount == 1U &&
                      service.status().relationCount == 1U,
                  "确认导入应一次性提交章节及关系统计");

    ImportChapterCommand second;
    second.sourcePath = std::filesystem::u8path("第二章.txt");
    second.key = "002";
    second.title = "同行";
    second.body = "悟空遇见八戒和沙僧。";
    second.selectedPersonIds = {fixture.monkey, fixture.pig};
    second.newCanonicalNames = {"沙僧"};
    second.newAliases = {StagedAliasCommand{"悟净", "沙僧"}};
    const auto secondId = service.importChapter(second);
    context.check(secondId && service.status().personCount == 4U &&
                      service.status().chapterCount == 2U,
                  "章节、新标准人物和别名应原子提交");
    const auto aliasRows = service.aliases();
    context.check(aliasRows.size() == 3U &&
                      std::any_of(aliasRows.begin(),
                                  aliasRows.end(),
                                  [](const auto& alias) {
                                      return alias.alias == "悟净" &&
                                             alias.targetName == "沙僧";
                                  }),
                  "别名列表应稳定排序并包含标准目标显示名");

    const auto people = service.people();
    const auto chapters = service.chapters();
    const auto relations = service.relations();
    context.check(people.size() == 4U && people[0].id == fixture.monkey &&
                      people[0].chapterCount == 2U &&
                      people[0].degree == 3U &&
                      people[0].strongestPerson == fixture.master &&
                      people[0].strongestPersonName == "唐僧" &&
                      people[0].strongestJaccard == 0.5,
                  "人物表应提供稳定编号、统计和最高关联人物");
    context.check(chapters.size() == 2U && chapters[0].key == "001" &&
                      chapters[1].personCount == 3U &&
                      chapters[1].sourceFile == "第二章.txt" &&
                      chapters[1].status ==
                          novel::application::ChapterStatus::Normal,
                  "章节表应提供稳定顺序、人物数、来源和派生状态");
    context.check(relations.size() == 4U,
                  "关系表应反映两章中全部人物共现边");

    const auto threshold = service.graphSnapshot(0.5);
    context.check(threshold && threshold.value().nodes.size() == 4U &&
                      threshold.value().edges.size() == 4U &&
                      std::any_of(threshold.value().edges.begin(),
                                  threshold.value().edges.end(),
                                  [](const auto& edge) {
                                      return edge.jaccard == 0.5;
                                  }),
                  "关系图阈值应含等于边界的边并保留全部人物");
    const auto above = service.graphSnapshot(0.5000001);
    context.check(above && above.value().nodes.size() == 4U &&
                      above.value().edges.size() == 1U &&
                      above.value().edges.front().jaccard == 1.0,
                  "高于边界的阈值应过滤关系但不移除孤立结点");
    const auto invalidThreshold = service.graphSnapshot(1.1);
    context.check(!invalidThreshold &&
                      invalidThreshold.error().code ==
                          ApplicationErrorCode::InvalidArgument,
                  "非法关系阈值应返回应用错误");

    const auto monkeyDetail = service.personDetail(fixture.monkey);
    context.check(monkeyDetail &&
                      monkeyDetail.value().chapters.size() == 2U &&
                      monkeyDetail.value().relations.size() == 3U &&
                      monkeyDetail.value().strongestRelation.has_value(),
                  "人物详情应包含出现章节、全部关系和最高关联人物");
    const auto firstDetail = service.chapterDetail(firstId.value());
    context.check(firstDetail && firstDetail.value().persons.size() == 2U &&
                      firstDetail.value().body == first.body,
                  "章节详情应包含正文和人物显示数据");
    const auto relationDetail =
        service.relationDetail(relations.front().id);
    context.check(relationDetail &&
                      relationDetail.value().commonChapters.size() == 1U,
                  "关系详情应列出共同章节");

    const auto dfs = service.depthFirst(fixture.monkey);
    const auto bfs = service.breadthFirst(fixture.monkey);
    context.check(dfs && bfs && dfs.value().order.size() == 4U &&
                      bfs.value().order.size() == 4U &&
                      !dfs.value().nodes.empty() &&
                      dfs.value().nodes.front().virtualRoot &&
                      dfs.value().treeEdges.size() == 4U,
                  "DFS/BFS DTO 应完整遍历并显式输出虚拟根和树边");
}

void testMutationsAndFailureAtomicity(TestContext& context) {
    Fixture fixture(context);
    auto& service = fixture.service;

    ImportChapterCommand first;
    first.key = "001";
    first.title = "第一章";
    first.body = "悟空和唐僧";
    first.selectedPersonIds = {fixture.monkey, fixture.master};
    const auto firstId = service.importChapter(first);
    context.check(firstId.hasValue(), "准备章节应导入成功");

    const auto beforeFailure = service.graphSnapshot(0.0);
    const auto revisionBeforeFailure = service.status().revision;
    ImportChapterCommand duplicate = first;
    duplicate.sourcePath = std::filesystem::u8path("duplicate-001.txt");
    duplicate.newCanonicalNames = {"不应泄漏"};
    duplicate.newAliases = {{"泄漏别名", "不应泄漏"}};
    const auto duplicateResult = service.importChapter(duplicate);
    context.check(!duplicateResult &&
                      duplicateResult.error().code ==
                          ApplicationErrorCode::Conflict &&
                      duplicateResult.error().message.find(
                          "duplicate-001.txt") != std::string::npos &&
                      duplicateResult.error().message.find("001") !=
                          std::string::npos &&
                      service.status().revision == revisionBeforeFailure &&
                      service.people().size() == 3U &&
                      service.aliases().size() == 2U,
                  "重复章节错误应包含文件和章节编号，且不得泄漏暂存修改");
    const auto afterFailure = service.graphSnapshot(0.0);
    context.check(beforeFailure && afterFailure &&
                      graphSignature(beforeFailure.value()) ==
                          graphSignature(afterFailure.value()),
                  "失败导入应保持完整图快照和修订号不变");

    ModifyChapterCommand modify;
    modify.id = firstId.value();
    modify.key = "001";
    modify.title = "第一章（修订）";
    modify.body = "三藏与猪八戒";
    modify.selectedPersonIds = {fixture.master, fixture.pig};
    context.check(service.modifyChapter(modify).hasValue() &&
                      service.status().relationCount == 1U,
                  "修改章节应重算人物和边统计");
    context.check(service.reextractChapter(firstId.value()).hasValue(),
                  "重新提取应使用当前词典替换章节人物集合");
    const auto reextracted = service.chapterDetail(firstId.value());
    context.check(reextracted && reextracted.value().persons.size() == 2U,
                  "重新提取后的章节详情应反映别名匹配结果");

    context.check(service.mergePersons(fixture.master, fixture.pig).hasValue(),
                  "合并人物应成功重定向章节和别名");
    const auto aliasesAfterMerge = service.aliases();
    context.check(service.people().size() == 2U &&
                      std::any_of(aliasesAfterMerge.begin(),
                                  aliasesAfterMerge.end(),
                                  [](const auto& alias) {
                                      return alias.alias == "唐僧" &&
                                             alias.targetName == "猪八戒";
                                  }),
                  "来源标准名和来源别名应在合并后指向目标人物");

    const auto isolated = service.addPerson("临时人物");
    context.check(isolated && service.addAlias("临时", isolated.value()) &&
                      service.deleteUnusedPerson(isolated.value()),
                  "未使用人物删除应同时清理其别名");
    const auto aliasesAfterDelete = service.aliases();
    context.check(std::none_of(aliasesAfterDelete.begin(),
                               aliasesAfterDelete.end(),
                               [](const auto& alias) {
                                   return alias.alias == "临时";
                               }),
                  "删除未使用人物后不得保留悬空别名");
    const auto inUseFailure = service.deleteUnusedPerson(fixture.pig);
    context.check(!inUseFailure &&
                      inUseFailure.error().code ==
                          ApplicationErrorCode::Conflict,
                  "仍在章节中的人物应拒绝删除");
    context.check(service.renamePerson(fixture.pig, "猪悟能").hasValue(),
                  "人物重命名应通过事务式服务提交");
    context.check(service.deleteChapter(firstId.value()).hasValue() &&
                      service.status().chapterCount == 0U &&
                      service.status().relationCount == 0U,
                  "删除章节应重建统计并移除无共现关系");
}

void testAddingPersonIncrementallyExtractsExistingChapters(
    TestContext& context) {
    ProjectApplicationService service;
    const auto retained = service.addPerson("人工保留");
    context.check(retained.hasValue(), "准备人工保留人物应成功");
    if (!retained) {
        return;
    }

    ImportChapterCommand first;
    first.key = "001";
    first.title = "首次出现";
    first.body = "林黛玉初进贾府。";
    first.selectedPersonIds = {retained.value()};
    const auto firstId = service.importChapter(first);

    ImportChapterCommand second;
    second.key = "002";
    second.title = "再次出现";
    second.body = "众人迎接林黛玉。";
    const auto secondId = service.importChapter(second);

    ImportChapterCommand unrelated;
    unrelated.key = "003";
    unrelated.title = "无关章节";
    unrelated.body = "这里没有目标姓名。";
    unrelated.selectedPersonIds = {retained.value()};
    const auto unrelatedId = service.importChapter(unrelated);
    context.check(firstId && secondId && unrelatedId,
                  "准备既有章节应全部导入成功");
    if (!firstId || !secondId || !unrelatedId) {
        return;
    }

    const auto revisionBeforeAdd = service.status().revision;
    const auto added = service.addPerson("林黛玉");
    context.check(added && service.status().revision == revisionBeforeAdd + 1U,
                  "新增人物和批量章节更新应作为一次事务提交");
    if (!added) {
        return;
    }

    const auto firstDetail = service.chapterDetail(firstId.value());
    const auto secondDetail = service.chapterDetail(secondId.value());
    const auto unrelatedDetail = service.chapterDetail(unrelatedId.value());
    context.check(firstDetail && secondDetail && unrelatedDetail,
                  "自动提取后全部章节详情应可读取");
    if (!firstDetail || !secondDetail || !unrelatedDetail) {
        return;
    }

    const auto containsPerson = [](const ChapterDetailDto& detail,
                                   PersonId person) {
        return std::any_of(detail.persons.begin(), detail.persons.end(),
                           [person](const auto& row) {
                               return row.id == person;
                           });
    };
    context.check(containsPerson(firstDetail.value(), retained.value()) &&
                      containsPerson(firstDetail.value(), added.value()) &&
                      containsPerson(secondDetail.value(), added.value()) &&
                      !containsPerson(unrelatedDetail.value(), added.value()),
                  "新增人物应补入所有匹配章节且不得改动无关章节");

    const auto addedDetail = service.personDetail(added.value());
    context.check(addedDetail && addedDetail.value().chapters.size() == 2U &&
                      service.status().relationCount == 1U,
                  "自动提取后人物章节数和共现关系应立即更新");
}

void testPersistenceAndOpenAtomicity(TestContext& context,
                                     const TemporaryDirectory& temporary) {
    Fixture fixture(context);
    auto& service = fixture.service;
    ImportChapterCommand chapter;
    chapter.key = "001";
    chapter.title = "持久化";
    chapter.body = "孙悟空与唐僧";
    chapter.selectedPersonIds = {fixture.monkey, fixture.master};
    context.check(service.importChapter(chapter).hasValue(),
                  "持久化准备章节应导入成功");

    const auto saveWithoutPath = service.save();
    context.check(!saveWithoutPath &&
                      saveWithoutPath.error().code ==
                          ApplicationErrorCode::ProjectPathRequired &&
                      service.status().dirty,
                  "无路径保存应失败并保留脏状态");
    const auto projectFile = temporary.file("阶段五项目.nprg");
    const auto saved = service.saveAs(projectFile);
    context.check(saved && !saved.value().dirty &&
                      saved.value().filePath == projectFile,
                  "另存为成功后应记录路径并清除脏状态");

    const auto cleanRevision = service.status().revision;
    context.check(service.renamePerson(fixture.monkey, "孙悟空") &&
                      !service.status().dirty &&
                      service.status().revision == cleanRevision,
                  "同名重命名不应制造未保存变更");
    ModifyChapterCommand unchanged;
    unchanged.expectedRevision = cleanRevision;
    unchanged.id = service.chapters().front().id;
    unchanged.key = chapter.key;
    unchanged.title = chapter.title;
    unchanged.body = chapter.body;
    unchanged.selectedPersonIds = chapter.selectedPersonIds;
    context.check(service.modifyChapter(unchanged) &&
                      service.reextractChapter(unchanged.id) &&
                      !service.status().dirty &&
                      service.status().revision == cleanRevision,
                  "无变化章节编辑和重提取不应制造未保存变更");

    const auto extra = service.addPerson("保存后人物");
    context.check(extra && service.status().dirty,
                  "保存后的修改应重新标记为脏");
    context.check(service.openProject(projectFile).hasValue() &&
                      service.people().size() == 3U &&
                      !service.status().dirty,
                  "重新打开应恢复保存快照并清除后续未保存修改");

    const auto beforeBadOpen = service.graphSnapshot(0.0);
    const auto pathBeforeBadOpen = service.status().filePath;
    const auto revisionBeforeBadOpen = service.status().revision;
    const auto invalidFile = temporary.file("损坏项目.nprg");
    writeText(invalidFile, "not-a-project");
    const auto badOpen = service.openProject(invalidFile);
    context.check(!badOpen &&
                      badOpen.error().code ==
                          ApplicationErrorCode::PersistenceFailure &&
                      service.status().filePath == pathBeforeBadOpen &&
                      service.status().revision == revisionBeforeBadOpen,
                  "打开损坏项目失败时应保留当前项目路径和修订号");
    const auto afterBadOpen = service.graphSnapshot(0.0);
    context.check(beforeBadOpen && afterBadOpen &&
                      graphSignature(beforeBadOpen.value()) ==
                          graphSignature(afterBadOpen.value()),
                  "打开失败时应保持界面图快照不变");

    const auto missingDirectory =
        temporary.file("不存在目录") / "失败.nprg";
    const auto statusBeforeBadSave = service.status();
    const auto badSave = service.saveAs(missingDirectory);
    context.check(!badSave &&
                      badSave.error().code ==
                          ApplicationErrorCode::PersistenceFailure &&
                      service.status().filePath == statusBeforeBadSave.filePath &&
                      service.status().dirty == statusBeforeBadSave.dirty &&
                      service.status().revision == statusBeforeBadSave.revision,
                  "另存失败时应保持路径、脏状态和修订号不变");
}

}  // namespace

int main() {
    TestContext context;
    TemporaryDirectory temporary;

    runCase(context, "详细名称提取", [&]() {
        testDetailedExtraction(context);
    });
    runCase(context, "新建字典和过期预览", [&]() {
        testNewProjectDictionaryAndStalePreview(context);
    });
    runCase(context, "从字典文件新建", [&]() {
        testNewProjectFromDictionaryFiles(context, temporary);
    });
    runCase(context, "预览、导入和查询", [&]() {
        testPreviewImportAndQueries(context);
    });
    runCase(context, "修改和失败不变性", [&]() {
        testMutationsAndFailureAtomicity(context);
    });
    runCase(context, "新增人物自动提取既有章节", [&]() {
        testAddingPersonIncrementallyExtractsExistingChapters(context);
    });
    runCase(context, "保存打开和失败不变性", [&]() {
        testPersistenceAndOpenAtomicity(context, temporary);
    });

    if (context.failures == 0) {
        std::cout << "阶段五纯 C++ 应用层测试全部通过。\n";
        return 0;
    }
    std::cerr << context.failures << " 项测试失败。\n";
    return 1;
}
