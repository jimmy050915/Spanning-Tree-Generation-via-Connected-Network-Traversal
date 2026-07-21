#include "domain/statistics/GraphStatisticsBuilder.h"

#include "domain/chapter/ChapterCollection.h"
#include "domain/error/DomainError.h"
#include "domain/graph/AdjacencyMultilistGraph.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace novel {

namespace {

void checkedIncrement(std::uint32_t& value, const char* message) {
    if (value == std::numeric_limits<std::uint32_t>::max()) {
        throw DomainError(DomainErrorCode::InvalidStatistics, message);
    }
    ++value;
}

}  // namespace

StatisticsSnapshot GraphStatisticsBuilder::analyze(
    const AdjacencyMultilistGraph& graph,
    const ChapterCollection& chapters) {
    StatisticsSnapshot result;
    const auto personIds = graph.personIds();
    result.personCounts.reserve(personIds.size());
    for (const auto personId : personIds) {
        result.personCounts.emplace(personId, 0U);
    }

    for (const auto& chapter : chapters.all()) {
        std::vector<PersonId> uniquePersons = chapter.persons;
        std::sort(uniquePersons.begin(), uniquePersons.end());
        uniquePersons.erase(
            std::unique(uniquePersons.begin(), uniquePersons.end()),
            uniquePersons.end());

        for (const auto personId : uniquePersons) {
            auto count = result.personCounts.find(personId);
            if (count == result.personCounts.end() ||
                graph.findPerson(personId) == nullptr) {
                throw DomainError(
                    DomainErrorCode::PersonNotFound,
                    "章节引用了不存在的人物编号：" +
                        std::to_string(personId));
            }
            checkedIncrement(count->second, "人物出现章节数超出 uint32_t 范围");
        }

        for (std::size_t first = 0; first < uniquePersons.size(); ++first) {
            for (std::size_t second = first + 1;
                 second < uniquePersons.size();
                 ++second) {
                const auto key = EdgeKey::make(uniquePersons[first],
                                               uniquePersons[second]);
                auto insertion = result.coOccurrenceCounts.emplace(key, 0U);
                checkedIncrement(insertion.first->second,
                                 "人物共同章节数超出 uint32_t 范围");
            }
        }
    }

    return result;
}

void GraphStatisticsBuilder::rebuild(AdjacencyMultilistGraph& graph,
                                     const ChapterCollection& chapters) {
    graph.replaceStatistics(analyze(graph, chapters));
}

}  // namespace novel
