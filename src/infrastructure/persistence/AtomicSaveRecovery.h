#pragma once

#include <cstdint>
#include <filesystem>

namespace novel::persistence_detail {

// Internal recovery seam kept separate so the documented Windows
// ERROR_UNABLE_TO_MOVE_REPLACEMENT_2 post-condition can be tested without
// relying on the operating system to reproduce that rare failure on demand.
[[nodiscard]] bool recoverWindowsReplacementFailure(
    std::uint32_t systemError,
    const std::filesystem::path& target,
    const std::filesystem::path& backup);

}  // namespace novel::persistence_detail
