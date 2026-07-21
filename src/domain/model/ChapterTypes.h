#pragma once

#include "domain/model/GraphTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace novel {

using ChapterId = std::uint64_t;

struct ChapterDraft {
    std::string chapterKey;
    std::string title;
    std::string sourceFileName;
    std::string contentUtf8;
    std::vector<PersonId> persons;
};

struct ChapterRecord {
    ChapterId id{};
    std::string chapterKey;
    std::string title;
    std::string sourceFileName;
    std::string contentUtf8;
    std::vector<PersonId> persons;
};

}  // namespace novel
