#pragma once

#include "domain/model/ChapterTypes.h"
#include "domain/validation/ValidationReport.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace novel {

class NovelRelationProject;

class ChapterCollection {
public:
    ChapterId add(ChapterDraft draft);
    bool modify(ChapterId id, ChapterDraft draft);
    bool remove(ChapterId id);

    const ChapterRecord* find(ChapterId id) const noexcept;
    const ChapterRecord* findByKey(const std::string& chapterKey) const noexcept;
    const std::vector<ChapterRecord>& all() const noexcept;
    std::size_t size() const noexcept;
    ValidationReport validate() const;

    void swap(ChapterCollection& other) noexcept;

private:
    ChapterId nextChapterId_{1};
    std::vector<ChapterRecord> chapters_;
    std::unordered_map<ChapterId, std::size_t> chapterIdIndex_;
    std::unordered_map<std::string, ChapterId> chapterKeyIndex_;

    static bool isBlank(const std::string& value) noexcept;
    static void normalizePersons(std::vector<PersonId>& persons);
    static ChapterRecord makeRecord(ChapterId id, ChapterDraft draft);

    friend class NovelRelationProject;
};

void swap(ChapterCollection& first, ChapterCollection& second) noexcept;

}  // namespace novel
