#pragma once

#include <string>

namespace novel::application {

enum class ApplicationErrorCode {
    InvalidArgument,
    ProjectPathRequired,
    PersonNotFound,
    ChapterNotFound,
    RelationNotFound,
    Conflict,
    ValidationFailed,
    TextFileFailure,
    PersistenceFailure,
    DomainFailure,
    UnexpectedFailure
};

struct ApplicationError {
    ApplicationErrorCode code{ApplicationErrorCode::UnexpectedFailure};
    std::string message;
};

}  // namespace novel::application
