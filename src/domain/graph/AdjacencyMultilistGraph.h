#pragma once

#include "domain/error/DomainError.h"
#include "domain/model/GraphTypes.h"
#include "domain/traversal/IGraphView.h"
#include "domain/validation/ValidationReport.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace novel {

class GraphTestAccess;
class GraphStatisticsBuilder;
struct StatisticsSnapshot;

class AdjacencyMultilistGraph final : public IGraphView {
public:
    AdjacencyMultilistGraph() = default;
    ~AdjacencyMultilistGraph() override = default;

    AdjacencyMultilistGraph(const AdjacencyMultilistGraph&) = delete;
    AdjacencyMultilistGraph& operator=(const AdjacencyMultilistGraph&) = delete;
    AdjacencyMultilistGraph(AdjacencyMultilistGraph&&) noexcept = default;
    AdjacencyMultilistGraph& operator=(AdjacencyMultilistGraph&&) noexcept = default;

    PersonId addPerson(const std::string& canonicalName);
    bool removePerson(PersonId id);
    bool renamePerson(PersonId id, const std::string& newName);
    bool setPersonChapterCount(PersonId id, std::uint32_t chapterCount);

    const EdgeNode& insertEdge(PersonId first,
                               PersonId second,
                               std::uint32_t coChapterCount);
    bool deleteEdge(PersonId first, PersonId second);

    const PersonVertex* findPerson(PersonId id) const noexcept;
    const PersonVertex* findPersonByName(
        const std::string& canonicalName) const noexcept;
    const EdgeNode* findEdge(PersonId first, PersonId second) const noexcept;

    std::vector<PersonId> vertices() const override;
    std::vector<PersonId> neighbors(PersonId id) const override;
    std::vector<PersonId> personIds() const;
    std::vector<EdgeKey> edgeKeys() const;

    std::size_t personCount() const noexcept;
    std::size_t edgeCount() const noexcept;
    ValidationReport validate() const;

private:
    static constexpr double jaccardEpsilon_ = 1e-9;

    PersonId nextPersonId_{1};
    EdgeId nextEdgeId_{1};
    std::unordered_map<PersonId, std::unique_ptr<PersonVertex>> persons_;
    std::unordered_map<std::string, PersonId> personNameIndex_;
    std::unordered_map<EdgeKey, std::unique_ptr<EdgeNode>, EdgeKeyHash> edges_;

    static bool isBlank(const std::string& value) noexcept;
    static double calculateJaccard(std::uint32_t firstChapterCount,
                                   std::uint32_t secondChapterCount,
                                   std::uint32_t coChapterCount);
    static EdgeNode* nextEdge(const EdgeNode* edge, PersonId currentVertex) noexcept;
    static PersonId opposite(const EdgeNode& edge, PersonId currentVertex);

    bool chainContains(const PersonVertex& vertex, const EdgeNode& target) const noexcept;
    bool unlinkFromVertex(PersonVertex& vertex, EdgeNode& target) noexcept;
    void eraseEdgeUnchecked(const EdgeKey& key);
    void recomputeIncidentJaccard(PersonId id);
    void replaceStatistics(const StatisticsSnapshot& statistics);
    void ensureValidState() const;

    friend class GraphTestAccess;
    friend class GraphStatisticsBuilder;
};

}  // namespace novel
