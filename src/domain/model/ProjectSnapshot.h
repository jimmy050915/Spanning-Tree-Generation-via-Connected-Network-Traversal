#pragma once

#include "domain/model/ChapterTypes.h"
#include "domain/model/GraphTypes.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace novel {

struct PersonSnapshot {
    PersonId id{};
    std::string canonicalName;
    std::uint32_t chapterCount{};
};

struct EdgeSnapshot {
    EdgeId id{};
    PersonId endpointA{};
    PersonId endpointB{};
    std::uint32_t coChapterCount{};
    double jaccard{};
};

// Pointer-free representation used at the domain/persistence boundary. The
// next identifiers are part of the snapshot so deleted identifiers are never
// reused after a save/load cycle.
struct ProjectSnapshot {
    PersonId nextPersonId{1};
    EdgeId nextEdgeId{1};
    ChapterId nextChapterId{1};
    std::vector<PersonSnapshot> persons;
    std::vector<EdgeSnapshot> edges;
    std::vector<std::pair<std::string, PersonId>> aliases;
    std::vector<ChapterRecord> chapters;
};

}  // namespace novel
