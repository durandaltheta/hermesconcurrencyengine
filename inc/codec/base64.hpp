#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis

#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <cstddef>
#include <system_error>

#include "expected.hpp"

namespace hce {

/**
 @brief base64 encoder/decoder (RFC 4648, with padding)

 Standard alphabet. Invalid input produces an error_code.
 */
struct base64 {
    /**
     @brief encode binary data to a base64 string
     */
    static std::string encode(std::span<const std::byte> data);

    /**
     @brief decode a base64 string to binary data

     @return unexpected on invalid characters, bad padding, or truncated input
     */
    static hce::expected<std::vector<std::byte>, std::error_code>
    decode(std::string_view encoded);

    /**
     @brief encode a string's bytes to base64
     */
    template<
        class CharT = char,  
        class Traits = std::char_traits<CharT>,
        class Allocator = std::allocator<CharT>
    >
    static std::basic_string<CharT, Traits, Allocator>
    encode(const std::basic_string<CharT, Traits, Allocator>& s) {
        auto bytes = std::as_bytes(std::span(s.data(), s.size()));
        auto encoded = encode(bytes);
        return std::basic_string<CharT, Traits, Allocator>(
            encoded.begin(), encoded.end());
    }

    /**
     @brief decode base64 into a string of the given character type
     */
    template<
        class CharT = char,  
        class Traits = std::char_traits<CharT>,
        class Allocator = std::allocator<CharT>
    >
    static hce::expected<
        std::basic_string<CharT, Traits, Allocator>,
        std::error_code>
    decode_to(std::string_view encoded) {
        auto result = decode(encoded);
        if(!result) {
            return hce::unexpected(result.error());
        }
        const auto& bytes = result.value();
        return std::basic_string<CharT, Traits, Allocator>(
            reinterpret_cast<const CharT*>(bytes.data()),
            bytes.size());
    }
};

} // hce
