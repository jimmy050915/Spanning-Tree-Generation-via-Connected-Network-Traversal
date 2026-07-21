#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace novel {

using PersonId = std::uint32_t;
using EdgeId = std::uint64_t;

struct EdgeNode;

struct PersonVertex {
    PersonId id{};
    std::string canonicalName;
    std::uint32_t chapterCount{};
    EdgeNode* firstEdge{};  // Non-owning.
};

struct EdgeNode {
    EdgeId id{};
    PersonId endpointA{};
    PersonId endpointB{};
    std::uint32_t coChapterCount{};
    double jaccard{};
    EdgeNode* linkA{};  // Non-owning; next edge in endpointA's chain.
    EdgeNode* linkB{};  // Non-owning; next edge in endpointB's chain.
};

struct EdgeKey {
    PersonId low{};
    PersonId high{};

    static EdgeKey make(PersonId first, PersonId second) noexcept {
        return first < second ? EdgeKey{first, second} : EdgeKey{second, first};
    }

    bool operator==(const EdgeKey& other) const noexcept {
        return low == other.low && high == other.high;
    }
};

struct EdgeKeyHash {
    std::size_t operator()(const EdgeKey& key) const noexcept {
        const auto lowHash = std::hash<PersonId>{}(key.low);
        const auto highHash = std::hash<PersonId>{}(key.high);
        return lowHash ^ (highHash + static_cast<std::size_t>(0x9e3779b9U) +
                          (lowHash << 6U) + (lowHash >> 2U));
    }
};

}  // namespace novel
