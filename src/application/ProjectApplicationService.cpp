#include "application/ProjectApplicationService.h"

#include "domain/error/DomainError.h"
#include "domain/traversal/GraphTraversal.h"
#include "infrastructure/persistence/BinaryProjectSerializer.h"
#include "infrastructure/persistence/PersistenceError.h"
#include "infrastructure/text/ChapterTextParser.h"
#include "infrastructure/text/DictionaryNameExtractor.h"
#include "infrastructure/text/DictionaryTextParser.h"
#include "infrastructure/text/TextFileError.h"
#include "infrastructure/text/Utf8TextFileLoader.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace novel::application {

namespace {

class ServiceFailure final : public std::runtime_error {
public:
    ServiceFailure(ApplicationErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}

    ApplicationErrorCode code() const noexcept {
        return code_;
    }

private:
    ApplicationErrorCode code_;
};

ApplicationError mapDomainError(const DomainError& error) {
    ApplicationErrorCode code = ApplicationErrorCode::DomainFailure;
    switch (error.code()) {
        case DomainErrorCode::PersonNotFound:
            code = ApplicationErrorCode::PersonNotFound;
            break;
        case DomainErrorCode::DuplicatePerson:
        case DomainErrorCode::DuplicateChapterKey:
        case DomainErrorCode::DuplicateAlias:
        case DomainErrorCode::NameConflict:
        case DomainErrorCode::PersonInUse:
            code = ApplicationErrorCode::Conflict;
            break;
        case DomainErrorCode::GraphValidationFailed:
        case DomainErrorCode::ProjectValidationFailed:
            code = ApplicationErrorCode::ValidationFailed;
            break;
        default:
            break;
    }
    return ApplicationError{code, error.what()};
}

template <typename T, typename Action>
Result<T> capture(Action&& action) {
    try {
        return Result<T>::success(action());
    } catch (const ServiceFailure& error) {
        return Result<T>::failure(
            ApplicationError{error.code(), error.what()});
    } catch (const DomainError& error) {
        return Result<T>::failure(mapDomainError(error));
    } catch (const TextFileError& error) {
        return Result<T>::failure(ApplicationError{
            ApplicationErrorCode::TextFileFailure, error.what()});
    } catch (const PersistenceError& error) {
        return Result<T>::failure(ApplicationError{
            ApplicationErrorCode::PersistenceFailure, error.what()});
    } catch (const std::invalid_argument& error) {
        return Result<T>::failure(ApplicationError{
            ApplicationErrorCode::InvalidArgument, error.what()});
    } catch (const std::exception& error) {
        return Result<T>::failure(ApplicationError{
            ApplicationErrorCode::UnexpectedFailure, error.what()});
    } catch (...) {
        return Result<T>::failure(ApplicationError{
            ApplicationErrorCode::UnexpectedFailure, "发生未知应用错误"});
    }
}

template <typename Action>
Result<void> captureVoid(Action&& action) {
    try {
        action();
        return Result<void>::success();
    } catch (const ServiceFailure& error) {
        return Result<void>::failure(
            ApplicationError{error.code(), error.what()});
    } catch (const DomainError& error) {
        return Result<void>::failure(mapDomainError(error));
    } catch (const TextFileError& error) {
        return Result<void>::failure(ApplicationError{
            ApplicationErrorCode::TextFileFailure, error.what()});
    } catch (const PersistenceError& error) {
        return Result<void>::failure(ApplicationError{
            ApplicationErrorCode::PersistenceFailure, error.what()});
    } catch (const std::invalid_argument& error) {
        return Result<void>::failure(ApplicationError{
            ApplicationErrorCode::InvalidArgument, error.what()});
    } catch (const std::exception& error) {
        return Result<void>::failure(ApplicationError{
            ApplicationErrorCode::UnexpectedFailure, error.what()});
    } catch (...) {
        return Result<void>::failure(ApplicationError{
            ApplicationErrorCode::UnexpectedFailure, "发生未知应用错误"});
    }
}

void requireValid(const NovelRelationProject& project) {
    const ValidationReport report = project.validate();
    if (report.isValid()) {
        return;
    }
    for (const auto& issue : report.issues) {
        if (issue.severity == ValidationSeverity::Error) {
            throw ServiceFailure(ApplicationErrorCode::ValidationFailed,
                                 "项目一致性校验失败：[" + issue.code +
                                     "] " + issue.message);
        }
    }
    throw ServiceFailure(ApplicationErrorCode::ValidationFailed,
                         "项目一致性校验失败");
}

NovelRelationProject cloneProject(const NovelRelationProject& project) {
    return NovelRelationProject::fromSnapshot(project.snapshot());
}

std::string sourceFileName(const std::filesystem::path& path) {
    return path.empty() ? std::string{} : path.filename().u8string();
}

void requireUtf8(std::string_view text, const std::string& fieldName) {
    if (!isValidUtf8(text)) {
        throw TextFileError(TextFileErrorCode::InvalidUtf8,
                            fieldName + "包含无效 UTF-8 字节序列");
    }
}

std::vector<PersonId> applyStagedDictionary(
    NovelRelationProject& project,
    const std::vector<std::string>& newCanonicalNames,
    const std::vector<StagedAliasCommand>& newAliases) {
    std::vector<PersonId> stagedPeople;
    stagedPeople.reserve(newCanonicalNames.size());
    for (const auto& canonicalName : newCanonicalNames) {
        requireUtf8(canonicalName, "标准人物名");
        stagedPeople.push_back(project.addPerson(canonicalName));
    }

    for (const auto& alias : newAliases) {
        requireUtf8(alias.alias, "人物别名");
        requireUtf8(alias.targetCanonicalName, "别名目标标准人物名");
        const auto* target =
            project.graph().findPersonByName(alias.targetCanonicalName);
        if (target == nullptr) {
            throw ServiceFailure(
                ApplicationErrorCode::PersonNotFound,
                "别名目标标准人物不存在：" + alias.targetCanonicalName);
        }
        project.addAlias(alias.alias, target->id);
    }
    return stagedPeople;
}

std::vector<PersonId> checkedSelection(
    const NovelRelationProject& project,
    std::vector<PersonId> selected,
    const std::vector<PersonId>& stagedPeople) {
    selected.insert(selected.end(), stagedPeople.begin(), stagedPeople.end());
    std::sort(selected.begin(), selected.end());
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
    for (const PersonId id : selected) {
        if (project.graph().findPerson(id) == nullptr) {
            throw ServiceFailure(ApplicationErrorCode::PersonNotFound,
                                 "章节选择了不存在的人物编号：" +
                                     std::to_string(id));
        }
    }
    return selected;
}

ChapterStatus deriveChapterStatus(const NovelRelationProject& project) {
    return project.validate().issues.empty() ? ChapterStatus::Normal
                                             : ChapterStatus::NeedsReview;
}

ChapterRowDto makeChapterRow(
    const ChapterRecord& chapter,
    ChapterStatus status = ChapterStatus::Normal) {
    return ChapterRowDto{chapter.id,
                         chapter.chapterKey,
                         chapter.title,
                         chapter.sourceFileName,
                         chapter.persons.size(),
                         status};
}

PersonRowDto makePersonRow(const NovelRelationProject& project,
                           PersonId id) {
    const auto* person = project.graph().findPerson(id);
    if (person == nullptr) {
        throw ServiceFailure(ApplicationErrorCode::PersonNotFound,
                             "人物不存在：" + std::to_string(id));
    }
    PersonRowDto result;
    result.id = id;
    result.name = person->canonicalName;
    result.chapterCount = person->chapterCount;
    result.degree = project.graph().neighbors(id).size();
    const EdgeNode* strongest = nullptr;
    PersonId strongestNeighbor{};
    for (const PersonId neighbor : project.graph().neighbors(id)) {
        const auto* edge = project.graph().findEdge(id, neighbor);
        if (edge == nullptr) {
            continue;
        }
        if (strongest == nullptr || edge->jaccard > strongest->jaccard ||
            (edge->jaccard == strongest->jaccard &&
             edge->coChapterCount > strongest->coChapterCount) ||
            (edge->jaccard == strongest->jaccard &&
             edge->coChapterCount == strongest->coChapterCount &&
             neighbor < strongestNeighbor)) {
            strongest = edge;
            strongestNeighbor = neighbor;
        }
    }
    if (strongest != nullptr) {
        const auto* neighbor = project.graph().findPerson(strongestNeighbor);
        if (neighbor == nullptr) {
            throw ServiceFailure(ApplicationErrorCode::ValidationFailed,
                                 "最高关联边引用了不存在的人物");
        }
        result.strongestPerson = strongestNeighbor;
        result.strongestPersonName = neighbor->canonicalName;
        result.strongestJaccard = strongest->jaccard;
    }
    return result;
}

RelationRowDto makeRelationRow(const NovelRelationProject& project,
                               const EdgeNode& edge) {
    const auto* first = project.graph().findPerson(edge.endpointA);
    const auto* second = project.graph().findPerson(edge.endpointB);
    if (first == nullptr || second == nullptr) {
        throw ServiceFailure(ApplicationErrorCode::ValidationFailed,
                             "关系边引用了不存在的人物");
    }
    return RelationRowDto{edge.id,
                          edge.endpointA,
                          edge.endpointB,
                          first->canonicalName,
                          second->canonicalName,
                          edge.coChapterCount,
                          edge.jaccard};
}

const EdgeNode* findEdgeById(const NovelRelationProject& project, EdgeId id) {
    for (const auto& key : project.graph().edgeKeys()) {
        const auto* edge = project.graph().findEdge(key.low, key.high);
        if (edge != nullptr && edge->id == id) {
            return edge;
        }
    }
    return nullptr;
}

ChapterPreviewDto previewFromRecord(const NovelRelationProject& project,
                                    std::uint64_t revision,
                                    const std::filesystem::path& path,
                                    std::string key,
                                    std::string title,
                                    std::string body) {
    DictionaryNameExtractor extractor(project);
    const auto detailed = extractor.extractDetailed(body);

    ChapterPreviewDto result;
    result.revision = revision;
    result.path = path;
    result.key = std::move(key);
    result.title = std::move(title);
    result.body = std::move(body);
    result.matches.reserve(detailed.size());
    std::unordered_set<PersonId> selected;
    selected.reserve(detailed.size());
    for (const auto& match : detailed) {
        result.matches.push_back(NameMatchDto{match.matchedText,
                                              match.isAlias,
                                              match.person,
                                              match.canonicalName,
                                              match.byteOffset,
                                              match.byteLength});
        selected.insert(match.person);
    }
    result.selectedPersonIds.assign(selected.begin(), selected.end());
    std::sort(result.selectedPersonIds.begin(), result.selectedPersonIds.end());
    return result;
}

TraversalResultDto makeTraversalDto(const NovelRelationProject& project,
                                    PersonId start,
                                    TraversalKind kind,
                                    const TraversalResult& result) {
    TraversalResultDto dto;
    dto.kind = kind;
    dto.start = start;
    dto.order = result.order;
    dto.orderNames.reserve(result.order.size());
    dto.nodes.reserve(result.order.size() + 1U);

    if (result.tree.findNode(kAllPersonsRootId) != nullptr) {
        dto.nodes.push_back(
            TraversalNodeDto{kAllPersonsRootId, {}, true});
        for (const auto child : result.tree.children(kAllPersonsRootId)) {
            dto.treeEdges.push_back(
                TraversalTreeEdgeDto{kAllPersonsRootId, child});
        }
    }

    for (const PersonId id : result.order) {
        const auto* person = project.graph().findPerson(id);
        if (person == nullptr) {
            throw ServiceFailure(ApplicationErrorCode::ValidationFailed,
                                 "遍历结果包含不存在的人物");
        }
        dto.orderNames.push_back(person->canonicalName);
        dto.nodes.push_back(
            TraversalNodeDto{id, person->canonicalName, false});
        for (const auto child : result.tree.children(id)) {
            dto.treeEdges.push_back(TraversalTreeEdgeDto{id, child});
        }
    }
    return dto;
}

}  // namespace

ProjectApplicationService::ProjectApplicationService()
    : serializer_(std::make_shared<BinaryProjectSerializer>()) {}

ProjectApplicationService::ProjectApplicationService(
    std::shared_ptr<IProjectSerializer> serializer)
    : serializer_(std::move(serializer)) {
    if (serializer_ == nullptr) {
        serializer_ = std::make_shared<BinaryProjectSerializer>();
    }
}

ProjectStatusDto ProjectApplicationService::status() const noexcept {
    return ProjectStatusDto{filePath_,
                            dirty_,
                            revision_,
                            project_.graph().personCount(),
                            project_.graph().edgeCount(),
                            project_.chapters().size()};
}

Result<ProjectStatusDto> ProjectApplicationService::newProject() {
    return newProject(NewProjectCommand{});
}

Result<ProjectStatusDto> ProjectApplicationService::newProject(
    const NewProjectCommand& command) {
    return capture<ProjectStatusDto>([this, &command]() {
        NovelRelationProject candidate;
        applyStagedDictionary(candidate,
                              command.canonicalNames,
                              command.aliases);
        requireValid(candidate);
        project_ = std::move(candidate);
        filePath_.clear();
        dirty_ = true;
        ++revision_;
        return status();
    });
}

Result<ProjectStatusDto> ProjectApplicationService::newProjectFromFiles(
    const NewProjectFileOptions& options) {
    return capture<ProjectStatusDto>([this, &options]() {
        if (options.maximumBytes == 0U) {
            throw ServiceFailure(ApplicationErrorCode::InvalidArgument,
                                 "字典文件大小上限必须大于零");
        }

        NewProjectCommand command;
        if (!options.personsFile.empty()) {
            command.canonicalNames = DictionaryTextParser::parsePersonsFile(
                options.personsFile, options.maximumBytes);
        }
        if (!options.aliasesFile.empty()) {
            const auto parsedAliases = DictionaryTextParser::parseAliasesFile(
                options.aliasesFile,
                command.canonicalNames,
                options.maximumBytes);
            command.aliases.reserve(parsedAliases.size());
            for (const auto& alias : parsedAliases) {
                command.aliases.push_back(
                    StagedAliasCommand{alias.alias, alias.canonicalName});
            }
        }

        NovelRelationProject candidate;
        applyStagedDictionary(candidate,
                              command.canonicalNames,
                              command.aliases);
        requireValid(candidate);
        project_ = std::move(candidate);
        filePath_.clear();
        dirty_ = true;
        ++revision_;
        return status();
    });
}

Result<ProjectStatusDto> ProjectApplicationService::openProject(
    const std::filesystem::path& path) {
    return capture<ProjectStatusDto>([this, &path]() {
        if (path.empty()) {
            throw ServiceFailure(ApplicationErrorCode::ProjectPathRequired,
                                 "打开项目时必须提供文件路径");
        }
        NovelRelationProject candidate = serializer_->load(path);
        requireValid(candidate);
        project_ = std::move(candidate);
        filePath_ = path;
        dirty_ = false;
        ++revision_;
        return status();
    });
}

Result<ProjectStatusDto> ProjectApplicationService::save() {
    return capture<ProjectStatusDto>([this]() {
        if (filePath_.empty()) {
            throw ServiceFailure(ApplicationErrorCode::ProjectPathRequired,
                                 "当前项目尚未指定保存路径");
        }
        requireValid(project_);
        serializer_->save(project_, filePath_);
        dirty_ = false;
        ++revision_;
        return status();
    });
}

Result<ProjectStatusDto> ProjectApplicationService::saveAs(
    const std::filesystem::path& path) {
    return capture<ProjectStatusDto>([this, &path]() {
        if (path.empty()) {
            throw ServiceFailure(ApplicationErrorCode::ProjectPathRequired,
                                 "另存项目时必须提供文件路径");
        }
        requireValid(project_);
        serializer_->save(project_, path);
        filePath_ = path;
        dirty_ = false;
        ++revision_;
        return status();
    });
}

Result<ChapterPreviewDto> ProjectApplicationService::previewChapterFile(
    const std::filesystem::path& path) const {
    return capture<ChapterPreviewDto>([this, &path]() {
        if (path.empty()) {
            throw ServiceFailure(ApplicationErrorCode::InvalidArgument,
                                 "预览章节时必须提供文件路径");
        }
        ParsedChapterText parsed = ChapterTextParser::parseFile(path);
        return previewFromRecord(project_,
                                 revision_,
                                 path,
                                 std::move(parsed.chapterKey),
                                 std::move(parsed.title),
                                 std::move(parsed.contentUtf8));
    });
}

Result<ChapterPreviewDto> ProjectApplicationService::previewChapterText(
    const std::filesystem::path& sourcePath,
    std::string_view contentUtf8) const {
    return capture<ChapterPreviewDto>([this, &sourcePath, contentUtf8]() {
        ParsedChapterText parsed = ChapterTextParser::parse(contentUtf8);
        return previewFromRecord(project_,
                                 revision_,
                                 sourcePath,
                                 std::move(parsed.chapterKey),
                                 std::move(parsed.title),
                                 std::move(parsed.contentUtf8));
    });
}

Result<ChapterPreviewDto>
ProjectApplicationService::previewChapterReextraction(ChapterId id) const {
    return capture<ChapterPreviewDto>([this, id]() {
        const auto* chapter = project_.chapters().find(id);
        if (chapter == nullptr) {
            throw ServiceFailure(ApplicationErrorCode::ChapterNotFound,
                                 "章节不存在：" + std::to_string(id));
        }
        return previewFromRecord(project_,
                                 revision_,
                                 std::filesystem::u8path(
                                     chapter->sourceFileName),
                                 chapter->chapterKey,
                                 chapter->title,
                                 chapter->contentUtf8);
    });
}

Result<ChapterId> ProjectApplicationService::importChapter(
    const ImportChapterCommand& command) {
    return capture<ChapterId>([this, &command]() {
        if (command.expectedRevision != 0 &&
            command.expectedRevision != revision_) {
            throw ServiceFailure(
                ApplicationErrorCode::Conflict,
                "章节预览已过期，请按当前项目重新识别后再导入");
        }
        requireUtf8(command.key, "章节编号");
        requireUtf8(command.title, "章节标题");
        requireUtf8(command.body, "章节正文");
        NovelRelationProject candidate = cloneProject(project_);
        const auto staged = applyStagedDictionary(
            candidate, command.newCanonicalNames, command.newAliases);
        auto selected = checkedSelection(
            candidate, command.selectedPersonIds, staged);
        const auto source = sourceFileName(command.sourcePath);
        ChapterId id{};
        try {
            id = candidate.addChapter(
                ChapterDraft{command.key,
                             command.title,
                             source,
                             command.body,
                             std::move(selected)});
        } catch (const DomainError& error) {
            const auto mapped = mapDomainError(error);
            const auto sourceContext = source.empty()
                                           ? std::string{"未指定来源文件"}
                                           : "文件“" + source + "”";
            throw ServiceFailure(
                mapped.code,
                sourceContext + "，章节编号“" + command.key +
                    "”导入失败：" + mapped.message);
        }
        requireValid(candidate);
        project_ = std::move(candidate);
        dirty_ = true;
        ++revision_;
        return id;
    });
}

Result<void> ProjectApplicationService::modifyChapter(
    const ModifyChapterCommand& command) {
    return captureVoid([this, &command]() {
        if (command.expectedRevision != 0 &&
            command.expectedRevision != revision_) {
            throw ServiceFailure(
                ApplicationErrorCode::Conflict,
                "章节编辑数据已过期，请按当前项目重新加载后再提交");
        }
        requireUtf8(command.key, "章节编号");
        requireUtf8(command.title, "章节标题");
        requireUtf8(command.body, "章节正文");
        NovelRelationProject candidate = cloneProject(project_);
        const auto* current = candidate.chapters().find(command.id);
        if (current == nullptr) {
            throw ServiceFailure(ApplicationErrorCode::ChapterNotFound,
                                 "章节不存在：" +
                                     std::to_string(command.id));
        }
        const std::string retainedSource = current->sourceFileName;
        const auto staged = applyStagedDictionary(
            candidate, command.newCanonicalNames, command.newAliases);
        auto selected = checkedSelection(
            candidate, command.selectedPersonIds, staged);
        const std::string source = command.sourcePath.empty()
                                       ? retainedSource
                                       : sourceFileName(command.sourcePath);
        if (command.newCanonicalNames.empty() && command.newAliases.empty() &&
            current->chapterKey == command.key &&
            current->title == command.title &&
            current->sourceFileName == source &&
            current->contentUtf8 == command.body &&
            current->persons == selected) {
            return;
        }
        if (!candidate.modifyChapter(
                command.id,
                ChapterDraft{command.key,
                             command.title,
                             source,
                             command.body,
                             std::move(selected)})) {
            throw ServiceFailure(ApplicationErrorCode::ChapterNotFound,
                                 "章节不存在：" +
                                     std::to_string(command.id));
        }
        requireValid(candidate);
        project_ = std::move(candidate);
        dirty_ = true;
        ++revision_;
    });
}

Result<void> ProjectApplicationService::reextractChapter(ChapterId id) {
    return captureVoid([this, id]() {
        NovelRelationProject candidate = cloneProject(project_);
        const auto* current = candidate.chapters().find(id);
        if (current == nullptr) {
            throw ServiceFailure(ApplicationErrorCode::ChapterNotFound,
                                 "章节不存在：" + std::to_string(id));
        }
        const auto extracted = DictionaryNameExtractor(candidate).extract(
            current->contentUtf8);
        if (current->persons == extracted) {
            return;
        }
        const ChapterDraft draft{current->chapterKey,
                                 current->title,
                                 current->sourceFileName,
                                 current->contentUtf8,
                                 extracted};
        if (!candidate.modifyChapter(id, draft)) {
            throw ServiceFailure(ApplicationErrorCode::ChapterNotFound,
                                 "章节不存在：" + std::to_string(id));
        }
        requireValid(candidate);
        project_ = std::move(candidate);
        dirty_ = true;
        ++revision_;
    });
}

Result<void> ProjectApplicationService::deleteChapter(ChapterId id) {
    return captureVoid([this, id]() {
        NovelRelationProject candidate = cloneProject(project_);
        if (!candidate.removeChapter(id)) {
            throw ServiceFailure(ApplicationErrorCode::ChapterNotFound,
                                 "章节不存在：" + std::to_string(id));
        }
        requireValid(candidate);
        project_ = std::move(candidate);
        dirty_ = true;
        ++revision_;
    });
}

Result<PersonId> ProjectApplicationService::addPerson(
    const std::string& canonicalName) {
    return capture<PersonId>([this, &canonicalName]() {
        requireUtf8(canonicalName, "标准人物名");
        NovelRelationProject candidate = cloneProject(project_);
        const PersonId id = candidate.addPerson(canonicalName);
        requireValid(candidate);
        project_ = std::move(candidate);
        dirty_ = true;
        ++revision_;
        return id;
    });
}

Result<void> ProjectApplicationService::renamePerson(
    PersonId id,
    const std::string& newName) {
    return captureVoid([this, id, &newName]() {
        requireUtf8(newName, "标准人物名");
        const auto* current = project_.graph().findPerson(id);
        if (current == nullptr) {
            throw ServiceFailure(ApplicationErrorCode::PersonNotFound,
                                 "人物不存在：" + std::to_string(id));
        }
        if (current->canonicalName == newName) {
            return;
        }
        NovelRelationProject candidate = cloneProject(project_);
        if (!candidate.renamePerson(id, newName)) {
            throw ServiceFailure(ApplicationErrorCode::PersonNotFound,
                                 "人物不存在：" + std::to_string(id));
        }
        requireValid(candidate);
        project_ = std::move(candidate);
        dirty_ = true;
        ++revision_;
    });
}

Result<void> ProjectApplicationService::mergePersons(PersonId source,
                                                       PersonId target) {
    return captureVoid([this, source, target]() {
        if (source == target) {
            throw ServiceFailure(ApplicationErrorCode::InvalidArgument,
                                 "不能把人物合并到自身");
        }
        NovelRelationProject candidate = cloneProject(project_);
        const auto* sourcePerson = candidate.graph().findPerson(source);
        const auto* targetPerson = candidate.graph().findPerson(target);
        if (sourcePerson == nullptr || targetPerson == nullptr) {
            throw ServiceFailure(ApplicationErrorCode::PersonNotFound,
                                 "待合并的人物不存在");
        }
        const std::string sourceName = sourcePerson->canonicalName;

        const auto chaptersCopy = candidate.chapters().all();
        for (const auto& chapter : chaptersCopy) {
            if (std::find(chapter.persons.begin(),
                          chapter.persons.end(),
                          source) == chapter.persons.end()) {
                continue;
            }
            auto persons = chapter.persons;
            std::replace(persons.begin(), persons.end(), source, target);
            candidate.modifyChapter(
                chapter.id,
                ChapterDraft{chapter.chapterKey,
                             chapter.title,
                             chapter.sourceFileName,
                             chapter.contentUtf8,
                             std::move(persons)});
        }

        std::vector<std::string> redirectedAliases;
        for (const auto& alias : candidate.aliases().entries()) {
            if (alias.second == source) {
                redirectedAliases.push_back(alias.first);
                candidate.removeAlias(alias.first);
            }
        }
        if (!candidate.removePerson(source)) {
            throw ServiceFailure(ApplicationErrorCode::PersonNotFound,
                                 "来源人物不存在");
        }
        candidate.addAlias(sourceName, target);
        for (const auto& alias : redirectedAliases) {
            candidate.addAlias(alias, target);
        }

        requireValid(candidate);
        project_ = std::move(candidate);
        dirty_ = true;
        ++revision_;
    });
}

Result<void> ProjectApplicationService::deleteUnusedPerson(PersonId id) {
    return captureVoid([this, id]() {
        NovelRelationProject candidate = cloneProject(project_);
        if (candidate.graph().findPerson(id) == nullptr) {
            throw ServiceFailure(ApplicationErrorCode::PersonNotFound,
                                 "人物不存在：" + std::to_string(id));
        }
        for (const auto& chapter : candidate.chapters().all()) {
            if (std::binary_search(chapter.persons.begin(),
                                   chapter.persons.end(), id)) {
                throw ServiceFailure(ApplicationErrorCode::Conflict,
                                     "人物仍被章节引用，不能删除");
            }
        }
        for (const auto& alias : candidate.aliases().entries()) {
            if (alias.second == id) {
                candidate.removeAlias(alias.first);
            }
        }
        if (!candidate.removePerson(id)) {
            throw ServiceFailure(ApplicationErrorCode::PersonNotFound,
                                 "人物不存在：" + std::to_string(id));
        }
        requireValid(candidate);
        project_ = std::move(candidate);
        dirty_ = true;
        ++revision_;
    });
}

Result<void> ProjectApplicationService::addAlias(const std::string& alias,
                                                  PersonId target) {
    return captureVoid([this, &alias, target]() {
        requireUtf8(alias, "人物别名");
        NovelRelationProject candidate = cloneProject(project_);
        candidate.addAlias(alias, target);
        requireValid(candidate);
        project_ = std::move(candidate);
        dirty_ = true;
        ++revision_;
    });
}

Result<void> ProjectApplicationService::removeAlias(
    const std::string& alias) {
    return captureVoid([this, &alias]() {
        NovelRelationProject candidate = cloneProject(project_);
        if (!candidate.removeAlias(alias)) {
            throw ServiceFailure(ApplicationErrorCode::Conflict,
                                 "人物别名不存在：" + alias);
        }
        requireValid(candidate);
        project_ = std::move(candidate);
        dirty_ = true;
        ++revision_;
    });
}

std::vector<ChapterRowDto> ProjectApplicationService::chapters() const {
    std::vector<ChapterRowDto> result;
    result.reserve(project_.chapters().size());
    const auto chapterStatus = deriveChapterStatus(project_);
    for (const auto& chapter : project_.chapters().all()) {
        result.push_back(makeChapterRow(chapter, chapterStatus));
    }
    return result;
}

std::vector<PersonRowDto> ProjectApplicationService::people() const {
    std::vector<PersonRowDto> result;
    result.reserve(project_.graph().personCount());
    for (const PersonId id : project_.graph().personIds()) {
        result.push_back(makePersonRow(project_, id));
    }
    return result;
}

std::vector<RelationRowDto> ProjectApplicationService::relations() const {
    std::vector<RelationRowDto> result;
    result.reserve(project_.graph().edgeCount());
    for (const auto& key : project_.graph().edgeKeys()) {
        const auto* edge = project_.graph().findEdge(key.low, key.high);
        if (edge != nullptr) {
            result.push_back(makeRelationRow(project_, *edge));
        }
    }
    return result;
}

std::vector<AliasRowDto> ProjectApplicationService::aliases() const {
    std::vector<AliasRowDto> result;
    const auto entries = project_.aliases().entries();
    result.reserve(entries.size());
    for (const auto& entry : entries) {
        const auto* target = project_.graph().findPerson(entry.second);
        if (target != nullptr) {
            result.push_back(
                AliasRowDto{entry.first, entry.second, target->canonicalName});
        }
    }
    return result;
}

Result<ChapterDetailDto> ProjectApplicationService::chapterDetail(
    ChapterId id) const {
    return capture<ChapterDetailDto>([this, id]() {
        const auto* chapter = project_.chapters().find(id);
        if (chapter == nullptr) {
            throw ServiceFailure(ApplicationErrorCode::ChapterNotFound,
                                 "章节不存在：" + std::to_string(id));
        }
        ChapterDetailDto result;
        const auto chapterStatus = deriveChapterStatus(project_);
        result.chapter = makeChapterRow(*chapter, chapterStatus);
        result.body = chapter->contentUtf8;
        result.persons.reserve(chapter->persons.size());
        for (const PersonId person : chapter->persons) {
            result.persons.push_back(makePersonRow(project_, person));
        }
        return result;
    });
}

Result<PersonDetailDto> ProjectApplicationService::personDetail(
    PersonId id) const {
    return capture<PersonDetailDto>([this, id]() {
        PersonDetailDto result;
        result.person = makePersonRow(project_, id);
        const auto chapterStatus = deriveChapterStatus(project_);
        for (const auto& alias : project_.aliases().entries()) {
            if (alias.second == id) {
                result.aliases.push_back(alias.first);
            }
        }
        for (const auto& chapter : project_.chapters().all()) {
            if (std::binary_search(chapter.persons.begin(),
                                   chapter.persons.end(), id)) {
                result.chapters.push_back(makeChapterRow(chapter, chapterStatus));
            }
        }
        for (const PersonId neighbor : project_.graph().neighbors(id)) {
            const auto* edge = project_.graph().findEdge(id, neighbor);
            if (edge != nullptr) {
                result.relations.push_back(makeRelationRow(project_, *edge));
            }
        }
        std::sort(result.relations.begin(), result.relations.end(),
                  [id](const RelationRowDto& first,
                       const RelationRowDto& second) {
                      const PersonId firstOther = first.personA == id
                                                      ? first.personB
                                                      : first.personA;
                      const PersonId secondOther = second.personA == id
                                                       ? second.personB
                                                       : second.personA;
                      return firstOther < secondOther;
                  });
        if (!result.relations.empty()) {
            result.strongestRelation = *std::max_element(
                result.relations.begin(),
                result.relations.end(),
                [id](const RelationRowDto& first,
                     const RelationRowDto& second) {
                    if (first.jaccard != second.jaccard) {
                        return first.jaccard < second.jaccard;
                    }
                    if (first.coChapterCount != second.coChapterCount) {
                        return first.coChapterCount < second.coChapterCount;
                    }
                    const PersonId firstOther = first.personA == id
                                                    ? first.personB
                                                    : first.personA;
                    const PersonId secondOther = second.personA == id
                                                     ? second.personB
                                                     : second.personA;
                    return firstOther > secondOther;
                });
        }
        return result;
    });
}

Result<RelationDetailDto> ProjectApplicationService::relationDetail(
    EdgeId id) const {
    return capture<RelationDetailDto>([this, id]() {
        const auto* edge = findEdgeById(project_, id);
        if (edge == nullptr) {
            throw ServiceFailure(ApplicationErrorCode::RelationNotFound,
                                 "人物关系不存在：" + std::to_string(id));
        }
        RelationDetailDto result;
        result.relation = makeRelationRow(project_, *edge);
        const auto chapterStatus = deriveChapterStatus(project_);
        for (const auto& chapter : project_.chapters().all()) {
            const bool containsFirst = std::binary_search(
                chapter.persons.begin(), chapter.persons.end(), edge->endpointA);
            const bool containsSecond = std::binary_search(
                chapter.persons.begin(), chapter.persons.end(), edge->endpointB);
            if (containsFirst && containsSecond) {
                result.commonChapters.push_back(
                    makeChapterRow(chapter, chapterStatus));
            }
        }
        return result;
    });
}

Result<GraphSnapshotDto> ProjectApplicationService::graphSnapshot(
    double minimumJaccard) const {
    return capture<GraphSnapshotDto>([this, minimumJaccard]() {
        if (!std::isfinite(minimumJaccard) || minimumJaccard < 0.0 ||
            minimumJaccard > 1.0) {
            throw ServiceFailure(ApplicationErrorCode::InvalidArgument,
                                 "关系阈值必须位于 0 到 1 之间");
        }
        GraphSnapshotDto result;
        result.revision = revision_;
        result.minimumJaccard = minimumJaccard;
        result.nodes.reserve(project_.graph().personCount());
        for (const PersonId id : project_.graph().personIds()) {
            const auto* person = project_.graph().findPerson(id);
            if (person != nullptr) {
                result.nodes.push_back(GraphNodeDto{
                    id, person->canonicalName, person->chapterCount});
            }
        }
        result.edges.reserve(project_.graph().edgeCount());
        for (const auto& key : project_.graph().edgeKeys()) {
            const auto* edge = project_.graph().findEdge(key.low, key.high);
            if (edge != nullptr && edge->jaccard >= minimumJaccard) {
                const auto relation = makeRelationRow(project_, *edge);
                result.edges.push_back(GraphEdgeDto{relation.id,
                                                    relation.personA,
                                                    relation.personB,
                                                    relation.personAName,
                                                    relation.personBName,
                                                    relation.coChapterCount,
                                                    relation.jaccard});
            }
        }
        return result;
    });
}

Result<TraversalResultDto> ProjectApplicationService::depthFirst(
    PersonId start) const {
    return capture<TraversalResultDto>([this, start]() {
        const TraversalResult result =
            depthFirstSearch(project_.graph(), start, TraversalScope::AllVertices);
        return makeTraversalDto(
            project_, start, TraversalKind::DepthFirst, result);
    });
}

Result<TraversalResultDto> ProjectApplicationService::breadthFirst(
    PersonId start) const {
    return capture<TraversalResultDto>([this, start]() {
        const TraversalResult result = breadthFirstSearch(
            project_.graph(), start, TraversalScope::AllVertices);
        return makeTraversalDto(
            project_, start, TraversalKind::BreadthFirst, result);
    });
}

}  // namespace novel::application
