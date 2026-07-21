#pragma once

#include "domain/alias/AliasDictionary.h"
#include "domain/chapter/ChapterCollection.h"
#include "domain/graph/AdjacencyMultilistGraph.h"
#include "domain/model/ProjectSnapshot.h"
#include "domain/validation/ValidationReport.h"

#include <optional>
#include <string>

namespace novel {

class ProjectTestAccess;

class NovelRelationProject {
public:
    NovelRelationProject() = default;
    ~NovelRelationProject() = default;

    NovelRelationProject(const NovelRelationProject&) = delete;
    NovelRelationProject& operator=(const NovelRelationProject&) = delete;
    NovelRelationProject(NovelRelationProject&&) noexcept = default;
    NovelRelationProject& operator=(NovelRelationProject&&) noexcept = default;

    const AdjacencyMultilistGraph& graph() const noexcept;
    const ChapterCollection& chapters() const noexcept;
    const AliasDictionary& aliases() const noexcept;

    PersonId addPerson(const std::string& canonicalName);
    bool renamePerson(PersonId id, const std::string& newName);
    bool removePerson(PersonId id);

    void addAlias(std::string alias, PersonId target);
    bool removeAlias(const std::string& alias);
    std::optional<PersonId> resolvePersonName(const std::string& name) const noexcept;

    ChapterId addChapter(ChapterDraft draft);
    bool modifyChapter(ChapterId id, ChapterDraft draft);
    bool removeChapter(ChapterId id);
    void rebuildStatistics();

    ValidationReport validate() const;

    ProjectSnapshot snapshot() const;
    static NovelRelationProject fromSnapshot(ProjectSnapshot snapshot);

private:
    AdjacencyMultilistGraph graph_;
    ChapterCollection chapters_;
    AliasDictionary aliases_;

    void rebuildAndCommit(ChapterCollection candidate);

    friend class ProjectTestAccess;
};

}  // namespace novel
