#include "domain/graph/AdjacencyMultilistGraph.h"
#include "domain/model/ProjectSnapshot.h"
#include "domain/project/NovelRelationProject.h"
#include "infrastructure/persistence/BinaryProjectSerializer.h"
#include "infrastructure/persistence/PersistenceError.h"
#include "infrastructure/persistence/Sha256.h"
#include "infrastructure/text/ChapterTextParser.h"
#include "infrastructure/text/DictionaryNameExtractor.h"
#include "infrastructure/text/DictionaryTextParser.h"
#include "infrastructure/text/TextFileError.h"
#include "infrastructure/text/Utf8TextFileLoader.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace novel {

class GraphTestAccess {
public:
    static void setNextPersonId(AdjacencyMultilistGraph& graph,
                                PersonId id) {
        graph.nextPersonId_ = id;
    }
};

class ProjectTestAccess {
public:
    static AdjacencyMultilistGraph& graph(NovelRelationProject& project) {
        return project.graph_;
    }
};

}  // namespace novel

namespace {

using Bytes = std::vector<std::uint8_t>;
using novel::BinaryProjectSerializer;
using novel::ChapterDraft;
using novel::DictionaryNameExtractor;
using novel::DictionaryTextParser;
using novel::EdgeSnapshot;
using novel::NovelRelationProject;
using novel::PersistenceError;
using novel::PersistenceErrorCode;
using novel::PersonSnapshot;
using novel::ProjectSnapshot;
using novel::Sha256;
using novel::TextFileError;
using novel::TextFileErrorCode;
using novel::Utf8TextFileLoader;

struct TestContext {
    int failures{};

    void check(bool condition, const std::string& message) {
        if (!condition) {
            ++failures;
            std::cerr << "失败：" << message << '\n';
        }
    }
};

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto stamp = static_cast<unsigned long long>(
            std::chrono::high_resolution_clock::now()
                .time_since_epoch()
                .count());
        path_ = std::filesystem::temp_directory_path() /
                ("novel-relation-phase-four-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const noexcept {
        return path_;
    }

    std::filesystem::path file(const std::string& name) const {
        return path_ / std::filesystem::u8path(name);
    }

private:
    std::filesystem::path path_;
};

template <typename Action>
void runCase(TestContext& context,
             const std::string& name,
             Action action) {
    try {
        action();
    } catch (const std::exception& error) {
        context.check(false, name + "抛出意外异常：" + error.what());
    } catch (...) {
        context.check(false, name + "抛出未知异常");
    }
}

template <typename Action>
void expectTextError(TestContext& context,
                     TextFileErrorCode expected,
                     Action action,
                     const std::string& message) {
    try {
        action();
        context.check(false, message + "（未抛出异常）");
    } catch (const TextFileError& error) {
        context.check(error.code() == expected,
                      message + "（错误码不正确）");
    } catch (const std::exception& error) {
        context.check(false,
                      message + "（异常类型不正确：" + error.what() +
                          "）");
    }
}

template <typename Action>
void expectPersistenceError(TestContext& context,
                            PersistenceErrorCode expected,
                            Action action,
                            const std::string& message) {
    try {
        action();
        context.check(false, message + "（未抛出异常）");
    } catch (const PersistenceError& error) {
        context.check(error.code() == expected,
                      message + "（错误码不正确）");
    } catch (const std::exception& error) {
        context.check(false,
                      message + "（异常类型不正确：" + error.what() +
                          "）");
    }
}

void writeBytes(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("无法创建测试文件");
    }
    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    output.close();
    if (!output) {
        throw std::runtime_error("无法完整写入测试文件");
    }
}

void writeText(const std::filesystem::path& path,
               const std::string& text) {
    writeBytes(path,
               Bytes(reinterpret_cast<const std::uint8_t*>(text.data()),
                     reinterpret_cast<const std::uint8_t*>(text.data()) +
                         text.size()));
}

Bytes readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        throw std::runtime_error("无法打开测试文件");
    }
    const auto end = input.tellg();
    if (end < std::streampos(0)) {
        throw std::runtime_error("无法读取测试文件长度");
    }
    Bytes result(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!result.empty()) {
        input.read(reinterpret_cast<char*>(result.data()),
                   static_cast<std::streamsize>(result.size()));
        if (!input) {
            throw std::runtime_error("无法完整读取测试文件");
        }
    }
    return result;
}

std::uint32_t readU32(const Bytes& bytes, std::size_t offset) {
    std::uint32_t result{};
    for (unsigned index = 0; index < 4U; ++index) {
        result |= static_cast<std::uint32_t>(bytes.at(offset + index))
                  << (index * 8U);
    }
    return result;
}

std::uint64_t readU64(const Bytes& bytes, std::size_t offset) {
    std::uint64_t result{};
    for (unsigned index = 0; index < 8U; ++index) {
        result |= static_cast<std::uint64_t>(bytes.at(offset + index))
                  << (index * 8U);
    }
    return result;
}

void writeU32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned index = 0; index < 4U; ++index) {
        bytes.at(offset + index) = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xffU);
    }
}

void writeU64(Bytes& bytes, std::size_t offset, std::uint64_t value) {
    for (unsigned index = 0; index < 8U; ++index) {
        bytes.at(offset + index) = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xffU);
    }
}

void appendU32(Bytes& bytes, std::uint32_t value) {
    const auto offset = bytes.size();
    bytes.resize(offset + 4U);
    writeU32(bytes, offset, value);
}

void appendU64(Bytes& bytes, std::uint64_t value) {
    const auto offset = bytes.size();
    bytes.resize(offset + 8U);
    writeU64(bytes, offset, value);
}

bool equalPerson(const PersonSnapshot& first,
                 const PersonSnapshot& second) {
    return first.id == second.id &&
           first.canonicalName == second.canonicalName &&
           first.chapterCount == second.chapterCount;
}

bool equalEdge(const EdgeSnapshot& first, const EdgeSnapshot& second) {
    return first.id == second.id && first.endpointA == second.endpointA &&
           first.endpointB == second.endpointB &&
           first.coChapterCount == second.coChapterCount &&
           first.jaccard == second.jaccard;
}

bool equalSnapshots(const ProjectSnapshot& first,
                    const ProjectSnapshot& second) {
    if (first.nextPersonId != second.nextPersonId ||
        first.nextEdgeId != second.nextEdgeId ||
        first.nextChapterId != second.nextChapterId ||
        first.persons.size() != second.persons.size() ||
        first.edges.size() != second.edges.size() ||
        first.aliases != second.aliases ||
        first.chapters.size() != second.chapters.size()) {
        return false;
    }
    for (std::size_t index = 0; index < first.persons.size(); ++index) {
        if (!equalPerson(first.persons[index], second.persons[index])) {
            return false;
        }
    }
    for (std::size_t index = 0; index < first.edges.size(); ++index) {
        if (!equalEdge(first.edges[index], second.edges[index])) {
            return false;
        }
    }
    for (std::size_t index = 0; index < first.chapters.size(); ++index) {
        const auto& left = first.chapters[index];
        const auto& right = second.chapters[index];
        if (left.id != right.id || left.chapterKey != right.chapterKey ||
            left.title != right.title ||
            left.sourceFileName != right.sourceFileName ||
            left.contentUtf8 != right.contentUtf8 ||
            left.persons != right.persons) {
            return false;
        }
    }
    return true;
}

NovelRelationProject makePersistedProject() {
    NovelRelationProject project;
    const auto monkey = project.addPerson("孙悟空");
    const auto master = project.addPerson("唐僧");
    const auto pig = project.addPerson("猪八戒");
    const auto removed = project.addPerson("临时人物");
    project.removePerson(removed);
    project.addAlias("悟空", monkey);

    project.addChapter(ChapterDraft{"001",
                                    "第一章 初入江湖",
                                    "第一章.txt",
                                    "孙悟空遇见唐僧。",
                                    {monkey, master, monkey}});
    project.addChapter(ChapterDraft{"002",
                                    "第二章 长正文",
                                    "第二章.txt",
                                    std::string(1024U * 1024U, 'x'),
                                    {monkey, pig}});
    const auto removedChapter = project.addChapter(
        ChapterDraft{"003",
                     "第三章 临时",
                     "第三章.txt",
                     "唐僧遇见猪八戒。",
                     {master, pig}});
    project.removeChapter(removedChapter);
    return project;
}

Bytes addUnknownSection(const Bytes& original) {
    constexpr std::size_t headerSize = 32U;
    const auto oldPayloadLength = readU64(original, 16U);
    const auto oldSectionCount = readU32(original, 24U);
    const auto oldPayloadEnd =
        headerSize + static_cast<std::size_t>(oldPayloadLength);

    Bytes result(original.begin(),
                 original.begin() +
                     static_cast<Bytes::difference_type>(oldPayloadEnd));
    appendU32(result, 0xf00d1234U);
    appendU32(result, 99U);
    appendU64(result, 3U);
    result.push_back(0x11U);
    result.push_back(0x22U);
    result.push_back(0x33U);

    const auto newPayloadLength = result.size() - headerSize;
    writeU64(result, 16U, newPayloadLength);
    writeU32(result, 24U, oldSectionCount + 1U);
    const auto digest =
        Sha256::digest(result.data() + headerSize, newPayloadLength);
    result.insert(result.end(), digest.begin(), digest.end());
    return result;
}

void refreshPayloadDigest(Bytes& file) {
    constexpr std::size_t headerSize = 32U;
    const auto payloadLength = readU64(file, 16U);
    const auto digestOffset =
        headerSize + static_cast<std::size_t>(payloadLength);
    const auto digest =
        Sha256::digest(file.data() + headerSize,
                       static_cast<std::size_t>(payloadLength));
    for (std::size_t index = 0; index < digest.size(); ++index) {
        file.at(digestOffset + index) = digest[index];
    }
}

void testSha256(TestContext& context) {
    const Bytes empty;
    context.check(
        Sha256::toHex(Sha256::digest(empty)) ==
            "e3b0c44298fc1c149afbf4c8996fb924"
            "27ae41e4649b934ca495991b7852b855",
        "SHA-256 空串向量应正确");

    const std::string abc = "abc";
    const Bytes abcBytes(abc.begin(), abc.end());
    context.check(
        Sha256::toHex(Sha256::digest(abcBytes)) ==
            "ba7816bf8f01cfea414140de5dae2223"
            "b00361a396177a9cb410ff61f20015ad",
        "SHA-256 abc 向量应正确");
}

void testUtf8AndTextParsing(TestContext& context,
                            const TemporaryDirectory& temporary) {
    const auto utf8Path = temporary.file("含中文 UTF-8.txt");
    Bytes bomText{0xefU, 0xbbU, 0xbfU};
    const std::string chinese = "孙悟空与唐僧";
    bomText.insert(bomText.end(), chinese.begin(), chinese.end());
    writeBytes(utf8Path, bomText);
    context.check(Utf8TextFileLoader::load(utf8Path) == chinese,
                  "UTF-8 loader 应剥离 BOM 并保留中文");
    context.check(novel::isValidUtf8("中文🙂"),
                  "UTF-8 校验应接受合法多字节字符");
    context.check(!novel::isValidUtf8(std::string("\xed\xa0\x80", 3U)) &&
                      !novel::isValidUtf8(
                          std::string("\xf4\x90\x80\x80", 4U)) &&
                      !novel::isValidUtf8(std::string("\xe2\x82", 2U)),
                  "UTF-8 校验应拒绝代理项、越界码点和截断序列");

    expectTextError(
        context,
        TextFileErrorCode::FileTooLarge,
        [&]() { static_cast<void>(Utf8TextFileLoader::load(utf8Path, 3U)); },
        "文本读取应在分配前拒绝超限文件");

    const auto invalidPath = temporary.file("invalid-utf8.txt");
    writeBytes(invalidPath, Bytes{0xc0U, 0xafU});
    expectTextError(
        context,
        TextFileErrorCode::InvalidUtf8,
        [&]() { static_cast<void>(Utf8TextFileLoader::load(invalidPath)); },
        "文本读取应拒绝 overlong UTF-8");

    const auto parsed = novel::ChapterTextParser::parse(
        "@chapter=001\r\n@title=第一章\r\n \t\r\n正文一\r\n正文二");
    context.check(parsed.chapterKey == "001" &&
                      parsed.title == "第一章" &&
                      parsed.contentUtf8 == "正文一\n正文二",
                  "章节解析应处理 CRLF、空白分隔行和正文");
    const auto missingKey =
        novel::ChapterTextParser::parse("@title=待补编号\n\n正文");
    context.check(missingKey.chapterKey.empty() &&
                      missingKey.contentUtf8 == "正文",
                  "章节解析应允许缺少章节编号供界面补录");
    expectTextError(
        context,
        TextFileErrorCode::DuplicateMetadata,
        []() {
            static_cast<void>(novel::ChapterTextParser::parse(
                "@chapter=1\n@chapter=2\n\n正文"));
        },
        "章节解析应拒绝重复元数据");
    expectTextError(
        context,
        TextFileErrorCode::MetadataOutOfPlace,
        []() {
            static_cast<void>(novel::ChapterTextParser::parse(
                "@chapter=1\n\n正文\n@title=错误位置"));
        },
        "章节解析应拒绝正文后的元数据");
    expectTextError(
        context,
        TextFileErrorCode::InvalidMetadata,
        []() {
            static_cast<void>(
                novel::ChapterTextParser::parse("@unknown=value\n正文"));
        },
        "章节解析应拒绝文件头未知元数据");

    const auto people = DictionaryTextParser::parsePersons(
        " # 注释\r\n 孙悟空 \r\n唐僧\r\n\r\n");
    context.check(people == std::vector<std::string>({"孙悟空", "唐僧"}),
                  "标准人物词典应忽略注释和空行并裁剪空白");
    expectTextError(
        context,
        TextFileErrorCode::DuplicatePersonName,
        []() {
            static_cast<void>(
                DictionaryTextParser::parsePersons("孙悟空\n孙悟空\n"));
        },
        "标准人物词典应拒绝重复名称");

    std::string tooManyPeople;
    for (std::size_t index = 0;
         index <= DictionaryTextParser::kMaximumPersonCount;
         ++index) {
        tooManyPeople += "人物" + std::to_string(index) + "\n";
    }
    expectTextError(
        context,
        TextFileErrorCode::LimitExceeded,
        [&]() {
            static_cast<void>(
                DictionaryTextParser::parsePersons(tooManyPeople));
        },
        "标准人物词典应限制记录数量");

    const auto aliases = DictionaryTextParser::parseAliases(
        "# 注释\n\n悟空\t孙悟空\n三藏\t唐僧\n", people);
    context.check(aliases.size() == 2U && aliases[0].alias == "悟空" &&
                      aliases[1].canonicalName == "唐僧",
                  "别名词典应解析 TAB 记录并允许空行注释");
    expectTextError(
        context,
        TextFileErrorCode::UnknownCanonicalName,
        [&]() {
            static_cast<void>(DictionaryTextParser::parseAliases(
                "八戒\t猪八戒", people));
        },
        "别名词典应拒绝未知标准人物目标");

    const auto chapterFile = temporary.file("chapter-001.txt");
    writeText(chapterFile,
              "@chapter=001\n@title=文件章节\n\n孙悟空拜见唐僧。");
    const auto fromFile = novel::ChapterTextParser::parseFile(chapterFile);
    context.check(fromFile.chapterKey == "001" &&
                      fromFile.contentUtf8 == "孙悟空拜见唐僧。",
                  "章节 parser 应能组合 UTF-8 文件读取");
}

void testDictionaryExtraction(TestContext& context) {
    NovelRelationProject project;
    const auto shortName = project.addPerson("大圣");
    const auto longName = project.addPerson("齐天大圣");
    project.addAlias("悟空", longName);
    DictionaryNameExtractor extractor(project);

    context.check(extractor.extract("齐天大圣") ==
                      std::vector<novel::PersonId>({longName}),
                  "较长名称应屏蔽其内部的较短人物名");
    context.check(extractor.extract("悟空见到大圣，悟空离开") ==
                      std::vector<novel::PersonId>({shortName, longName}),
                  "名称提取应统一别名并按人物编号去重排序");
    expectTextError(
        context,
        TextFileErrorCode::InvalidUtf8,
        [&]() {
            const std::string invalid("\xc0\xaf", 2U);
            static_cast<void>(extractor.extract(invalid));
        },
        "名称提取应拒绝非法 UTF-8 正文");
}

void testPersistenceRoundTrip(TestContext& context,
                              const TemporaryDirectory& temporary) {
    BinaryProjectSerializer serializer;
    NovelRelationProject project = makePersistedProject();
    const auto expected = project.snapshot();
    context.check(expected.nextPersonId == 5U &&
                      expected.nextChapterId == 4U &&
                      expected.nextEdgeId == 4U,
                  "测试项目应包含人物、章节和边编号高水位空洞");

    const auto path = temporary.file("完整项目 中文.nprg");
    serializer.save(project, path);
    auto loaded = serializer.load(path);
    context.check(equalSnapshots(expected, loaded.snapshot()),
                  "二进制保存加载应完整恢复项目与 next-ID 高水位");
    context.check(loaded.validate().isValid(),
                  "加载后项目应通过领域完整验证");

    const auto deterministicPath = temporary.file("deterministic.nprg");
    serializer.save(loaded, deterministicPath);
    context.check(readBytes(path) == readBytes(deterministicPath),
                  "相同项目的二进制输出应确定且逐字节相同");

    const auto newPerson = loaded.addPerson("沙僧");
    const auto newChapter = loaded.addChapter(
        ChapterDraft{"004",
                     "第四章",
                     "第四章.txt",
                     "孙悟空遇见沙僧。",
                     {1U, newPerson}});
    const auto* newEdge = loaded.graph().findEdge(1U, newPerson);
    context.check(newPerson == expected.nextPersonId &&
                      newChapter == expected.nextChapterId &&
                      newEdge != nullptr &&
                      newEdge->id == expected.nextEdgeId,
                  "加载后新人物、章节和关系不得复用已删除编号");

    NovelRelationProject empty;
    const auto emptyPath = temporary.file("empty.nprg");
    serializer.save(empty, emptyPath);
    context.check(equalSnapshots(empty.snapshot(),
                                 serializer.load(emptyPath).snapshot()),
                  "空项目应能保存并重新加载");

    const auto replacePath = temporary.file("atomic-replace.nprg");
    serializer.save(empty, replacePath);
    serializer.save(project, replacePath);
    context.check(equalSnapshots(expected,
                                 serializer.load(replacePath).snapshot()),
                  "原子覆盖应提交完整的新项目");
    const auto prefix = replacePath.filename().u8string() + ".";
    bool foundTemporary = false;
    for (const auto& entry :
         std::filesystem::directory_iterator(temporary.path())) {
        const auto name = entry.path().filename().u8string();
        if (name.compare(0, prefix.size(), prefix) == 0 &&
            (name.find(".tmp-") != std::string::npos ||
             name.find(".backup-") != std::string::npos)) {
            foundTemporary = true;
        }
    }
    context.check(!foundTemporary,
                  "成功原子保存后不应残留临时文件或备份");
}

void testPersistenceFailures(TestContext& context,
                             const TemporaryDirectory& temporary) {
    BinaryProjectSerializer serializer;
    NovelRelationProject project = makePersistedProject();
    const auto validPath = temporary.file("valid-for-corruption.nprg");
    serializer.save(project, validPath);
    const Bytes valid = readBytes(validPath);

    Bytes wrongMagic = valid;
    wrongMagic[0] ^= 0xffU;
    const auto wrongMagicPath = temporary.file("wrong-magic.nprg");
    writeBytes(wrongMagicPath, wrongMagic);
    expectPersistenceError(
        context,
        PersistenceErrorCode::InvalidMagic,
        [&]() { static_cast<void>(serializer.load(wrongMagicPath)); },
        "加载应拒绝错误魔数");

    Bytes wrongVersion = valid;
    writeU32(wrongVersion, 8U, 999U);
    const auto wrongVersionPath = temporary.file("wrong-version.nprg");
    writeBytes(wrongVersionPath, wrongVersion);
    expectPersistenceError(
        context,
        PersistenceErrorCode::UnsupportedVersion,
        [&]() { static_cast<void>(serializer.load(wrongVersionPath)); },
        "加载应拒绝不支持的文件版本");

    Bytes truncated = valid;
    truncated.pop_back();
    const auto truncatedPath = temporary.file("truncated.nprg");
    writeBytes(truncatedPath, truncated);
    expectPersistenceError(
        context,
        PersistenceErrorCode::TruncatedFile,
        [&]() { static_cast<void>(serializer.load(truncatedPath)); },
        "加载应拒绝被截断的项目文件");

    Bytes badDigest = valid;
    badDigest.back() ^= 0x01U;
    const auto badDigestPath = temporary.file("bad-digest.nprg");
    writeBytes(badDigestPath, badDigest);
    const auto currentBefore = project.snapshot();
    expectPersistenceError(
        context,
        PersistenceErrorCode::DigestMismatch,
        [&]() { static_cast<void>(serializer.load(badDigestPath)); },
        "加载应拒绝摘要错误的项目文件");
    context.check(equalSnapshots(currentBefore, project.snapshot()),
                  "失败的 load 不得改变当前项目");

    Bytes unsupportedSection = valid;
    writeU32(unsupportedSection, 36U, 2U);
    refreshPayloadDigest(unsupportedSection);
    const auto unsupportedSectionPath =
        temporary.file("unsupported-section.nprg");
    writeBytes(unsupportedSectionPath, unsupportedSection);
    expectPersistenceError(
        context,
        PersistenceErrorCode::UnsupportedVersion,
        [&]() {
            static_cast<void>(serializer.load(unsupportedSectionPath));
        },
        "摘要正确时加载仍应拒绝不支持的已知区段版本");

    Bytes invalidBusinessData = valid;
    writeU32(invalidBusinessData, 48U, 0U);
    refreshPayloadDigest(invalidBusinessData);
    const auto invalidBusinessPath =
        temporary.file("invalid-business-data.nprg");
    writeBytes(invalidBusinessPath, invalidBusinessData);
    expectPersistenceError(
        context,
        PersistenceErrorCode::InvalidProject,
        [&]() { static_cast<void>(serializer.load(invalidBusinessPath)); },
        "摘要正确时加载仍应执行项目领域完整验证");

    const auto unknownPath = temporary.file("unknown-section.nprg");
    writeBytes(unknownPath, addUnknownSection(valid));
    context.check(equalSnapshots(project.snapshot(),
                                 serializer.load(unknownPath).snapshot()),
                  "加载应按 SectionLength 安全跳过未知区段");

    const auto protectedPath = temporary.file("protected-old.nprg");
    serializer.save(project, protectedPath);
    const Bytes oldBytes = readBytes(protectedPath);
    NovelRelationProject invalid;
    invalid.addPerson("损坏项目");
    novel::GraphTestAccess::setNextPersonId(
        novel::ProjectTestAccess::graph(invalid), 0U);
    expectPersistenceError(
        context,
        PersistenceErrorCode::InvalidProject,
        [&]() { serializer.save(invalid, protectedPath); },
        "保存无效项目应在触碰旧文件前失败");
    context.check(readBytes(protectedPath) == oldBytes &&
                      serializer.load(protectedPath).validate().isValid(),
                  "保存失败时旧项目文件必须逐字节不变且仍可加载");

#ifdef _WIN32
    const HANDLE lockedFile = CreateFileW(protectedPath.c_str(),
                                          GENERIC_READ,
                                          0U,
                                          nullptr,
                                          OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL,
                                          nullptr);
    context.check(lockedFile != INVALID_HANDLE_VALUE,
                  "原子替换失败测试应能独占锁定旧项目文件");
    if (lockedFile != INVALID_HANDLE_VALUE) {
        NovelRelationProject replacement;
        replacement.addPerson("替换项目人物");
        expectPersistenceError(
            context,
            PersistenceErrorCode::AtomicReplaceFailed,
            [&]() { serializer.save(replacement, protectedPath); },
            "原子替换被系统拒绝时 save 应明确失败");
        CloseHandle(lockedFile);
        context.check(readBytes(protectedPath) == oldBytes &&
                          serializer.load(protectedPath).validate().isValid(),
                      "原子替换阶段失败后旧文件仍应逐字节不变且可加载");
    }
#endif
}

}  // namespace

int main() {
    TestContext context;
    TemporaryDirectory temporary;

    runCase(context, "SHA-256", [&]() { testSha256(context); });
    runCase(context, "UTF-8 与文本解析", [&]() {
        testUtf8AndTextParsing(context, temporary);
    });
    runCase(context, "人物名称提取", [&]() {
        testDictionaryExtraction(context);
    });
    runCase(context, "二进制往返与原子保存", [&]() {
        testPersistenceRoundTrip(context, temporary);
    });
    runCase(context, "损坏文件与失败安全", [&]() {
        testPersistenceFailures(context, temporary);
    });

    if (context.failures == 0) {
        std::cout << "阶段四文件系统测试全部通过。\n";
        return 0;
    }
    std::cerr << context.failures << " 项测试失败。\n";
    return 1;
}
