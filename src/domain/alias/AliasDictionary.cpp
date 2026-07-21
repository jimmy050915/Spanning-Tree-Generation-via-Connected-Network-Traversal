#include "domain/alias/AliasDictionary.h"

#include "domain/error/DomainError.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace novel {

bool AliasDictionary::isBlank(const std::string& value) noexcept {
    return value.empty() ||
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isspace(character) != 0;
           });
}

void AliasDictionary::addAlias(std::string alias, PersonId target) {
    if (isBlank(alias)) {
        throw DomainError(DomainErrorCode::EmptyAlias,
                          "人物别名不能为空或仅包含空白字符");
    }

    const auto insertion = aliases_.emplace(std::move(alias), target);
    if (!insertion.second) {
        throw DomainError(DomainErrorCode::DuplicateAlias,
                          "人物别名已存在：" + insertion.first->first);
    }
}

bool AliasDictionary::removeAlias(const std::string& alias) noexcept {
    return aliases_.erase(alias) != 0;
}

std::optional<PersonId> AliasDictionary::resolve(
    const std::string& alias) const noexcept {
    const auto iterator = aliases_.find(alias);
    if (iterator == aliases_.end()) {
        return std::nullopt;
    }
    return iterator->second;
}

std::vector<std::pair<std::string, PersonId>> AliasDictionary::entries() const {
    std::vector<std::pair<std::string, PersonId>> result;
    result.reserve(aliases_.size());
    for (const auto& entry : aliases_) {
        result.emplace_back(entry.first, entry.second);
    }
    std::sort(result.begin(), result.end(), [](const auto& first, const auto& second) {
        return first.first < second.first;
    });
    return result;
}

}  // namespace novel
