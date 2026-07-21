#include "domain/traversal/ChildListTree.h"

#include "domain/error/DomainError.h"

#include <memory>
#include <string>

namespace novel {
namespace {

std::string nodeDescription(PersonId person) {
    return "遍历树结点 " + std::to_string(person);
}

}  // namespace

void ChildListTree::createNode(PersonId person) {
    if (nodes_.find(person) != nodes_.end()) {
        throw DomainError(DomainErrorCode::DuplicateTreeNode,
                          nodeDescription(person) + " 已存在。");
    }

    auto node = std::make_unique<TreeNode>();
    node->person = person;
    nodes_.emplace(person, std::move(node));

    try {
        components_.emplace(person, ComponentEntry{person, 0});
    } catch (...) {
        nodes_.erase(person);
        throw;
    }
}

void ChildListTree::addChild(PersonId parent, PersonId child) {
    auto parentIt = nodes_.find(parent);
    if (parentIt == nodes_.end()) {
        throw DomainError(DomainErrorCode::TreeNodeNotFound,
                          nodeDescription(parent) + " 不存在。");
    }
    if (nodes_.find(child) == nodes_.end()) {
        throw DomainError(DomainErrorCode::TreeNodeNotFound,
                          nodeDescription(child) + " 不存在。");
    }
    if (parent == child) {
        throw DomainError(DomainErrorCode::InvalidTreeRelation,
                          "遍历树结点不能作为自己的孩子。");
    }

    const auto existingParent = parentByChild_.find(child);
    if (existingParent != parentByChild_.end()) {
        const std::string reason = existingParent->second == parent
                                       ? "该父子关系已存在。"
                                       : "孩子结点已有父结点。";
        throw DomainError(DomainErrorCode::InvalidTreeRelation, reason);
    }

    if (findComponentRoot(parent) == findComponentRoot(child)) {
        throw DomainError(DomainErrorCode::InvalidTreeRelation,
                          "父子关系会在遍历树中形成环。");
    }

    auto link = std::make_unique<ChildLink>();
    link->child = child;
    ChildLink* const appendedLink = link.get();

    parentByChild_.emplace(child, parent);

    TreeNode& parentNode = *parentIt->second;
    if (parentNode.lastChild == nullptr) {
        parentNode.firstChild = std::move(link);
    } else {
        parentNode.lastChild->next = std::move(link);
    }
    parentNode.lastChild = appendedLink;
    uniteComponents(parent, child);
}

const TreeNode* ChildListTree::findNode(PersonId person) const noexcept {
    const auto it = nodes_.find(person);
    return it == nodes_.end() ? nullptr : it->second.get();
}

std::vector<PersonId> ChildListTree::children(PersonId parent) const {
    const TreeNode* const parentNode = findNode(parent);
    if (parentNode == nullptr) {
        throw DomainError(DomainErrorCode::TreeNodeNotFound,
                          nodeDescription(parent) + " 不存在。");
    }

    std::vector<PersonId> result;
    for (const ChildLink* link = parentNode->firstChild.get(); link != nullptr;
         link = link->next.get()) {
        result.push_back(link->child);
    }
    return result;
}

PersonId ChildListTree::findComponentRoot(PersonId person) noexcept {
    PersonId root = person;
    for (;;) {
        const auto rootIt = components_.find(root);
        if (rootIt->second.parent == root) {
            break;
        }
        root = rootIt->second.parent;
    }

    while (person != root) {
        const auto personIt = components_.find(person);
        const PersonId next = personIt->second.parent;
        personIt->second.parent = root;
        person = next;
    }
    return root;
}

void ChildListTree::uniteComponents(PersonId first, PersonId second) noexcept {
    PersonId firstRoot = findComponentRoot(first);
    PersonId secondRoot = findComponentRoot(second);
    if (firstRoot == secondRoot) {
        return;
    }

    auto firstEntry = components_.find(firstRoot);
    auto secondEntry = components_.find(secondRoot);
    if (firstEntry->second.rank < secondEntry->second.rank) {
        firstEntry->second.parent = secondRoot;
        return;
    }
    if (firstEntry->second.rank > secondEntry->second.rank) {
        secondEntry->second.parent = firstRoot;
        return;
    }

    secondEntry->second.parent = firstRoot;
    ++firstEntry->second.rank;
}

}  // namespace novel
