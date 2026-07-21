#include "infrastructure/persistence/BinaryProjectSerializer.h"

#include "domain/error/DomainError.h"
#include "domain/model/ProjectSnapshot.h"
#include "infrastructure/persistence/PersistenceError.h"
#include "infrastructure/persistence/Sha256.h"
#include "infrastructure/text/Utf8TextFileLoader.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace novel {

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::array<std::uint8_t, 8> fileMagic{
    'N', 'P', 'R', 'G', 'R', 'A', 'P', 'H'};
constexpr std::uint32_t endianMarker = 0x01020304U;
constexpr std::uint32_t sectionVersion = 1U;
constexpr std::size_t fileHeaderSize = 32U;
constexpr std::size_t digestSize = 32U;
constexpr std::uint64_t maxFileBytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t maxPayloadBytes =
    maxFileBytes - fileHeaderSize - digestSize;
constexpr std::uint32_t maxSectionCount = 64U;
constexpr std::uint32_t maxPersonCount = 5000U;
constexpr std::uint32_t maxChapterCount = 10000U;
constexpr std::uint32_t maxEdgeCount = 100000U;
constexpr std::uint32_t maxAliasCount = 100000U;
constexpr std::uint32_t maxNameBytes = 1024U * 1024U;
constexpr std::uint32_t maxChapterContentBytes = 20U * 1024U * 1024U;

enum class SectionType : std::uint32_t {
    Metadata = 1U,
    Persons = 2U,
    Relations = 3U,
    Aliases = 4U,
    Chapters = 5U
};

std::string pathDescription(const std::filesystem::path& path) {
    return path.u8string();
}

class ByteWriter {
public:
    void writeU32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32U; shift += 8U) {
            bytes_.push_back(
                static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }

    void writeU64(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64U; shift += 8U) {
            bytes_.push_back(
                static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }

    void writeDouble(double value) {
        static_assert(sizeof(double) == sizeof(std::uint64_t),
                      "二进制项目格式要求 64 位 double");
        static_assert(std::numeric_limits<double>::is_iec559,
                      "二进制项目格式要求 IEEE-754 double");
        std::uint64_t bits{};
        std::memcpy(&bits, &value, sizeof(bits));
        writeU64(bits);
    }

    void writeBytes(const std::uint8_t* data, std::size_t size) {
        if (size == 0U) {
            return;
        }
        bytes_.insert(bytes_.end(), data, data + size);
    }

    void writeBytes(const Bytes& value) {
        writeBytes(value.data(), value.size());
    }

    void writeString(const std::string& value,
                     std::uint32_t maximumBytes,
                     const char* fieldName) {
        if (value.size() > maximumBytes ||
            value.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw PersistenceError(
                PersistenceErrorCode::InvalidProject,
                std::string(fieldName) + "超过二进制格式允许的长度");
        }
        if (!isValidUtf8(value)) {
            throw PersistenceError(PersistenceErrorCode::InvalidUtf8,
                                   std::string(fieldName) +
                                       "不是合法 UTF-8 文本");
        }
        writeU32(static_cast<std::uint32_t>(value.size()));
        writeBytes(reinterpret_cast<const std::uint8_t*>(value.data()),
                   value.size());
    }

    const Bytes& bytes() const noexcept {
        return bytes_;
    }

    Bytes take() noexcept {
        return std::move(bytes_);
    }

private:
    Bytes bytes_;
};

class ByteReader {
public:
    ByteReader(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size) {}

    std::uint32_t readU32(const char* fieldName) {
        require(4U, fieldName);
        std::uint32_t result{};
        for (unsigned byte = 0; byte < 4U; ++byte) {
            result |= static_cast<std::uint32_t>(data_[offset_ + byte])
                      << (byte * 8U);
        }
        offset_ += 4U;
        return result;
    }

    std::uint64_t readU64(const char* fieldName) {
        require(8U, fieldName);
        std::uint64_t result{};
        for (unsigned byte = 0; byte < 8U; ++byte) {
            result |= static_cast<std::uint64_t>(data_[offset_ + byte])
                      << (byte * 8U);
        }
        offset_ += 8U;
        return result;
    }

    double readDouble(const char* fieldName) {
        const auto bits = readU64(fieldName);
        double result{};
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }

    std::string readString(std::uint32_t maximumBytes,
                           const char* fieldName) {
        const auto length = readU32(fieldName);
        if (length > maximumBytes) {
            throw PersistenceError(
                PersistenceErrorCode::InvalidSection,
                std::string(fieldName) + "超过允许的长度上限");
        }
        require(length, fieldName);
        std::string result(reinterpret_cast<const char*>(data_ + offset_),
                           length);
        offset_ += length;
        if (!isValidUtf8(result)) {
            throw PersistenceError(PersistenceErrorCode::InvalidUtf8,
                                   std::string(fieldName) +
                                       "包含非法 UTF-8 字节序列");
        }
        return result;
    }

    ByteReader readSlice(std::uint64_t length, const char* fieldName) {
        if (length > std::numeric_limits<std::size_t>::max()) {
            throw PersistenceError(PersistenceErrorCode::InvalidSection,
                                   std::string(fieldName) + "长度无法表示");
        }
        const auto nativeLength = static_cast<std::size_t>(length);
        require(nativeLength, fieldName);
        ByteReader result(data_ + offset_, nativeLength);
        offset_ += nativeLength;
        return result;
    }

    void readBytes(std::uint8_t* destination,
                   std::size_t length,
                   const char* fieldName) {
        require(length, fieldName);
        std::copy_n(data_ + offset_, length, destination);
        offset_ += length;
    }

    std::size_t remaining() const noexcept {
        return size_ - offset_;
    }

    bool atEnd() const noexcept {
        return offset_ == size_;
    }

    void requireEnd(const char* sectionName) const {
        if (!atEnd()) {
            throw PersistenceError(
                PersistenceErrorCode::InvalidSection,
                std::string(sectionName) + "包含未声明的尾随数据");
        }
    }

private:
    void require(std::size_t length, const char* fieldName) const {
        if (length > size_ - offset_) {
            throw PersistenceError(
                PersistenceErrorCode::TruncatedFile,
                std::string("读取") + fieldName + "时文件已截断");
        }
    }

    const std::uint8_t* data_{};
    std::size_t size_{};
    std::size_t offset_{};
};

struct EncodedSection {
    SectionType type{};
    Bytes payload;
};

template <typename Container>
std::uint32_t checkedCount(const Container& values,
                           std::uint32_t maximum,
                           const char* description) {
    if (values.size() > maximum ||
        values.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw PersistenceError(PersistenceErrorCode::InvalidProject,
                               std::string(description) + "数量超过上限");
    }
    return static_cast<std::uint32_t>(values.size());
}

EncodedSection encodeMetadata(const ProjectSnapshot& snapshot) {
    ByteWriter writer;
    writer.writeU32(snapshot.nextPersonId);
    writer.writeU64(snapshot.nextEdgeId);
    writer.writeU64(snapshot.nextChapterId);
    return EncodedSection{SectionType::Metadata, writer.take()};
}

EncodedSection encodePersons(const ProjectSnapshot& snapshot) {
    ByteWriter writer;
    writer.writeU32(
        checkedCount(snapshot.persons, maxPersonCount, "人物"));
    for (const auto& person : snapshot.persons) {
        writer.writeU32(person.id);
        writer.writeString(person.canonicalName,
                           maxNameBytes,
                           "标准人物名");
        writer.writeU32(person.chapterCount);
    }
    return EncodedSection{SectionType::Persons, writer.take()};
}

EncodedSection encodeRelations(const ProjectSnapshot& snapshot) {
    ByteWriter writer;
    writer.writeU32(
        checkedCount(snapshot.edges, maxEdgeCount, "人物关系"));
    for (const auto& edge : snapshot.edges) {
        writer.writeU64(edge.id);
        writer.writeU32(edge.endpointA);
        writer.writeU32(edge.endpointB);
        writer.writeU32(edge.coChapterCount);
        writer.writeDouble(edge.jaccard);
    }
    return EncodedSection{SectionType::Relations, writer.take()};
}

EncodedSection encodeAliases(const ProjectSnapshot& snapshot) {
    ByteWriter writer;
    writer.writeU32(
        checkedCount(snapshot.aliases, maxAliasCount, "人物别名"));
    for (const auto& alias : snapshot.aliases) {
        writer.writeString(alias.first, maxNameBytes, "人物别名");
        writer.writeU32(alias.second);
    }
    return EncodedSection{SectionType::Aliases, writer.take()};
}

EncodedSection encodeChapters(const ProjectSnapshot& snapshot) {
    ByteWriter writer;
    writer.writeU32(
        checkedCount(snapshot.chapters, maxChapterCount, "章节"));
    for (const auto& chapter : snapshot.chapters) {
        writer.writeU64(chapter.id);
        writer.writeString(chapter.chapterKey, maxNameBytes, "章节编号");
        writer.writeString(chapter.title, maxNameBytes, "章节标题");
        writer.writeString(chapter.sourceFileName,
                           maxNameBytes,
                           "章节源文件名");
        writer.writeString(chapter.contentUtf8,
                           maxChapterContentBytes,
                           "章节正文");
        writer.writeU32(checkedCount(chapter.persons,
                                     maxPersonCount,
                                     "单章人物"));
        for (const auto personId : chapter.persons) {
            writer.writeU32(personId);
        }
    }
    return EncodedSection{SectionType::Chapters, writer.take()};
}

Bytes serializeProject(const NovelRelationProject& project) {
    const auto validation = project.validate();
    if (!validation.isValid()) {
        std::string reason = "项目未通过完整一致性验证";
        for (const auto& issue : validation.issues) {
            if (issue.severity == ValidationSeverity::Error) {
                reason += " [" + issue.code + "] " + issue.message;
                break;
            }
        }
        throw PersistenceError(PersistenceErrorCode::InvalidProject,
                               std::move(reason));
    }

    const ProjectSnapshot snapshot = project.snapshot();
    std::vector<EncodedSection> sections;
    sections.reserve(5U);
    sections.push_back(encodeMetadata(snapshot));
    sections.push_back(encodePersons(snapshot));
    sections.push_back(encodeRelations(snapshot));
    sections.push_back(encodeAliases(snapshot));
    sections.push_back(encodeChapters(snapshot));

    ByteWriter payloadWriter;
    for (const auto& section : sections) {
        payloadWriter.writeU32(static_cast<std::uint32_t>(section.type));
        payloadWriter.writeU32(sectionVersion);
        payloadWriter.writeU64(section.payload.size());
        payloadWriter.writeBytes(section.payload);
    }
    const Bytes payload = payloadWriter.take();
    if (payload.size() > maxPayloadBytes) {
        throw PersistenceError(PersistenceErrorCode::FileTooLarge,
                               "项目数据超过 512 MiB 文件上限");
    }

    ByteWriter fileWriter;
    fileWriter.writeBytes(fileMagic.data(), fileMagic.size());
    fileWriter.writeU32(BinaryProjectSerializer::formatVersion);
    fileWriter.writeU32(endianMarker);
    fileWriter.writeU64(payload.size());
    fileWriter.writeU32(static_cast<std::uint32_t>(sections.size()));
    fileWriter.writeU32(0U);
    fileWriter.writeBytes(payload);
    const auto digest = Sha256::digest(payload);
    fileWriter.writeBytes(digest.data(), digest.size());
    return fileWriter.take();
}

Bytes readFile(const std::filesystem::path& path) {
    std::error_code sizeError;
    const auto fileSize = std::filesystem::file_size(path, sizeError);
    if (sizeError) {
        throw PersistenceError(
            PersistenceErrorCode::FileOpenFailed,
            "无法读取项目文件大小：" + pathDescription(path) +
                "（" + sizeError.message() + "）");
    }
    if (fileSize > maxFileBytes) {
        throw PersistenceError(PersistenceErrorCode::FileTooLarge,
                               "项目文件超过 512 MiB 上限：" +
                                   pathDescription(path));
    }

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw PersistenceError(PersistenceErrorCode::FileOpenFailed,
                               "无法打开项目文件：" +
                                   pathDescription(path));
    }

    Bytes result(static_cast<std::size_t>(fileSize));
    if (!result.empty()) {
        input.read(reinterpret_cast<char*>(result.data()),
                   static_cast<std::streamsize>(result.size()));
        if (input.gcount() != static_cast<std::streamsize>(result.size())) {
            throw PersistenceError(PersistenceErrorCode::FileReadFailed,
                                   "项目文件读取不完整：" +
                                       pathDescription(path));
        }
    }
    char extra{};
    if (input.read(&extra, 1)) {
        throw PersistenceError(PersistenceErrorCode::FileReadFailed,
                               "读取期间项目文件长度发生变化：" +
                                   pathDescription(path));
    }
    return result;
}

void parseMetadata(ByteReader& reader, ProjectSnapshot& snapshot) {
    snapshot.nextPersonId = reader.readU32("下一人物编号");
    snapshot.nextEdgeId = reader.readU64("下一关系编号");
    snapshot.nextChapterId = reader.readU64("下一章节编号");
    reader.requireEnd("项目元数据区段");
}

void parsePersons(ByteReader& reader, ProjectSnapshot& snapshot) {
    const auto count = reader.readU32("人物数量");
    if (count > maxPersonCount) {
        throw PersistenceError(PersistenceErrorCode::InvalidSection,
                               "人物数量超过 5000 上限");
    }
    snapshot.persons.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        PersonSnapshot person;
        person.id = reader.readU32("人物编号");
        person.canonicalName =
            reader.readString(maxNameBytes, "标准人物名");
        person.chapterCount = reader.readU32("人物出现章节数");
        snapshot.persons.push_back(std::move(person));
    }
    reader.requireEnd("人物区段");
}

void parseRelations(ByteReader& reader, ProjectSnapshot& snapshot) {
    const auto count = reader.readU32("关系数量");
    if (count > maxEdgeCount) {
        throw PersistenceError(PersistenceErrorCode::InvalidSection,
                               "人物关系数量超过 100000 上限");
    }
    snapshot.edges.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        EdgeSnapshot edge;
        edge.id = reader.readU64("关系编号");
        edge.endpointA = reader.readU32("关系端点 A");
        edge.endpointB = reader.readU32("关系端点 B");
        edge.coChapterCount = reader.readU32("共同章节数");
        edge.jaccard = reader.readDouble("Jaccard 关联度");
        snapshot.edges.push_back(edge);
    }
    reader.requireEnd("关系区段");
}

void parseAliases(ByteReader& reader, ProjectSnapshot& snapshot) {
    const auto count = reader.readU32("别名数量");
    if (count > maxAliasCount) {
        throw PersistenceError(PersistenceErrorCode::InvalidSection,
                               "人物别名数量超过 100000 上限");
    }
    snapshot.aliases.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        auto alias = reader.readString(maxNameBytes, "人物别名");
        const auto target = reader.readU32("别名目标人物编号");
        snapshot.aliases.emplace_back(std::move(alias), target);
    }
    reader.requireEnd("别名区段");
}

void parseChapters(ByteReader& reader, ProjectSnapshot& snapshot) {
    const auto count = reader.readU32("章节数量");
    if (count > maxChapterCount) {
        throw PersistenceError(PersistenceErrorCode::InvalidSection,
                               "章节数量超过 10000 上限");
    }
    snapshot.chapters.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        ChapterRecord chapter;
        chapter.id = reader.readU64("章节内部编号");
        chapter.chapterKey = reader.readString(maxNameBytes, "章节编号");
        chapter.title = reader.readString(maxNameBytes, "章节标题");
        chapter.sourceFileName =
            reader.readString(maxNameBytes, "章节源文件名");
        chapter.contentUtf8 =
            reader.readString(maxChapterContentBytes, "章节正文");
        const auto personCount = reader.readU32("单章人物数量");
        if (personCount > maxPersonCount) {
            throw PersistenceError(PersistenceErrorCode::InvalidSection,
                                   "单章人物数量超过 5000 上限");
        }
        chapter.persons.reserve(personCount);
        for (std::uint32_t person = 0; person < personCount; ++person) {
            chapter.persons.push_back(reader.readU32("章节人物编号"));
        }
        snapshot.chapters.push_back(std::move(chapter));
    }
    reader.requireEnd("章节区段");
}

ProjectSnapshot parseFileBytes(const Bytes& file) {
    if (file.size() < fileHeaderSize + digestSize) {
        throw PersistenceError(PersistenceErrorCode::TruncatedFile,
                               "项目文件短于最小文件头和摘要长度");
    }

    ByteReader header(file.data(), fileHeaderSize);
    std::array<std::uint8_t, 8> magic{};
    header.readBytes(magic.data(), magic.size(), "文件魔数");
    if (magic != fileMagic) {
        throw PersistenceError(PersistenceErrorCode::InvalidMagic,
                               "项目文件魔数不是 NPRGRAPH");
    }
    const auto version = header.readU32("文件格式版本");
    if (version != BinaryProjectSerializer::formatVersion) {
        throw PersistenceError(PersistenceErrorCode::UnsupportedVersion,
                               "不支持的项目文件格式版本：" +
                                   std::to_string(version));
    }
    if (header.readU32("端序标记") != endianMarker) {
        throw PersistenceError(PersistenceErrorCode::InvalidEndianMarker,
                               "项目文件端序标记无效");
    }
    const auto payloadLength = header.readU64("数据区长度");
    const auto sectionCount = header.readU32("区段数量");
    if (header.readU32("保留字段") != 0U) {
        throw PersistenceError(PersistenceErrorCode::InvalidHeader,
                               "项目文件头保留字段必须为零");
    }
    header.requireEnd("项目文件头");

    if (payloadLength > maxFileBytes ||
        payloadLength > std::numeric_limits<std::size_t>::max()) {
        throw PersistenceError(PersistenceErrorCode::FileTooLarge,
                               "项目文件声明的数据区长度超过上限");
    }
    const auto actualPayloadLength =
        file.size() - fileHeaderSize - digestSize;
    if (payloadLength > actualPayloadLength) {
        throw PersistenceError(PersistenceErrorCode::TruncatedFile,
                               "项目文件数据区短于文件头声明长度");
    }
    if (payloadLength != actualPayloadLength) {
        throw PersistenceError(PersistenceErrorCode::InvalidHeader,
                               "项目文件含有未声明的尾随字节");
    }
    if (sectionCount > maxSectionCount) {
        throw PersistenceError(PersistenceErrorCode::InvalidHeader,
                               "项目文件区段数量超过上限");
    }

    const auto calculatedDigest = Sha256::digest(
        file.data() + fileHeaderSize,
        static_cast<std::size_t>(payloadLength));
    const auto* storedDigest =
        file.data() + fileHeaderSize + static_cast<std::size_t>(payloadLength);
    if (!std::equal(calculatedDigest.begin(),
                    calculatedDigest.end(),
                    storedDigest)) {
        throw PersistenceError(PersistenceErrorCode::DigestMismatch,
                               "项目文件 SHA-256 摘要校验失败");
    }

    ByteReader payload(file.data() + fileHeaderSize,
                       static_cast<std::size_t>(payloadLength));
    ProjectSnapshot snapshot;
    bool metadataSeen{};
    bool personsSeen{};
    bool relationsSeen{};
    bool aliasesSeen{};
    bool chaptersSeen{};

    for (std::uint32_t index = 0; index < sectionCount; ++index) {
        const auto rawType = payload.readU32("区段类型");
        const auto versionValue = payload.readU32("区段版本");
        const auto sectionLength = payload.readU64("区段长度");
        ByteReader section = payload.readSlice(sectionLength, "区段内容");

        auto rejectDuplicate = [](bool& seen, const char* name) {
            if (seen) {
                throw PersistenceError(
                    PersistenceErrorCode::InvalidSection,
                    std::string("项目文件包含重复") + name + "区段");
            }
            seen = true;
        };

        switch (static_cast<SectionType>(rawType)) {
        case SectionType::Metadata:
            rejectDuplicate(metadataSeen, "项目元数据");
            if (versionValue != sectionVersion) {
                throw PersistenceError(
                    PersistenceErrorCode::UnsupportedVersion,
                    "不支持的项目元数据区段版本");
            }
            parseMetadata(section, snapshot);
            break;
        case SectionType::Persons:
            rejectDuplicate(personsSeen, "人物");
            if (versionValue != sectionVersion) {
                throw PersistenceError(
                    PersistenceErrorCode::UnsupportedVersion,
                    "不支持的人物区段版本");
            }
            parsePersons(section, snapshot);
            break;
        case SectionType::Relations:
            rejectDuplicate(relationsSeen, "关系");
            if (versionValue != sectionVersion) {
                throw PersistenceError(
                    PersistenceErrorCode::UnsupportedVersion,
                    "不支持的关系区段版本");
            }
            parseRelations(section, snapshot);
            break;
        case SectionType::Aliases:
            rejectDuplicate(aliasesSeen, "别名");
            if (versionValue != sectionVersion) {
                throw PersistenceError(
                    PersistenceErrorCode::UnsupportedVersion,
                    "不支持的别名区段版本");
            }
            parseAliases(section, snapshot);
            break;
        case SectionType::Chapters:
            rejectDuplicate(chaptersSeen, "章节");
            if (versionValue != sectionVersion) {
                throw PersistenceError(
                    PersistenceErrorCode::UnsupportedVersion,
                    "不支持的章节区段版本");
            }
            parseChapters(section, snapshot);
            break;
        default:
            // Unknown sections are intentionally skipped by their declared
            // length so newer files can add optional data.
            break;
        }
    }
    payload.requireEnd("项目数据区");

    if (!metadataSeen || !personsSeen || !relationsSeen || !aliasesSeen ||
        !chaptersSeen) {
        throw PersistenceError(PersistenceErrorCode::InvalidSection,
                               "项目文件缺少一个或多个必需区段");
    }
    return snapshot;
}

void writeFile(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw PersistenceError(PersistenceErrorCode::FileOpenFailed,
                               "无法创建临时项目文件：" +
                                   pathDescription(path));
    }
    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    output.flush();
    if (!output) {
        throw PersistenceError(PersistenceErrorCode::FileWriteFailed,
                               "写入或刷新临时项目文件失败：" +
                                   pathDescription(path));
    }
    output.close();
    if (!output) {
        throw PersistenceError(PersistenceErrorCode::FileWriteFailed,
                               "关闭临时项目文件失败：" +
                                   pathDescription(path));
    }

#ifdef _WIN32
    const HANDLE handle = CreateFileW(path.c_str(),
                                      GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ,
                                      nullptr,
                                      OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL,
                                      nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw PersistenceError(
            PersistenceErrorCode::FileWriteFailed,
            "无法打开临时项目文件执行磁盘刷新，系统错误码：" +
                std::to_string(static_cast<unsigned long>(GetLastError())));
    }
    if (FlushFileBuffers(handle) == 0) {
        const auto error = GetLastError();
        CloseHandle(handle);
        throw PersistenceError(
            PersistenceErrorCode::FileWriteFailed,
            "临时项目文件磁盘刷新失败，系统错误码：" +
                std::to_string(static_cast<unsigned long>(error)));
    }
    CloseHandle(handle);
#else
    const int descriptor = ::open(path.c_str(), O_RDONLY);
    if (descriptor < 0) {
        throw PersistenceError(PersistenceErrorCode::FileWriteFailed,
                               "无法打开临时项目文件执行磁盘刷新：" +
                                   std::string(std::strerror(errno)));
    }
    if (::fsync(descriptor) != 0) {
        const int error = errno;
        ::close(descriptor);
        throw PersistenceError(PersistenceErrorCode::FileWriteFailed,
                               "临时项目文件磁盘刷新失败：" +
                                   std::string(std::strerror(error)));
    }
    ::close(descriptor);
#endif
}

std::filesystem::path uniqueSibling(const std::filesystem::path& target,
                                    const char* role) {
    static std::atomic<unsigned long long> sequence{0U};
    const auto stamp = static_cast<unsigned long long>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto serial = sequence.fetch_add(1U, std::memory_order_relaxed);
#ifdef _WIN32
    const auto processId =
        static_cast<unsigned long long>(GetCurrentProcessId());
#else
    const auto processId = static_cast<unsigned long long>(::getpid());
#endif
    for (unsigned attempt = 0; attempt < 1000U; ++attempt) {
        auto candidate = target;
        candidate += std::string(".") + role + "-" +
                     std::to_string(processId) + "-" +
                     std::to_string(stamp) + "-" +
                     std::to_string(serial) + "-" +
                     std::to_string(attempt);
        std::error_code existenceError;
        const bool exists = std::filesystem::exists(candidate, existenceError);
        if (existenceError) {
            throw PersistenceError(
                PersistenceErrorCode::FileOpenFailed,
                "无法检查临时文件名：" + pathDescription(candidate));
        }
        if (!exists) {
            return candidate;
        }
    }
    throw PersistenceError(PersistenceErrorCode::FileOpenFailed,
                           "无法在目标目录生成唯一临时文件名");
}

void removeNoThrow(const std::filesystem::path& path) noexcept {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

void replaceWithBackup(const std::filesystem::path& temporary,
                       const std::filesystem::path& target) {
    std::error_code existenceError;
    const bool hadTarget = std::filesystem::exists(target, existenceError);
    if (existenceError) {
        throw PersistenceError(PersistenceErrorCode::AtomicReplaceFailed,
                               "无法检查原项目文件：" +
                                   pathDescription(target));
    }

#ifdef _WIN32
    if (!hadTarget) {
        if (MoveFileExW(temporary.c_str(),
                        target.c_str(),
                        MOVEFILE_WRITE_THROUGH) == 0) {
            throw PersistenceError(
                PersistenceErrorCode::AtomicReplaceFailed,
                "无法将临时文件提交为项目文件，系统错误码：" +
                    std::to_string(
                        static_cast<unsigned long>(GetLastError())));
        }
        return;
    }

    const auto backup = uniqueSibling(target, "backup");
    if (ReplaceFileW(target.c_str(),
                     temporary.c_str(),
                     backup.c_str(),
                     REPLACEFILE_WRITE_THROUGH,
                     nullptr,
                     nullptr) == 0) {
        throw PersistenceError(
            PersistenceErrorCode::AtomicReplaceFailed,
            "无法原子替换项目文件，旧文件未被修改，系统错误码：" +
                std::to_string(static_cast<unsigned long>(GetLastError())));
    }

    // Failure to remove a now-redundant backup must not turn a successfully
    // committed save into a reported failure. The project file is already
    // complete and the leftover backup remains recoverable.
    removeNoThrow(backup);
#else
    std::filesystem::path backup;
    if (hadTarget) {
        backup = uniqueSibling(target, "backup");
        std::error_code backupError;
        std::filesystem::create_hard_link(target, backup, backupError);
        if (backupError) {
            throw PersistenceError(
                PersistenceErrorCode::AtomicReplaceFailed,
                "无法为旧项目文件创建备份，旧文件未被修改：" +
                    backupError.message());
        }
    }

    std::error_code commitError;
    std::filesystem::rename(temporary, target, commitError);
    if (commitError) {
        removeNoThrow(backup);
        throw PersistenceError(
            PersistenceErrorCode::AtomicReplaceFailed,
            "无法原子替换项目文件，旧文件未被修改：" +
                commitError.message());
    }
    removeNoThrow(backup);
#endif
}

}  // namespace

void BinaryProjectSerializer::save(const NovelRelationProject& project,
                                   const std::filesystem::path& path) const {
    if (path.empty() || path.filename().empty()) {
        throw PersistenceError(PersistenceErrorCode::FileOpenFailed,
                               "项目保存路径不能为空");
    }

    const auto bytes = serializeProject(project);
    auto parent = path.parent_path();
    if (parent.empty()) {
        parent = std::filesystem::current_path();
    }
    std::error_code parentError;
    if (!std::filesystem::is_directory(parent, parentError) || parentError) {
        throw PersistenceError(PersistenceErrorCode::FileOpenFailed,
                               "项目目标目录不存在或不可访问：" +
                                   pathDescription(parent));
    }
    std::error_code typeError;
    if (std::filesystem::exists(path, typeError) &&
        std::filesystem::is_directory(path, typeError)) {
        throw PersistenceError(PersistenceErrorCode::FileOpenFailed,
                               "项目保存路径指向目录：" +
                                   pathDescription(path));
    }
    if (typeError) {
        throw PersistenceError(PersistenceErrorCode::FileOpenFailed,
                               "无法检查项目保存路径：" +
                                   pathDescription(path));
    }

    const auto temporary = uniqueSibling(path, "tmp");
    try {
        writeFile(temporary, bytes);
        // Reopen through the public loader before touching the old project.
        auto verifiedProject = load(temporary);
        static_cast<void>(verifiedProject);
        replaceWithBackup(temporary, path);
    } catch (...) {
        removeNoThrow(temporary);
        throw;
    }
}

NovelRelationProject BinaryProjectSerializer::load(
    const std::filesystem::path& path) const {
    const auto bytes = readFile(path);
    auto snapshot = parseFileBytes(bytes);
    try {
        return NovelRelationProject::fromSnapshot(std::move(snapshot));
    } catch (const DomainError& error) {
        throw PersistenceError(
            PersistenceErrorCode::InvalidProject,
            std::string("项目文件中的业务数据无效：") + error.what());
    }
}

}  // namespace novel
