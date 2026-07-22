#pragma once

#include "application/ApplicationDtos.h"
#include "application/Result.h"
#include "domain/project/NovelRelationProject.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace novel {
class IProjectSerializer;
}

namespace novel::application {

class ProjectApplicationService final {
public:
    ProjectApplicationService();
    explicit ProjectApplicationService(
        std::shared_ptr<IProjectSerializer> serializer);

    ProjectStatusDto status() const noexcept;
    Result<ProjectStatusDto> newProject();
    Result<ProjectStatusDto> newProject(const NewProjectCommand& command);
    Result<ProjectStatusDto> newProjectFromFiles(
        const NewProjectFileOptions& options);
    Result<ProjectStatusDto> openProject(const std::filesystem::path& path);
    Result<ProjectStatusDto> save();
    Result<ProjectStatusDto> saveAs(const std::filesystem::path& path);

    Result<ChapterPreviewDto> previewChapterFile(
        const std::filesystem::path& path) const;
    Result<ChapterPreviewDto> previewChapterText(
        const std::filesystem::path& sourcePath,
        std::string_view contentUtf8) const;
    Result<ChapterPreviewDto> previewChapterReextraction(
        ChapterId id) const;
    Result<ChapterId> importChapter(const ImportChapterCommand& command);
    Result<ChapterId> confirmChapterImport(
        const ImportChapterCommand& command) {
        return importChapter(command);
    }
    Result<void> modifyChapter(const ModifyChapterCommand& command);
    Result<void> reextractChapter(ChapterId id);
    Result<void> deleteChapter(ChapterId id);

    Result<PersonId> addPerson(const std::string& canonicalName);
    Result<void> renamePerson(PersonId id, const std::string& newName);
    Result<void> mergePersons(PersonId source, PersonId target);
    Result<void> deleteUnusedPerson(PersonId id);
    Result<void> addAlias(const std::string& alias, PersonId target);
    Result<void> removeAlias(const std::string& alias);

    std::vector<ChapterRowDto> chapters() const;
    std::vector<PersonRowDto> people() const;
    std::vector<RelationRowDto> relations() const;
    std::vector<AliasRowDto> aliases() const;
    Result<ChapterDetailDto> chapterDetail(ChapterId id) const;
    Result<PersonDetailDto> personDetail(PersonId id) const;
    Result<RelationDetailDto> relationDetail(EdgeId id) const;

    Result<GraphSnapshotDto> graphSnapshot(double minimumJaccard) const;
    Result<TraversalResultDto> depthFirst(PersonId start) const;
    Result<TraversalResultDto> breadthFirst(PersonId start) const;

private:
    NovelRelationProject project_;
    std::filesystem::path filePath_;
    bool dirty_{};
    // Revision zero is reserved as the command DTO's "unchecked" sentinel.
    std::uint64_t revision_{1};
    std::shared_ptr<IProjectSerializer> serializer_;
};

}  // namespace novel::application
