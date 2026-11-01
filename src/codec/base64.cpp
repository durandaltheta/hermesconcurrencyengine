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
    if(encoded.size() % 4 != 0) {
        return hce::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    std::vector<std::byte> out;
    out.reserve((encoded.size() / 4) * 3);

    size_t i = 0;
    while(i < encoded.size()) {
        const char c0 = encoded[i];
        const char c1 = encoded[i + 1];
        const char c2 = encoded[i + 2];
        const char c3 = encoded[i + 3];

        const uint8_t a = decode_table[static_cast<uint8_t>(c0)];
        const uint8_t b = decode_table[static_cast<uint8_t>(c1)];
        if(a == 0xFF || b == 0xFF) {
            return hce::unexpected(std::make_error_code(std::errc::invalid_argument));
        }

        // padding may only appear in the final quartet
        if(c2 == '=') {
            if(c3 != '=' || i + 4 != encoded.size()) {
                return hce::unexpected(std::make_error_code(std::errc::invalid_argument));
            }
            // two-pad: low 4 bits of second character must be zero
            if((b & 0x0F) != 0) {
                return hce::unexpected(std::make_error_code(std::errc::invalid_argument));
            }
        } else if(c3 == '=') {
            if(i + 4 != encoded.size()) {
                return hce::unexpected(std::make_error_code(std::errc::invalid_argument));
            }
        }

        const uint8_t c = (c2 == '=') ? 0 : decode_table[static_cast<uint8_t>(c2)];
        const uint8_t d = (c3 == '=') ? 0 : decode_table[static_cast<uint8_t>(c3)];

        if((c2 != '=' && c == 0xFF) || (c3 != '=' && d == 0xFF)) {
            return hce::unexpected(std::make_error_code(std::errc::invalid_argument));
        }

        // one-pad: low 2 bits of third character must be zero
        if(c3 == '=' && c2 != '=' && (c & 0x03) != 0) {
            return hce::unexpected(std::make_error_code(std::errc::invalid_argument));
        }

        out.push_back(static_cast<std::byte>((a << 2) | (b >> 4)));
        if(c2 != '=') {
            out.push_back(static_cast<std::byte>(((b & 0x0F) << 4) | (c >> 2)));
        }
        if(c3 != '=') {
            out.push_back(static_cast<std::byte>(((c & 0x03) << 6) | d));
        }

        i += 4;
    }

    return out;
}

} // hce
