#include "aml_graph.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "[失败] " << message << '\n';
    }
}

fs::path writeFixture(const fs::path& directory,
                      const std::string& name,
                      const std::string& content) {
    const auto path = directory / name;
    std::ofstream output(path);
    output << content;
    return path;
}

bool loadText(const fs::path& directory,
              const std::string& name,
              const std::string& content,
              aml::AMLGraph& graph,
              std::string& error) {
    return graph.loadFromFile(writeFixture(directory, name, content).string(), error);
}

void testLoadValidation(const fs::path& directory) {
    const std::string valid = "4 4\nA B C D\nA B 1\nB C 2\nC D 3\nD A 4\n";
    aml::AMLGraph graph;
    std::string error;
    check(loadText(directory, "valid.txt", valid, graph, error), "合法图应成功载入：" + error);
    check(graph.vertexCount() == 4 && graph.edgeCount() == 4, "载入后的顶点数和边数应正确");
    check(graph.validate(error), "合法图应通过完整性验证：" + error);

    const std::vector<std::pair<std::string, std::string>> invalidCases = {
        {"bad_count.txt", "3 1\nA B\nA B 1\n"},
        {"duplicate_vertex.txt", "3 2\nA A C\nA C 1\nA C 2\n"},
        {"unknown_vertex.txt", "2 1\nA B\nA C 1\n"},
        {"self_loop.txt", "2 1\nA B\nA A 1\n"},
        {"duplicate_edge.txt", "3 3\nA B C\nA B 1\nB A 2\nB C 3\n"},
        {"disconnected.txt", "4 2\nA B C D\nA B 1\nC D 2\n"},
        {"extra.txt", "2 1\nA B\nA B 1\nextra\n"}
    };
    for (const auto& item : invalidCases) {
        aml::AMLGraph invalid;
        error.clear();
        check(!loadText(directory, item.first, item.second, invalid, error),
              item.first + " 应被拒绝");
        check(!error.empty(), item.first + " 应给出错误原因");
    }
}

void testTraversalAndTree(const fs::path& directory) {
    aml::AMLGraph graph;
    std::string error;
    const std::string square = "4 4\nA B C D\nA B 1\nB C 2\nC D 3\nD A 4\n";
    check(loadText(directory, "traversal.txt", square, graph, error), "遍历样例应载入成功");

    std::vector<std::string> dfsOrder;
    check(graph.dfs("A", dfsOrder, error), "DFS 应成功");
    check(dfsOrder == std::vector<std::string>({"A", "B", "C", "D"}),
          "DFS 应按顶点输入次序访问邻接点");

    aml::TraversalResult bfsResult;
    check(graph.bfs("A", bfsResult, error), "BFS 应成功");
    check(bfsResult.order == std::vector<std::string>({"A", "B", "D", "C"}),
          "BFS 序列应稳定");
    auto tree = graph.makeChildTree(bfsResult);
    check(tree.size() == 4 && tree.edgeCount() == 3, "BFS 树应有 V-1 条树边");
    check(tree.childrenOf("A") == std::vector<std::string>({"B", "D"}),
          "A 的孩子应为 B、D");
    check(tree.childrenOf("B") == std::vector<std::string>({"C"}),
          "B 的孩子应为 C");

    check(!graph.dfs("X", dfsOrder, error), "不存在的 DFS 起点应被拒绝");
    check(!graph.bfs("X", bfsResult, error), "不存在的 BFS 起点应被拒绝");
}

void testMutations(const fs::path& directory) {
    aml::AMLGraph graph;
    std::string error;
    const std::string graphText =
        "5 6\nA B C D E\nA B 1\nA C 2\nA D 3\nB C 4\nC D 5\nD E 6\n";
    check(loadText(directory, "mutations.txt", graphText, graph, error), "修改样例应载入成功");

    check(graph.insertEdge("B", "D", 7, aml::ExistingEdgeAction::Reject, error),
          "新边应插入成功");
    check(graph.edgeCount() == 7 && graph.validate(error), "插入后结构应合法");
    check(!graph.insertEdge("B", "D", 8, aml::ExistingEdgeAction::Reject, error),
          "未指定覆盖时应拒绝重复边");
    check(graph.insertEdge("B", "D", 8, aml::ExistingEdgeAction::UpdateWeight, error),
          "应支持就地更新权值");
    int weight = 0;
    check(graph.edgeWeight("D", "B", weight) && weight == 8, "无向边反向查询应得到更新权值");
    check(graph.insertEdge("B", "D", -9, aml::ExistingEdgeAction::ReplaceNode, error),
          "应支持重建边结点并允许负权");
    check(graph.edgeWeight("B", "D", weight) && weight == -9, "重建后的权值应正确");
    check(graph.validate(error), "两种覆盖操作后结构应合法");

    check(graph.removeEdge("A", "D", error), "链中非桥边应可删除");
    check(graph.removeEdge("A", "B", error), "链中另一非桥边应可删除");
    check(!graph.removeEdge("A", "E", error), "不存在的边应删除失败");
    check(!graph.insertEdge("A", "A", 1, aml::ExistingEdgeAction::Reject, error), "自环应插入失败");
    check(!graph.insertEdge("A", "X", 1, aml::ExistingEdgeAction::Reject, error), "未知端点应插入失败");
    check(graph.validate(error), "连续增删后结构应合法：" + error);

    aml::AMLGraph path;
    check(loadText(directory, "bridge.txt", "3 2\nA B C\nA B 1\nB C 2\n", path, error),
          "路径图应载入成功");
    check(!path.removeEdge("B", "C", error), "桥边删除应被拒绝");
    check(path.edgeCount() == 2 && path.validate(error), "拒绝删除桥边后图应保持不变");

    const std::string complete =
        "4 6\nA B C D\nA B 1\nA C 2\nA D 3\nB C 4\nB D 5\nC D 6\n";
    const std::vector<std::pair<std::string, std::string>> chainPositions = {
        {"A", "D"},  // A 的边链链首
        {"A", "C"},  // A 的边链链中
        {"A", "B"}   // A 的边链链尾
    };
    for (std::size_t index = 0; index < chainPositions.size(); ++index) {
        aml::AMLGraph positionGraph;
        check(loadText(directory, "position_" + std::to_string(index) + ".txt",
                       complete, positionGraph, error),
              "链位置删除样例应载入成功");
        check(positionGraph.removeEdge(chainPositions[index].first,
                                       chainPositions[index].second, error),
              "链首、链中或链尾的非桥边都应可删除");
        check(positionGraph.edgeCount() == 5 && positionGraph.validate(error),
              "不同链位置删除后结构都应合法");
    }
}

void testRoundTrip(const fs::path& directory) {
    aml::AMLGraph original;
    std::string error;
    const std::string content = "4 5\n北京 上海 广州 深圳\n北京 上海 8\n北京 广州 -2\n上海 广州 3\n上海 深圳 6\n广州 深圳 1\n";
    check(loadText(directory, "utf8.txt", content, original, error), "UTF-8 顶点样例应载入成功");
    const auto output = directory / "saved.txt";
    check(original.saveToFile(output.string(), error), "图应成功保存：" + error);

    aml::AMLGraph reloaded;
    check(reloaded.loadFromFile(output.string(), error), "保存结果应能重新载入：" + error);
    check(reloaded.vertexCount() == original.vertexCount(), "往返后顶点数应一致");
    check(reloaded.edgeCount() == original.edgeCount(), "往返后边数应一致");
    const auto originalEdges = original.edges();
    const auto reloadedEdges = reloaded.edges();
    check(reloadedEdges.size() == originalEdges.size(), "往返后边集合大小应一致");
    bool sameEdges = reloadedEdges.size() == originalEdges.size();
    for (std::size_t index = 0; sameEdges && index < originalEdges.size(); ++index) {
        sameEdges = originalEdges[index].first == reloadedEdges[index].first &&
                    originalEdges[index].second == reloadedEdges[index].second &&
                    originalEdges[index].weight == reloadedEdges[index].weight;
    }
    check(sameEdges, "往返后每条边及其权值应完全一致");
    int weight = 0;
    check(reloaded.edgeWeight("北京", "广州", weight) && weight == -2,
          "往返后 UTF-8 标签与负权应保留");
    check(reloaded.validate(error), "往返后的图应通过验证");
}

}  // namespace

int main() {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = fs::temp_directory_path() /
                           ("aml_graph_tests_" + std::to_string(suffix));
    fs::create_directories(directory);

    testLoadValidation(directory);
    testTraversalAndTree(directory);
    testMutations(directory);
    testRoundTrip(directory);

    std::error_code cleanupError;
    fs::remove_all(directory, cleanupError);
    if (failures == 0) {
        std::cout << "全部测试通过。\n";
        return 0;
    }
    std::cerr << failures << " 项测试失败。\n";
    return 1;
}
