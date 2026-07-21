#include "domain/project/NovelRelationProject.h"

#include "domain/error/DomainError.h"
#include "domain/statistics/GraphStatisticsBuilder.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>

namespace novel {

namespace {

constexpr double jaccardEpsilon = 1e-9;

bool isBlank(const std::string& value) noexcept {
    return value.empty() ||
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isspace(character) != 0;
           });
}

std::string personDescription(PersonId id) {
    return std::to_string(id);
}

std::string edgeDescription(const EdgeKey& key) {
    return std::to_string(key.low) + "-" + std::to_string(key.high);
}

}  // namespace

const AdjacencyMultilistGraph& NovelRelationProject::graph() const noexcept {
    return graph_;
}

const ChapterCollection& NovelRelationProject::chapters() const noexcept {
    return chapters_;
}

const AliasDictionary& NovelRelationProject::aliases() const noexcept {
    return aliases_;
}

PersonId NovelRelationProject::addPerson(const std::string& canonicalName) {
    if (aliases_.resolve(canonicalName).has_value()) {
        throw DomainError(DomainErrorCode::NameConflict,
                          "标准人物名与已有别名冲突：" + canonicalName);
    }
    return graph_.addPerson(canonicalName);
}

bool NovelRelationProject::renamePerson(PersonId id,
                                        const std::string& newName) {
    if (graph_.findPerson(id) == nullptr) {
        return false;
    }
    if (aliases_.resolve(newName).has_value()) {
        throw DomainError(DomainErrorCode::NameConflict,
                          "标准人物名与已有别名冲突：" + newName);
    }
    return graph_.renamePerson(id, newName);
}

bool NovelRelationProject::removePerson(PersonId id) {
    if (graph_.findPerson(id) == nullptr) {
        return false;
    }

    for (const auto& chapter : chapters_.all()) {
        if (std::find(chapter.persons.begin(), chapter.persons.end(), id) !=
            chapter.persons.end()) {
            throw DomainError(
                DomainErrorCode::PersonInUse,
                "人物仍被章节引用，不能删除：" + personDescription(id));
        }
    }
    for (const auto& alias : aliases_.entries()) {
        if (alias.second == id) {
            throw DomainError(
                DomainErrorCode::PersonInUse,
                "人物仍被别名引用，不能删除：" + personDescription(id));
        }
    }
    return graph_.removePerson(id);
}

void NovelRelationProject::addAlias(std::string alias, PersonId target) {
    if (graph_.findPerson(target) == nullptr) {
        throw DomainError(DomainErrorCode::PersonNotFound,
                          "别名目标人物不存在：" + personDescription(target));
    }
    if (graph_.findPersonByName(alias) != nullptr) {
        throw DomainError(DomainErrorCode::NameConflict,
                          "人物别名与标准人物名冲突：" + alias);
    }
    aliases_.addAlias(std::move(alias), target);
}

bool NovelRelationProject::removeAlias(const std::string& alias) {
    return aliases_.removeAlias(alias);
}

std::optional<PersonId> NovelRelationProject::resolvePersonName(
    const std::string& name) const noexcept {
    if (const auto* person = graph_.findPersonByName(name); person != nullptr) {
        return person->id;
    }
    return aliases_.resolve(name);
}

ChapterId NovelRelationProject::addChapter(ChapterDraft draft) {
    ChapterCollection candidate = chapters_;
    const ChapterId id = candidate.add(std::move(draft));
    rebuildAndCommit(std::move(candidate));
    return id;
}

bool NovelRelationProject::modifyChapter(ChapterId id, ChapterDraft draft) {
    ChapterCollection candidate = chapters_;
    if (!candidate.modify(id, std::move(draft))) {
        return false;
    }
    rebuildAndCommit(std::move(candidate));
    return true;
}

bool NovelRelationProject::removeChapter(ChapterId id) {
    ChapterCollection candidate = chapters_;
    if (!candidate.remove(id)) {
        return false;
    }
    rebuildAndCommit(std::move(candidate));
    return true;
}

void NovelRelationProject::rebuildStatistics() {
    GraphStatisticsBuilder::rebuild(graph_, chapters_);
}

void NovelRelationProject::rebuildAndCommit(ChapterCollection candidate) {
    GraphStatisticsBuilder::rebuild(graph_, candidate);
    chapters_.swap(candidate);
}

ValidationReport NovelRelationProject::validate() const {
    ValidationReport report = graph_.validate();
    const auto chapterReport = chapters_.validate();
    report.issues.insert(report.issues.end(),
                         chapterReport.issues.begin(),
                         chapterReport.issues.end());

    std::unordered_set<ChapterId> chapterIds;
    std::unordered_set<std::string> chapterKeys;
    ChapterId previousChapterId{};
    for (const auto& chapter : chapters_.all()) {
        if (chapter.id == 0 || !chapterIds.insert(chapter.id).second) {
            report.add(ValidationSeverity::Error,
                       "project.chapter.id.invalid",
                       "章节编号必须非零且在项目中唯一");
        }
        if (previousChapterId != 0 && chapter.id <= previousChapterId) {
            report.add(ValidationSeverity::Error,
                       "project.chapter.id.order",
                       "章节记录必须按内部编号递增保存");
        }
        previousChapterId = chapter.id;

        if (isBlank(chapter.chapterKey)) {
            report.add(ValidationSeverity::Error,
                       "project.chapter.key.empty",
                       "章节键不能为空或仅包含空白字符");
        } else if (!chapterKeys.insert(chapter.chapterKey).second) {
            report.add(ValidationSeverity::Error,
                       "project.chapter.key.duplicate",
                       "章节键在项目中重复：" + chapter.chapterKey);
        }

        if (!std::is_sorted(chapter.persons.begin(), chapter.persons.end()) ||
            std::adjacent_find(chapter.persons.begin(), chapter.persons.end()) !=
                chapter.persons.end()) {
            report.add(ValidationSeverity::Error,
                       "project.chapter.persons.not_unique",
                       "章节人物编号必须排序且不得重复：" +
                           chapter.chapterKey);
        }
        for (const auto personId : chapter.persons) {
            if (graph_.findPerson(personId) == nullptr) {
                report.add(ValidationSeverity::Error,
                           "project.chapter.person.missing",
                           "章节引用了不存在的人物编号：" +
                               personDescription(personId));
            }
        }
    }

    for (const auto& alias : aliases_.entries()) {
        if (isBlank(alias.first)) {
            report.add(ValidationSeverity::Error,
                       "project.alias.empty",
                       "人物别名不能为空或仅包含空白字符");
        }
        if (graph_.findPerson(alias.second) == nullptr) {
            report.add(ValidationSeverity::Error,
                       "project.alias.target_missing",
                       "人物别名指向不存在的人物编号：" +
                           personDescription(alias.second));
        }
        if (graph_.findPersonByName(alias.first) != nullptr) {
            report.add(ValidationSeverity::Error,
                       "project.alias.name_conflict",
                       "人物别名与标准人物名冲突：" + alias.first);
        }
    }

    StatisticsSnapshot expected;
    try {
        expected = GraphStatisticsBuilder::analyze(graph_, chapters_);
    } catch (const DomainError& error) {
        report.add(ValidationSeverity::Error,
                   "project.statistics.rebuild_failed",
                   std::string("无法根据章节重算统计：") + error.what());
        return report;
    }

    for (const auto personId : graph_.personIds()) {
        const auto* person = graph_.findPerson(personId);
        const auto expectedCount = expected.personCounts.find(personId);
        if (person == nullptr || expectedCount == expected.personCounts.end() ||
            person->chapterCount != expectedCount->second) {
            report.add(ValidationSeverity::Error,
                       "project.statistics.person_count.mismatch",
                       "人物出现章节数与章节记录不一致：" +
                           personDescription(personId));
        }
    }

    std::size_t expectedEdgeCount{};
    for (const auto& entry : expected.coOccurrenceCounts) {
        if (entry.second == 0) {
            continue;
        }
        ++expectedEdgeCount;
        const auto* edge = graph_.findEdge(entry.first.low, entry.first.high);
        if (edge == nullptr) {
            report.add(ValidationSeverity::Error,
                       "project.statistics.edge.missing",
                       "章节统计对应的人物关系边不存在：" +
                           edgeDescription(entry.first));
            continue;
        }
        if (edge->coChapterCount != entry.second) {
            report.add(ValidationSeverity::Error,
                       "project.statistics.co_count.mismatch",
                       "人物共同章节数与章节记录不一致：" +
                           edgeDescription(entry.first));
        }

        const auto countA = expected.personCounts.at(entry.first.low);
        const auto countB = expected.personCounts.at(entry.first.high);
        const auto denominator = static_cast<std::uint64_t>(countA) +
                                 static_cast<std::uint64_t>(countB) -
                                 static_cast<std::uint64_t>(entry.second);
        const double expectedJaccard =
            static_cast<double>(entry.second) /
            static_cast<double>(denominator);
        if (!std::isfinite(edge->jaccard) ||
            std::abs(edge->jaccard - expectedJaccard) > jaccardEpsilon) {
            report.add(ValidationSeverity::Error,
                       "project.statistics.jaccard.mismatch",
                       "Jaccard 关联度与章节记录不一致：" +
                           edgeDescription(entry.first));
        }
    }

    if (graph_.edgeCount() != expectedEdgeCount) {
        report.add(ValidationSeverity::Error,
                   "project.statistics.edge_count.mismatch",
                   "人物关系边数量与章节统计不一致");
    }
    for (const auto& key : graph_.edgeKeys()) {
        const auto expectedCount = expected.coOccurrenceCounts.find(key);
        if (expectedCount == expected.coOccurrenceCounts.end() ||
            expectedCount->second == 0) {
            report.add(ValidationSeverity::Error,
                       "project.statistics.edge.extra",
                       "人物关系图包含章节统计中不存在的边：" +
                           edgeDescription(key));
        }
    }

    return report;
}

}  // namespace novel
