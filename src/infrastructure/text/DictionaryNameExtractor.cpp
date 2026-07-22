#include "infrastructure/text/DictionaryNameExtractor.h"

#include "domain/project/NovelRelationProject.h"
#include "infrastructure/text/TextFileError.h"
#include "infrastructure/text/Utf8TextFileLoader.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace novel {

namespace {

void requireValidEntry(std::string_view text, std::string_view context) {
    if (text.empty()) {
        throw TextFileError(TextFileErrorCode::InvalidDictionaryEntry,
                            std::string(context) + "不能为空");
    }
    if (!isValidUtf8(text)) {
        throw TextFileError(TextFileErrorCode::InvalidUtf8,
                            std::string(context) + "包含无效 UTF-8：" +
                                std::string(text));
    }
}

}  // namespace

DictionaryNameExtractor::DictionaryNameExtractor(
    const NovelRelationProject& project) {
    const auto personIds = project.graph().personIds();
    const auto aliasEntries = project.aliases().entries();
    entries_.reserve(personIds.size() + aliasEntries.size());

    std::unordered_map<std::string, PersonId> targetsByText;
    targetsByText.reserve(personIds.size() + aliasEntries.size());

    for (const PersonId id : personIds) {
        const PersonVertex* person = project.graph().findPerson(id);
        if (person == nullptr) {
            throw TextFileError(
                TextFileErrorCode::InvalidDictionaryEntry,
                "构造名称词典快照时找不到人物编号 " + std::to_string(id));
        }
        requireValidEntry(person->canonicalName,
                          "人物编号 " + std::to_string(id) + " 的标准名称");
        const auto insertion = targetsByText.emplace(person->canonicalName, id);
        if (!insertion.second && insertion.first->second != id) {
            throw TextFileError(
                TextFileErrorCode::InvalidDictionaryEntry,
                "名称“" + person->canonicalName + "”指向多个不同人物");
        }
        if (insertion.second) {
            entries_.push_back(
                Entry{person->canonicalName, id, person->canonicalName, false});
        }
    }

    for (const auto& alias : aliasEntries) {
        requireValidEntry(alias.first, "人物别名");
        if (project.graph().findPerson(alias.second) == nullptr) {
            throw TextFileError(
                TextFileErrorCode::UnknownCanonicalName,
                "人物别名“" + alias.first + "”指向不存在的人物编号 " +
                    std::to_string(alias.second));
        }

        const auto insertion = targetsByText.emplace(alias.first, alias.second);
        if (!insertion.second) {
            if (insertion.first->second != alias.second) {
                throw TextFileError(
                    TextFileErrorCode::InvalidDictionaryEntry,
                    "名称“" + alias.first + "”指向多个不同人物");
            }
            continue;
        }
        const auto* target = project.graph().findPerson(alias.second);
        entries_.push_back(
            Entry{alias.first, alias.second, target->canonicalName, true});
    }

    std::sort(entries_.begin(), entries_.end(), [](const Entry& left,
                                                   const Entry& right) {
        if (left.text.size() != right.text.size()) {
            return left.text.size() > right.text.size();
        }
        if (left.text != right.text) {
            return left.text < right.text;
        }
        return left.person < right.person;
    });
}

std::vector<PersonId> DictionaryNameExtractor::extract(
    std::string_view contentUtf8) const {
    const auto matches = extractDetailed(contentUtf8);

    std::unordered_set<PersonId> matchedPeople;
    matchedPeople.reserve(matches.size());
    for (const auto& match : matches) {
        matchedPeople.insert(match.person);
    }

    std::vector<PersonId> result(matchedPeople.begin(), matchedPeople.end());
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<DictionaryNameMatch> DictionaryNameExtractor::extractDetailed(
    std::string_view contentUtf8) const {
    if (!isValidUtf8(contentUtf8)) {
        throw TextFileError(TextFileErrorCode::InvalidUtf8,
                            "待提取章节正文包含无效 UTF-8 字节序列");
    }

    std::vector<DictionaryNameMatch> matches;
    std::size_t offset = 0;
    while (offset < contentUtf8.size()) {
        const Entry* match = nullptr;
        for (const auto& entry : entries_) {
            if (entry.text.size() <= contentUtf8.size() - offset &&
                contentUtf8.compare(offset, entry.text.size(), entry.text) == 0) {
                match = &entry;
                break;
            }
        }

        if (match == nullptr) {
            ++offset;
            continue;
        }
        matches.push_back(DictionaryNameMatch{match->text,
                                              match->person,
                                              match->canonicalName,
                                              match->isAlias,
                                              offset,
                                              match->text.size()});
        offset += match->text.size();
    }
    return matches;
}

}  // namespace novel
