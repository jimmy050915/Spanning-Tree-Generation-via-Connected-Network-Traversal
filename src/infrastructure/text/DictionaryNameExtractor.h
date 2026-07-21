#pragma once

#include "domain/model/GraphTypes.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace novel {

class NovelRelationProject;

class DictionaryNameExtractor final {
public:
    // Takes an immutable snapshot; later project edits do not alter this extractor.
    explicit DictionaryNameExtractor(const NovelRelationProject& project);

    std::vector<PersonId> extract(std::string_view contentUtf8) const;

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
    };

    std::vector<Entry> entries_;
};

}  // namespace novel
