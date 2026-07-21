#include "domain/chapter/ChapterCollection.h"

#include "domain/error/DomainError.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace novel {

static_assert(std::is_nothrow_swappable_v<ChapterRecord>,
              "章节替换必须能够无异常提交");

bool ChapterCollection::isBlank(const std::string& value) noexcept {
    return value.empty() ||
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isspace(character) != 0;
           });
}

void ChapterCollection::normalizePersons(std::vector<PersonId>& persons) {
    std::sort(persons.begin(), persons.end());
    persons.erase(std::unique(persons.begin(), persons.end()), persons.end());
}

ChapterRecord ChapterCollection::makeRecord(ChapterId id,
                                            ChapterDraft draft) {
    normalizePersons(draft.persons);
    return ChapterRecord{id,
                         std::move(draft.chapterKey),
                         std::move(draft.title),
                         std::move(draft.sourceFileName),
                         std::move(draft.contentUtf8),
                         std::move(draft.persons)};
}

ChapterId ChapterCollection::add(ChapterDraft draft) {
    if (isBlank(draft.chapterKey)) {
        throw DomainError(DomainErrorCode::EmptyChapterKey,
                          "章节键不能为空或仅包含空白字符");
    }
    if (chapterKeyIndex_.find(draft.chapterKey) != chapterKeyIndex_.end()) {
        throw DomainError(DomainErrorCode::DuplicateChapterKey,
                          "章节键已存在：" + draft.chapterKey);
    }
    if (nextChapterId_ == std::numeric_limits<ChapterId>::max()) {
        throw DomainError(DomainErrorCode::IdentifierExhausted,
                          "章节编号已经耗尽");
    }

    const ChapterId id = nextChapterId_;
    ChapterRecord record = makeRecord(id, std::move(draft));
    chapters_.push_back(std::move(record));

    try {
        const auto keyInsertion =
            chapterKeyIndex_.emplace(chapters_.back().chapterKey, id);
        if (!keyInsertion.second) {
            throw DomainError(DomainErrorCode::DuplicateChapterKey,
                              "章节键已存在：" +
                                  chapters_.back().chapterKey);
        }

        try {
            const auto idInsertion =
                chapterIdIndex_.emplace(id, chapters_.size() - 1U);
            if (!idInsertion.second) {
                throw DomainError(DomainErrorCode::IdentifierExhausted,
                                  "章节编号发生冲突");
            }
        } catch (...) {
            chapterKeyIndex_.erase(keyInsertion.first);
            throw;
        }
    } catch (...) {
        chapters_.pop_back();
        throw;
    }

    ++nextChapterId_;
    return id;
}

bool ChapterCollection::modify(ChapterId id, ChapterDraft draft) {
    const auto idIterator = chapterIdIndex_.find(id);
    if (idIterator == chapterIdIndex_.end()) {
        return false;
    }
    if (isBlank(draft.chapterKey)) {
        throw DomainError(DomainErrorCode::EmptyChapterKey,
                          "章节键不能为空或仅包含空白字符");
    }

    const auto existingKey = chapterKeyIndex_.find(draft.chapterKey);
    if (existingKey != chapterKeyIndex_.end() && existingKey->second != id) {
        throw DomainError(DomainErrorCode::DuplicateChapterKey,
                          "章节键已存在：" + draft.chapterKey);
    }

    ChapterRecord replacement = makeRecord(id, std::move(draft));
    ChapterRecord& current = chapters_[idIterator->second];
    if (current.chapterKey == replacement.chapterKey) {
        using std::swap;
        swap(current, replacement);
        return true;
    }

    const auto keyInsertion =
        chapterKeyIndex_.emplace(replacement.chapterKey, id);
    if (!keyInsertion.second) {
        throw DomainError(DomainErrorCode::DuplicateChapterKey,
                          "章节键已存在：" + replacement.chapterKey);
    }

    decltype(chapterKeyIndex_)::iterator oldKey;
    try {
        oldKey = chapterKeyIndex_.find(current.chapterKey);
    } catch (...) {
        chapterKeyIndex_.erase(keyInsertion.first);
        throw;
    }

    using std::swap;
    swap(current, replacement);
    chapterKeyIndex_.erase(oldKey);
    return true;
}

bool ChapterCollection::remove(ChapterId id) {
    const auto idIterator = chapterIdIndex_.find(id);
    if (idIterator == chapterIdIndex_.end()) {
        return false;
    }

    const std::size_t removedPosition = idIterator->second;
    chapterKeyIndex_.erase(chapters_[removedPosition].chapterKey);
    chapterIdIndex_.erase(idIterator);
    chapters_.erase(chapters_.begin() +
                    static_cast<std::vector<ChapterRecord>::difference_type>(
                        removedPosition));

    for (std::size_t position = removedPosition; position < chapters_.size();
         ++position) {
        chapterIdIndex_.find(chapters_[position].id)->second = position;
    }
    return true;
}

const ChapterRecord* ChapterCollection::find(ChapterId id) const noexcept {
    const auto iterator = chapterIdIndex_.find(id);
    if (iterator == chapterIdIndex_.end()) {
        return nullptr;
    }
    return &chapters_[iterator->second];
}

const ChapterRecord* ChapterCollection::findByKey(
    const std::string& chapterKey) const noexcept {
    const auto keyIterator = chapterKeyIndex_.find(chapterKey);
    if (keyIterator == chapterKeyIndex_.end()) {
        return nullptr;
    }
    return find(keyIterator->second);
}

const std::vector<ChapterRecord>& ChapterCollection::all() const noexcept {
    return chapters_;
}

std::size_t ChapterCollection::size() const noexcept {
    return chapters_.size();
}

ValidationReport ChapterCollection::validate() const {
    ValidationReport report;
    if (chapterIdIndex_.size() != chapters_.size() ||
        chapterKeyIndex_.size() != chapters_.size()) {
        report.add(ValidationSeverity::Error,
                   "chapter.index.size_mismatch",
                   "章节记录数量与章节索引数量不一致");
    }

    std::unordered_set<ChapterId> ids;
    std::unordered_set<std::string> keys;
    ChapterId maximumId{};
    ChapterId previousId{};
    for (std::size_t position = 0; position < chapters_.size(); ++position) {
        const auto& chapter = chapters_[position];
        maximumId = std::max(maximumId, chapter.id);
        if (chapter.id == 0 || !ids.insert(chapter.id).second) {
            report.add(ValidationSeverity::Error,
                       "chapter.id.invalid",
                       "章节编号必须非零且在集合中唯一");
        }
        if (previousId != 0 && chapter.id <= previousId) {
            report.add(ValidationSeverity::Error,
                       "chapter.id.order",
                       "章节记录必须按内部编号递增保存");
        }
        previousId = chapter.id;

        const auto idIndex = chapterIdIndex_.find(chapter.id);
        if (idIndex == chapterIdIndex_.end() || idIndex->second != position) {
            report.add(ValidationSeverity::Error,
                       "chapter.id_index.mismatch",
                       "章节编号索引与章节记录不一致");
        }

        if (isBlank(chapter.chapterKey)) {
            report.add(ValidationSeverity::Error,
                       "chapter.key.empty",
                       "章节键不能为空或仅包含空白字符");
        } else if (!keys.insert(chapter.chapterKey).second) {
            report.add(ValidationSeverity::Error,
                       "chapter.key.duplicate",
                       "章节键在集合中重复：" + chapter.chapterKey);
        }
        const auto keyIndex = chapterKeyIndex_.find(chapter.chapterKey);
        if (keyIndex == chapterKeyIndex_.end() ||
            keyIndex->second != chapter.id) {
            report.add(ValidationSeverity::Error,
                       "chapter.key_index.mismatch",
                       "章节键索引与章节记录不一致：" +
                           chapter.chapterKey);
        }

        if (!std::is_sorted(chapter.persons.begin(), chapter.persons.end()) ||
            std::adjacent_find(chapter.persons.begin(), chapter.persons.end()) !=
                chapter.persons.end()) {
            report.add(ValidationSeverity::Error,
                       "chapter.persons.not_unique",
                       "章节人物编号必须排序且不得重复：" +
                           chapter.chapterKey);
        }
    }

    for (const auto& entry : chapterIdIndex_) {
        if (entry.second >= chapters_.size() ||
            chapters_[entry.second].id != entry.first) {
            report.add(ValidationSeverity::Error,
                       "chapter.id_index.orphan",
                       "章节编号索引指向不存在或编号不匹配的章节");
        }
    }
    for (const auto& entry : chapterKeyIndex_) {
        const auto idIndex = chapterIdIndex_.find(entry.second);
        if (idIndex == chapterIdIndex_.end() ||
            idIndex->second >= chapters_.size() ||
            chapters_[idIndex->second].chapterKey != entry.first) {
            report.add(ValidationSeverity::Error,
                       "chapter.key_index.orphan",
                       "章节键索引指向不存在或键不匹配的章节：" +
                           entry.first);
        }
    }

    if (nextChapterId_ == 0 ||
        (!chapters_.empty() && nextChapterId_ <= maximumId)) {
        report.add(ValidationSeverity::Error,
                   "chapter.next_id.invalid",
                   "下一个章节编号没有保持单调递增");
    }
    return report;
}

void ChapterCollection::swap(ChapterCollection& other) noexcept {
    using std::swap;
    swap(nextChapterId_, other.nextChapterId_);
    chapters_.swap(other.chapters_);
    chapterIdIndex_.swap(other.chapterIdIndex_);
    chapterKeyIndex_.swap(other.chapterKeyIndex_);
}

void swap(ChapterCollection& first, ChapterCollection& second) noexcept {
    first.swap(second);
}

}  // namespace novel
