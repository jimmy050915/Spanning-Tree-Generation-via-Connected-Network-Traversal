#include "aml_graph.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

void printMenu() {
    std::cout << "\n========== 无向网邻接多重表 ==========\n"
              << "1. 显示全部顶点和边\n"
              << "2. 插入边\n"
              << "3. 删除边\n"
              << "4. DFS 遍历\n"
              << "5. BFS 遍历并显示孩子链表\n"
              << "6. 验证当前结构\n"
              << "7. 保存到文件\n"
              << "0. 退出\n"
              << "请选择：";
}

bool readInteger(const std::string& prompt, int& value) {
    std::cout << prompt;
    std::string line;
    if (!std::getline(std::cin, line)) {
        return false;
    }
    std::istringstream input(line);
    std::string extra;
    return (input >> value) && !(input >> extra);
}

bool readEdge(const std::string& prompt,
              std::string& first,
              std::string& second,
              int* weight = nullptr) {
    std::cout << prompt;
    std::string line;
    if (!std::getline(std::cin, line)) {
        return false;
    }
    std::istringstream input(line);
    std::string extra;
    if (weight != nullptr) {
        return (input >> first >> second >> *weight) && !(input >> extra);
    }
    return (input >> first >> second) && !(input >> extra);
}

void printSequence(const std::vector<std::string>& sequence) {
    for (std::size_t index = 0; index < sequence.size(); ++index) {
        if (index != 0) {
            std::cout << " -> ";
        }
        std::cout << sequence[index];
    }
    std::cout << '\n';
}

void showMutationResult(aml::AMLGraph& graph) {
    std::string validationError;
    if (graph.validate(validationError)) {
        std::cout << "结构验证：通过（邻接多重表合法且图保持连通）\n";
    } else {
        std::cout << "结构验证：失败——" << validationError << '\n';
    }
    graph.display(std::cout);
}

}  // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    if (argc != 2) {
        std::cerr << "用法：graph_app <input-file>\n";
        return 1;
    }

    aml::AMLGraph graph;
    std::string error;
    if (!graph.loadFromFile(argv[1], error)) {
        std::cerr << "载入失败：" << error << '\n';
        return 1;
    }
    std::cout << "图数据载入成功。\n";
    showMutationResult(graph);

    while (true) {
        printMenu();
        std::string choice;
        if (!std::getline(std::cin, choice)) {
            std::cout << "\n输入结束，程序退出。\n";
            break;
        }

        if (choice == "0") {
            std::cout << "程序退出。\n";
            break;
        }
        if (choice == "1") {
            graph.display(std::cout);
        } else if (choice == "2") {
            std::string first;
            std::string second;
            int weight = 0;
            if (!readEdge("请输入边（端点1 端点2 权值）：", first, second, &weight)) {
                std::cout << "输入格式错误。\n";
                continue;
            }
            aml::ExistingEdgeAction action = aml::ExistingEdgeAction::Reject;
            int oldWeight = 0;
            if (graph.edgeWeight(first, second, oldWeight)) {
                std::cout << "该边已存在，当前权值为 " << oldWeight << "。\n"
                          << "1. 就地修改权值\n"
                          << "2. 删除原结点后重新插入\n"
                          << "0. 取消\n";
                int overwriteChoice = 0;
                if (!readInteger("请选择覆盖方式：", overwriteChoice) ||
                    overwriteChoice < 0 || overwriteChoice > 2) {
                    std::cout << "覆盖方式无效，操作取消。\n";
                    continue;
                }
                if (overwriteChoice == 0) {
                    std::cout << "操作已取消。\n";
                    continue;
                }
                action = overwriteChoice == 1
                             ? aml::ExistingEdgeAction::UpdateWeight
                             : aml::ExistingEdgeAction::ReplaceNode;
            }
            if (!graph.insertEdge(first, second, weight, action, error)) {
                std::cout << error << '\n';
                continue;
            }
            std::cout << "边插入/覆盖成功。\n";
            showMutationResult(graph);
        } else if (choice == "3") {
            std::string first;
            std::string second;
            if (!readEdge("请输入要删除的边（端点1 端点2）：", first, second)) {
                std::cout << "输入格式错误。\n";
                continue;
            }
            if (!graph.removeEdge(first, second, error)) {
                std::cout << error << '\n';
                continue;
            }
            std::cout << "边删除成功。\n";
            showMutationResult(graph);
        } else if (choice == "4") {
            std::cout << "请输入 DFS 起点：";
            std::string start;
            std::getline(std::cin, start);
            std::vector<std::string> order;
            if (!graph.dfs(start, order, error)) {
                std::cout << error << '\n';
                continue;
            }
            std::cout << "DFS 序列：";
            printSequence(order);
        } else if (choice == "5") {
            std::cout << "请输入 BFS 起点：";
            std::string start;
            std::getline(std::cin, start);
            aml::TraversalResult result;
            if (!graph.bfs(start, result, error)) {
                std::cout << error << '\n';
                continue;
            }
            std::cout << "BFS 序列：";
            printSequence(result.order);
            auto tree = graph.makeChildTree(result);
            tree.display(std::cout);
        } else if (choice == "6") {
            if (graph.validate(error)) {
                std::cout << "验证通过：邻接多重表合法且图保持连通。\n";
            } else {
                std::cout << "验证失败：" << error << '\n';
            }
        } else if (choice == "7") {
            std::cout << "请输入输出文件路径：";
            std::string path;
            std::getline(std::cin, path);
            if (path.empty()) {
                std::cout << "路径不能为空。\n";
            } else if (graph.saveToFile(path, error)) {
                std::cout << "保存成功：" << path << '\n';
            } else {
                std::cout << error << '\n';
            }
        } else {
            std::cout << "无效选项，请重新输入。\n";
        }
    }
    return 0;
}
