//SPDX-License-Identifier: MIT
//Author: Blayne Dennis

#include "codec/base64.hpp"

#include <array>
#include <cstdint>

namespace hce {

namespace {

constexpr char alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 0xFF = invalid
constexpr std::array<uint8_t, 256> decode_table = [] {
    std::array<uint8_t, 256> t{};
    t.fill(0xFF);
    for(uint8_t i = 0; i < 64; ++i) {
        t[static_cast<uint8_t>(alphabet[i])] = i;
    }
    return t;
}();

} // namespace

std::string base64::encode(std::span<const std::byte> data) {
    const size_t len = data.size();
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    while(i + 2 < len) {
        uint32_t n = (static_cast<uint8_t>(data[i]) << 16) |
                     (static_cast<uint8_t>(data[i + 1]) << 8) |
                      static_cast<uint8_t>(data[i + 2]);
        out.push_back(alphabet[(n >> 18) & 0x3F]);
        out.push_back(alphabet[(n >> 12) & 0x3F]);
        out.push_back(alphabet[(n >> 6) & 0x3F]);
        out.push_back(alphabet[n & 0x3F]);
        i += 3;
    }

    if(i < len) {
        uint32_t n = static_cast<uint8_t>(data[i]) << 16;
        if(i + 1 < len) {
            n |= static_cast<uint8_t>(data[i + 1]) << 8;
        }
        out.push_back(alphabet[(n >> 18) & 0x3F]);
        out.push_back(alphabet[(n >> 12) & 0x3F]);
        if(i + 1 < len) {
            out.push_back(alphabet[(n >> 6) & 0x3F]);
            out.push_back('=');
        } else {
            out.push_back('=');
            out.push_back('=');
        }
    }

    return out;
}

hce::expected<std::vector<std::byte>, std::error_code>
base64::decode(std::string_view encoded) {
    // strip trailing padding for length check; we still validate it
    size_t len = encoded.size();
    while(len > 0 && encoded[len - 1] == '=') {
        --len;
    }

    if(encoded.size() % 4 != 0) {
        return hce::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    std::vector<std::byte> out;
    out.reserve((len * 3) / 4);

    size_t i = 0;
    while(i + 3 < encoded.size()) {
        uint8_t a = decode_table[static_cast<uint8_t>(encoded[i])];
        uint8_t b = decode_table[static_cast<uint8_t>(encoded[i + 1])];
        uint8_t c = decode_table[static_cast<uint8_t>(encoded[i + 2])];
        uint8_t d = decode_table[static_cast<uint8_t>(encoded[i + 3])];

        if(a == 0xFF || b == 0xFF ||
           (encoded[i + 2] != '=' && c == 0xFF) ||
           (encoded[i + 3] != '=' && d == 0xFF)) {
            return hce::unexpected(std::make_error_code(std::errc::invalid_argument));
        }

        out.push_back(static_cast<std::byte>((a << 2) | (b >> 4)));
        if(encoded[i + 2] != '=') {
            out.push_back(static_cast<std::byte>(((b & 0x0F) << 4) | (c >> 2)));
        }
        if(encoded[i + 3] != '=') {
            out.push_back(static_cast<std::byte>(((c & 0x03) << 6) | d));
        }
        i += 4;
    }

    return out;
}

} // hce
