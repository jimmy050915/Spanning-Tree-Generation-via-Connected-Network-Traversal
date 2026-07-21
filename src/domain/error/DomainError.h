#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace novel {

enum class DomainErrorCode {
    EmptyPersonName,
    DuplicatePerson,
    PersonNotFound,
    EmptyChapterKey,
    DuplicateChapterKey,
    EmptyAlias,
    DuplicateAlias,
    NameConflict,
    PersonInUse,
    SelfLoop,
    DuplicateEdge,
    InvalidCoChapterCount,
    InvalidStatistics,
    IdentifierExhausted,
    GraphValidationFailed,
    ProjectValidationFailed
};

class DomainError final : public std::runtime_error {
public:
    DomainError(DomainErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}

    DomainErrorCode code() const noexcept {
        return code_;
    }

private:
    DomainErrorCode code_;
};

}  // namespace novel
