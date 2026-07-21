#include "domain/error/DomainError.h"
#include "domain/graph/AdjacencyMultilistGraph.h"
#include "domain/traversal/ChildListTree.h"
#include "domain/traversal/GraphTraversal.h"
#include "domain/traversal/IGraphView.h"

#include <cstddef>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using novel::AdjacencyMultilistGraph;
using novel::ChildListTree;
using novel::DomainError;
using novel::DomainErrorCode;
using novel::IGraphView;
using novel::PersonId;
using novel::TraversalResult;
using novel::TraversalScope;
using novel::breadthFirstSearch;
using novel::depthFirstSearch;
using novel::kAllPersonsRootId;

static_assert(!std::is_copy_constructible_v<ChildListTree>,
              "孩子链表树不得被复制");
static_assert(!std::is_copy_assignable_v<ChildListTree>,
              "孩子链表树不得被复制赋值");
static_assert(std::is_nothrow_move_constructible_v<ChildListTree>,
              "孩子链表树应支持 noexcept 移动构造");
static_assert(std::is_nothrow_move_assignable_v<ChildListTree>,
              "孩子链表树应支持 noexcept 移动赋值");

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
        context.check(error.code() == expectedCode,
                      message + "（错误码不正确）");
    } catch (...) {
        context.check(false, message + "（抛出了错误的异常类型）");
    }
}

PersonId addPerson(AdjacencyMultilistGraph& graph, const std::string& name) {
    const auto id = graph.addPerson(name);
    graph.setPersonChapterCount(id, 100);
    return id;
}

void addEdge(AdjacencyMultilistGraph& graph,
             PersonId first,
             PersonId second) {
    graph.insertEdge(first, second, 1);
}

void checkChildren(TestContext& context,
                   const ChildListTree& tree,
                   PersonId parent,
                   const std::vector<PersonId>& expected,
                   const std::string& message) {
    context.check(tree.findNode(parent) != nullptr,
                  message + "（父结点不存在）");
    if (tree.findNode(parent) != nullptr) {
        context.check(tree.children(parent) == expected, message);
    }
}

std::size_t treeEdgeCount(const TraversalResult& result) {
    std::size_t count{};
    if (result.tree.findNode(kAllPersonsRootId) != nullptr) {
        count += result.tree.children(kAllPersonsRootId).size();
    }
    for (const auto id : result.order) {
        count += result.tree.children(id).size();
    }
    return count;
}

void checkEquivalentTrees(TestContext& context,
                          const TraversalResult& first,
                          const TraversalResult& second,
                          const std::string& message) {
    context.check(first.order == second.order, message + "（访问序列不同）");
    const bool firstHasVirtualRoot =
        first.tree.findNode(kAllPersonsRootId) != nullptr;
    const bool secondHasVirtualRoot =
        second.tree.findNode(kAllPersonsRootId) != nullptr;
    context.check(firstHasVirtualRoot == secondHasVirtualRoot,
                  message + "（虚拟根状态不同）");
    if (firstHasVirtualRoot && secondHasVirtualRoot) {
        context.check(first.tree.children(kAllPersonsRootId) ==
                          second.tree.children(kAllPersonsRootId),
                      message + "（分量根不同）");
    }
    for (const auto id : first.order) {
        context.check(second.tree.findNode(id) != nullptr &&
                          first.tree.children(id) == second.tree.children(id),
                      message + "（人物结点孩子不同）");
    }
}

void testChildListTree(TestContext& context) {
    ChildListTree tree;
    tree.createNode(kAllPersonsRootId);
    tree.createNode(1);
    tree.createNode(2);
    tree.createNode(3);
    tree.createNode(4);
    tree.addChild(kAllPersonsRootId, 1);
    tree.addChild(kAllPersonsRootId, 4);
    tree.addChild(1, 2);
    tree.addChild(1, 3);

    context.check(tree.findNode(9999) == nullptr,
                  "查找不存在的树结点应返回 nullptr");
    checkChildren(context, tree, kAllPersonsRootId, {1, 4},
                  "虚拟根孩子应按追加顺序保存");
    checkChildren(context, tree, 1, {2, 3},
                  "人物结点孩子应按追加顺序保存");
    checkChildren(context, tree, 2, {}, "叶结点应返回空孩子列表");

    expectDomainError(context, DomainErrorCode::DuplicateTreeNode,
                      [&tree]() { tree.createNode(1); },
                      "重复创建树结点应被拒绝");
    expectDomainError(context, DomainErrorCode::TreeNodeNotFound,
                      [&tree]() { tree.addChild(9999, 2); },
                      "关系的父结点必须存在");
    expectDomainError(context, DomainErrorCode::TreeNodeNotFound,
                      [&tree]() { tree.addChild(2, 9999); },
                      "关系的子结点必须存在");
    expectDomainError(context, DomainErrorCode::TreeNodeNotFound,
                      [&tree]() { static_cast<void>(tree.children(9999)); },
                      "读取不存在结点的孩子应给出明确错误");
    expectDomainError(context, DomainErrorCode::InvalidTreeRelation,
                      [&tree]() { tree.addChild(2, 2); },
                      "树中不得添加自环");
    expectDomainError(context, DomainErrorCode::InvalidTreeRelation,
                      [&tree]() { tree.addChild(1, 2); },
                      "树中不得添加重复父子关系");
    expectDomainError(context, DomainErrorCode::InvalidTreeRelation,
                      [&tree]() { tree.addChild(4, 2); },
                      "一个树结点不得拥有多个父结点");
    checkChildren(context, tree, 1, {2, 3},
                  "失败的树关系请求不得改变孩子链");

    ChildListTree cyclic;
    cyclic.createNode(10);
    cyclic.createNode(11);
    cyclic.createNode(12);
    cyclic.addChild(10, 11);
    cyclic.addChild(11, 12);
    expectDomainError(context, DomainErrorCode::InvalidTreeRelation,
                      [&cyclic]() { cyclic.addChild(12, 10); },
                      "树中不得添加成环关系");

    ChildListTree moved(std::move(tree));
    checkChildren(context, moved, kAllPersonsRootId, {1, 4},
                  "移动构造应保留孩子链");
    ChildListTree assigned;
    assigned = std::move(moved);
    checkChildren(context, assigned, 1, {2, 3},
                  "移动赋值应保留孩子链");
}

void testSingletonAndChain(TestContext& context) {
    AdjacencyMultilistGraph singleton;
    const auto only = addPerson(singleton, "唯一人物");

    const auto dfsComponent =
        depthFirstSearch(singleton, only, TraversalScope::ReachableComponent);
    const auto bfsComponent =
        breadthFirstSearch(singleton, only, TraversalScope::ReachableComponent);
    context.check(dfsComponent.order == std::vector<PersonId>({only}) &&
                      bfsComponent.order == std::vector<PersonId>({only}),
                  "单顶点分量的 DFS/BFS 都应只访问起点");
    context.check(dfsComponent.tree.findNode(kAllPersonsRootId) == nullptr &&
                      bfsComponent.tree.findNode(kAllPersonsRootId) == nullptr,
                  "分量遍历树不得包含虚拟根");
    context.check(treeEdgeCount(dfsComponent) == 0 &&
                      treeEdgeCount(bfsComponent) == 0,
                  "单顶点分量遍历树不得包含边");

    const auto defaultResult = depthFirstSearch(singleton, only);
    context.check(defaultResult.order == std::vector<PersonId>({only}),
                  "默认 DFS 应执行完整图遍历");
    checkChildren(context, defaultResult.tree, kAllPersonsRootId, {only},
                  "完整遍历应把单顶点分量挂到虚拟根");
    context.check(treeEdgeCount(defaultResult) == 1,
                  "含虚拟根的单顶点完整遍历树应有一条边");

    AdjacencyMultilistGraph chain;
    const auto one = addPerson(chain, "链 1");
    const auto two = addPerson(chain, "链 2");
    const auto three = addPerson(chain, "链 3");
    const auto four = addPerson(chain, "链 4");
    addEdge(chain, one, two);
    addEdge(chain, two, three);
    addEdge(chain, three, four);

    const auto dfs =
        depthFirstSearch(chain, two, TraversalScope::ReachableComponent);
    const auto bfs =
        breadthFirstSearch(chain, two, TraversalScope::ReachableComponent);
    const std::vector<PersonId> expected{two, one, three, four};
    context.check(dfs.order == expected && bfs.order == expected,
                  "从链中间开始时 DFS/BFS 应稳定地优先低编号邻居");
    checkChildren(context, dfs.tree, two, {one, three},
                  "链式 DFS 应记录起点的两个首次发现孩子");
    checkChildren(context, dfs.tree, three, {four},
                  "链式 DFS 应记录后续首次发现关系");
    checkChildren(context, bfs.tree, two, {one, three},
                  "链式 BFS 应记录起点的两个首次发现孩子");
    checkChildren(context, bfs.tree, three, {four},
                  "链式 BFS 应记录后续首次发现关系");
    context.check(treeEdgeCount(dfs) == expected.size() - 1U &&
                      treeEdgeCount(bfs) == expected.size() - 1U,
                  "连通分量遍历树边数应为访问顶点数减一");
}

void testCycleAndStar(TestContext& context) {
    AdjacencyMultilistGraph cycle;
    const auto one = addPerson(cycle, "环 1");
    const auto two = addPerson(cycle, "环 2");
    const auto three = addPerson(cycle, "环 3");
    addEdge(cycle, two, three);
    addEdge(cycle, one, three);
    addEdge(cycle, one, two);

    const auto dfs =
        depthFirstSearch(cycle, one, TraversalScope::ReachableComponent);
    const auto bfs =
        breadthFirstSearch(cycle, one, TraversalScope::ReachableComponent);
    context.check(dfs.order == std::vector<PersonId>({one, two, three}) &&
                      bfs.order == std::vector<PersonId>({one, two, three}),
                  "环图遍历不得重复访问人物");
    checkChildren(context, dfs.tree, one, {two},
                  "环图 DFS 应沿最低编号未访问邻居深入");
    checkChildren(context, dfs.tree, two, {three},
                  "环图 DFS 应只记录首次发现边");
    checkChildren(context, bfs.tree, one, {two, three},
                  "环图 BFS 应把同层首次发现人物挂到起点");
    context.check(treeEdgeCount(dfs) == 2 && treeEdgeCount(bfs) == 2,
                  "环图遍历输出仍应是树");

    AdjacencyMultilistGraph star;
    const auto leafOne = addPerson(star, "星 1");
    const auto leafTwo = addPerson(star, "星 2");
    const auto center = addPerson(star, "星中心");
    const auto leafFour = addPerson(star, "星 4");
    const auto leafFive = addPerson(star, "星 5");
    addEdge(star, center, leafFive);
    addEdge(star, center, leafTwo);
    addEdge(star, center, leafFour);
    addEdge(star, center, leafOne);

    const std::vector<PersonId> starOrder{
        center, leafOne, leafTwo, leafFour, leafFive};
    const auto starDfs =
        depthFirstSearch(star, center, TraversalScope::ReachableComponent);
    const auto starBfs =
        breadthFirstSearch(star, center, TraversalScope::ReachableComponent);
    context.check(starDfs.order == starOrder && starBfs.order == starOrder,
                  "星形图应按人物编号访问各叶结点");
    checkChildren(context, starDfs.tree, center,
                  {leafOne, leafTwo, leafFour, leafFive},
                  "星形 DFS 的中心孩子顺序应稳定");
    checkChildren(context, starBfs.tree, center,
                  {leafOne, leafTwo, leafFour, leafFive},
                  "星形 BFS 的中心孩子顺序应稳定");
}

void testBranchingOrders(TestContext& context) {
    AdjacencyMultilistGraph graph;
    const auto one = addPerson(graph, "分支 1");
    const auto two = addPerson(graph, "分支 2");
    const auto three = addPerson(graph, "分支 3");
    const auto four = addPerson(graph, "分支 4");
    const auto five = addPerson(graph, "分支 5");
    const auto six = addPerson(graph, "分支 6");

    addEdge(graph, three, six);
    addEdge(graph, two, five);
    addEdge(graph, one, three);
    addEdge(graph, two, four);
    addEdge(graph, one, two);

    const auto dfs =
        depthFirstSearch(graph, one, TraversalScope::ReachableComponent);
    const auto bfs =
        breadthFirstSearch(graph, one, TraversalScope::ReachableComponent);
    context.check(dfs.order ==
                      std::vector<PersonId>({one, two, four, five, three, six}),
                  "分支图 DFS 应模拟递归式低编号优先顺序");
    context.check(bfs.order ==
                      std::vector<PersonId>({one, two, three, four, five, six}),
                  "分支图 BFS 应按层及低编号顺序访问");
    checkChildren(context, dfs.tree, one, {two, three},
                  "分支图 DFS 的根孩子应按发现顺序保存");
    checkChildren(context, dfs.tree, two, {four, five},
                  "分支图 DFS 的第二层孩子应正确");
    checkChildren(context, dfs.tree, three, {six},
                  "分支图 DFS 应保存另一分支");
    checkChildren(context, bfs.tree, one, {two, three},
                  "分支图 BFS 的根孩子应正确");
    checkChildren(context, bfs.tree, two, {four, five},
                  "分支图 BFS 的第二层孩子应正确");
    checkChildren(context, bfs.tree, three, {six},
                  "分支图 BFS 应保存另一分支");
}

void testDisconnectedScopes(TestContext& context) {
    AdjacencyMultilistGraph graph;
    const auto one = addPerson(graph, "分量 1");
    const auto two = addPerson(graph, "分量 2");
    const auto isolatedThree = addPerson(graph, "孤立 3");
    const auto four = addPerson(graph, "分量 4");
    const auto five = addPerson(graph, "分量 5");
    const auto six = addPerson(graph, "分量 6");
    const auto isolatedSeven = addPerson(graph, "孤立 7");
    addEdge(graph, one, two);
    addEdge(graph, four, five);
    addEdge(graph, five, six);

    const auto reachableDfs =
        depthFirstSearch(graph, five, TraversalScope::ReachableComponent);
    const auto reachableBfs =
        breadthFirstSearch(graph, five, TraversalScope::ReachableComponent);
    const std::vector<PersonId> reachableOrder{five, four, six};
    context.check(reachableDfs.order == reachableOrder &&
                      reachableBfs.order == reachableOrder,
                  "分量模式只能访问起点所在连通分量");
    context.check(reachableDfs.tree.findNode(kAllPersonsRootId) == nullptr &&
                      reachableBfs.tree.findNode(kAllPersonsRootId) == nullptr,
                  "分量模式不得创建虚拟根");
    context.check(treeEdgeCount(reachableDfs) == reachableOrder.size() - 1U &&
                      treeEdgeCount(reachableBfs) == reachableOrder.size() - 1U,
                  "分量模式树边数应为已访问顶点数减一");

    const std::vector<PersonId> allOrder{
        five, four, six, one, two, isolatedThree, isolatedSeven};
    const auto allDfs = depthFirstSearch(graph, five);
    const auto allBfs = breadthFirstSearch(graph, five);
    context.check(allDfs.order == allOrder && allBfs.order == allOrder,
                  "完整遍历应先访问起点分量，再按顶点编号扫描剩余分量");
    checkChildren(context, allDfs.tree, kAllPersonsRootId,
                  {five, one, isolatedThree, isolatedSeven},
                  "完整 DFS 的分量根顺序应稳定");
    checkChildren(context, allBfs.tree, kAllPersonsRootId,
                  {five, one, isolatedThree, isolatedSeven},
                  "完整 BFS 的分量根顺序应稳定");
    checkChildren(context, allDfs.tree, five, {four, six},
                  "完整 DFS 应保存起点分量的树结构");
    checkChildren(context, allDfs.tree, one, {two},
                  "完整 DFS 应保存后续分量的树结构");
    checkChildren(context, allBfs.tree, five, {four, six},
                  "完整 BFS 应保存起点分量的树结构");
    checkChildren(context, allBfs.tree, one, {two},
                  "完整 BFS 应保存后续分量的树结构");
    context.check(treeEdgeCount(allDfs) == allOrder.size() &&
                      treeEdgeCount(allBfs) == allOrder.size(),
                  "含虚拟根的完整遍历树边数应等于真实顶点数");
}

void testFailuresAndNonMutation(TestContext& context) {
    AdjacencyMultilistGraph empty;
    expectDomainError(context, DomainErrorCode::PersonNotFound,
                      [&empty]() {
                          static_cast<void>(depthFirstSearch(empty, 1));
                      },
                      "空图 DFS 应报告起点不存在");
    expectDomainError(context, DomainErrorCode::PersonNotFound,
                      [&empty]() {
                          static_cast<void>(breadthFirstSearch(empty, 1));
                      },
                      "空图 BFS 应报告起点不存在");

    AdjacencyMultilistGraph graph;
    const auto one = addPerson(graph, "不变 1");
    const auto two = addPerson(graph, "不变 2");
    const auto three = addPerson(graph, "不变 3");
    addEdge(graph, one, two);
    addEdge(graph, two, three);

    expectDomainError(context, DomainErrorCode::PersonNotFound,
                      [&graph]() {
                          static_cast<void>(depthFirstSearch(
                              graph, 9999,
                              TraversalScope::ReachableComponent));
                      },
                      "未知起点的分量 DFS 应被拒绝");
    expectDomainError(context, DomainErrorCode::PersonNotFound,
                      [&graph]() {
                          static_cast<void>(breadthFirstSearch(graph, 9999));
                      },
                      "未知起点的完整 BFS 应被拒绝");

    const auto personsBefore = graph.personIds();
    const auto edgesBefore = graph.edgeKeys();
    std::vector<std::vector<PersonId>> neighborsBefore;
    for (const auto id : personsBefore) {
        neighborsBefore.push_back(graph.neighbors(id));
    }
    const auto dfsFirst = depthFirstSearch(graph, one);
    const auto dfsSecond = depthFirstSearch(graph, one);
    const auto bfsFirst = breadthFirstSearch(graph, one);
    const auto bfsSecond = breadthFirstSearch(graph, one);
    checkEquivalentTrees(context, dfsFirst, dfsSecond,
                         "重复 DFS 应生成等价结果");
    checkEquivalentTrees(context, bfsFirst, bfsSecond,
                         "重复 BFS 应生成等价结果");
    context.check(graph.personIds() == personsBefore &&
                      graph.edgeKeys() == edgesBefore && graph.validate().isValid(),
                  "重复遍历不得修改图的顶点、边或合法状态");
    for (std::size_t index = 0; index < personsBefore.size(); ++index) {
        context.check(graph.neighbors(personsBefore[index]) ==
                          neighborsBefore[index],
                      "重复遍历不得修改人物邻接链");
    }
}

class LongChainGraph final : public IGraphView {
public:
    explicit LongChainGraph(PersonId vertexCount) : vertexCount_(vertexCount) {}

    std::vector<PersonId> vertices() const override {
        std::vector<PersonId> result;
        result.reserve(vertexCount_);
        for (PersonId id = 1; id <= vertexCount_; ++id) {
            result.push_back(id);
        }
        return result;
    }

    std::vector<PersonId> neighbors(PersonId id) const override {
        if (id == 0 || id > vertexCount_) {
            throw DomainError(DomainErrorCode::PersonNotFound,
                              "长链测试图中不存在该人物");
        }
        std::vector<PersonId> result;
        if (id > 1) {
            result.push_back(id - 1U);
        }
        if (id < vertexCount_) {
            result.push_back(id + 1U);
        }
        return result;
    }

private:
    PersonId vertexCount_;
};

void testLongChainUsesIterativeDfs(TestContext& context) {
    constexpr PersonId vertexCount = 50000;
    const LongChainGraph graph(vertexCount);
    const auto result =
        depthFirstSearch(graph, 1, TraversalScope::ReachableComponent);
    context.check(result.order.size() == vertexCount &&
                      result.order.front() == 1 &&
                      result.order.back() == vertexCount,
                  "长链 DFS 应在不依赖递归栈的情况下访问全部顶点");
    context.check(treeEdgeCount(result) ==
                      static_cast<std::size_t>(vertexCount - 1U),
                  "长链 DFS 的遍历树边数应正确");
    checkChildren(context, result.tree, vertexCount, {},
                  "长链 DFS 的末端应是叶结点");
}

}  // namespace

int main() {
    TestContext context;
    testChildListTree(context);
    testSingletonAndChain(context);
    testCycleAndStar(context);
    testBranchingOrders(context);
    testDisconnectedScopes(context);
    testFailuresAndNonMutation(context);
    testLongChainUsesIterativeDfs(context);

    if (context.failures == 0) {
        std::cout << "阶段三图算法测试全部通过。\n";
        return 0;
    }
    std::cerr << context.failures << " 项测试失败。\n";
    return 1;
}
