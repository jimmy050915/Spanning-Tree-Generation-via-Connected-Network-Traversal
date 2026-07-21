#pragma once

#include "infrastructure/text/Utf8TextFileLoader.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace novel {

struct ParsedChapterText {
    std::string chapterKey;
    std::string title;
    std::string contentUtf8;

    bool operator==(const ParsedChapterText& other) const {
        return chapterKey == other.chapterKey && title == other.title &&
               contentUtf8 == other.contentUtf8;
    }
};

class ChapterTextParser final {
public:
    static constexpr std::size_t kMaximumMetadataValueBytes =
        1024U * 1024U;

    static ParsedChapterText parse(std::string_view contentUtf8);

    static ParsedChapterText parseFile(
        const std::filesystem::path& path,
        std::size_t maximumBytes = Utf8TextFileLoader::kDefaultMaximumBytes);
};

}  // namespace novel
