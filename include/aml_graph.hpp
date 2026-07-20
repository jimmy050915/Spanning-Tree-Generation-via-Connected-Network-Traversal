#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace aml {

struct EdgeNode {
    std::size_t ivex{};
    std::size_t jvex{};
    int weight{};
    EdgeNode* ilink{};
    EdgeNode* jlink{};
};

struct VertexNode {
    std::string label;
    EdgeNode* firstEdge{};
};

struct ChildNode {
    std::size_t childIndex{};
    ChildNode* next{};
};

struct TreeNode {
    std::string label;
    ChildNode* firstChild{};
};

struct EdgeInfo {
    std::size_t first{};
    std::size_t second{};
    int weight{};
};

struct TraversalResult {
    std::vector<std::string> order;
    std::vector<int> parent;
};

enum class ExistingEdgeAction {
    Reject,
    UpdateWeight,
    ReplaceNode
};

class ChildTree {
public:
    ChildTree() = default;
    ChildTree(const std::vector<std::string>& labels, const std::vector<int>& parent);
    ~ChildTree();

    ChildTree(const ChildTree&) = delete;
    ChildTree& operator=(const ChildTree&) = delete;
    ChildTree(ChildTree&& other) noexcept;
    ChildTree& operator=(ChildTree&& other) noexcept;

    void display(std::ostream& out) const;
    std::size_t size() const noexcept;
    std::size_t edgeCount() const noexcept;
    std::vector<std::string> childrenOf(const std::string& label) const;

private:
    std::vector<TreeNode> nodes_;
    void clear() noexcept;
};

class AMLGraph {
public:
    AMLGraph() = default;
    ~AMLGraph();

    AMLGraph(const AMLGraph&) = delete;
    AMLGraph& operator=(const AMLGraph&) = delete;

    bool loadFromFile(const std::string& path, std::string& error);
    bool saveToFile(const std::string& path, std::string& error) const;

    bool insertEdge(const std::string& firstLabel,
                    const std::string& secondLabel,
                    int weight,
                    ExistingEdgeAction action,
                    std::string& error);
    bool removeEdge(const std::string& firstLabel,
                    const std::string& secondLabel,
                    std::string& error);

    bool dfs(const std::string& startLabel,
             std::vector<std::string>& order,
             std::string& error) const;
    bool bfs(const std::string& startLabel,
             TraversalResult& result,
             std::string& error) const;
    ChildTree makeChildTree(const TraversalResult& traversal) const;

    bool validate(std::string& error) const;
    void display(std::ostream& out) const;

    std::size_t vertexCount() const noexcept;
    std::size_t edgeCount() const noexcept;
    std::vector<EdgeInfo> edges() const;
    bool edgeWeight(const std::string& firstLabel,
                    const std::string& secondLabel,
                    int& weight) const;

private:
    std::vector<VertexNode> vertices_;
    std::size_t edgeCount_{};

    void clear() noexcept;
    int indexOf(const std::string& label) const;
    EdgeNode* findEdge(std::size_t first, std::size_t second) const;
    EdgeNode* insertFresh(std::size_t first, std::size_t second, int weight);
    bool unlinkFromVertex(std::size_t vertex, EdgeNode* edge) noexcept;
    void eraseEdgeNode(EdgeNode* edge) noexcept;
    std::vector<std::size_t> neighbors(std::size_t vertex,
                                       const EdgeNode* ignored = nullptr) const;
    bool isConnectedIgnoring(const EdgeNode* ignored = nullptr) const;
};

}  // namespace aml
