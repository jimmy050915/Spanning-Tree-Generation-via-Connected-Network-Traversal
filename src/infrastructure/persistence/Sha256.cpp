#include "infrastructure/persistence/Sha256.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace novel {

namespace {

constexpr std::array<std::uint32_t, 64> roundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

constexpr std::uint32_t rotateRight(std::uint32_t value,
                                    std::uint32_t count) noexcept {
    return (value >> count) | (value << (32U - count));
}

void processBlock(const std::uint8_t* block,
                  std::array<std::uint32_t, 8>& state) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
        const std::size_t offset = index * 4U;
        words[index] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                       (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
                       (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
                       static_cast<std::uint32_t>(block[offset + 3U]);
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
        const auto first = rotateRight(words[index - 15U], 7U) ^
                           rotateRight(words[index - 15U], 18U) ^
                           (words[index - 15U] >> 3U);
        const auto second = rotateRight(words[index - 2U], 17U) ^
                            rotateRight(words[index - 2U], 19U) ^
                            (words[index - 2U] >> 10U);
        words[index] = words[index - 16U] + first + words[index - 7U] +
                       second;
    }

    auto a = state[0];
    auto b = state[1];
    auto c = state[2];
    auto d = state[3];
    auto e = state[4];
    auto f = state[5];
    auto g = state[6];
    auto h = state[7];

    for (std::size_t index = 0; index < words.size(); ++index) {
        const auto sumOne = rotateRight(e, 6U) ^ rotateRight(e, 11U) ^
                            rotateRight(e, 25U);
        const auto choose = (e & f) ^ ((~e) & g);
        const auto temporaryOne =
            h + sumOne + choose + roundConstants[index] + words[index];
        const auto sumZero = rotateRight(a, 2U) ^ rotateRight(a, 13U) ^
                             rotateRight(a, 22U);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto temporaryTwo = sumZero + majority;

        h = g;
        g = f;
        f = e;
        e = d + temporaryOne;
        d = c;
        c = b;
        b = a;
        a = temporaryOne + temporaryTwo;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

}  // namespace

Sha256::Digest Sha256::digest(const std::uint8_t* data, std::size_t size) {
    std::array<std::uint32_t, 8> state{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

    const std::size_t fullBlockCount = size / 64U;
    for (std::size_t block = 0; block < fullBlockCount; ++block) {
        processBlock(data + block * 64U, state);
    }

    std::array<std::uint8_t, 128> tail{};
    const std::size_t remaining = size % 64U;
    if (remaining != 0U) {
        std::memcpy(tail.data(), data + fullBlockCount * 64U, remaining);
    }
    tail[remaining] = 0x80U;

    const std::size_t paddedSize = remaining < 56U ? 64U : 128U;
    const auto bitLength = static_cast<std::uint64_t>(size) * 8U;
    for (std::size_t byte = 0; byte < 8U; ++byte) {
        tail[paddedSize - 1U - byte] = static_cast<std::uint8_t>(
            (bitLength >> static_cast<unsigned>(byte * 8U)) & 0xffU);
    }
    processBlock(tail.data(), state);
    if (paddedSize == 128U) {
        processBlock(tail.data() + 64U, state);
    }

    Digest result{};
    for (std::size_t index = 0; index < state.size(); ++index) {
        result[index * 4U] =
            static_cast<std::uint8_t>((state[index] >> 24U) & 0xffU);
        result[index * 4U + 1U] =
            static_cast<std::uint8_t>((state[index] >> 16U) & 0xffU);
        result[index * 4U + 2U] =
            static_cast<std::uint8_t>((state[index] >> 8U) & 0xffU);
        result[index * 4U + 3U] =
            static_cast<std::uint8_t>(state[index] & 0xffU);
    }
    return result;
}

Sha256::Digest Sha256::digest(const std::vector<std::uint8_t>& data) {
    return digest(data.data(), data.size());
}

std::string Sha256::toHex(const Digest& digestValue) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : digestValue) {
        output << std::setw(2) << static_cast<unsigned>(byte);
    }
    return output.str();
}

}  // namespace novel
