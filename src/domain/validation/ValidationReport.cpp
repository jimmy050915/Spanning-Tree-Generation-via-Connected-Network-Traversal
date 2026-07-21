#include "domain/validation/ValidationReport.h"

#include <algorithm>
#include <utility>

namespace novel {

void ValidationReport::add(ValidationSeverity severity,
                           std::string code,
                           std::string message) {
    issues.push_back(ValidationIssue{severity, std::move(code), std::move(message)});
}

bool ValidationReport::isValid() const noexcept {
    return std::none_of(issues.begin(), issues.end(), [](const ValidationIssue& issue) {
        return issue.severity == ValidationSeverity::Error;
    });
}

}  // namespace novel
