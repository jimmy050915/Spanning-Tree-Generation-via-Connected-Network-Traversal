#pragma once

#include "domain/project/NovelRelationProject.h"

#include <filesystem>

namespace novel {

// Persistence failures are reported with PersistenceError. Loading returns a
// new candidate project, so a failed load cannot mutate the currently opened
// project.
class IProjectSerializer {
public:
    virtual ~IProjectSerializer() = default;

    virtual void save(const NovelRelationProject& project,
                      const std::filesystem::path& path) const = 0;
    virtual NovelRelationProject load(
        const std::filesystem::path& path) const = 0;
};

}  // namespace novel
