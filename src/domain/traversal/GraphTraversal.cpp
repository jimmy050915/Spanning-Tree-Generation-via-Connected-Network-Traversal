#include "domain/traversal/GraphTraversal.h"

#include "domain/error/DomainError.h"

#include <algorithm>
#include <cstddef>
#include <queue>
#include <unordered_set>
#include <utility>
#include <vector>

namespace novel {

namespace {

std::vector<PersonId> requireStartVertex(const IGraphView& graph,
                                         PersonId start) {
    auto vertices = graph.vertices();
    if (!std::binary_search(vertices.begin(), vertices.end(), start)) {
        throw DomainError(DomainErrorCode::PersonNotFound,
                          "遍历起点人物不存在");
    }
    return vertices;
}

void createComponentRoot(TraversalResult& result,
                         PersonId root,
                         TraversalScope scope) {
    result.tree.createNode(root);
    if (scope == TraversalScope::AllVertices) {
        result.tree.addChild(kAllPersonsRootId, root);
    }
    result.order.push_back(root);
}

}  // namespace

TraversalResult depthFirstSearch(const IGraphView& graph,
                                 PersonId start,
                                 TraversalScope scope) {
    const auto vertices = requireStartVertex(graph, start);

    TraversalResult result;
    if (scope == TraversalScope::AllVertices) {
        result.tree.createNode(kAllPersonsRootId);
    }

    std::unordered_set<PersonId> visited;
    visited.reserve(vertices.size());

    struct Frame {
        PersonId vertex{};
        std::vector<PersonId> neighbors;
        std::size_t nextNeighbor{};
    };

    const auto traverseComponent = [&](PersonId root) {
        visited.insert(root);
        createComponentRoot(result, root, scope);

        std::vector<Frame> stack;
        stack.push_back(Frame{root, graph.neighbors(root), 0});

        while (!stack.empty()) {
            auto& frame = stack.back();
            if (frame.nextNeighbor == frame.neighbors.size()) {
                stack.pop_back();
                continue;
            }

            const auto parent = frame.vertex;
            const auto neighbor = frame.neighbors[frame.nextNeighbor++];
            if (!visited.insert(neighbor).second) {
                continue;
            }

            result.tree.createNode(neighbor);
            result.tree.addChild(parent, neighbor);
            result.order.push_back(neighbor);
            stack.push_back(Frame{neighbor, graph.neighbors(neighbor), 0});
        }
    };

    traverseComponent(start);
    if (scope == TraversalScope::AllVertices) {
        for (const auto vertex : vertices) {
            if (visited.find(vertex) == visited.end()) {
                traverseComponent(vertex);
            }
        }
    }

    return result;
}

TraversalResult breadthFirstSearch(const IGraphView& graph,
                                   PersonId start,
                                   TraversalScope scope) {
    const auto vertices = requireStartVertex(graph, start);

    TraversalResult result;
    if (scope == TraversalScope::AllVertices) {
        result.tree.createNode(kAllPersonsRootId);
    }

    std::unordered_set<PersonId> visited;
    visited.reserve(vertices.size());

    const auto traverseComponent = [&](PersonId root) {
        visited.insert(root);
        createComponentRoot(result, root, scope);

        std::queue<PersonId> pending;
        pending.push(root);
        while (!pending.empty()) {
            const auto parent = pending.front();
            pending.pop();

            for (const auto neighbor : graph.neighbors(parent)) {
                if (!visited.insert(neighbor).second) {
                    continue;
                }

                result.tree.createNode(neighbor);
                result.tree.addChild(parent, neighbor);
                result.order.push_back(neighbor);
                pending.push(neighbor);
            }
        }
    };

    traverseComponent(start);
    if (scope == TraversalScope::AllVertices) {
        for (const auto vertex : vertices) {
            if (visited.find(vertex) == visited.end()) {
                traverseComponent(vertex);
            }
        }
    }

    return result;
}

}  // namespace novel
