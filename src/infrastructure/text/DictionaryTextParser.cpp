#include "infrastructure/text/DictionaryTextParser.h"

#include "infrastructure/text/TextFileError.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace novel {

namespace {

std::string_view withoutBom(std::string_view content) noexcept {
    if (content.size() >= 3U &&
        static_cast<unsigned char>(content[0]) == 0xefU &&
        static_cast<unsigned char>(content[1]) == 0xbbU &&
        static_cast<unsigned char>(content[2]) == 0xbfU) {
        content.remove_prefix(3U);
    }
    return content;
}

std::string normalizeLineEndings(std::string_view input) {
    std::string normalized;
    normalized.reserve(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        if (input[index] == '\r') {
            normalized.push_back('\n');
            if (index + 1U < input.size() && input[index + 1U] == '\n') {
                ++index;
            }
        } else {
            normalized.push_back(input[index]);
        }
    }
    return normalized;
}

bool isAsciiWhitespace(unsigned char character) noexcept {
    return character == 0x20U ||
           (character >= 0x09U && character <= 0x0dU);
}

std::string asciiTrim(std::string_view text) {
    std::size_t first = 0;
    while (first < text.size() &&
           isAsciiWhitespace(static_cast<unsigned char>(text[first]))) {
        ++first;
    }
    std::size_t last = text.size();
    while (last > first &&
           isAsciiWhitespace(static_cast<unsigned char>(text[last - 1U]))) {
        --last;
    }
    return std::string(text.substr(first, last - first));
}

template <typename Callback>
void forEachLine(std::string_view content, Callback callback) {
    if (content.empty()) {
        return;
    }
    std::size_t start = 0;
    std::size_t lineNumber = 1;
    while (start < content.size()) {
        const std::size_t end = content.find('\n', start);
        if (end == std::string_view::npos) {
            callback(content.substr(start), lineNumber);
            break;
        }
        callback(content.substr(start, end - start), lineNumber);
        start = end + 1U;
        ++lineNumber;
    }
}

TextFileError dictionaryError(TextFileErrorCode code,
                              std::size_t line,
                              std::string message) {
    return TextFileError(code,
                         {},
                         line,
                         "词典第 " + std::to_string(line) + " 行：" +
                             std::move(message));
}

std::vector<ParsedAliasEntry> parseAliasRecords(std::string_view contentUtf8) {
    if (contentUtf8.size() > Utf8TextFileLoader::kDefaultMaximumBytes) {
        throw TextFileError(TextFileErrorCode::FileTooLarge,
                            "人物别名词典超过 20 MiB 大小上限");
    }
    if (!isValidUtf8(contentUtf8)) {
        throw TextFileError(TextFileErrorCode::InvalidUtf8,
                            "人物别名词典包含无效 UTF-8 字节序列");
    }

    const std::string normalized =
        normalizeLineEndings(withoutBom(contentUtf8));
    std::vector<ParsedAliasEntry> result;
    std::unordered_map<std::string, std::size_t> aliasLines;

    forEachLine(normalized, [&](std::string_view line, std::size_t lineNumber) {
        const std::string trimmedLine = asciiTrim(line);
        if (trimmedLine.empty() || trimmedLine.front() == '#') {
            return;
        }
        const std::size_t separator = line.find('\t');
        if (separator == std::string_view::npos ||
            line.find('\t', separator + 1U) != std::string_view::npos) {
            throw dictionaryError(
                TextFileErrorCode::InvalidAliasFormat,
                lineNumber,
                "别名记录必须且只能包含一个 TAB 分隔符");
        }

        std::string alias = asciiTrim(line.substr(0, separator));
        std::string canonicalName = asciiTrim(line.substr(separator + 1U));
        if (alias.empty() || canonicalName.empty()) {
            throw dictionaryError(TextFileErrorCode::EmptyAliasField,
                                  lineNumber,
                                  "别名和标准人物名都不能为空");
        }
        if (alias.size() > DictionaryTextParser::kMaximumNameBytes ||
            canonicalName.size() >
                DictionaryTextParser::kMaximumNameBytes) {
            throw dictionaryError(TextFileErrorCode::LimitExceeded,
                                  lineNumber,
                                  "别名或标准人物名超过 1 MiB 长度上限");
        }
        if (result.size() >=
            DictionaryTextParser::kMaximumAliasCount) {
            throw dictionaryError(TextFileErrorCode::LimitExceeded,
                                  lineNumber,
                                  "人物别名数量超过 100000 上限");
        }

        const auto insertion = aliasLines.emplace(alias, lineNumber);
        if (!insertion.second) {
            throw dictionaryError(
                TextFileErrorCode::DuplicateAlias,
                lineNumber,
                "人物别名重复：“" + alias + "”（首次出现在第 " +
                    std::to_string(insertion.first->second) + " 行）");
        }
        result.push_back(ParsedAliasEntry{
            std::move(alias), std::move(canonicalName), lineNumber});
    });

    return result;
}

void validateAliasTargets(const std::vector<std::string>& canonicalNames,
                          const std::vector<ParsedAliasEntry>& aliases) {
    std::unordered_set<std::string> canonicalSet;
    canonicalSet.reserve(canonicalNames.size());
    for (const auto& name : canonicalNames) {
        canonicalSet.insert(name);
    }

    for (const auto& entry : aliases) {
        if (canonicalSet.find(entry.canonicalName) == canonicalSet.end()) {
            throw dictionaryError(
                TextFileErrorCode::UnknownCanonicalName,
                entry.sourceLine,
                "别名“" + entry.alias + "”指向不存在的标准人物名“" +
                    entry.canonicalName + "”");
        }
        if (canonicalSet.find(entry.alias) != canonicalSet.end()) {
            throw dictionaryError(
                TextFileErrorCode::AliasConflictsWithCanonicalName,
                entry.sourceLine,
                "别名“" + entry.alias + "”与标准人物名冲突");
        }
    }
}

template <typename Result, typename Parser>
Result parseWithFileContext(const std::filesystem::path& path,
                            std::size_t maximumBytes,
                            Parser parser) {
    const std::string content = Utf8TextFileLoader::load(path, maximumBytes);
    try {
        return parser(content);
    } catch (const TextFileError& error) {
        if (!error.path().empty()) {
            throw;
        }
        throw TextFileError(error.code(),
                            path,
                            error.line(),
                            "解析词典文件失败：" + path.u8string() + "；" +
                                error.what());
    }
}

}  // namespace

std::vector<std::string> DictionaryTextParser::parsePersons(
    std::string_view contentUtf8) {
    if (contentUtf8.size() > Utf8TextFileLoader::kDefaultMaximumBytes) {
        throw TextFileError(TextFileErrorCode::FileTooLarge,
                            "标准人物词典超过 20 MiB 大小上限");
    }
    if (!isValidUtf8(contentUtf8)) {
        throw TextFileError(TextFileErrorCode::InvalidUtf8,
                            "标准人物词典包含无效 UTF-8 字节序列");
    }

    const std::string normalized =
        normalizeLineEndings(withoutBom(contentUtf8));
    std::vector<std::string> result;
    std::unordered_map<std::string, std::size_t> nameLines;

    forEachLine(normalized, [&](std::string_view line, std::size_t lineNumber) {
        std::string name = asciiTrim(line);
        if (name.empty() || name.front() == '#') {
            return;
        }
        if (name.size() > kMaximumNameBytes) {
            throw dictionaryError(TextFileErrorCode::LimitExceeded,
                                  lineNumber,
                                  "标准人物名超过 1 MiB 长度上限");
        }
        if (result.size() >= kMaximumPersonCount) {
            throw dictionaryError(TextFileErrorCode::LimitExceeded,
                                  lineNumber,
                                  "标准人物数量超过 5000 上限");
        }

        const auto insertion = nameLines.emplace(name, lineNumber);
        if (!insertion.second) {
            throw dictionaryError(
                TextFileErrorCode::DuplicatePersonName,
                lineNumber,
                "标准人物名重复：“" + name + "”（首次出现在第 " +
                    std::to_string(insertion.first->second) + " 行）");
        }
        result.push_back(std::move(name));
    });

    return result;
}

std::vector<ParsedAliasEntry> DictionaryTextParser::parseAliases(
    std::string_view contentUtf8) {
    return parseAliasRecords(contentUtf8);
}

std::vector<ParsedAliasEntry> DictionaryTextParser::parseAliases(
    std::string_view contentUtf8,
    const std::vector<std::string>& canonicalNames) {
    auto aliases = parseAliasRecords(contentUtf8);
    validateAliasTargets(canonicalNames, aliases);
    return aliases;
}

ParsedDictionaryText DictionaryTextParser::parse(
    std::string_view personsContentUtf8,
    std::string_view aliasesContentUtf8) {
    ParsedDictionaryText result;
    result.canonicalNames = parsePersons(personsContentUtf8);
    result.aliases = parseAliases(aliasesContentUtf8, result.canonicalNames);
    return result;
}

std::vector<std::string> DictionaryTextParser::parsePersonsFile(
    const std::filesystem::path& path,
    std::size_t maximumBytes) {
    return parseWithFileContext<std::vector<std::string>>(
        path,
        maximumBytes,
        [](const std::string& content) { return parsePersons(content); });
}

std::vector<ParsedAliasEntry> DictionaryTextParser::parseAliasesFile(
    const std::filesystem::path& path,
    const std::vector<std::string>& canonicalNames,
    std::size_t maximumBytes) {
    return parseWithFileContext<std::vector<ParsedAliasEntry>>(
        path,
        maximumBytes,
        [&canonicalNames](const std::string& content) {
            return parseAliases(content, canonicalNames);
        });
}

ParsedDictionaryText DictionaryTextParser::parseFiles(
    const std::filesystem::path& personsPath,
    const std::filesystem::path& aliasesPath,
    std::size_t maximumBytes) {
    ParsedDictionaryText result;
    result.canonicalNames = parsePersonsFile(personsPath, maximumBytes);
    result.aliases =
        parseAliasesFile(aliasesPath, result.canonicalNames, maximumBytes);
    return result;
}

}  // namespace novel
