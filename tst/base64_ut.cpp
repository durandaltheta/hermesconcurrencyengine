//SPDX-License-Identifier: MIT
//Author: Blayne Dennis

#include <span>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "codec/base64.hpp"

namespace {

std::vector<std::byte> to_bytes(std::string_view s) {
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

std::string from_bytes(const std::vector<std::byte>& b) {
    return std::string(reinterpret_cast<const char*>(b.data()), b.size());
}

} // namespace

TEST(base64, empty) {
    EXPECT_EQ(hce::base64::encode({}), "");

    auto dec = hce::base64::decode("");
    ASSERT_TRUE(dec);
    EXPECT_TRUE(dec.value().empty());
}

TEST(base64, padding_cases) {
    // 1 byte -> 2 pad
    EXPECT_EQ(hce::base64::encode(to_bytes("f")), "Zg==");
    auto d1 = hce::base64::decode("Zg==");
    ASSERT_TRUE(d1);
    EXPECT_EQ(from_bytes(d1.value()), "f");

    // 2 bytes -> 1 pad
    EXPECT_EQ(hce::base64::encode(to_bytes("fo")), "Zm8=");
    auto d2 = hce::base64::decode("Zm8=");
    ASSERT_TRUE(d2);
    EXPECT_EQ(from_bytes(d2.value()), "fo");

    // 3 bytes -> no pad
    EXPECT_EQ(hce::base64::encode(to_bytes("foo")), "Zm9v");
    auto d3 = hce::base64::decode("Zm9v");
    ASSERT_TRUE(d3);
    EXPECT_EQ(from_bytes(d3.value()), "foo");
}

TEST(base64, known_vectors) {
    EXPECT_EQ(hce::base64::encode(to_bytes("Hello")), "SGVsbG8=");
    EXPECT_EQ(hce::base64::encode(to_bytes("Hello, World!")), "SGVsbG8sIFdvcmxkIQ==");
    EXPECT_EQ(hce::base64::encode(to_bytes("Man")), "TWFu");
    EXPECT_EQ(hce::base64::encode(to_bytes("any carnal pleasure")),
              "YW55IGNhcm5hbCBwbGVhc3VyZQ==");
}

TEST(base64, round_trip) {
    const std::string inputs[] = {
        "",
        "a",
        "ab",
        "abc",
        "Hello, World!",
        std::string(100, 'x'),
        std::string("\0\x01\xff", 3),
    };

    for(const auto& in : inputs) {
        auto encoded = hce::base64::encode(to_bytes(in));
        auto decoded = hce::base64::decode(encoded);
        ASSERT_TRUE(decoded) << "failed on input of size " << in.size();
        EXPECT_EQ(from_bytes(decoded.value()), in);
    }
}

TEST(base64, string_helpers) {
    const std::string original = "test string";

    auto encoded = hce::base64::encode(original);
    EXPECT_EQ(encoded, "dGVzdCBzdHJpbmc=");

    auto decoded = hce::base64::decode_to<char>(encoded);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded.value(), original);
}

TEST(base64, invalid_length) {
    EXPECT_FALSE(hce::base64::decode("Zg="));   // not multiple of 4
    EXPECT_FALSE(hce::base64::decode("Z"));
    EXPECT_FALSE(hce::base64::decode("Zg"));
}

TEST(base64, invalid_characters) {
    EXPECT_FALSE(hce::base64::decode("Zg=0"));  // invalid char
    EXPECT_FALSE(hce::base64::decode("@@@@"));
    EXPECT_FALSE(hce::base64::decode("Zg===")); // extra padding
}

TEST(base64, bad_padding) {
    EXPECT_FALSE(hce::base64::decode("Z==="));
    EXPECT_FALSE(hce::base64::decode("Zm=="));
    EXPECT_FALSE(hce::base64::decode("=Zg="));
}

TEST(base64, binary_round_trip) {
    std::vector<std::byte> data(256);
    for(size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::byte>(i);
    }

    auto encoded = hce::base64::encode(data);
    auto decoded = hce::base64::decode(encoded);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded.value(), data);
}
