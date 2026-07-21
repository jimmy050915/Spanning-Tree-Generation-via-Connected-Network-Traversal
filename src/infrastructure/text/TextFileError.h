#pragma once

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace novel {

enum class TextFileErrorCode {
    FileOpenFailed,
    FileReadFailed,
    FileTooLarge,
    LimitExceeded,
    InvalidUtf8,
    InvalidMetadata,
    DuplicateMetadata,
    MetadataOutOfPlace,
    InvalidDictionaryEntry,
    DuplicatePersonName,
    InvalidAliasFormat,
    EmptyAliasField,
    DuplicateAlias,
    UnknownCanonicalName,
    AliasConflictsWithCanonicalName,

    // More explicit aliases retained for callers that prefer domain wording.
    InvalidChapterMetadata = InvalidMetadata,
    DuplicateChapterMetadata = DuplicateMetadata,
    DuplicatePerson = DuplicatePersonName,
    EmptyAlias = EmptyAliasField,
    AliasTargetNotFound = UnknownCanonicalName,
    AliasNameConflict = AliasConflictsWithCanonicalName
};

class TextFileError final : public std::runtime_error {
public:
    TextFileError(TextFileErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}

    TextFileError(TextFileErrorCode code,
                  std::filesystem::path path,
                  std::size_t line,
                  std::string message)
        : std::runtime_error(std::move(message)),
          code_(code),
          path_(std::move(path)),
          line_(line) {}

    TextFileErrorCode code() const noexcept {
        return code_;
    }

    const std::filesystem::path& path() const noexcept {
        return path_;
    }

    const std::filesystem::path& sourcePath() const noexcept {
        return path_;
    }

    std::size_t line() const noexcept {
        return line_;
    }

    std::size_t lineNumber() const noexcept {
        return line_;
    }

private:
    TextFileErrorCode code_;
    std::filesystem::path path_;
    std::size_t line_{};
};

}  // namespace novel
