#include "infrastructure/text/ChapterTextParser.h"

#include "infrastructure/text/TextFileError.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace novel {

namespace {

struct LineView {
    std::string_view text;
    std::size_t start{};
    std::size_t number{};
};

std::string normalizeLineEndings(std::string_view input) {
    std::string normalized;
    normalized.reserve(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        if (input[index] == '\r') {
            normalized.push_back('\n');
            if (index + 1U < input.size() && input[index + 1U] == '\n') {
                ++index;
            }
        } else {
            normalized.push_back(input[index]);
        }
    }
    return normalized;
}

std::vector<LineView> linesOf(std::string_view content) {
    std::vector<LineView> lines;
    if (content.empty()) {
        return lines;
    }

    std::size_t start = 0;
    std::size_t lineNumber = 1;
    while (start < content.size()) {
        const std::size_t end = content.find('\n', start);
        if (end == std::string_view::npos) {
            lines.push_back(LineView{content.substr(start), start, lineNumber});
            break;
        }
        lines.push_back(
            LineView{content.substr(start, end - start), start, lineNumber});
        start = end + 1U;
        ++lineNumber;
    }
    return lines;
}

bool startsWith(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() &&
           text.compare(0, prefix.size(), prefix) == 0;
}

bool looksLikeUnknownMetadata(std::string_view line) noexcept {
    return !line.empty() && line.front() == '@' &&
           line.find('=') != std::string_view::npos;
}

std::string asciiTrim(std::string_view text) {
    const auto isAsciiWhitespace = [](unsigned char character) {
        return character == 0x20U ||
               (character >= 0x09U && character <= 0x0dU);
    };
    std::size_t first = 0;
    while (first < text.size() &&
           isAsciiWhitespace(static_cast<unsigned char>(text[first]))) {
        ++first;
    }
    std::size_t last = text.size();
    while (last > first &&
           isAsciiWhitespace(static_cast<unsigned char>(text[last - 1U]))) {
        --last;
    }
    return std::string(text.substr(first, last - first));
}

TextFileError parserError(TextFileErrorCode code,
                          std::size_t line,
                          std::string message) {
    return TextFileError(code,
                         {},
                         line,
                         "章节文本第 " + std::to_string(line) + " 行：" +
                             std::move(message));
}

ParsedChapterText parseImpl(std::string_view input) {
    if (input.size() > Utf8TextFileLoader::kDefaultMaximumBytes) {
        throw TextFileError(TextFileErrorCode::FileTooLarge,
                            "章节文本超过 20 MiB 大小上限");
    }
    if (!isValidUtf8(input)) {
        throw TextFileError(TextFileErrorCode::InvalidUtf8,
                            "章节文本包含无效 UTF-8 字节序列");
    }

    if (input.size() >= 3U &&
        static_cast<unsigned char>(input[0]) == 0xefU &&
        static_cast<unsigned char>(input[1]) == 0xbbU &&
        static_cast<unsigned char>(input[2]) == 0xbfU) {
        input.remove_prefix(3U);
    }

    std::string normalized = normalizeLineEndings(input);
    const auto lines = linesOf(normalized);

    ParsedChapterText result;
    bool seenChapter = false;
    bool seenTitle = false;
    bool seenMetadata = false;
    std::size_t bodyStart = 0;
    std::size_t firstBodyLine = lines.size();

    constexpr std::string_view chapterPrefix = "@chapter=";
    constexpr std::string_view titlePrefix = "@title=";

    std::size_t index = 0;
    for (; index < lines.size(); ++index) {
        const auto& line = lines[index];
        if (startsWith(line.text, chapterPrefix)) {
            if (seenChapter) {
                throw parserError(TextFileErrorCode::DuplicateMetadata,
                                  line.number,
                                  "重复声明 @chapter 元数据");
            }
            seenChapter = true;
            seenMetadata = true;
            result.chapterKey = asciiTrim(line.text.substr(chapterPrefix.size()));
            if (result.chapterKey.size() >
                ChapterTextParser::kMaximumMetadataValueBytes) {
                throw parserError(TextFileErrorCode::LimitExceeded,
                                  line.number,
                                  "@chapter 值超过 1 MiB 长度上限");
            }
            continue;
        }
        if (startsWith(line.text, titlePrefix)) {
            if (seenTitle) {
                throw parserError(TextFileErrorCode::DuplicateMetadata,
                                  line.number,
                                  "重复声明 @title 元数据");
            }
            seenTitle = true;
            seenMetadata = true;
            result.title = asciiTrim(line.text.substr(titlePrefix.size()));
            if (result.title.size() >
                ChapterTextParser::kMaximumMetadataValueBytes) {
                throw parserError(TextFileErrorCode::LimitExceeded,
                                  line.number,
                                  "@title 值超过 1 MiB 长度上限");
            }
            continue;
        }
        if (looksLikeUnknownMetadata(line.text) && seenMetadata) {
            throw parserError(TextFileErrorCode::InvalidMetadata,
                              line.number,
                              "未知元数据行“" + std::string(line.text) + "”");
        }

        firstBodyLine = index;
        bodyStart = line.start;
        if (seenMetadata && asciiTrim(line.text).empty()) {
            bodyStart = line.start + line.text.size();
            if (bodyStart < normalized.size() &&
                normalized[bodyStart] == '\n') {
                ++bodyStart;
            }
            firstBodyLine = index + 1U;
        }
        break;
    }

    if (index == lines.size()) {
        bodyStart = normalized.size();
        firstBodyLine = lines.size();
    }

    for (std::size_t bodyLine = firstBodyLine; bodyLine < lines.size();
         ++bodyLine) {
        const auto& line = lines[bodyLine];
        if (startsWith(line.text, chapterPrefix) ||
            startsWith(line.text, titlePrefix)) {
            throw parserError(TextFileErrorCode::MetadataOutOfPlace,
                              line.number,
                              "元数据只能位于文件开头、正文之前");
        }
    }

    if (!seenMetadata && !lines.empty() &&
        looksLikeUnknownMetadata(lines.front().text)) {
        throw parserError(TextFileErrorCode::InvalidMetadata,
                          lines.front().number,
                          "未知元数据行“" +
                              std::string(lines.front().text) + "”");
    }

    result.contentUtf8 = normalized.substr(bodyStart);
    return result;
}

}  // namespace

ParsedChapterText ChapterTextParser::parse(std::string_view contentUtf8) {
    return parseImpl(contentUtf8);
}

ParsedChapterText ChapterTextParser::parseFile(
    const std::filesystem::path& path,
    std::size_t maximumBytes) {
    const std::string content = Utf8TextFileLoader::load(path, maximumBytes);
    try {
        return parseImpl(content);
    } catch (const TextFileError& error) {
        if (!error.path().empty()) {
            throw;
        }
        const std::string pathText = path.u8string();
        throw TextFileError(error.code(),
                            path,
                            error.line(),
                            "解析章节文件失败：" + pathText + "；" +
                                error.what());
    }
}

}  // namespace novel
