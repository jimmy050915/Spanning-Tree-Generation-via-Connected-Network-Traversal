#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace novel {

enum class PersistenceErrorCode {
    FileOpenFailed,
    FileReadFailed,
    FileWriteFailed,
    FileTooLarge,
    InvalidMagic,
    UnsupportedVersion,
    InvalidEndianMarker,
    InvalidHeader,
    TruncatedFile,
    InvalidSection,
    InvalidUtf8,
    DigestMismatch,
    InvalidProject,
    AtomicReplaceFailed
};

class PersistenceError final : public std::runtime_error {
public:
    PersistenceError(PersistenceErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}

    PersistenceErrorCode code() const noexcept {
        return code_;
    }

private:
    PersistenceErrorCode code_;
};

}  // namespace novel
