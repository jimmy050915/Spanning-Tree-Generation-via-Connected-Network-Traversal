#include "aml_graph.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <limits>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace aml {
namespace {

struct ParsedEdge {
    std::size_t first{};
    std::size_t second{};
    int weight{};
};

bool isBlank(const std::string& line) {
    return line.find_first_not_of(" \t\r\n") == std::string::npos;
}

std::uint64_t edgeKey(std::size_t first, std::size_t second) {
    const auto low = static_cast<std::uint64_t>(std::min(first, second));
    const auto high = static_cast<std::uint64_t>(std::max(first, second));
    return (low << 32U) | high;
}

template <typename... Values>
bool parseExact(const std::string& line, Values&... values) {
    std::istringstream input(line);
    if (!((input >> values) && ...)) {
        return false;
    }
    std::string extra;
    return !(input >> extra);
}

}  // namespace

ChildTree::ChildTree(const std::vector<std::string>& labels,
                     const std::vector<int>& parent) {
    nodes_.reserve(labels.size());
    for (const auto& label : labels) {
        nodes_.push_back(TreeNode{label, nullptr});
    }

    std::vector<ChildNode*> tails(nodes_.size(), nullptr);
    const auto count = std::min(nodes_.size(), parent.size());
    for (std::size_t child = 0; child < count; ++child) {
        if (parent[child] < 0) {
            continue;
        }
        const auto parentIndex = static_cast<std::size_t>(parent[child]);
        if (parentIndex >= nodes_.size()) {
            continue;
        }
        auto* node = new ChildNode{child, nullptr};
        if (nodes_[parentIndex].firstChild == nullptr) {
            nodes_[parentIndex].firstChild = node;
        } else {
            tails[parentIndex]->next = node;
        }
        tails[parentIndex] = node;
    }
}

ChildTree::~ChildTree() {
    clear();
}

ChildTree::ChildTree(ChildTree&& other) noexcept
    : nodes_(std::move(other.nodes_)) {
    other.nodes_.clear();
}

ChildTree& ChildTree::operator=(ChildTree&& other) noexcept {
    if (this != &other) {
        clear();
        nodes_ = std::move(other.nodes_);
        other.nodes_.clear();
    }
    return *this;
}

void ChildTree::clear() noexcept {
    for (auto& node : nodes_) {
        auto* child = node.firstChild;
        while (child != nullptr) {
            auto* next = child->next;
            delete child;
            child = next;
        }
        node.firstChild = nullptr;
    }
    nodes_.clear();
}

void ChildTree::display(std::ostream& out) const {
    out << "孩子链表（父结点: 孩子结点）:\n";
    for (const auto& node : nodes_) {
        out << node.label << ":";
        for (auto* child = node.firstChild; child != nullptr; child = child->next) {
            out << ' ' << nodes_[child->childIndex].label;
        }
        out << '\n';
    }
}

std::size_t ChildTree::size() const noexcept {
    return nodes_.size();
}

std::size_t ChildTree::edgeCount() const noexcept {
    std::size_t count = 0;
    for (const auto& node : nodes_) {
        for (auto* child = node.firstChild; child != nullptr; child = child->next) {
            ++count;
        }
    }
    return count;
}

std::vector<std::string> ChildTree::childrenOf(const std::string& label) const {
    std::vector<std::string> result;
    const auto iterator = std::find_if(nodes_.begin(), nodes_.end(),
                                       [&label](const TreeNode& node) {
                                           return node.label == label;
                                       });
    if (iterator == nodes_.end()) {
        return result;
    }
    for (auto* child = iterator->firstChild; child != nullptr; child = child->next) {
        result.push_back(nodes_[child->childIndex].label);
    }
    return result;
}

AMLGraph::~AMLGraph() {
    clear();
}

void AMLGraph::clear() noexcept {
    std::unordered_set<EdgeNode*> uniqueEdges;
    for (const auto& vertex : vertices_) {
        std::unordered_set<EdgeNode*> seenInChain;
        auto* edge = vertex.firstEdge;
        while (edge != nullptr && seenInChain.insert(edge).second) {
            uniqueEdges.insert(edge);
            if (edge->ivex < vertices_.size() && &vertex == &vertices_[edge->ivex]) {
                edge = edge->ilink;
            } else if (edge->jvex < vertices_.size() && &vertex == &vertices_[edge->jvex]) {
                edge = edge->jlink;
            } else {
                break;
            }
        }
    }
    for (auto* edge : uniqueEdges) {
        delete edge;
    }
    vertices_.clear();
    edgeCount_ = 0;
}

int AMLGraph::indexOf(const std::string& label) const {
    for (std::size_t index = 0; index < vertices_.size(); ++index) {
        if (vertices_[index].label == label) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

EdgeNode* AMLGraph::findEdge(std::size_t first, std::size_t second) const {
    if (first >= vertices_.size() || second >= vertices_.size()) {
        return nullptr;
    }
    auto* edge = vertices_[first].firstEdge;
    while (edge != nullptr) {
        if ((edge->ivex == first && edge->jvex == second) ||
            (edge->ivex == second && edge->jvex == first)) {
            return edge;
        }
        edge = edge->ivex == first ? edge->ilink : edge->jlink;
    }
    return nullptr;
}

EdgeNode* AMLGraph::insertFresh(std::size_t first, std::size_t second, int weight) {
    if (first > second) {
        std::swap(first, second);
    }
    auto* edge = new EdgeNode{first, second, weight,
                              vertices_[first].firstEdge,
                              vertices_[second].firstEdge};
    vertices_[first].firstEdge = edge;
    vertices_[second].firstEdge = edge;
    ++edgeCount_;
    return edge;
}

bool AMLGraph::unlinkFromVertex(std::size_t vertex, EdgeNode* target) noexcept {
    EdgeNode** link = &vertices_[vertex].firstEdge;
    while (*link != nullptr) {
        EdgeNode* current = *link;
        EdgeNode** nextLink = nullptr;
        if (current->ivex == vertex) {
            nextLink = &current->ilink;
        } else if (current->jvex == vertex) {
            nextLink = &current->jlink;
        } else {
            return false;
        }
        if (current == target) {
            *link = *nextLink;
            return true;
        }
        link = nextLink;
    }
    return false;
}

void AMLGraph::eraseEdgeNode(EdgeNode* edge) noexcept {
    if (edge == nullptr) {
        return;
    }
    const bool firstUnlinked = unlinkFromVertex(edge->ivex, edge);
    const bool secondUnlinked = unlinkFromVertex(edge->jvex, edge);
    if (firstUnlinked && secondUnlinked) {
        delete edge;
        --edgeCount_;
    }
}

std::vector<std::size_t> AMLGraph::neighbors(std::size_t vertex,
                                             const EdgeNode* ignored) const {
    std::vector<std::size_t> result;
    auto* edge = vertices_[vertex].firstEdge;
    while (edge != nullptr) {
        if (edge != ignored) {
            result.push_back(edge->ivex == vertex ? edge->jvex : edge->ivex);
        }
        edge = edge->ivex == vertex ? edge->ilink : edge->jlink;
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool AMLGraph::isConnectedIgnoring(const EdgeNode* ignored) const {
    if (vertices_.empty()) {
        return false;
    }
    std::vector<bool> visited(vertices_.size(), false);
    std::queue<std::size_t> pending;
    visited[0] = true;
    pending.push(0);
    while (!pending.empty()) {
        const auto current = pending.front();
        pending.pop();
        for (const auto next : neighbors(current, ignored)) {
            if (!visited[next]) {
                visited[next] = true;
                pending.push(next);
            }
        }
    }
    return std::all_of(visited.begin(), visited.end(), [](bool value) { return value; });
}

bool AMLGraph::loadFromFile(const std::string& path, std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "无法打开文件：" + path;
        return false;
    }

    std::string line;
    long long vertexCount = 0;
    long long edgeCount = 0;
    if (!std::getline(input, line) || !parseExact(line, vertexCount, edgeCount)) {
        error = "第 1 行格式错误，应为：顶点数 边数";
        return false;
    }
    if (vertexCount <= 0 || edgeCount < 0) {
        error = "第 1 行数量非法：顶点数必须大于 0，边数不能为负数";
        return false;
    }
    const auto maxEdges = vertexCount * (vertexCount - 1) / 2;
    if (edgeCount > maxEdges) {
        error = "第 1 行边数超过简单无向图允许的最大值";
        return false;
    }

    if (!std::getline(input, line)) {
        error = "缺少第 2 行顶点标志";
        return false;
    }
    std::istringstream labelInput(line);
    std::vector<std::string> labels;
    std::unordered_map<std::string, std::size_t> indices;
    std::string label;
    while (labelInput >> label) {
        if (!indices.emplace(label, labels.size()).second) {
            error = "第 2 行存在重复顶点标志：" + label;
            return false;
        }
        labels.push_back(label);
    }
    if (labels.size() != static_cast<std::size_t>(vertexCount)) {
        error = "第 2 行顶点标志数量与第 1 行声明不一致";
        return false;
    }

    std::vector<ParsedEdge> parsedEdges;
    parsedEdges.reserve(static_cast<std::size_t>(edgeCount));
    std::unordered_set<std::uint64_t> pairs;
    for (long long edgeNumber = 0; edgeNumber < edgeCount; ++edgeNumber) {
        const auto lineNumber = edgeNumber + 3;
        if (!std::getline(input, line)) {
            error = "文件提前结束，缺少第 " + std::to_string(lineNumber) + " 行边数据";
            return false;
        }
        std::string firstLabel;
        std::string secondLabel;
        int weight = 0;
        if (!parseExact(line, firstLabel, secondLabel, weight)) {
            error = "第 " + std::to_string(lineNumber) + " 行格式错误，应为：端点1 端点2 整数权值";
            return false;
        }
        const auto first = indices.find(firstLabel);
        const auto second = indices.find(secondLabel);
        if (first == indices.end() || second == indices.end()) {
            error = "第 " + std::to_string(lineNumber) + " 行引用了未知顶点";
            return false;
        }
        if (first->second == second->second) {
            error = "第 " + std::to_string(lineNumber) + " 行包含自环";
            return false;
        }
        const auto key = edgeKey(first->second, second->second);
        if (!pairs.insert(key).second) {
            error = "第 " + std::to_string(lineNumber) + " 行包含重复边";
            return false;
        }
        parsedEdges.push_back(ParsedEdge{first->second, second->second, weight});
    }
    std::size_t lineNumber = static_cast<std::size_t>(edgeCount) + 3;
    while (std::getline(input, line)) {
        if (!isBlank(line)) {
            error = "第 " + std::to_string(lineNumber) + " 行存在多余数据";
            return false;
        }
        ++lineNumber;
    }

    std::vector<std::vector<std::size_t>> adjacency(labels.size());
    for (const auto& edge : parsedEdges) {
        adjacency[edge.first].push_back(edge.second);
        adjacency[edge.second].push_back(edge.first);
    }
    std::vector<bool> visited(labels.size(), false);
    std::queue<std::size_t> pending;
    visited[0] = true;
    pending.push(0);
    while (!pending.empty()) {
        const auto current = pending.front();
        pending.pop();
        for (const auto next : adjacency[current]) {
            if (!visited[next]) {
                visited[next] = true;
                pending.push(next);
            }
        }
    }
    if (!std::all_of(visited.begin(), visited.end(), [](bool value) { return value; })) {
        error = "输入图不是连通图";
        return false;
    }

    clear();
    vertices_.reserve(labels.size());
    for (auto& vertexLabel : labels) {
        vertices_.push_back(VertexNode{std::move(vertexLabel), nullptr});
    }
    for (const auto& edge : parsedEdges) {
        insertFresh(edge.first, edge.second, edge.weight);
    }
    error.clear();
    return true;
}

bool AMLGraph::saveToFile(const std::string& path, std::string& error) const {
    std::ofstream output(path);
    if (!output) {
        error = "无法创建输出文件：" + path;
        return false;
    }
    output << vertices_.size() << ' ' << edgeCount_ << '\n';
    for (std::size_t index = 0; index < vertices_.size(); ++index) {
        if (index != 0) {
            output << ' ';
        }
        output << vertices_[index].label;
    }
    output << '\n';
    for (const auto& edge : edges()) {
        output << vertices_[edge.first].label << ' '
               << vertices_[edge.second].label << ' '
               << edge.weight << '\n';
    }
    if (!output) {
        error = "写入文件失败：" + path;
        return false;
    }
    error.clear();
    return true;
}

bool AMLGraph::insertEdge(const std::string& firstLabel,
                          const std::string& secondLabel,
                          int weight,
                          ExistingEdgeAction action,
                          std::string& error) {
    const int firstIndex = indexOf(firstLabel);
    const int secondIndex = indexOf(secondLabel);
    if (firstIndex < 0 || secondIndex < 0) {
        error = "插入失败：端点包含不存在的顶点";
        return false;
    }
    if (firstIndex == secondIndex) {
        error = "插入失败：不允许自环";
        return false;
    }
    const auto first = static_cast<std::size_t>(firstIndex);
    const auto second = static_cast<std::size_t>(secondIndex);
    auto* existing = findEdge(first, second);
    if (existing == nullptr) {
        insertFresh(first, second, weight);
        error.clear();
        return true;
    }
    if (action == ExistingEdgeAction::Reject) {
        error = "插入取消：该边已经存在";
        return false;
    }
    if (action == ExistingEdgeAction::UpdateWeight) {
        existing->weight = weight;
        error.clear();
        return true;
    }

    auto* replacement = new EdgeNode{};
    const auto normalizedFirst = std::min(first, second);
    const auto normalizedSecond = std::max(first, second);
    eraseEdgeNode(existing);
    replacement->ivex = normalizedFirst;
    replacement->jvex = normalizedSecond;
    replacement->weight = weight;
    replacement->ilink = vertices_[normalizedFirst].firstEdge;
    replacement->jlink = vertices_[normalizedSecond].firstEdge;
    vertices_[normalizedFirst].firstEdge = replacement;
    vertices_[normalizedSecond].firstEdge = replacement;
    ++edgeCount_;
    error.clear();
    return true;
}

bool AMLGraph::removeEdge(const std::string& firstLabel,
                          const std::string& secondLabel,
                          std::string& error) {
    const int firstIndex = indexOf(firstLabel);
    const int secondIndex = indexOf(secondLabel);
    if (firstIndex < 0 || secondIndex < 0) {
        error = "删除失败：端点包含不存在的顶点";
        return false;
    }
    if (firstIndex == secondIndex) {
        error = "删除失败：图中不存在自环";
        return false;
    }
    auto* edge = findEdge(static_cast<std::size_t>(firstIndex),
                          static_cast<std::size_t>(secondIndex));
    if (edge == nullptr) {
        error = "删除失败：找不到指定边";
        return false;
    }
    if (!isConnectedIgnoring(edge)) {
        error = "删除失败：该边是桥，删除后图将不再连通";
        return false;
    }
    eraseEdgeNode(edge);
    error.clear();
    return true;
}

bool AMLGraph::dfs(const std::string& startLabel,
                   std::vector<std::string>& order,
                   std::string& error) const {
    const int start = indexOf(startLabel);
    if (start < 0) {
        error = "遍历失败：起点不存在";
        return false;
    }
    order.clear();
    std::vector<bool> visited(vertices_.size(), false);
    std::function<void(std::size_t)> visit = [&](std::size_t current) {
        visited[current] = true;
        order.push_back(vertices_[current].label);
        for (const auto next : neighbors(current)) {
            if (!visited[next]) {
                visit(next);
            }
        }
    };
    visit(static_cast<std::size_t>(start));
    error.clear();
    return true;
}

bool AMLGraph::bfs(const std::string& startLabel,
                   TraversalResult& result,
                   std::string& error) const {
    const int start = indexOf(startLabel);
    if (start < 0) {
        error = "遍历失败：起点不存在";
        return false;
    }
    result.order.clear();
    result.parent.assign(vertices_.size(), -1);
    std::vector<bool> visited(vertices_.size(), false);
    std::queue<std::size_t> pending;
    const auto startIndex = static_cast<std::size_t>(start);
    visited[startIndex] = true;
    pending.push(startIndex);
    while (!pending.empty()) {
        const auto current = pending.front();
        pending.pop();
        result.order.push_back(vertices_[current].label);
        for (const auto next : neighbors(current)) {
            if (!visited[next]) {
                visited[next] = true;
                result.parent[next] = static_cast<int>(current);
                pending.push(next);
            }
        }
    }
    error.clear();
    return true;
}

ChildTree AMLGraph::makeChildTree(const TraversalResult& traversal) const {
    std::vector<std::string> labels;
    labels.reserve(vertices_.size());
    for (const auto& vertex : vertices_) {
        labels.push_back(vertex.label);
    }
    return ChildTree(labels, traversal.parent);
}

bool AMLGraph::validate(std::string& error) const {
    if (vertices_.empty()) {
        error = "图中没有顶点";
        return false;
    }
    std::unordered_set<std::string> labels;
    std::unordered_map<EdgeNode*, std::size_t> occurrences;
    for (std::size_t vertexIndex = 0; vertexIndex < vertices_.size(); ++vertexIndex) {
        const auto& vertex = vertices_[vertexIndex];
        if (vertex.label.empty() || !labels.insert(vertex.label).second) {
            error = "顶点标志为空或重复";
            return false;
        }
        std::unordered_set<EdgeNode*> inThisChain;
        auto* edge = vertex.firstEdge;
        while (edge != nullptr) {
            if (!inThisChain.insert(edge).second) {
                error = "顶点 " + vertex.label + " 的边链存在环或重复结点";
                return false;
            }
            if (edge->ivex >= vertices_.size() || edge->jvex >= vertices_.size() ||
                edge->ivex == edge->jvex) {
                error = "存在端点下标非法的边结点";
                return false;
            }
            if (edge->ivex != vertexIndex && edge->jvex != vertexIndex) {
                error = "边结点出现在非端点的边链中";
                return false;
            }
            ++occurrences[edge];
            edge = edge->ivex == vertexIndex ? edge->ilink : edge->jlink;
        }
    }
    if (occurrences.size() != edgeCount_) {
        error = "边结点数量与记录的边数不一致";
        return false;
    }
    std::unordered_set<std::uint64_t> pairs;
    for (const auto& item : occurrences) {
        if (item.second != 2) {
            error = "某个边结点没有恰好出现在两个端点链中";
            return false;
        }
        const auto* edge = item.first;
        if (!pairs.insert(edgeKey(edge->ivex, edge->jvex)).second) {
            error = "存在重复端点对";
            return false;
        }
    }
    if (!isConnectedIgnoring()) {
        error = "图不连通";
        return false;
    }
    error.clear();
    return true;
}

void AMLGraph::display(std::ostream& out) const {
    out << "顶点（" << vertices_.size() << "）：";
    for (const auto& vertex : vertices_) {
        out << ' ' << vertex.label;
    }
    out << "\n边（" << edgeCount_ << "）：\n";
    for (const auto& edge : edges()) {
        out << "  " << vertices_[edge.first].label
            << " --(" << edge.weight << ")-- "
            << vertices_[edge.second].label << '\n';
    }
}

std::size_t AMLGraph::vertexCount() const noexcept {
    return vertices_.size();
}

std::size_t AMLGraph::edgeCount() const noexcept {
    return edgeCount_;
}

std::vector<EdgeInfo> AMLGraph::edges() const {
    std::vector<EdgeInfo> result;
    std::unordered_set<EdgeNode*> seen;
    for (const auto& vertex : vertices_) {
        auto* edge = vertex.firstEdge;
        while (edge != nullptr) {
            if (seen.insert(edge).second) {
                result.push_back(EdgeInfo{std::min(edge->ivex, edge->jvex),
                                          std::max(edge->ivex, edge->jvex),
                                          edge->weight});
            }
            const auto vertexIndex = static_cast<std::size_t>(&vertex - vertices_.data());
            edge = edge->ivex == vertexIndex ? edge->ilink : edge->jlink;
        }
    }
    std::sort(result.begin(), result.end(), [](const EdgeInfo& left, const EdgeInfo& right) {
        if (left.first != right.first) {
            return left.first < right.first;
        }
        return left.second < right.second;
    });
    return result;
}

bool AMLGraph::edgeWeight(const std::string& firstLabel,
                          const std::string& secondLabel,
                          int& weight) const {
    const int first = indexOf(firstLabel);
    const int second = indexOf(secondLabel);
    if (first < 0 || second < 0) {
        return false;
    }
    const auto* edge = findEdge(static_cast<std::size_t>(first),
                                static_cast<std::size_t>(second));
    if (edge == nullptr) {
        return false;
    }
    weight = edge->weight;
    return true;
}

}  // namespace aml
