#include "domain/model/ProjectSnapshot.h"
#include "domain/project/NovelRelationProject.h"
#include "domain/traversal/GraphTraversal.h"
#include "infrastructure/persistence/BinaryProjectSerializer.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::milliseconds;
using novel::BinaryProjectSerializer;
using novel::ChapterId;
using novel::ChapterRecord;
using novel::EdgeId;
using novel::EdgeKey;
using novel::EdgeSnapshot;
using novel::NovelRelationProject;
using novel::PersonId;
using novel::PersonSnapshot;
using novel::ProjectSnapshot;
using novel::TraversalResult;
using novel::TraversalScope;
using novel::breadthFirstSearch;
using novel::depthFirstSearch;

struct TestContext {
    int failures{};

    void check(bool condition, const std::string& message) {
        if (!condition) {
            ++failures;
            std::cerr << "失败：" << message << '\n';
        }
    }
};

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto stamp = static_cast<unsigned long long>(
            Clock::now().time_since_epoch().count());
        path_ = std::filesystem::temp_directory_path() /
                ("novel-relation-phase-six-" + std::to_string(stamp));
        if (!std::filesystem::create_directory(path_)) {
            throw std::runtime_error("无法创建阶段六性能测试临时目录");
        }
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    std::filesystem::path file(const std::string& name) const {
        return path_ / std::filesystem::u8path(name);
    }

private:
    std::filesystem::path path_;
};

template <typename Action>
void runCase(TestContext& context,
             const std::string& name,
             Action action) {
    try {
        action();
    } catch (const std::exception& error) {
        context.check(false, name + "抛出意外异常：" + error.what());
    } catch (...) {
        context.check(false, name + "抛出未知异常");
    }
}

Milliseconds elapsedSince(Clock::time_point start) {
    return std::chrono::duration_cast<Milliseconds>(Clock::now() - start);
}

void reportStage(TestContext& context,
                 const std::string& name,
                 Milliseconds elapsed,
                 Milliseconds limit) {
    std::cout << "[性能] " << name << "：" << elapsed.count()
              << " ms（Debug 上限 " << limit.count() << " ms）\n";
    context.check(elapsed <= limit,
                  name + "耗时 " + std::to_string(elapsed.count()) +
                      " ms，超过 Debug 上限 " +
                      std::to_string(limit.count()) + " ms");
}

std::string personName(PersonId id) {
    return "Person-" + std::to_string(id);
}

ProjectSnapshot makeRingSnapshot(std::size_t personCount) {
    if (personCount < 3U ||
        personCount >= std::numeric_limits<PersonId>::max()) {
        throw std::invalid_argument("环形性能测试规模超出人物编号范围");
    }

    ProjectSnapshot snapshot;
    snapshot.nextPersonId = static_cast<PersonId>(personCount + 1U);
    snapshot.nextEdgeId = static_cast<EdgeId>(personCount + 1U);
    snapshot.nextChapterId = static_cast<ChapterId>(personCount + 1U);
    snapshot.persons.reserve(personCount);
    snapshot.edges.reserve(personCount);
    snapshot.chapters.reserve(personCount);

    for (std::size_t index = 0; index < personCount; ++index) {
        const auto id = static_cast<PersonId>(index + 1U);
        snapshot.persons.push_back(PersonSnapshot{id, personName(id), 2U});
    }

    constexpr double expectedJaccard = 1.0 / 3.0;
    for (std::size_t index = 0; index < personCount; ++index) {
        const auto current = static_cast<PersonId>(index + 1U);
        const auto next = static_cast<PersonId>(
            ((index + 1U) % personCount) + 1U);
        const EdgeKey endpoints = EdgeKey::make(current, next);
        const auto sequence = std::to_string(index + 1U);

        snapshot.edges.push_back(
            EdgeSnapshot{static_cast<EdgeId>(index + 1U),
                         endpoints.low,
                         endpoints.high,
                         1U,
                         expectedJaccard});
        snapshot.chapters.push_back(
            ChapterRecord{static_cast<ChapterId>(index + 1U),
                          "chapter-" + sequence,
                          "Chapter " + sequence,
                          "chapter-" + sequence + ".txt",
                          "Synthetic ring chapter " + sequence,
                          std::vector<PersonId>{endpoints.low,
                                                endpoints.high}});
    }

    return snapshot;
}

bool equalSnapshots(const ProjectSnapshot& first,
                    const ProjectSnapshot& second) {
    if (first.nextPersonId != second.nextPersonId ||
        first.nextEdgeId != second.nextEdgeId ||
        first.nextChapterId != second.nextChapterId ||
        first.persons.size() != second.persons.size() ||
        first.edges.size() != second.edges.size() ||
        first.aliases != second.aliases ||
        first.chapters.size() != second.chapters.size()) {
        return false;
    }

    for (std::size_t index = 0; index < first.persons.size(); ++index) {
        const auto& left = first.persons[index];
        const auto& right = second.persons[index];
        if (left.id != right.id ||
            left.canonicalName != right.canonicalName ||
            left.chapterCount != right.chapterCount) {
            return false;
        }
    }

    for (std::size_t index = 0; index < first.edges.size(); ++index) {
        const auto& left = first.edges[index];
        const auto& right = second.edges[index];
        if (left.id != right.id || left.endpointA != right.endpointA ||
            left.endpointB != right.endpointB ||
            left.coChapterCount != right.coChapterCount ||
            left.jaccard != right.jaccard) {
            return false;
        }
    }

    for (std::size_t index = 0; index < first.chapters.size(); ++index) {
        const auto& left = first.chapters[index];
        const auto& right = second.chapters[index];
        if (left.id != right.id || left.chapterKey != right.chapterKey ||
            left.title != right.title ||
            left.sourceFileName != right.sourceFileName ||
            left.contentUtf8 != right.contentUtf8 ||
            left.persons != right.persons) {
            return false;
        }
    }

    return true;
}

bool traversalCoversProject(const TraversalResult& traversal,
                            std::size_t personCount) {
    if (traversal.order.size() != personCount || traversal.order.empty() ||
        traversal.order.front() != 1U) {
        return false;
    }

    std::vector<std::uint8_t> seen(personCount + 1U, 0U);
    for (const auto id : traversal.order) {
        const auto position = static_cast<std::size_t>(id);
        if (id == 0U || position > personCount || seen[position] != 0U ||
            traversal.tree.findNode(id) == nullptr) {
            return false;
        }
        seen[position] = 1U;
    }
    for (std::size_t id = 1U; id <= personCount; ++id) {
        if (seen[id] == 0U) {
            return false;
        }
    }
    return true;
}

void testLargeRingProject(TestContext& context,
                          const TemporaryDirectory& temporary,
                          std::size_t personCount) {
    const Milliseconds constructionLimit{15000};
    const Milliseconds queryLimit{5000};
    const Milliseconds traversalLimit{5000};
    const Milliseconds persistenceLimit{15000};

    NovelRelationProject project;
    const auto constructionStarted = Clock::now();
    auto snapshot = makeRingSnapshot(personCount);
    project = NovelRelationProject::fromSnapshot(std::move(snapshot));
    const bool projectIsValid = project.validate().isValid();
    const auto constructionElapsed = elapsedSince(constructionStarted);

    context.check(projectIsValid, "五千人物环形项目应通过完整领域验证");
    context.check(project.graph().personCount() == personCount,
                  "构造后应保留全部人物");
    context.check(project.graph().edgeCount() == personCount,
                  "构造后应保留全部环形关系边");
    context.check(project.chapters().size() == personCount,
                  "构造后应保留全部章节");
    reportStage(context,
                "快照构造、fromSnapshot 与完整验证",
                constructionElapsed,
                constructionLimit);

    bool allQueriesCorrect = true;
    constexpr double expectedJaccard = 1.0 / 3.0;
    const auto queryStarted = Clock::now();
    for (std::size_t index = 0; index < personCount; ++index) {
        const auto current = static_cast<PersonId>(index + 1U);
        const auto next = static_cast<PersonId>(
            ((index + 1U) % personCount) + 1U);
        const auto resolved = project.resolvePersonName(personName(current));
        const auto* person = project.graph().findPerson(current);
        const auto* edge = project.graph().findEdge(current, next);

        allQueriesCorrect =
            allQueriesCorrect && resolved.has_value() &&
            *resolved == current && person != nullptr &&
            person->chapterCount == 2U && edge != nullptr &&
            edge->id == static_cast<EdgeId>(index + 1U) &&
            edge->coChapterCount == 1U &&
            std::abs(edge->jaccard - expectedJaccard) <= 1e-12;
    }
    const auto queryElapsed = elapsedSince(queryStarted);

    context.check(allQueriesCorrect,
                  "批量名称与环形关系查询应返回正确人物及统计值");
    context.check(!project.resolvePersonName("Person-not-found").has_value(),
                  "不存在的人物名称不应被解析");
    context.check(project.graph().findEdge(1U, 3U) == nullptr,
                  "非相邻人物之间不应存在环形关系边");
    reportStage(context, "批量名称与关系边查询", queryElapsed, queryLimit);

    TraversalResult dfs;
    TraversalResult bfs;
    const auto traversalStarted = Clock::now();
    dfs = depthFirstSearch(project.graph(),
                           1U,
                           TraversalScope::ReachableComponent);
    bfs = breadthFirstSearch(project.graph(),
                             1U,
                             TraversalScope::ReachableComponent);
    const auto traversalElapsed = elapsedSince(traversalStarted);

    context.check(traversalCoversProject(dfs, personCount),
                  "DFS 应无重复地覆盖全部五千人物并生成树结点");
    context.check(traversalCoversProject(bfs, personCount),
                  "BFS 应无重复地覆盖全部五千人物并生成树结点");
    reportStage(context, "DFS 与 BFS 全图遍历", traversalElapsed,
                traversalLimit);

    const auto expected = project.snapshot();
    const auto projectFile = temporary.file("large-ring-project.nprg");
    BinaryProjectSerializer serializer;
    NovelRelationProject loaded;
    bool loadedIsValid = false;
    ProjectSnapshot loadedSnapshot;

    const auto persistenceStarted = Clock::now();
    serializer.save(project, projectFile);
    loaded = serializer.load(projectFile);
    loadedIsValid = loaded.validate().isValid();
    loadedSnapshot = loaded.snapshot();
    const auto persistenceElapsed = elapsedSince(persistenceStarted);

    std::error_code fileError;
    const auto fileSize = std::filesystem::file_size(projectFile, fileError);
    context.check(!fileError && fileSize > 0U,
                  "二进制性能测试项目应成功写入非空文件");
    context.check(loadedIsValid,
                  "五千人物项目重新加载后应通过完整领域验证");
    context.check(equalSnapshots(expected, loadedSnapshot),
                  "二进制保存加载应逐字段恢复五千人物项目");
    reportStage(context,
                "二进制保存、加载与往返验证",
                persistenceElapsed,
                persistenceLimit);
}

}  // namespace

int main() {
    TestContext context;
    constexpr std::size_t personCount = 5000U;

    runCase(context, "阶段六五千人物性能测试", [&]() {
        TemporaryDirectory temporary;
        testLargeRingProject(context, temporary, personCount);
    });

    if (context.failures == 0) {
        std::cout << "阶段六性能测试全部通过。\n";
        return 0;
    }
    std::cerr << context.failures << " 项测试失败。\n";
    return 1;
}
