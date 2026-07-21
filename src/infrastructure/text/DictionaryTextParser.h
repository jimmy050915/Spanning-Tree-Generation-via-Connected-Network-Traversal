#pragma once

#include "infrastructure/text/Utf8TextFileLoader.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace novel {

struct ParsedAliasEntry {
    std::string alias;
    std::string canonicalName;
    std::size_t sourceLine{};

    bool operator==(const ParsedAliasEntry& other) const {
        return alias == other.alias && canonicalName == other.canonicalName &&
               sourceLine == other.sourceLine;
    }
};

struct ParsedDictionaryText {
    std::vector<std::string> canonicalNames;
    std::vector<ParsedAliasEntry> aliases;
};

using ParsedDictionaries = ParsedDictionaryText;

class DictionaryTextParser final {
public:
    static constexpr std::size_t kMaximumPersonCount = 5000U;
    static constexpr std::size_t kMaximumAliasCount = 100000U;
    static constexpr std::size_t kMaximumNameBytes = 1024U * 1024U;

    static std::vector<std::string> parsePersons(std::string_view contentUtf8);

    static std::vector<std::string> parsePersonNames(
        std::string_view contentUtf8) {
        return parsePersons(contentUtf8);
    }

    static std::vector<ParsedAliasEntry> parseAliases(
        std::string_view contentUtf8);

    static std::vector<ParsedAliasEntry> parseAliases(
        std::string_view contentUtf8,
        const std::vector<std::string>& canonicalNames);

    static ParsedDictionaryText parse(std::string_view personsContentUtf8,
                                      std::string_view aliasesContentUtf8);

    static std::vector<std::string> parsePersonsFile(
        const std::filesystem::path& path,
        std::size_t maximumBytes = Utf8TextFileLoader::kDefaultMaximumBytes);

    static std::vector<ParsedAliasEntry> parseAliasesFile(
        const std::filesystem::path& path,
        const std::vector<std::string>& canonicalNames,
        std::size_t maximumBytes = Utf8TextFileLoader::kDefaultMaximumBytes);

    static ParsedDictionaryText parseFiles(
        const std::filesystem::path& personsPath,
        const std::filesystem::path& aliasesPath,
        std::size_t maximumBytes = Utf8TextFileLoader::kDefaultMaximumBytes);
};

}  // namespace novel
