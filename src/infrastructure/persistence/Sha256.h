#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace novel {

class Sha256 final {
public:
    using Digest = std::array<std::uint8_t, 32>;

    static Digest digest(const std::uint8_t* data, std::size_t size);
    static Digest digest(const std::vector<std::uint8_t>& data);
    static std::string toHex(const Digest& digest);
};

}  // namespace novel
