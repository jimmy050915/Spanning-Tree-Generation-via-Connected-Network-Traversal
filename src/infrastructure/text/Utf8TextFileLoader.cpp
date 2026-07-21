#include "infrastructure/text/Utf8TextFileLoader.h"

#include "infrastructure/text/TextFileError.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>

namespace novel {

namespace {

constexpr unsigned char kUtf8Bom[] = {0xefU, 0xbbU, 0xbfU};

bool isContinuation(unsigned char byte) noexcept {
    return byte >= 0x80U && byte <= 0xbfU;
}

std::string displayPath(const std::filesystem::path& path) {
    return path.u8string();
}

std::size_t lineAtByte(std::string_view text, std::size_t offset) noexcept {
    const auto boundedOffset = std::min(offset, text.size());
    return 1U + static_cast<std::size_t>(
                    std::count(text.begin(), text.begin() +
                                                  static_cast<std::ptrdiff_t>(
                                                      boundedOffset),
                               '\n'));
}

std::size_t firstInvalidUtf8Byte(std::string_view text) noexcept {
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto first = static_cast<unsigned char>(text[offset]);
        if (first <= 0x7fU) {
            ++offset;
            continue;
        }

        if (first >= 0xc2U && first <= 0xdfU) {
            if (offset + 1U >= text.size() ||
                !isContinuation(static_cast<unsigned char>(text[offset + 1U]))) {
                return offset;
            }
            offset += 2U;
            continue;
        }

        if (first >= 0xe0U && first <= 0xefU) {
            if (offset + 2U >= text.size()) {
                return offset;
            }
            const auto second = static_cast<unsigned char>(text[offset + 1U]);
            const auto third = static_cast<unsigned char>(text[offset + 2U]);
            const bool secondInRange =
                first == 0xe0U ? second >= 0xa0U && second <= 0xbfU
                               : first == 0xedU
                                     ? second >= 0x80U && second <= 0x9fU
                                     : isContinuation(second);
            if (!secondInRange || !isContinuation(third)) {
                return offset;
            }
            offset += 3U;
            continue;
        }

        if (first >= 0xf0U && first <= 0xf4U) {
            if (offset + 3U >= text.size()) {
                return offset;
            }
            const auto second = static_cast<unsigned char>(text[offset + 1U]);
            const auto third = static_cast<unsigned char>(text[offset + 2U]);
            const auto fourth = static_cast<unsigned char>(text[offset + 3U]);
            const bool secondInRange =
                first == 0xf0U ? second >= 0x90U && second <= 0xbfU
                               : first == 0xf4U
                                     ? second >= 0x80U && second <= 0x8fU
                                     : isContinuation(second);
            if (!secondInRange || !isContinuation(third) ||
                !isContinuation(fourth)) {
                return offset;
            }
            offset += 4U;
            continue;
        }

        return offset;
    }
    return text.size();
}

}  // namespace

bool isValidUtf8(std::string_view text) noexcept {
    return firstInvalidUtf8Byte(text) == text.size();
}

std::string Utf8TextFileLoader::load(const std::filesystem::path& path,
                                     std::size_t maximumBytes) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        throw TextFileError(TextFileErrorCode::FileOpenFailed,
                            path,
                            0,
                            "无法打开 UTF-8 文本文件：" +
                                displayPath(path));
    }

    std::error_code typeError;
    if (!std::filesystem::is_regular_file(path, typeError)) {
        throw TextFileError(
            TextFileErrorCode::FileReadFailed,
            path,
            0,
            "读取目标不是普通文本文件：" + displayPath(path) +
                (typeError ? "（无法确认文件类型：" + typeError.message() +
                                 "）"
                           : ""));
    }

    const std::streampos endPosition = input.tellg();
    if (endPosition < std::streampos(0)) {
        throw TextFileError(TextFileErrorCode::FileReadFailed,
                            path,
                            0,
                            "无法取得文本文件长度：" +
                                displayPath(path));
    }

    const std::streamoff endOffset = endPosition - std::streampos(0);
    if (endOffset < 0) {
        throw TextFileError(TextFileErrorCode::FileReadFailed,
                            path,
                            0,
                            "文本文件长度无效：" + displayPath(path));
    }
    const auto fileSize = static_cast<std::uintmax_t>(endOffset);
    if (fileSize > static_cast<std::uintmax_t>(maximumBytes)) {
        throw TextFileError(
            TextFileErrorCode::FileTooLarge,
            path,
            0,
            "文本文件超过大小上限：" + displayPath(path) + "（实际 " +
                std::to_string(fileSize) + " 字节，上限 " +
                std::to_string(maximumBytes) + " 字节）");
    }
    if (fileSize >
        static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        throw TextFileError(TextFileErrorCode::FileTooLarge,
                            path,
                            0,
                            "文本文件长度无法由当前程序处理：" +
                                displayPath(path));
    }
    if (fileSize > static_cast<std::uintmax_t>(
                       std::numeric_limits<std::streamsize>::max())) {
        throw TextFileError(TextFileErrorCode::FileTooLarge,
                            path,
                            0,
                            "文本文件长度超过单次安全读取范围：" +
                                displayPath(path));
    }

    std::string content(static_cast<std::size_t>(fileSize), '\0');
    input.seekg(0, std::ios::beg);
    if (!input) {
        throw TextFileError(TextFileErrorCode::FileReadFailed,
                            path,
                            0,
                            "无法定位到文本文件开头：" +
                                displayPath(path));
    }
    if (!content.empty()) {
        input.read(content.data(), static_cast<std::streamsize>(content.size()));
        if (!input || input.gcount() !=
                          static_cast<std::streamsize>(content.size())) {
            throw TextFileError(TextFileErrorCode::FileReadFailed,
                                path,
                                0,
                                "读取文本文件时数据不完整：" +
                                    displayPath(path));
        }
    }
    char extra{};
    if (input.read(&extra, 1)) {
        throw TextFileError(TextFileErrorCode::FileReadFailed,
                            path,
                            0,
                            "读取期间文本文件长度发生变化：" +
                                displayPath(path));
    }

    if (content.size() >= 3U &&
        static_cast<unsigned char>(content[0]) == kUtf8Bom[0] &&
        static_cast<unsigned char>(content[1]) == kUtf8Bom[1] &&
        static_cast<unsigned char>(content[2]) == kUtf8Bom[2]) {
        content.erase(0, 3U);
    }

    const std::size_t invalidOffset = firstInvalidUtf8Byte(content);
    if (invalidOffset != content.size()) {
        const std::size_t line = lineAtByte(content, invalidOffset);
        throw TextFileError(
            TextFileErrorCode::InvalidUtf8,
            path,
            line,
            "文本文件不是合法 UTF-8：" + displayPath(path) + "（第 " +
                std::to_string(line) + " 行，字节偏移 " +
                std::to_string(invalidOffset) + "）");
    }

    return content;
}

}  // namespace novel
