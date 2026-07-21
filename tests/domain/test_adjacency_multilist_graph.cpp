#include "domain/error/DomainError.h"
#include "domain/graph/AdjacencyMultilistGraph.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace novel {

class GraphTestAccess {
public:
    static PersonVertex& person(AdjacencyMultilistGraph& graph, PersonId id) {
        return *graph.persons_.at(id);
    }

    static EdgeNode& edge(AdjacencyMultilistGraph& graph,
                          PersonId first,
                          PersonId second) {
        return *graph.edges_.at(EdgeKey::make(first, second));
    }
};

}  // namespace novel

namespace {

using novel::AdjacencyMultilistGraph;
using novel::DomainError;
using novel::DomainErrorCode;
using novel::EdgeKey;
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

bool almostEqual(double first, double second, double epsilon = 1e-9) {
    return std::abs(first - second) <= epsilon;
}

bool hasIssue(const ValidationReport& report, const std::string& code) {
    return std::any_of(report.issues.begin(), report.issues.end(),
                       [&code](const novel::ValidationIssue& issue) {
                           return issue.code == code;
                       });
}

PersonId addPersonWithCount(TestContext& context,
                            AdjacencyMultilistGraph& graph,
                            const std::string& name,
                            std::uint32_t chapterCount) {
    const PersonId id = graph.addPerson(name);
    context.check(graph.setPersonChapterCount(id, chapterCount),
                  "设置已存在人物的章节数应成功");
    return id;
}

void testPersonLifecycle(TestContext& context) {
    AdjacencyMultilistGraph graph;
    context.check(graph.validate().isValid(), "空图应是合法状态");

    const auto first = graph.addPerson("张三");
    const auto second = graph.addPerson("李四");
    context.check(first == 1 && second == 2, "人物编号应从 1 开始单调生成");
    context.check(graph.personIds() == std::vector<PersonId>({first, second}),
                  "人物枚举应按编号排序");

    expectDomainError(context, DomainErrorCode::EmptyPersonName,
                      [&graph]() { graph.addPerson(" \t"); },
                      "空白人物名应被拒绝");
    expectDomainError(context, DomainErrorCode::DuplicatePerson,
                      [&graph]() { graph.addPerson("张三"); },
                      "重复人物名应被拒绝");

    context.check(graph.renamePerson(first, "张无忌"), "人物重命名应成功");
    context.check(graph.findPerson(first) != nullptr &&
                      graph.findPerson(first)->canonicalName == "张无忌",
                  "重命名结果应写入人物对象");
    context.check(!graph.renamePerson(9999, "不存在"),
                  "重命名不存在的人物应返回 false");
    expectDomainError(context, DomainErrorCode::DuplicatePerson,
                      [&graph, first]() { graph.renamePerson(first, "李四"); },
                      "重命名不得产生重复人物名");

    context.check(graph.setPersonChapterCount(first, 5),
                  "设置人物章节数应成功");
    context.check(!graph.setPersonChapterCount(9999, 5),
                  "设置不存在人物的章节数应返回 false");
    context.check(graph.findPerson(first)->chapterCount == 5,
                  "人物章节数应被保存");

    context.check(graph.removePerson(second), "删除已存在人物应成功");
    context.check(!graph.removePerson(second), "重复删除人物应返回 false");
    const auto third = graph.addPerson("王五");
    context.check(third == 3, "删除人物后不得复用其编号");
    context.check(graph.personCount() == 2 && graph.validate().isValid(),
                  "人物增删改后图结构应保持合法");
}

void testInsertionAndJaccard(TestContext& context) {
    AdjacencyMultilistGraph graph;
    const auto a = addPersonWithCount(context, graph, "A", 5);
    const auto b = addPersonWithCount(context, graph, "B", 4);
    const auto c = addPersonWithCount(context, graph, "C", 5);
    const auto isolated = addPersonWithCount(context, graph, "孤立人物", 0);

    const auto& ab = graph.insertEdge(b, a, 3);
    context.check(ab.endpointA == a && ab.endpointB == b,
                  "边端点应按人物编号规范化");
    context.check(almostEqual(ab.jaccard, 0.5),
                  "5、4、3 对应的 Jaccard 应为 0.5");
    context.check(graph.findEdge(a, b) == &ab && graph.findEdge(b, a) == &ab,
                  "正反向查找应返回同一个边结点");
    context.check(graph.findPerson(a)->firstEdge == &ab &&
                      graph.findPerson(b)->firstEdge == &ab,
                  "一条边结点应同时挂入两个端点边链");

    const auto& ac = graph.insertEdge(a, c, 5);
    context.check(ab.id == 1 && ac.id == 2, "边编号应从 1 开始单调生成");
    context.check(almostEqual(ac.jaccard, 1.0),
                  "两个人物每次共同出现时 Jaccard 应为 1");
    context.check(graph.neighbors(a) == std::vector<PersonId>({b, c}),
                  "邻居应按人物编号排序");
    context.check(graph.neighbors(isolated).empty(), "孤立人物的邻居应为空");
    context.check(graph.edgeKeys() == std::vector<EdgeKey>({{a, b}, {a, c}}),
                  "关系边枚举应按规范化端点排序");

    const auto edgeCountBeforeFailures = graph.edgeCount();
    expectDomainError(context, DomainErrorCode::DuplicateEdge,
                      [&graph, a, b]() { graph.insertEdge(b, a, 3); },
                      "反向重复边应被拒绝");
    expectDomainError(context, DomainErrorCode::SelfLoop,
                      [&graph, a]() { graph.insertEdge(a, a, 1); },
                      "人物自环应被拒绝");
    expectDomainError(context, DomainErrorCode::PersonNotFound,
                      [&graph, a]() { graph.insertEdge(a, 9999, 1); },
                      "未知端点应被拒绝");
    expectDomainError(context, DomainErrorCode::InvalidCoChapterCount,
                      [&graph, b, c]() { graph.insertEdge(b, c, 0); },
                      "零共同章节数应被拒绝");
    expectDomainError(context, DomainErrorCode::InvalidStatistics,
                      [&graph, b, c]() { graph.insertEdge(b, c, 5); },
                      "共同章节数超过人物章节数时应被拒绝");
    expectDomainError(context, DomainErrorCode::PersonNotFound,
                      [&graph]() { static_cast<void>(graph.neighbors(9999)); },
                      "查询未知人物邻居应给出明确错误");
    context.check(graph.edgeCount() == edgeCountBeforeFailures,
                  "失败的插边请求不得修改图");

    context.check(graph.setPersonChapterCount(a, 6),
                  "修改人物章节数应成功");
    context.check(almostEqual(graph.findEdge(a, b)->jaccard, 3.0 / 7.0) &&
                      almostEqual(graph.findEdge(a, c)->jaccard, 5.0 / 6.0),
                  "修改人物章节数后应重算全部关联边的 Jaccard");
    expectDomainError(context, DomainErrorCode::InvalidStatistics,
                      [&graph, c]() { graph.setPersonChapterCount(c, 4); },
                      "人物章节数不得低于已有共同章节数");
    context.check(graph.findPerson(c)->chapterCount == 5,
                  "失败的章节数修改不得改变人物统计");
    context.check(graph.validate().isValid(), "合法插边和统计修改后验证应通过");
}

void testDeletionPositionsAndDisconnectedGraphs(TestContext& context) {
    AdjacencyMultilistGraph graph;
    const auto a = addPersonWithCount(context, graph, "A", 10);
    const auto b = addPersonWithCount(context, graph, "B", 10);
    const auto c = addPersonWithCount(context, graph, "C", 10);
    const auto d = addPersonWithCount(context, graph, "D", 10);

    graph.insertEdge(a, b, 1);  // A 链尾
    graph.insertEdge(a, c, 1);  // A 链中
    graph.insertEdge(a, d, 1);  // A 链首
    context.check(graph.deleteEdge(a, c) && graph.validate().isValid(),
                  "应能删除边链中间结点");
    context.check(graph.deleteEdge(a, d) && graph.validate().isValid(),
                  "应能删除边链头结点");
    context.check(graph.deleteEdge(a, b) && graph.validate().isValid(),
                  "应能删除边链尾结点");
    context.check(!graph.deleteEdge(a, b), "删除不存在的边应返回 false");
    context.check(graph.edgeCount() == 0 && graph.personCount() == 4,
                  "删完边后孤立人物仍应保留");

    AdjacencyMultilistGraph endpointBGraph;
    const auto lowA = addPersonWithCount(context, endpointBGraph, "低 A", 10);
    const auto lowB = addPersonWithCount(context, endpointBGraph, "低 B", 10);
    const auto lowC = addPersonWithCount(context, endpointBGraph, "低 C", 10);
    const auto high = addPersonWithCount(context, endpointBGraph, "高端点", 10);
    endpointBGraph.insertEdge(lowA, high, 1);  // high 的 linkB 链尾
    endpointBGraph.insertEdge(lowB, high, 1);  // high 的 linkB 链中
    endpointBGraph.insertEdge(lowC, high, 1);  // high 的 linkB 链首
    context.check(endpointBGraph.deleteEdge(lowB, high) &&
                      endpointBGraph.validate().isValid(),
                  "应能删除 linkB 边链中间结点");
    context.check(endpointBGraph.deleteEdge(lowC, high) &&
                      endpointBGraph.validate().isValid(),
                  "应能删除 linkB 边链头结点");
    context.check(endpointBGraph.deleteEdge(lowA, high) &&
                      endpointBGraph.validate().isValid(),
                  "应能删除 linkB 边链尾结点");

    AdjacencyMultilistGraph bridgeGraph;
    const auto bridgeA = addPersonWithCount(context, bridgeGraph, "桥端点 A", 1);
    const auto bridgeB = addPersonWithCount(context, bridgeGraph, "桥端点 B", 1);
    bridgeGraph.insertEdge(bridgeA, bridgeB, 1);
    context.check(bridgeGraph.deleteEdge(bridgeA, bridgeB),
                  "阶段一图允许删除桥边");
    context.check(bridgeGraph.validate().isValid(),
                  "删除桥边形成非连通图后仍应合法");

    AdjacencyMultilistGraph disconnected;
    const auto one = addPersonWithCount(context, disconnected, "一", 2);
    const auto two = addPersonWithCount(context, disconnected, "二", 2);
    const auto three = addPersonWithCount(context, disconnected, "三", 2);
    const auto four = addPersonWithCount(context, disconnected, "四", 2);
    disconnected.insertEdge(one, two, 1);
    disconnected.insertEdge(three, four, 1);
    context.check(disconnected.validate().isValid(),
                  "多个连通分量应被视为合法图");
}

void testRemovePersonAndStableIds(TestContext& context) {
    AdjacencyMultilistGraph graph;
    const auto hub = addPersonWithCount(context, graph, "中心", 5);
    const auto b = addPersonWithCount(context, graph, "B", 5);
    const auto c = addPersonWithCount(context, graph, "C", 5);
    const auto d = addPersonWithCount(context, graph, "D", 5);
    graph.insertEdge(hub, b, 2);
    graph.insertEdge(hub, c, 2);
    graph.insertEdge(hub, d, 2);
    const auto& surviving = graph.insertEdge(b, c, 1);
    const auto survivingId = surviving.id;

    context.check(graph.removePerson(hub), "删除人物应成功");
    context.check(graph.findPerson(hub) == nullptr &&
                      graph.findEdge(hub, b) == nullptr &&
                      graph.findEdge(hub, c) == nullptr &&
                      graph.findEdge(hub, d) == nullptr,
                  "删除人物应清理其全部关联边");
    context.check(graph.edgeCount() == 1 &&
                      graph.findEdge(b, c) != nullptr &&
                      graph.findEdge(b, c)->id == survivingId,
                  "删除人物不得影响无关关系边");

    const auto& nextEdge = graph.insertEdge(b, d, 1);
    context.check(nextEdge.id == 5, "删除关系边后不得复用边编号");
    context.check(graph.validate().isValid(), "删除人物后图结构应保持合法");
}

void testValidationDiagnostics(TestContext& context) {
    {
        AdjacencyMultilistGraph graph;
        const auto a = addPersonWithCount(context, graph, "A", 3);
        const auto b = addPersonWithCount(context, graph, "B", 3);
        const auto c = addPersonWithCount(context, graph, "C", 3);
        graph.insertEdge(a, b, 1);
        graph.insertEdge(a, c, 1);
        auto& ac = novel::GraphTestAccess::edge(graph, a, c);
        ac.linkA = &ac;
        const auto report = graph.validate();
        context.check(!report.isValid() && hasIssue(report, "edge.chain.cycle"),
                      "验证器应检测人物边链环");
    }
    {
        AdjacencyMultilistGraph graph;
        const auto a = addPersonWithCount(context, graph, "A", 3);
        const auto b = addPersonWithCount(context, graph, "B", 3);
        const auto c = addPersonWithCount(context, graph, "C", 3);
        const auto d = addPersonWithCount(context, graph, "D", 3);
        graph.insertEdge(a, b, 1);
        graph.insertEdge(c, d, 1);
        auto& cd = novel::GraphTestAccess::edge(graph, c, d);
        novel::GraphTestAccess::person(graph, a).firstEdge = &cd;
        const auto report = graph.validate();
        context.check(!report.isValid() &&
                          hasIssue(report, "edge.chain.unrelated_vertex"),
                      "验证器应检测挂入无关人物边链的关系边");
    }
    {
        AdjacencyMultilistGraph graph;
        const auto a = addPersonWithCount(context, graph, "A", 5);
        const auto b = addPersonWithCount(context, graph, "B", 4);
        graph.insertEdge(a, b, 3);
        auto& edge = novel::GraphTestAccess::edge(graph, a, b);
        edge.jaccard = 1.5;
        const auto rangeReport = graph.validate();
        context.check(!rangeReport.isValid() &&
                          hasIssue(rangeReport, "edge.jaccard.range"),
                      "验证器应检测越界的 Jaccard 值");

        edge.jaccard = 0.0;
        context.check(hasIssue(graph.validate(), "edge.jaccard.range"),
                      "验证器应拒绝零 Jaccard 值");
        edge.jaccard = -0.1;
        context.check(hasIssue(graph.validate(), "edge.jaccard.range"),
                      "验证器应拒绝负 Jaccard 值");
        edge.jaccard = std::numeric_limits<double>::quiet_NaN();
        context.check(hasIssue(graph.validate(), "edge.jaccard.range"),
                      "验证器应拒绝 NaN Jaccard 值");
        edge.jaccard = std::numeric_limits<double>::infinity();
        context.check(hasIssue(graph.validate(), "edge.jaccard.range"),
                      "验证器应拒绝无穷大 Jaccard 值");
        edge.jaccard = 1.0 + 5e-10;
        context.check(hasIssue(graph.validate(), "edge.jaccard.range"),
                      "Jaccard 即使只略大于 1 也应被拒绝");

        edge.jaccard = 0.5;
        edge.coChapterCount = 0;
        const auto countReport = graph.validate();
        context.check(!countReport.isValid() &&
                          hasIssue(countReport, "edge.co_count.invalid"),
                      "验证器应检测非法共同章节数");
    }
    {
        AdjacencyMultilistGraph graph;
        constexpr auto maximumCount = std::numeric_limits<std::uint32_t>::max();
        const auto a = addPersonWithCount(context, graph, "极大 A", maximumCount);
        const auto b = addPersonWithCount(context, graph, "极大 B", maximumCount);
        graph.insertEdge(a, b, 1);
        auto& edge = novel::GraphTestAccess::edge(graph, a, b);
        const double expected = 1.0 /
            (2.0 * static_cast<double>(maximumCount) - 1.0);
        context.check(almostEqual(edge.jaccard, expected, 1e-18),
                      "极大章节数下仍应正确计算很小的 Jaccard 值");
        edge.jaccard = expected + 2e-9;
        context.check(hasIssue(graph.validate(), "edge.jaccard.mismatch"),
                      "Jaccard 与公式相差超过 1e-9 时应被拒绝");
    }
}

}  // namespace

int main() {
    TestContext context;
    testPersonLifecycle(context);
    testInsertionAndJaccard(context);
    testDeletionPositionsAndDisconnectedGraphs(context);
    testRemovePersonAndStableIds(context);
    testValidationDiagnostics(context);

    if (context.failures == 0) {
        std::cout << "阶段一领域数据结构测试全部通过。\n";
        return 0;
    }
    std::cerr << context.failures << " 项测试失败。\n";
    return 1;
}
