#include "domain/alias/AliasDictionary.h"
#include "domain/chapter/ChapterCollection.h"
#include "domain/error/DomainError.h"
#include "domain/project/NovelRelationProject.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace novel {

class GraphTestAccess {
public:
    static void setNextEdgeId(AdjacencyMultilistGraph& graph, EdgeId id) {
        graph.nextEdgeId_ = id;
    }

    static EdgeId nextEdgeId(const AdjacencyMultilistGraph& graph) {
        return graph.nextEdgeId_;
    }
};

class ProjectTestAccess {
public:
    static AdjacencyMultilistGraph& graph(NovelRelationProject& project) {
        return project.graph_;
    }

    static ChapterCollection& chapters(NovelRelationProject& project) {
        return project.chapters_;
    }

    static ChapterRecord& chapter(NovelRelationProject& project,
                                  ChapterId id) {
        return *const_cast<ChapterRecord*>(project.chapters_.find(id));
    }
};

}  // namespace novel

namespace {

using novel::AliasDictionary;
using novel::ChapterCollection;
using novel::ChapterDraft;
using novel::DomainError;
using novel::DomainErrorCode;
using novel::NovelRelationProject;
using novel::PersonId;
using novel::ValidationReport;

struct TestContext {
    int failures{};

    void check(bool condition, const std::string& message) {
        if (!condition) {
            ++failures;
            std::cerr << "[失败] " << message << '\n';
        }
    }
};

template <typename Callable>
void expectDomainError(TestContext& context,
                       DomainErrorCode expectedCode,
                       Callable&& callable,
                       const std::string& message) {
    try {
        callable();
        context.check(false, message + "（未抛出 DomainError）");
    } catch (const DomainError& error) {
        context.check(error.code() == expectedCode, message + "（错误码不正确）");
    } catch (...) {
        context.check(false, message + "（抛出了错误的异常类型）");
    }
}

ChapterDraft makeDraft(std::string chapterKey,
                       std::vector<PersonId> persons,
                       std::string title = {},
                       std::string sourceFileName = {},
                       std::string contentUtf8 = {}) {
    return ChapterDraft{std::move(chapterKey),
                        std::move(title),
                        std::move(sourceFileName),
                        std::move(contentUtf8),
                        std::move(persons)};
}

bool almostEqual(double first, double second, double epsilon = 1e-9) {
    return std::abs(first - second) <= epsilon;
}

bool hasIssue(const ValidationReport& report, const std::string& code) {
    return std::any_of(report.issues.begin(), report.issues.end(),
                       [&code](const novel::ValidationIssue& issue) {
                           return issue.code == code;
                       });
}

void testChapterCollection(TestContext& context) {
    ChapterCollection chapters;
    context.check(chapters.size() == 0 && chapters.all().empty(),
                  "新章节集合应为空");

    expectDomainError(context, DomainErrorCode::EmptyChapterKey,
                      [&chapters]() { chapters.add(makeDraft(" \t", {})); },
                      "空白章节键应被拒绝");

    const auto first = chapters.add(
        makeDraft("001", {3, 1, 3, 2}, "第一章", "001.txt", "正文"));
    const auto* stored = chapters.find(first);
    context.check(first == 1 && stored != nullptr,
                  "章节编号应从 1 开始并可按编号查询");
    context.check(stored != nullptr &&
                      stored->persons == std::vector<PersonId>({1, 2, 3}),
                  "章节人物应排序并去重");
    context.check(stored != nullptr && stored->title == "第一章" &&
                      stored->sourceFileName == "001.txt" &&
                      stored->contentUtf8 == "正文",
                  "章节非统计字段应原样保存");

    expectDomainError(context, DomainErrorCode::DuplicateChapterKey,
                      [&chapters]() { chapters.add(makeDraft("001", {})); },
                      "重复章节键应被拒绝");
    const auto second = chapters.add(makeDraft("002", {}));
    context.check(second == 2,
                  "失败的新增请求不得消耗章节编号");

    context.check(chapters.modify(
                      first,
                      makeDraft("010", {4, 4, 2}, "新标题", "010.txt", "新正文")),
                  "修改已有章节应成功");
    context.check(chapters.find(first) != nullptr &&
                      chapters.find(first)->id == first &&
                      chapters.find(first)->persons ==
                          std::vector<PersonId>({2, 4}) &&
                      chapters.find(first)->title == "新标题" &&
                      chapters.find(first)->sourceFileName == "010.txt" &&
                      chapters.find(first)->contentUtf8 == "新正文",
                  "修改章节应保留编号、字段并重新规范化人物");
    context.check(chapters.findByKey("001") == nullptr &&
                      chapters.findByKey("010") != nullptr,
                  "修改章节键后索引应同步更新");

    expectDomainError(context, DomainErrorCode::DuplicateChapterKey,
                      [&chapters, first]() {
                          chapters.modify(first, makeDraft("002", {}));
                      },
                      "修改为其他章节已有的键应被拒绝");
    context.check(chapters.find(first) != nullptr &&
                      chapters.find(first)->chapterKey == "010" &&
                      chapters.find(first)->title == "新标题" &&
                      chapters.find(first)->persons ==
                          std::vector<PersonId>({2, 4}),
                  "失败的章节修改不得改变原记录");
    context.check(!chapters.modify(9999, makeDraft("不存在", {})),
                  "修改不存在章节应返回 false");
    context.check(chapters.remove(second) && !chapters.remove(second),
                  "删除章节成功后重复删除应返回 false");

    const auto third = chapters.add(makeDraft("003", {}));
    context.check(third == 3,
                  "删除章节后不得复用其编号");
    context.check(chapters.all().size() == 2 &&
                      chapters.all()[0].id == first &&
                      chapters.all()[1].id == third,
                  "章节枚举应保持编号递增顺序");

    const auto fourth = chapters.add(makeDraft("004", {}));
    context.check(chapters.remove(first) && chapters.find(third) != nullptr &&
                      chapters.find(fourth) != nullptr &&
                      chapters.findByKey("003") != nullptr &&
                      chapters.findByKey("004") != nullptr &&
                      chapters.validate().isValid(),
                  "删除非尾部章节后全部编号和章节键索引应修复");
}

void testAliasesAndNames(TestContext& context) {
    AliasDictionary dictionary;
    dictionary.addAlias("悟空", 2);
    dictionary.addAlias("齐天大圣", 2);
    context.check(dictionary.resolve("悟空") == std::optional<PersonId>(2) &&
                      !dictionary.resolve("未知").has_value(),
                  "别名字典应解析已知别名并拒绝猜测未知名称");
    const auto entries = dictionary.entries();
    context.check(entries.size() == 2 && entries[0].first < entries[1].first,
                  "别名枚举应按名称稳定排序");
    expectDomainError(context, DomainErrorCode::EmptyAlias,
                      [&dictionary]() { dictionary.addAlias(" \t", 2); },
                      "空白别名应被拒绝");
    expectDomainError(context, DomainErrorCode::DuplicateAlias,
                      [&dictionary]() { dictionary.addAlias("悟空", 2); },
                      "重复别名即使目标相同也应被拒绝");
    context.check(dictionary.removeAlias("悟空") &&
                      !dictionary.removeAlias("悟空"),
                  "删除别名成功后重复删除应返回 false");

    NovelRelationProject project;
    const auto wukong = project.addPerson("孙悟空");
    const auto tang = project.addPerson("唐僧");
    project.addAlias("悟空", wukong);
    project.addAlias("行者", wukong);

    context.check(project.resolvePersonName("孙悟空") == wukong &&
                      project.resolvePersonName("悟空") == wukong &&
                      !project.resolvePersonName("白龙马").has_value(),
                  "项目名称解析应先支持标准名再支持别名");
    expectDomainError(context, DomainErrorCode::NameConflict,
                      [&project, wukong]() {
                          project.addAlias("孙悟空", wukong);
                      },
                      "别名不得与标准人物名冲突");
    const auto aliasesBeforeFailure = project.aliases().entries();
    expectDomainError(context, DomainErrorCode::PersonNotFound,
                      [&project]() { project.addAlias("未知目标", 9999); },
                      "别名目标人物必须存在");
    context.check(project.aliases().entries() == aliasesBeforeFailure,
                  "失败的别名请求不得改变已有映射");
    expectDomainError(context, DomainErrorCode::NameConflict,
                      [&project]() { project.addPerson("悟空"); },
                      "标准人物名不得占用现有别名");
    expectDomainError(context, DomainErrorCode::NameConflict,
                      [&project, tang]() { project.renamePerson(tang, "行者"); },
                      "人物重命名不得占用现有别名");
    context.check(project.renamePerson(tang, "玄奘") &&
                      project.resolvePersonName("玄奘") == tang &&
                      !project.resolvePersonName("唐僧").has_value(),
                  "成功重命名后标准人物词典索引应同步更新");
    expectDomainError(context, DomainErrorCode::PersonInUse,
                      [&project, wukong]() { project.removePerson(wukong); },
                      "有别名引用的人物不得删除");

    context.check(project.removeAlias("悟空") && project.removeAlias("行者"),
                  "项目应能删除人物的全部别名");
    context.check(project.removePerson(wukong),
                  "无章节或别名引用的人物应可删除");
}

void testStatisticsAndTransactions(TestContext& context) {
    NovelRelationProject project;
    const auto a = project.addPerson("A");
    const auto b = project.addPerson("B");
    const auto c = project.addPerson("C");
    const auto d = project.addPerson("D");
    const auto isolated = project.addPerson("孤立人物");

    const auto chapterOne =
        project.addChapter(makeDraft("001", {a, b, c, a}));
    const auto chapterTwo = project.addChapter(makeDraft("002", {a, b}));
    const auto chapterThree = project.addChapter(makeDraft("003", {a, d}));
    context.check(chapterOne == 1 && chapterTwo == 2 && chapterThree == 3,
                  "项目应按成功提交顺序生成章节编号");

    const auto& graph = project.graph();
    context.check(graph.findPerson(a)->chapterCount == 3 &&
                      graph.findPerson(b)->chapterCount == 2 &&
                      graph.findPerson(c)->chapterCount == 1 &&
                      graph.findPerson(d)->chapterCount == 1 &&
                      graph.findPerson(isolated)->chapterCount == 0,
                  "文档样例的人物章节计数应正确");
    context.check(graph.edgeCount() == 4 &&
                      graph.findEdge(a, b)->coChapterCount == 2 &&
                      graph.findEdge(a, c)->coChapterCount == 1 &&
                      graph.findEdge(b, c)->coChapterCount == 1 &&
                      graph.findEdge(a, d)->coChapterCount == 1,
                  "文档样例的共同章节计数应正确");
    context.check(almostEqual(graph.findEdge(a, b)->jaccard, 2.0 / 3.0) &&
                      almostEqual(graph.findEdge(a, c)->jaccard, 1.0 / 3.0) &&
                      almostEqual(graph.findEdge(b, c)->jaccard, 0.5) &&
                      almostEqual(graph.findEdge(a, d)->jaccard, 1.0 / 3.0),
                  "Jaccard 应按 C(A,B)/(C(A)+C(B)-C(A,B)) 计算");

    const auto abId = graph.findEdge(a, b)->id;
    const auto adId = graph.findEdge(a, d)->id;
    const auto* aAddress = graph.findPerson(a);
    project.rebuildStatistics();
    context.check(project.graph().findEdge(a, b)->id == abId &&
                      project.graph().findEdge(a, d)->id == adId,
                  "重复重建应保持仍存在关系的边编号");
    context.check(project.graph().findPerson(a) == aAddress,
                  "统计重建应保留人物对象地址");

    const auto chapterCountBeforeFailure = project.chapters().size();
    const auto edgeCountBeforeFailure = project.graph().edgeCount();
    expectDomainError(context, DomainErrorCode::PersonNotFound,
                      [&project]() {
                          project.addChapter(makeDraft("bad", {9999}));
                      },
                      "章节引用未知人物时应拒绝提交");
    context.check(project.chapters().size() == chapterCountBeforeFailure &&
                      project.graph().edgeCount() == edgeCountBeforeFailure &&
                      project.graph().findEdge(a, b)->id == abId,
                  "失败的章节事务不得修改章节或图");
    expectDomainError(context, DomainErrorCode::PersonNotFound,
                      [&project, chapterOne]() {
                          project.modifyChapter(chapterOne,
                                                makeDraft("001", {9999}));
                      },
                      "修改章节时引用未知人物也应拒绝提交");
    context.check(project.chapters().find(chapterOne) != nullptr &&
                      project.chapters().find(chapterOne)->persons ==
                          std::vector<PersonId>({a, b, c}) &&
                      project.graph().findPerson(a)->chapterCount == 3 &&
                      project.graph().findEdge(a, b)->id == abId,
                  "失败的章节修改不得改变原章节或派生统计");

    const auto emptyChapter = project.addChapter(makeDraft("004", {}));
    context.check(emptyChapter == 4,
                  "失败的章节事务不得消耗章节编号");
    const auto singleChapter =
        project.addChapter(makeDraft("005", {isolated, isolated}));
    context.check(project.graph().findPerson(isolated)->chapterCount == 1 &&
                      project.graph().neighbors(isolated).empty(),
                  "单人物章应只增加人物计数且不产生关系边");
    expectDomainError(context, DomainErrorCode::PersonInUse,
                      [&project, isolated]() {
                          project.removePerson(isolated);
                      },
                      "被章节引用的人物不得删除");

    context.check(project.modifyChapter(chapterThree,
                                        makeDraft("003", {a})),
                  "修改章节人物集合应成功");
    context.check(project.graph().findEdge(a, d) == nullptr,
                  "共同章节数降为零时应删除关系边");
    context.check(project.modifyChapter(chapterThree,
                                        makeDraft("003", {a, d})),
                  "应能再次把人物加入章节");
    context.check(project.graph().findEdge(a, d) != nullptr &&
                      project.graph().findEdge(a, d)->id > adId,
                  "消失后重新出现的关系应获得未复用的新编号");

    context.check(project.removeChapter(singleChapter),
                  "删除单人物章节应成功");
    context.check(project.graph().findPerson(isolated)->chapterCount == 0 &&
                      project.removePerson(isolated),
                  "删除引用章节并重建后应可删除未使用人物");
    context.check(project.removeChapter(emptyChapter),
                  "删除空章节应成功");
    context.check(!project.modifyChapter(9999, makeDraft("不存在", {})) &&
                      !project.removeChapter(9999),
                  "修改或删除不存在章节应返回 false 且不重建");

    expectDomainError(context, DomainErrorCode::DuplicateChapterKey,
                      [&project, a]() {
                          project.addChapter(makeDraft("001", {a}));
                      },
                      "项目事务应拒绝重复章节键");
    context.check(project.validate().isValid(),
                  "合法阶段二项目应通过完整一致性验证");

    context.check(project.removeChapter(chapterOne) &&
                      project.removeChapter(chapterTwo) &&
                      project.removeChapter(chapterThree),
                  "应能删除全部有统计贡献的章节");
    context.check(project.graph().edgeCount() == 0 &&
                      project.graph().findPerson(a)->chapterCount == 0 &&
                      project.graph().findPerson(b)->chapterCount == 0 &&
                      project.graph().findPerson(c)->chapterCount == 0 &&
                      project.graph().findPerson(d)->chapterCount == 0 &&
                      project.validate().isValid(),
                  "清空章节后应删除全部边并把人物计数归零");
}

void testProjectValidationAndRepair(TestContext& context) {
    NovelRelationProject project;
    const auto a = project.addPerson("A");
    const auto b = project.addPerson("B");
    const auto chapterId = project.addChapter(makeDraft("001", {a, b}));
    context.check(project.validate().isValid(),
                  "统计一致的项目应通过验证");

    auto& storedChapter =
        novel::ProjectTestAccess::chapter(project, chapterId);
    storedChapter.id = 42;
    context.check(hasIssue(project.validate(), "chapter.id_index.mismatch"),
                  "章节验证器应识别记录与编号索引不一致");
    storedChapter.id = chapterId;
    storedChapter.chapterKey = "被篡改的章节键";
    context.check(hasIssue(project.validate(), "chapter.key_index.mismatch"),
                  "章节验证器应识别记录与章节键索引不一致");
    storedChapter.chapterKey = "001";
    context.check(project.validate().isValid(),
                  "恢复章节记录后项目应重新通过验证");

    auto& mutableGraph = novel::ProjectTestAccess::graph(project);
    context.check(mutableGraph.setPersonChapterCount(a, 2) &&
                      mutableGraph.validate().isValid(),
                  "测试应构造图内部合法但与章节不一致的统计");
    const auto report = project.validate();
    context.check(!report.isValid() &&
                      hasIssue(report, "project.statistics.person_count.mismatch"),
                  "项目验证器应识别人物计数与章节记录不一致");

    project.rebuildStatistics();
    context.check(project.graph().findPerson(a)->chapterCount == 1 &&
                      project.validate().isValid(),
                  "全量重建应修复派生统计不一致");

    auto& mutableChapters = novel::ProjectTestAccess::chapters(project);
    const auto invalidChapter =
        mutableChapters.add(makeDraft("bad", {9999}));
    const auto invalidChapterReport = project.validate();
    context.check(!invalidChapterReport.isValid() &&
                      hasIssue(invalidChapterReport,
                               "project.chapter.person.missing") &&
                      hasIssue(invalidChapterReport,
                               "project.statistics.rebuild_failed"),
                  "项目验证器应识别章节中的未知人物引用");
    context.check(mutableChapters.remove(invalidChapter) &&
                      project.validate().isValid(),
                  "移除非法测试章节后项目应恢复合法");

    project.addAlias("冲突名", a);
    context.check(mutableGraph.renamePerson(b, "冲突名"),
                  "测试应能绕过项目聚合制造名称冲突");
    const auto aliasConflictReport = project.validate();
    context.check(!aliasConflictReport.isValid() &&
                      hasIssue(aliasConflictReport,
                               "project.alias.name_conflict"),
                  "项目验证器应识别别名与标准人物名冲突");
    context.check(mutableGraph.renamePerson(b, "B") &&
                      project.removeAlias("冲突名") &&
                      project.validate().isValid(),
                  "修复名称冲突后项目应恢复合法");
}

void testLateStatisticsFailureIsAtomic(TestContext& context) {
    NovelRelationProject project;
    const auto a = project.addPerson("A");
    const auto b = project.addPerson("B");
    const auto c = project.addPerson("C");
    auto& graph = novel::ProjectTestAccess::graph(project);
    const auto almostExhausted =
        std::numeric_limits<novel::EdgeId>::max() - 1U;
    novel::GraphTestAccess::setNextEdgeId(graph, almostExhausted);

    expectDomainError(context, DomainErrorCode::IdentifierExhausted,
                      [&project, a, b, c]() {
                          project.addChapter(makeDraft("001", {a, b, c}));
                      },
                      "候选边分配到中途耗尽编号时应拒绝事务");
    context.check(project.chapters().size() == 0 &&
                      project.graph().edgeCount() == 0 &&
                      project.graph().findPerson(a)->chapterCount == 0 &&
                      novel::GraphTestAccess::nextEdgeId(project.graph()) ==
                          almostExhausted &&
                      project.validate().isValid(),
                  "晚期统计构建失败不得提交边、计数或消耗编号");

    const auto firstSuccessful =
        project.addChapter(makeDraft("002", {a}));
    context.check(firstSuccessful == 1 &&
                      project.graph().findPerson(a)->chapterCount == 1 &&
                      project.graph().edgeCount() == 0,
                  "失败的候选章节不得消耗章节编号");
}

}  // namespace

int main() {
    TestContext context;
    testChapterCollection(context);
    testAliasesAndNames(context);
    testStatisticsAndTransactions(context);
    testProjectValidationAndRepair(context);
    testLateStatisticsFailureIsAtomic(context);

    if (context.failures == 0) {
        std::cout << "阶段二章节与统计测试全部通过。\n";
        return 0;
    }
    std::cerr << context.failures << " 项测试失败。\n";
    return 1;
}
