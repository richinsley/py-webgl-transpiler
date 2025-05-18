#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <string_view>

namespace base64 {

static constexpr std::string_view base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

static inline bool is_base64(unsigned char c) {
    return (std::isalnum(c) || (c == '+') || (c == '/'));
}

inline std::string base64_encode(const unsigned char* buf, unsigned int bufLen) {
    std::string ret;
    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (bufLen--) {
        char_array_3[i++] = *(buf++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; ++i)
                ret += base64_chars[i];
            i = 0;
        }
    }

    if (i) {
        for (int j = i; j < 3; ++j)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (int j = 0; j < i + 1; ++j)
            ret += base64_chars[j];

        while (i++ < 3)
            ret += '=';
    }

    return ret;
}

inline std::string base64_encode(const std::string& s) {
    return base64_encode(reinterpret_cast<const unsigned char*>(s.data()), static_cast<unsigned int>(s.size()));
}

inline std::vector<unsigned char> decode(const std::string& encoded_string) {
    std::string str = encoded_string;
    str.erase(std::remove_if(str.begin(), str.end(), [](char c) {
        return !is_base64(static_cast<unsigned char>(c)) && c != '=';
    }), str.end());

    size_t in_len = str.size();
    size_t i = 0;
    size_t in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::vector<unsigned char> ret;

    while (in_len-- && (str[in_] != '=') && is_base64(static_cast<unsigned char>(str[in_]))) {
        char_array_4[i++] = str[in_];
        ++in_;
        if (i == 4) {
            for (i = 0; i < 4; ++i)
                char_array_4[i] = static_cast<unsigned char>(base64_chars.find(char_array_4[i]));

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; i < 3; ++i)
                ret.push_back(char_array_3[i]);
            i = 0;
        }
    }

    if (i) {
        for (size_t j = i; j < 4; ++j)
            char_array_4[j] = 0;

        for (size_t j = 0; j < 4; ++j)
            char_array_4[j] = static_cast<unsigned char>(base64_chars.find(char_array_4[j]));

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (size_t j = 0; j < i - 1; ++j)
            ret.push_back(char_array_3[j]);
    }

    return ret;
}

inline std::string base64_decode_to_string(const std::string& s) {
    auto bytes = decode(s);
    return std::string(bytes.begin(), bytes.end());
}

} // namespace base64
