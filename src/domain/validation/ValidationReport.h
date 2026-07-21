#pragma once

#include <string>
#include <vector>

namespace novel {

enum class ValidationSeverity {
    Information,
    Warning,
    Error
};

struct ValidationIssue {
    ValidationSeverity severity{ValidationSeverity::Error};
    std::string code;
    std::string message;
};

struct ValidationReport {
    std::vector<ValidationIssue> issues;

    void add(ValidationSeverity severity, std::string code, std::string message);
    bool isValid() const noexcept;
};

}  // namespace novel
