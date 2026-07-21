#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace novel {

// Validates the complete byte sequence, including overlong encodings,
// surrogate code points, values above U+10FFFF, and truncated sequences.
bool isValidUtf8(std::string_view text) noexcept;

class Utf8TextFileLoader final {
public:
    static constexpr std::size_t kDefaultMaximumBytes =
        20U * 1024U * 1024U;
    static constexpr std::size_t kDefaultMaxBytes = kDefaultMaximumBytes;

    Utf8TextFileLoader() = default;

    static std::string load(
        const std::filesystem::path& path,
        std::size_t maximumBytes = kDefaultMaximumBytes);

    static std::string loadFile(
        const std::filesystem::path& path,
        std::size_t maximumBytes = kDefaultMaximumBytes) {
        return load(path, maximumBytes);
    }

    static bool isValidUtf8(std::string_view text) noexcept {
        return novel::isValidUtf8(text);
    }
};

}  // namespace novel
