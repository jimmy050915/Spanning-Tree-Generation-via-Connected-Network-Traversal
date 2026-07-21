#pragma once

#include "domain/traversal/ChildListTree.h"
#include "domain/traversal/IGraphView.h"

#include <vector>

namespace novel {

enum class TraversalScope {
    AllVertices,
    ReachableComponent
};

struct TraversalResult {
    // Contains real people only; kAllPersonsRootId is represented only in tree.
    std::vector<PersonId> order;
    ChildListTree tree;
};

TraversalResult depthFirstSearch(
    const IGraphView& graph,
    PersonId start,
    TraversalScope scope = TraversalScope::AllVertices);

TraversalResult breadthFirstSearch(
    const IGraphView& graph,
    PersonId start,
    TraversalScope scope = TraversalScope::AllVertices);

}  // namespace novel
