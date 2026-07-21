#pragma once

#include "domain/model/GraphTypes.h"

#include <cstdint>
#include <unordered_map>

namespace novel {

class AdjacencyMultilistGraph;
class ChapterCollection;

struct StatisticsSnapshot {
    std::unordered_map<PersonId, std::uint32_t> personCounts;
    std::unordered_map<EdgeKey, std::uint32_t, EdgeKeyHash> coOccurrenceCounts;
};

class GraphStatisticsBuilder {
public:
    static StatisticsSnapshot analyze(const AdjacencyMultilistGraph& graph,
                                      const ChapterCollection& chapters);

    // Preserves people and surviving EdgeIds, but invalidates all EdgeNode
    // pointers and references obtained before the rebuild.
    static void rebuild(AdjacencyMultilistGraph& graph,
                        const ChapterCollection& chapters);
};

}  // namespace novel
