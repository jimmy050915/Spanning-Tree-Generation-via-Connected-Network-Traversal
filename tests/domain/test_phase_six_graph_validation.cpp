#include "domain/error/DomainError.h"
#include "domain/graph/AdjacencyMultilistGraph.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>

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
using novel::EdgeNode;
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

bool hasIssue(const ValidationReport& report, const std::string& code) {
    return std::any_of(report.issues.begin(), report.issues.end(),
                       [&code](const novel::ValidationIssue& issue) {
                           return issue.code == code;
                       });
}

void expectValidationIssue(TestContext& context,
                           const AdjacencyMultilistGraph& graph,
                           const std::string& code,
                           const std::string& message) {
    const auto report = graph.validate();
    context.check(!report.isValid() && hasIssue(report, code), message);
}

struct PairIds {
    PersonId first{};
    PersonId second{};
};

PairIds buildValidPair(TestContext& context,
                       AdjacencyMultilistGraph& graph,
                       const std::string& namePrefix) {
    const auto first = graph.addPerson(namePrefix + "甲");
    const auto second = graph.addPerson(namePrefix + "乙");
    context.check(graph.setPersonChapterCount(first, 3) &&
                      graph.setPersonChapterCount(second, 3),
                  namePrefix + "：有效基线应能设置人物章节数");
    static_cast<void>(graph.insertEdge(first, second, 1));
    context.check(graph.validate().isValid(),
                  namePrefix + "：破坏前的小图应通过验证");
    return PairIds{first, second};
}

void testInvalidChainPointer(TestContext& context) {
    EdgeNode unownedEdge{};
    AdjacencyMultilistGraph graph;
    const auto ids = buildValidPair(context, graph, "无效边链指针");

    // 哨兵只作为不受图所有的地址；测试与验证器都不会解引用它。
    novel::GraphTestAccess::person(graph, ids.first).firstEdge = &unownedEdge;
    expectValidationIssue(context, graph, "edge.chain.invalid_pointer",
                          "验证器应报告人物边链指向非图所有边");
}

void testInvalidEdgeLink(TestContext& context) {
    EdgeNode unownedEdge{};
    AdjacencyMultilistGraph graph;
    const auto ids = buildValidPair(context, graph, "无效 link 指针");

    // linkA 的哨兵地址在所有权检查失败后即停止处理，不会被解引用。
    novel::GraphTestAccess::edge(graph, ids.first, ids.second).linkA =
        &unownedEdge;
    expectValidationIssue(context, graph, "edge.link.invalid",
                          "验证器应报告关系边 link 指向非图所有边");
}

void testMissingEndpoint(TestContext& context) {
    AdjacencyMultilistGraph graph;
    const auto ids = buildValidPair(context, graph, "缺失端点");

    novel::GraphTestAccess::edge(graph, ids.first, ids.second).endpointB = 9999;
    expectValidationIssue(context, graph, "edge.endpoint.missing",
                          "验证器应报告关系边引用不存在人物");
}

void testMissingChainOccurrence(TestContext& context) {
    AdjacencyMultilistGraph graph;
    const auto ids = buildValidPair(context, graph, "边链出现次数");

    novel::GraphTestAccess::person(graph, ids.second).firstEdge = nullptr;
    expectValidationIssue(context, graph, "edge.chain.occurrence",
                          "验证器应报告关系边未在两个端点边链各出现一次");
}

void testDuplicateEdgeKey(TestContext& context) {
    AdjacencyMultilistGraph graph;
    const auto first = graph.addPerson("重复键甲");
    const auto second = graph.addPerson("重复键乙");
    const auto third = graph.addPerson("重复键丙");
    context.check(graph.setPersonChapterCount(first, 3) &&
                      graph.setPersonChapterCount(second, 3) &&
                      graph.setPersonChapterCount(third, 3),
                  "重复键：有效基线应能设置人物章节数");
    static_cast<void>(graph.insertEdge(first, second, 1));
    static_cast<void>(graph.insertEdge(first, third, 1));
    context.check(graph.validate().isValid(),
                  "重复键：破坏前的小图应通过验证");

    novel::GraphTestAccess::edge(graph, first, third).endpointB = second;
    expectValidationIssue(context, graph, "edge.key.duplicate",
                          "验证器应报告规范化端点相同的重复关系边");
}

void testSelfLoop(TestContext& context) {
    AdjacencyMultilistGraph graph;
    const auto ids = buildValidPair(context, graph, "自环");

    novel::GraphTestAccess::edge(graph, ids.first, ids.second).endpointB =
        ids.first;
    expectValidationIssue(context, graph, "edge.self_loop",
                          "验证器应报告关系边自环");
}

void testCoCountExceedsPersonCount(TestContext& context) {
    AdjacencyMultilistGraph graph;
    const auto ids = buildValidPair(context, graph, "共同出现次数越界");

    novel::GraphTestAccess::edge(graph, ids.first, ids.second).coChapterCount = 4;
    expectValidationIssue(context, graph, "edge.co_count.exceeds_person",
                          "验证器应报告共同章节数超过端点人物章节数");
}

void testZeroCoOccurrenceDoesNotCreateEdge(TestContext& context) {
    AdjacencyMultilistGraph graph;
    const auto first = graph.addPerson("从未共同出现甲");
    const auto second = graph.addPerson("从未共同出现乙");
    context.check(graph.setPersonChapterCount(first, 2) &&
                      graph.setPersonChapterCount(second, 2),
                  "零共现合同：应能设置各自的人物章节数");

    expectDomainError(
        context, DomainErrorCode::InvalidCoChapterCount,
        [&graph, first, second]() {
            static_cast<void>(graph.insertEdge(first, second, 0));
        },
        "从未共同出现的人物不得通过 coCount=0 创建关系边");
    context.check(graph.edgeCount() == 0 &&
                      graph.findEdge(first, second) == nullptr &&
                      graph.validate().isValid(),
                  "被拒绝的零共现插边不得留下关系边或破坏图状态");
}

}  // namespace

int main() {
    TestContext context;
    testInvalidChainPointer(context);
    testInvalidEdgeLink(context);
    testMissingEndpoint(context);
    testMissingChainOccurrence(context);
    testDuplicateEdgeKey(context);
    testSelfLoop(context);
    testCoCountExceedsPersonCount(context);
    testZeroCoOccurrenceDoesNotCreateEdge(context);

    if (context.failures == 0) {
        std::cout << "阶段六图结构验证测试全部通过。\n";
        return 0;
    }
    std::cerr << context.failures << " 项测试失败。\n";
    return 1;
}
