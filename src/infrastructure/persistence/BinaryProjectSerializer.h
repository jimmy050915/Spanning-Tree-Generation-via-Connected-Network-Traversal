#pragma once

#include "infrastructure/persistence/IProjectSerializer.h"

#include <cstdint>

namespace novel {

class BinaryProjectSerializer final : public IProjectSerializer {
public:
    static constexpr std::uint32_t formatVersion = 1;

    void save(const NovelRelationProject& project,
              const std::filesystem::path& path) const override;
    NovelRelationProject load(
        const std::filesystem::path& path) const override;
};

}  // namespace novel
