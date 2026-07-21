#include "domain/graph/AdjacencyMultilistGraph.h"

#include "domain/statistics/GraphStatisticsBuilder.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace novel {

namespace {

struct EdgeOccurrences {
    std::size_t total{};
    bool atEndpointA{};
    bool atEndpointB{};
};

std::string edgeDescription(const EdgeNode& edge) {
    return std::to_string(edge.endpointA) + "-" + std::to_string(edge.endpointB);
}

}  // namespace

bool AdjacencyMultilistGraph::isBlank(const std::string& value) noexcept {
    return value.empty() ||
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isspace(character) != 0;
           });
}

double AdjacencyMultilistGraph::calculateJaccard(
    std::uint32_t firstChapterCount,
    std::uint32_t secondChapterCount,
    std::uint32_t coChapterCount) {
    const auto denominator = static_cast<std::uint64_t>(firstChapterCount) +
                             static_cast<std::uint64_t>(secondChapterCount) -
                             static_cast<std::uint64_t>(coChapterCount);
    if (coChapterCount == 0 || denominator == 0) {
        throw DomainError(DomainErrorCode::InvalidStatistics,
                          "人物章节统计无法计算 Jaccard 关联度");
    }
    return static_cast<double>(coChapterCount) /
           static_cast<double>(denominator);
}

EdgeNode* AdjacencyMultilistGraph::nextEdge(const EdgeNode* edge,
                                            PersonId currentVertex) noexcept {
    if (edge == nullptr) {
        return nullptr;
    }
    if (edge->endpointA == currentVertex) {
        return edge->linkA;
    }
    if (edge->endpointB == currentVertex) {
        return edge->linkB;
    }
    return nullptr;
}

PersonId AdjacencyMultilistGraph::opposite(const EdgeNode& edge,
                                           PersonId currentVertex) {
    if (edge.endpointA == currentVertex) {
        return edge.endpointB;
    }
    if (edge.endpointB == currentVertex) {
        return edge.endpointA;
    }
    throw DomainError(DomainErrorCode::GraphValidationFailed,
                      "边结点出现在无关人物的边链中");
}

PersonId AdjacencyMultilistGraph::addPerson(const std::string& canonicalName) {
    ensureValidState();
    if (isBlank(canonicalName)) {
        throw DomainError(DomainErrorCode::EmptyPersonName,
                          "标准人物名不能为空或仅包含空白字符");
    }
    if (personNameIndex_.find(canonicalName) != personNameIndex_.end()) {
        throw DomainError(DomainErrorCode::DuplicatePerson,
                          "标准人物名已存在：" + canonicalName);
    }
    if (nextPersonId_ == std::numeric_limits<PersonId>::max()) {
        throw DomainError(DomainErrorCode::IdentifierExhausted,
                          "人物编号已经耗尽");
    }

    const PersonId id = nextPersonId_;
    auto person = std::make_unique<PersonVertex>();
    person->id = id;
    person->canonicalName = canonicalName;

    const auto personInsertion = persons_.emplace(id, std::move(person));
    try {
        const auto nameInsertion = personNameIndex_.emplace(canonicalName, id);
        if (!nameInsertion.second) {
            persons_.erase(personInsertion.first);
            throw DomainError(DomainErrorCode::DuplicatePerson,
                              "标准人物名已存在：" + canonicalName);
        }
    } catch (...) {
        persons_.erase(id);
        throw;
    }

    ++nextPersonId_;
    ensureValidState();
    return id;
}

bool AdjacencyMultilistGraph::removePerson(PersonId id) {
    ensureValidState();
    auto personIterator = persons_.find(id);
    if (personIterator == persons_.end()) {
        return false;
    }

    std::vector<EdgeKey> incidentEdges;
    for (auto* edge = personIterator->second->firstEdge;
         edge != nullptr;
         edge = nextEdge(edge, id)) {
        incidentEdges.push_back(EdgeKey::make(edge->endpointA, edge->endpointB));
    }
    for (const auto& key : incidentEdges) {
        eraseEdgeUnchecked(key);
    }

    personNameIndex_.erase(personIterator->second->canonicalName);
    persons_.erase(personIterator);
    ensureValidState();
    return true;
}

bool AdjacencyMultilistGraph::renamePerson(PersonId id,
                                           const std::string& newName) {
    ensureValidState();
    auto personIterator = persons_.find(id);
    if (personIterator == persons_.end()) {
        return false;
    }
    if (isBlank(newName)) {
        throw DomainError(DomainErrorCode::EmptyPersonName,
                          "标准人物名不能为空或仅包含空白字符");
    }

    auto existing = personNameIndex_.find(newName);
    if (existing != personNameIndex_.end()) {
        if (existing->second == id) {
            return true;
        }
        throw DomainError(DomainErrorCode::DuplicatePerson,
                          "标准人物名已存在：" + newName);
    }

    std::string replacement = newName;
    const auto insertion = personNameIndex_.emplace(replacement, id);
    if (!insertion.second) {
        throw DomainError(DomainErrorCode::DuplicatePerson,
                          "标准人物名已存在：" + newName);
    }

    std::string oldName;
    oldName.swap(personIterator->second->canonicalName);
    personIterator->second->canonicalName.swap(replacement);
    personNameIndex_.erase(oldName);

    ensureValidState();
    return true;
}

bool AdjacencyMultilistGraph::setPersonChapterCount(
    PersonId id,
    std::uint32_t chapterCount) {
    ensureValidState();
    auto personIterator = persons_.find(id);
    if (personIterator == persons_.end()) {
        return false;
    }

    for (auto* edge = personIterator->second->firstEdge;
         edge != nullptr;
         edge = nextEdge(edge, id)) {
        if (edge->coChapterCount > chapterCount) {
            throw DomainError(
                DomainErrorCode::InvalidStatistics,
                "人物出现章节数不能小于其与其他人物的共同章节数");
        }
    }

    personIterator->second->chapterCount = chapterCount;
    recomputeIncidentJaccard(id);
    ensureValidState();
    return true;
}

const EdgeNode& AdjacencyMultilistGraph::insertEdge(
    PersonId first,
    PersonId second,
    std::uint32_t coChapterCount) {
    ensureValidState();
    if (first == second) {
        throw DomainError(DomainErrorCode::SelfLoop,
                          "不允许创建人物自环");
    }

    const auto key = EdgeKey::make(first, second);
    auto firstPerson = persons_.find(key.low);
    auto secondPerson = persons_.find(key.high);
    if (firstPerson == persons_.end() || secondPerson == persons_.end()) {
        throw DomainError(DomainErrorCode::PersonNotFound,
                          "关系边的一个或两个端点不存在");
    }
    if (coChapterCount == 0) {
        throw DomainError(DomainErrorCode::InvalidCoChapterCount,
                          "共同章节数必须大于零");
    }
    if (coChapterCount > firstPerson->second->chapterCount ||
        coChapterCount > secondPerson->second->chapterCount) {
        throw DomainError(
            DomainErrorCode::InvalidStatistics,
            "共同章节数不能超过任一人物的出现章节数");
    }
    if (edges_.find(key) != edges_.end()) {
        throw DomainError(DomainErrorCode::DuplicateEdge,
                          "两个人物之间的关系边已经存在");
    }
    if (nextEdgeId_ == std::numeric_limits<EdgeId>::max()) {
        throw DomainError(DomainErrorCode::IdentifierExhausted,
                          "关系边编号已经耗尽");
    }

    auto edge = std::make_unique<EdgeNode>();
    edge->id = nextEdgeId_;
    edge->endpointA = key.low;
    edge->endpointB = key.high;
    edge->coChapterCount = coChapterCount;
    edge->jaccard = calculateJaccard(firstPerson->second->chapterCount,
                                      secondPerson->second->chapterCount,
                                      coChapterCount);
    edge->linkA = firstPerson->second->firstEdge;
    edge->linkB = secondPerson->second->firstEdge;

    auto* rawEdge = edge.get();
    const auto insertion = edges_.emplace(key, std::move(edge));
    if (!insertion.second) {
        throw DomainError(DomainErrorCode::DuplicateEdge,
                          "两个人物之间的关系边已经存在");
    }
    firstPerson->second->firstEdge = rawEdge;
    secondPerson->second->firstEdge = rawEdge;
    ++nextEdgeId_;

    ensureValidState();
    return *rawEdge;
}

bool AdjacencyMultilistGraph::deleteEdge(PersonId first, PersonId second) {
    ensureValidState();
    if (first == second) {
        return false;
    }
    const auto key = EdgeKey::make(first, second);
    if (edges_.find(key) == edges_.end()) {
        return false;
    }

    eraseEdgeUnchecked(key);
    ensureValidState();
    return true;
}

const PersonVertex* AdjacencyMultilistGraph::findPerson(PersonId id) const noexcept {
    const auto iterator = persons_.find(id);
    return iterator == persons_.end() ? nullptr : iterator->second.get();
}

const PersonVertex* AdjacencyMultilistGraph::findPersonByName(
    const std::string& canonicalName) const noexcept {
    const auto name = personNameIndex_.find(canonicalName);
    return name == personNameIndex_.end() ? nullptr : findPerson(name->second);
}

const EdgeNode* AdjacencyMultilistGraph::findEdge(PersonId first,
                                                  PersonId second) const noexcept {
    if (first == second) {
        return nullptr;
    }
    const auto iterator = edges_.find(EdgeKey::make(first, second));
    return iterator == edges_.end() ? nullptr : iterator->second.get();
}

std::vector<PersonId> AdjacencyMultilistGraph::neighbors(PersonId id) const {
    const auto personIterator = persons_.find(id);
    if (personIterator == persons_.end()) {
        throw DomainError(DomainErrorCode::PersonNotFound,
                          "无法查询不存在人物的邻居");
    }

    std::vector<PersonId> result;
    std::unordered_set<const EdgeNode*> visited;
    auto* edge = personIterator->second->firstEdge;
    while (edge != nullptr) {
        if (!visited.insert(edge).second) {
            throw DomainError(DomainErrorCode::GraphValidationFailed,
                              "人物边链中存在环");
        }
        result.push_back(opposite(*edge, id));
        edge = nextEdge(edge, id);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<PersonId> AdjacencyMultilistGraph::vertices() const {
    std::vector<PersonId> result;
    result.reserve(persons_.size());
    for (const auto& entry : persons_) {
        result.push_back(entry.first);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<PersonId> AdjacencyMultilistGraph::personIds() const {
    return vertices();
}

std::vector<EdgeKey> AdjacencyMultilistGraph::edgeKeys() const {
    std::vector<EdgeKey> result;
    result.reserve(edges_.size());
    for (const auto& entry : edges_) {
        result.push_back(entry.first);
    }
    std::sort(result.begin(), result.end(), [](const EdgeKey& left, const EdgeKey& right) {
        return left.low < right.low ||
               (left.low == right.low && left.high < right.high);
    });
    return result;
}

std::size_t AdjacencyMultilistGraph::personCount() const noexcept {
    return persons_.size();
}

std::size_t AdjacencyMultilistGraph::edgeCount() const noexcept {
    return edges_.size();
}

bool AdjacencyMultilistGraph::chainContains(const PersonVertex& vertex,
                                            const EdgeNode& target) const noexcept {
    auto* edge = vertex.firstEdge;
    while (edge != nullptr) {
        if (edge == &target) {
            return true;
        }
        edge = nextEdge(edge, vertex.id);
    }
    return false;
}

bool AdjacencyMultilistGraph::unlinkFromVertex(PersonVertex& vertex,
                                               EdgeNode& target) noexcept {
    EdgeNode** link = &vertex.firstEdge;
    while (*link != nullptr) {
        auto* current = *link;
        EdgeNode** nextLink = nullptr;
        if (current->endpointA == vertex.id) {
            nextLink = &current->linkA;
        } else if (current->endpointB == vertex.id) {
            nextLink = &current->linkB;
        } else {
            return false;
        }

        if (current == &target) {
            *link = *nextLink;
            return true;
        }
        link = nextLink;
    }
    return false;
}

void AdjacencyMultilistGraph::eraseEdgeUnchecked(const EdgeKey& key) {
    auto edgeIterator = edges_.find(key);
    if (edgeIterator == edges_.end()) {
        return;
    }

    auto& edge = *edgeIterator->second;
    auto firstPerson = persons_.find(edge.endpointA);
    auto secondPerson = persons_.find(edge.endpointB);
    if (firstPerson == persons_.end() || secondPerson == persons_.end() ||
        !chainContains(*firstPerson->second, edge) ||
        !chainContains(*secondPerson->second, edge)) {
        throw DomainError(DomainErrorCode::GraphValidationFailed,
                          "删除关系边前发现端点边链不一致");
    }

    const bool firstUnlinked = unlinkFromVertex(*firstPerson->second, edge);
    const bool secondUnlinked = unlinkFromVertex(*secondPerson->second, edge);
    if (!firstUnlinked || !secondUnlinked) {
        throw DomainError(DomainErrorCode::GraphValidationFailed,
                          "关系边未能从两个端点边链完整摘除");
    }
    edges_.erase(edgeIterator);
}

void AdjacencyMultilistGraph::recomputeIncidentJaccard(PersonId id) {
    auto personIterator = persons_.find(id);
    if (personIterator == persons_.end()) {
        return;
    }

    auto* edge = personIterator->second->firstEdge;
    while (edge != nullptr) {
        const auto otherId = opposite(*edge, id);
        const auto otherPerson = persons_.find(otherId);
        if (otherPerson == persons_.end()) {
            throw DomainError(DomainErrorCode::GraphValidationFailed,
                              "重新计算 Jaccard 时发现关系端点不存在");
        }
        edge->jaccard = calculateJaccard(personIterator->second->chapterCount,
                                          otherPerson->second->chapterCount,
                                          edge->coChapterCount);
        edge = nextEdge(edge, id);
    }
}

void AdjacencyMultilistGraph::replaceStatistics(
    const StatisticsSnapshot& statistics) {
    ensureValidState();

    if (statistics.personCounts.size() != persons_.size()) {
        throw DomainError(DomainErrorCode::InvalidStatistics,
                          "统计快照未覆盖图中的全部人物");
    }
    for (const auto& personEntry : persons_) {
        if (statistics.personCounts.find(personEntry.first) ==
            statistics.personCounts.end()) {
            throw DomainError(DomainErrorCode::InvalidStatistics,
                              "统计快照缺少图中人物的章节数");
        }
    }

    std::vector<EdgeKey> candidateKeys;
    candidateKeys.reserve(statistics.coOccurrenceCounts.size());
    for (const auto& countEntry : statistics.coOccurrenceCounts) {
        if (countEntry.second == 0) {
            continue;
        }

        const auto& key = countEntry.first;
        if (key.low == key.high || key.low > key.high) {
            throw DomainError(DomainErrorCode::InvalidStatistics,
                              "统计快照中包含未规范化的人物关系");
        }
        const auto firstPerson = persons_.find(key.low);
        const auto secondPerson = persons_.find(key.high);
        if (firstPerson == persons_.end() || secondPerson == persons_.end()) {
            throw DomainError(DomainErrorCode::PersonNotFound,
                              "统计快照中的人物关系引用了不存在的人物");
        }

        const auto firstCount = statistics.personCounts.find(key.low);
        const auto secondCount = statistics.personCounts.find(key.high);
        if (firstCount == statistics.personCounts.end() ||
            secondCount == statistics.personCounts.end() ||
            countEntry.second > firstCount->second ||
            countEntry.second > secondCount->second) {
            throw DomainError(
                DomainErrorCode::InvalidStatistics,
                "共同章节数不能超过任一端点人物的出现章节数");
        }
        candidateKeys.push_back(key);
    }

    std::sort(candidateKeys.begin(),
              candidateKeys.end(),
              [](const EdgeKey& left, const EdgeKey& right) {
                  return left.low < right.low ||
                         (left.low == right.low && left.high < right.high);
              });

    decltype(edges_) candidateEdges;
    candidateEdges.reserve(candidateKeys.size());
    std::unordered_map<PersonId, EdgeNode*> candidateHeads;
    candidateHeads.reserve(persons_.size());
    for (const auto& personEntry : persons_) {
        candidateHeads.emplace(personEntry.first, nullptr);
    }

    EdgeId candidateNextEdgeId = nextEdgeId_;
    for (const auto& key : candidateKeys) {
        EdgeId edgeId{};
        const auto existingEdge = edges_.find(key);
        if (existingEdge != edges_.end()) {
            edgeId = existingEdge->second->id;
        } else {
            if (candidateNextEdgeId == std::numeric_limits<EdgeId>::max()) {
                throw DomainError(DomainErrorCode::IdentifierExhausted,
                                  "关系边编号已经耗尽");
            }
            edgeId = candidateNextEdgeId;
            ++candidateNextEdgeId;
        }

        const auto coChapterCount = statistics.coOccurrenceCounts.at(key);
        const auto firstChapterCount = statistics.personCounts.at(key.low);
        const auto secondChapterCount = statistics.personCounts.at(key.high);

        auto edge = std::make_unique<EdgeNode>();
        edge->id = edgeId;
        edge->endpointA = key.low;
        edge->endpointB = key.high;
        edge->coChapterCount = coChapterCount;
        edge->jaccard = calculateJaccard(firstChapterCount,
                                          secondChapterCount,
                                          coChapterCount);
        edge->linkA = candidateHeads.at(key.low);
        edge->linkB = candidateHeads.at(key.high);

        auto* rawEdge = edge.get();
        const auto insertion = candidateEdges.emplace(key, std::move(edge));
        if (!insertion.second) {
            throw DomainError(DomainErrorCode::InvalidStatistics,
                              "统计快照中包含重复人物关系");
        }
        candidateHeads.at(key.low) = rawEdge;
        candidateHeads.at(key.high) = rawEdge;
    }

    struct PersonStatisticsAssignment {
        PersonVertex* person{};
        std::uint32_t chapterCount{};
        EdgeNode* firstEdge{};
    };
    std::vector<PersonStatisticsAssignment> assignments;
    assignments.reserve(persons_.size());
    for (const auto& personEntry : persons_) {
        assignments.push_back(PersonStatisticsAssignment{
            personEntry.second.get(),
            statistics.personCounts.at(personEntry.first),
            candidateHeads.at(personEntry.first)});
    }

    static_assert(noexcept(edges_.swap(candidateEdges)),
                  "统计提交需要无异常的边容器交换");
    edges_.swap(candidateEdges);
    nextEdgeId_ = candidateNextEdgeId;
    for (const auto& assignment : assignments) {
        assignment.person->chapterCount = assignment.chapterCount;
        assignment.person->firstEdge = assignment.firstEdge;
    }
}

ValidationReport AdjacencyMultilistGraph::validate() const {
    ValidationReport report;
    std::unordered_set<std::string> personNames;
    std::unordered_set<EdgeId> edgeIds;
    std::unordered_set<EdgeKey, EdgeKeyHash> endpointPairs;
    std::unordered_set<const EdgeNode*> ownedEdges;
    std::unordered_map<const EdgeNode*, EdgeOccurrences> occurrences;
    PersonId maximumPersonId = 0;
    EdgeId maximumEdgeId = 0;

    for (const auto& entry : edges_) {
        if (entry.second != nullptr) {
            ownedEdges.insert(entry.second.get());
        }
    }

    for (const auto& entry : persons_) {
        if (entry.second == nullptr) {
            report.add(ValidationSeverity::Error,
                       "person.null",
                       "人物所有权容器中存在空指针");
            continue;
        }
        const auto& person = *entry.second;
        maximumPersonId = std::max(maximumPersonId, person.id);
        if (person.id == 0 || person.id != entry.first) {
            report.add(ValidationSeverity::Error,
                       "person.id.invalid",
                       "人物编号为零或与容器键不一致");
        }
        if (isBlank(person.canonicalName)) {
            report.add(ValidationSeverity::Error,
                       "person.name.empty",
                       "人物标准名称为空");
        } else if (!personNames.insert(person.canonicalName).second) {
            report.add(ValidationSeverity::Error,
                       "person.name.duplicate",
                       "存在重复人物标准名称：" + person.canonicalName);
        }

        const auto nameEntry = personNameIndex_.find(person.canonicalName);
        if (nameEntry == personNameIndex_.end() || nameEntry->second != person.id) {
            report.add(ValidationSeverity::Error,
                       "person.name_index.mismatch",
                       "人物名称索引与人物对象不一致：" + person.canonicalName);
        }
    }

    for (const auto& entry : personNameIndex_) {
        const auto person = persons_.find(entry.second);
        if (person == persons_.end() || person->second == nullptr ||
            person->second->canonicalName != entry.first) {
            report.add(ValidationSeverity::Error,
                       "person.name_index.orphan",
                       "人物名称索引指向不存在或名称不匹配的人物：" + entry.first);
        }
    }

    for (const auto& entry : edges_) {
        if (entry.second == nullptr) {
            report.add(ValidationSeverity::Error,
                       "edge.null",
                       "关系边所有权容器中存在空指针");
            continue;
        }
        const auto& edge = *entry.second;
        maximumEdgeId = std::max(maximumEdgeId, edge.id);
        if (edge.id == 0 || !edgeIds.insert(edge.id).second) {
            report.add(ValidationSeverity::Error,
                       "edge.id.invalid",
                       "关系边编号为零或重复");
        }
        if (edge.endpointA == edge.endpointB) {
            report.add(ValidationSeverity::Error,
                       "edge.self_loop",
                       "关系边形成了人物自环");
        }
        if (edge.endpointA > edge.endpointB) {
            report.add(ValidationSeverity::Error,
                       "edge.endpoint.order",
                       "关系边端点没有按编号规范化");
        }

        const auto normalizedKey = EdgeKey::make(edge.endpointA, edge.endpointB);
        if (!(entry.first == normalizedKey)) {
            report.add(ValidationSeverity::Error,
                       "edge.key.mismatch",
                       "关系边端点与所有权容器键不一致");
        }
        if (!endpointPairs.insert(normalizedKey).second) {
            report.add(ValidationSeverity::Error,
                       "edge.key.duplicate",
                       "存在重复人物关系：" + edgeDescription(edge));
        }

        const auto firstPerson = persons_.find(edge.endpointA);
        const auto secondPerson = persons_.find(edge.endpointB);
        if (firstPerson == persons_.end() || secondPerson == persons_.end() ||
            firstPerson->second == nullptr || secondPerson->second == nullptr) {
            report.add(ValidationSeverity::Error,
                       "edge.endpoint.missing",
                       "关系边引用了不存在的人物：" + edgeDescription(edge));
        }
        if (edge.linkA != nullptr && ownedEdges.find(edge.linkA) == ownedEdges.end()) {
            report.add(ValidationSeverity::Error,
                       "edge.link.invalid",
                       "关系边的 linkA 指向未被图拥有的边");
        }
        if (edge.linkB != nullptr && ownedEdges.find(edge.linkB) == ownedEdges.end()) {
            report.add(ValidationSeverity::Error,
                       "edge.link.invalid",
                       "关系边的 linkB 指向未被图拥有的边");
        }

        bool statisticsUsable = true;
        if (edge.coChapterCount == 0) {
            report.add(ValidationSeverity::Error,
                       "edge.co_count.invalid",
                       "共同章节数必须大于零");
            statisticsUsable = false;
        }
        if (firstPerson != persons_.end() && secondPerson != persons_.end() &&
            firstPerson->second != nullptr && secondPerson->second != nullptr &&
            (edge.coChapterCount > firstPerson->second->chapterCount ||
             edge.coChapterCount > secondPerson->second->chapterCount)) {
            report.add(ValidationSeverity::Error,
                       "edge.co_count.exceeds_person",
                       "共同章节数超过了端点人物的出现章节数");
            statisticsUsable = false;
        }
        if (!std::isfinite(edge.jaccard) || edge.jaccard <= 0.0 ||
            edge.jaccard > 1.0) {
            report.add(ValidationSeverity::Error,
                       "edge.jaccard.range",
                       "Jaccard 关联度不在合法范围 (0, 1] 内");
        }
        if (statisticsUsable && firstPerson != persons_.end() &&
            secondPerson != persons_.end() && firstPerson->second != nullptr &&
            secondPerson->second != nullptr) {
            const double expected = calculateJaccard(
                firstPerson->second->chapterCount,
                secondPerson->second->chapterCount,
                edge.coChapterCount);
            if (std::abs(edge.jaccard - expected) > jaccardEpsilon_) {
                report.add(ValidationSeverity::Error,
                           "edge.jaccard.mismatch",
                           "Jaccard 关联度与人物章节统计不一致");
            }
        }
    }

    for (const auto& entry : persons_) {
        if (entry.second == nullptr) {
            continue;
        }
        const auto& person = *entry.second;
        std::unordered_set<const EdgeNode*> seenInChain;
        auto* edge = person.firstEdge;
        while (edge != nullptr) {
            if (ownedEdges.find(edge) == ownedEdges.end()) {
                report.add(ValidationSeverity::Error,
                           "edge.chain.invalid_pointer",
                           "人物边链指向未被图拥有的边");
                break;
            }
            if (!seenInChain.insert(edge).second) {
                report.add(ValidationSeverity::Error,
                           "edge.chain.cycle",
                           "人物边链存在环或重复边结点");
                break;
            }
            if (edge->endpointA != person.id && edge->endpointB != person.id) {
                report.add(ValidationSeverity::Error,
                           "edge.chain.unrelated_vertex",
                           "关系边出现在无关人物的边链中");
                break;
            }

            auto& occurrence = occurrences[edge];
            ++occurrence.total;
            if (edge->endpointA == person.id) {
                occurrence.atEndpointA = true;
            }
            if (edge->endpointB == person.id) {
                occurrence.atEndpointB = true;
            }
            edge = nextEdge(edge, person.id);
        }
    }

    for (const auto* edge : ownedEdges) {
        const auto iterator = occurrences.find(edge);
        if (iterator == occurrences.end() || iterator->second.total != 2 ||
            !iterator->second.atEndpointA || !iterator->second.atEndpointB) {
            report.add(ValidationSeverity::Error,
                       "edge.chain.occurrence",
                       "每条关系边必须在两个端点边链中各出现一次");
        }
    }

    if (nextPersonId_ == 0 ||
        (!persons_.empty() && nextPersonId_ <= maximumPersonId)) {
        report.add(ValidationSeverity::Error,
                   "person.next_id.invalid",
                   "下一个人物编号没有保持单调递增");
    }
    if (nextEdgeId_ == 0 || (!edges_.empty() && nextEdgeId_ <= maximumEdgeId)) {
        report.add(ValidationSeverity::Error,
                   "edge.next_id.invalid",
                   "下一个关系边编号没有保持单调递增");
    }

    return report;
}

void AdjacencyMultilistGraph::ensureValidState() const {
    const auto report = validate();
    if (report.isValid()) {
        return;
    }

    std::ostringstream message;
    message << "人物关系图结构验证失败";
    for (const auto& issue : report.issues) {
        if (issue.severity == ValidationSeverity::Error) {
            message << " [" << issue.code << "] " << issue.message;
            break;
        }
    }
    throw DomainError(DomainErrorCode::GraphValidationFailed, message.str());
}

}  // namespace novel
