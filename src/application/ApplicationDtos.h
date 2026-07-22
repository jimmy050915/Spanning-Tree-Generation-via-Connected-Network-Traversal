#pragma once

#include "domain/model/ChapterTypes.h"
#include "domain/model/GraphTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace novel::application {

struct ProjectStatusDto {
    std::filesystem::path filePath;
    bool dirty{};
    std::uint64_t revision{};
    std::size_t personCount{};
    std::size_t relationCount{};
    std::size_t chapterCount{};
};

enum class ChapterStatus {
    Normal,
    NeedsReview
};

struct ChapterRowDto {
    ChapterId id{};
    std::string key;
    std::string title;
    std::string sourceFile;
    std::size_t personCount{};
    ChapterStatus status{ChapterStatus::Normal};
};

struct PersonRowDto {
    PersonId id{};
    std::string name;
    std::uint32_t chapterCount{};
    std::size_t degree{};
    std::optional<PersonId> strongestPerson;
    std::string strongestPersonName;
    double strongestJaccard{};
};

struct RelationRowDto {
    EdgeId id{};
    PersonId personA{};
    PersonId personB{};
    std::string personAName;
    std::string personBName;
    std::uint32_t coChapterCount{};
    double jaccard{};
};

struct AliasRowDto {
    std::string alias;
    PersonId targetPerson{};
    std::string targetName;
};

struct NameMatchDto {
    std::string matchedText;
    bool isAlias{};
    PersonId person{};
    std::string canonicalName;
    std::size_t byteOffset{};
    std::size_t byteLength{};
};

struct ChapterPreviewDto {
    std::uint64_t revision{};
    std::filesystem::path path;
    std::string key;
    std::string title;
    std::string body;
    std::vector<NameMatchDto> matches;
    std::vector<PersonId> selectedPersonIds;
};

struct StagedAliasCommand {
    std::string alias;
    // Must name a standard person that already exists or is staged in the
    // same command. Alias-to-alias chains are deliberately unsupported.
    std::string targetCanonicalName;
};

struct NewProjectCommand {
    std::vector<std::string> canonicalNames;
    std::vector<StagedAliasCommand> aliases;
};

struct NewProjectFileOptions {
    std::filesystem::path personsFile;
    std::filesystem::path aliasesFile;
    std::size_t maximumBytes{20U * 1024U * 1024U};
};

struct ImportChapterCommand {
    // Zero preserves compatibility for callers that did not originate from a
    // preview. Service revisions start at one, so every preview revision is
    // non-zero and therefore participates in stale-dialog detection.
    std::uint64_t expectedRevision{};
    std::filesystem::path sourcePath;
    std::string key;
    std::string title;
    std::string body;
    std::vector<PersonId> selectedPersonIds;
    // Every staged standard person is selected for the chapter.
    std::vector<std::string> newCanonicalNames;
    std::vector<StagedAliasCommand> newAliases;
};

struct ModifyChapterCommand {
    std::uint64_t expectedRevision{};
    ChapterId id{};
    std::filesystem::path sourcePath;
    std::string key;
    std::string title;
    std::string body;
    std::vector<PersonId> selectedPersonIds;
    // Staged dictionary edits and the chapter change commit atomically.
    std::vector<std::string> newCanonicalNames;
    std::vector<StagedAliasCommand> newAliases;
};

struct PersonDetailDto {
    PersonRowDto person;
    std::vector<std::string> aliases;
    std::vector<ChapterRowDto> chapters;
    std::vector<RelationRowDto> relations;
    std::optional<RelationRowDto> strongestRelation;
};

struct RelationDetailDto {
    RelationRowDto relation;
    std::vector<ChapterRowDto> commonChapters;
};

struct ChapterDetailDto {
    ChapterRowDto chapter;
    std::string body;
    std::vector<PersonRowDto> persons;
};

struct GraphNodeDto {
    PersonId id{};
    std::string canonicalName;
    std::uint32_t chapterCount{};
};

struct GraphEdgeDto {
    EdgeId id{};
    PersonId endpointA{};
    PersonId endpointB{};
    std::string endpointAName;
    std::string endpointBName;
    std::uint32_t coChapterCount{};
    double jaccard{};
};

struct GraphSnapshotDto {
    std::uint64_t revision{};
    double minimumJaccard{};
    std::vector<GraphNodeDto> nodes;
    std::vector<GraphEdgeDto> edges;
};

enum class TraversalKind {
    DepthFirst,
    BreadthFirst
};

struct TraversalNodeDto {
    PersonId id{};
    std::string canonicalName;
    bool virtualRoot{};
};

struct TraversalTreeEdgeDto {
    PersonId parent{};
    PersonId child{};
};

struct TraversalResultDto {
    TraversalKind kind{TraversalKind::DepthFirst};
    PersonId start{};
    std::vector<PersonId> order;
    std::vector<std::string> orderNames;
    std::vector<TraversalNodeDto> nodes;
    std::vector<TraversalTreeEdgeDto> treeEdges;
};

}  // namespace novel::application
