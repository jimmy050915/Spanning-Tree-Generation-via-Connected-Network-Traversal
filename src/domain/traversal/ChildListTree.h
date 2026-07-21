#pragma once

#include "domain/model/GraphTypes.h"

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

namespace novel {

// Tree-only sentinel used to join the roots of a complete graph traversal.
inline constexpr PersonId kAllPersonsRootId{0};

struct ChildLink {
    PersonId child{};
    std::unique_ptr<ChildLink> next;
};

struct TreeNode {
    PersonId person{};
    std::unique_ptr<ChildLink> firstChild;
    ChildLink* lastChild{};  // Non-owning; enables stable O(1) append.
};

class ChildListTree {
public:
    ChildListTree() = default;
    ~ChildListTree() = default;

    ChildListTree(const ChildListTree&) = delete;
    ChildListTree& operator=(const ChildListTree&) = delete;
    ChildListTree(ChildListTree&&) noexcept = default;
    ChildListTree& operator=(ChildListTree&&) noexcept = default;

    void createNode(PersonId person);
    void addChild(PersonId parent, PersonId child);

    const TreeNode* findNode(PersonId person) const noexcept;
    std::vector<PersonId> children(PersonId parent) const;

private:
    struct ComponentEntry {
        PersonId parent{};
        std::size_t rank{};
    };

    std::unordered_map<PersonId, std::unique_ptr<TreeNode>> nodes_;
    std::unordered_map<PersonId, PersonId> parentByChild_;
    std::unordered_map<PersonId, ComponentEntry> components_;

    PersonId findComponentRoot(PersonId person) noexcept;
    void uniteComponents(PersonId first, PersonId second) noexcept;
};

}  // namespace novel
