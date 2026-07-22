#pragma once

#include "domain/model/GraphTypes.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace novel {

class NovelRelationProject;

struct DictionaryNameMatch {
    std::string matchedText;
    PersonId person{};
    std::string canonicalName;
    bool isAlias{};
    std::size_t byteOffset{};
    std::size_t byteLength{};
};

class DictionaryNameExtractor final {
public:
    // Takes an immutable snapshot; later project edits do not alter this extractor.
    explicit DictionaryNameExtractor(const NovelRelationProject& project);

    std::vector<PersonId> extract(std::string_view contentUtf8) const;

    // Returns every non-overlapping, longest-at-position match in source
    // order. Offsets and lengths count UTF-8 bytes so callers can highlight
    // the exact original slice without normalizing the text.
    std::vector<DictionaryNameMatch> extractDetailed(
        std::string_view contentUtf8) const;

    std::vector<DictionaryNameMatch> extractMatches(
        std::string_view contentUtf8) const {
        return extractDetailed(contentUtf8);
    }

    std::vector<PersonId> extractPersonIds(
        std::string_view contentUtf8) const {
        return extract(contentUtf8);
    }

    std::size_t dictionaryEntryCount() const noexcept {
        return entries_.size();
    }

private:
    struct Entry {
        std::string text;
        PersonId person{};
        std::string canonicalName;
        bool isAlias{};
    };

    std::vector<Entry> entries_;
};

}  // namespace novel
