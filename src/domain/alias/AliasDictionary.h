#pragma once

#include "domain/model/GraphTypes.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace novel {

class AliasDictionary {
public:
    void addAlias(std::string alias, PersonId target);
    bool removeAlias(const std::string& alias) noexcept;
    std::optional<PersonId> resolve(const std::string& alias) const noexcept;
    std::vector<std::pair<std::string, PersonId>> entries() const;

private:
    static bool isBlank(const std::string& value) noexcept;

    std::unordered_map<std::string, PersonId> aliases_;
};

}  // namespace novel
