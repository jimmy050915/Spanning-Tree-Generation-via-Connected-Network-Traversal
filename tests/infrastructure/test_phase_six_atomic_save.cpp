#include "infrastructure/persistence/AtomicSaveRecovery.h"
#include "infrastructure/persistence/PersistenceError.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

struct TestContext {
    int failures{};

    void check(bool condition, const std::string& message) {
        if (!condition) {
            ++failures;
            std::cerr << "[失败] " << message << '\n';
        }
    }
};

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        path_ = std::filesystem::temp_directory_path() /
                ("novel-phase-six-atomic-" + std::to_string(stamp));
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

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("无法创建原子恢复测试文件");
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error("无法写入原子恢复测试文件");
    }
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("无法读取原子恢复测试文件");
    }
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

void testWindowsReplacementRecovery(TestContext& context,
                                    const TemporaryDirectory& temporary) {
#ifdef _WIN32
    constexpr std::uint32_t unableToMoveReplacement2 = 1177U;

    const auto target = temporary.file("target.nprg");
    const auto backup = temporary.file("target.nprg.backup");
    writeText(backup, "OLD PROJECT BYTES");

    const bool restored =
        novel::persistence_detail::recoverWindowsReplacementFailure(
            unableToMoveReplacement2, target, backup);
    context.check(restored,
                  "1177 后置状态应触发旧项目文件恢复");
    context.check(std::filesystem::exists(target) &&
                      readText(target) == "OLD PROJECT BYTES",
                  "恢复后目标路径应包含原项目完整内容");
    context.check(!std::filesystem::exists(backup),
                  "成功恢复应将备份原子移回目标路径");

    const auto untouchedBackup = temporary.file("untouched.backup");
    writeText(untouchedBackup, "UNCHANGED");
    const bool ignored =
        novel::persistence_detail::recoverWindowsReplacementFailure(
            1175U, temporary.file("untouched.nprg"), untouchedBackup);
    context.check(!ignored && std::filesystem::exists(untouchedBackup),
                  "非 1177 错误不得擅自移动备份文件");

    const auto retainedBackup = temporary.file("retained.backup");
    const auto unreachableTarget =
        temporary.path() / "missing-parent" / "target.nprg";
    writeText(retainedBackup, "RECOVERABLE OLD PROJECT");
    try {
        static_cast<void>(
            novel::persistence_detail::recoverWindowsReplacementFailure(
                unableToMoveReplacement2,
                unreachableTarget,
                retainedBackup));
        context.check(false, "恢复失败应返回明确的持久化错误");
    } catch (const novel::PersistenceError& error) {
        context.check(
            error.code() == novel::PersistenceErrorCode::AtomicReplaceFailed,
            "恢复失败应使用 AtomicReplaceFailed 错误码");
        context.check(
            std::string(error.what()).find(retainedBackup.u8string()) !=
                std::string::npos,
            "恢复失败信息应给出仍可人工恢复的备份路径");
    } catch (...) {
        context.check(false, "恢复失败抛出了错误的异常类型");
    }
    context.check(std::filesystem::exists(retainedBackup) &&
                      readText(retainedBackup) ==
                          "RECOVERABLE OLD PROJECT",
                  "自动恢复失败时必须保留原项目备份");
#else
    context.check(
        !novel::persistence_detail::recoverWindowsReplacementFailure(
            1177U, temporary.file("target.nprg"),
            temporary.file("target.backup")),
        "非 Windows 平台不应执行 Windows ReplaceFile 恢复逻辑");
#endif
}

}  // namespace

int main() {
    TestContext context;
    try {
        TemporaryDirectory temporary;
        testWindowsReplacementRecovery(context, temporary);
    } catch (const std::exception& error) {
        std::cerr << "[失败] 阶段六原子保存恢复测试异常："
                  << error.what() << '\n';
        return 1;
    }

    if (context.failures != 0) {
        std::cerr << "阶段六原子保存恢复测试失败数："
                  << context.failures << '\n';
        return 1;
    }
    std::cout << "阶段六原子保存恢复测试全部通过\n";
    return 0;
}
