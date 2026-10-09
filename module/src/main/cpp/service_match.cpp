#include "service_match.h"

#include <cstring>
#include <string>

namespace {
constexpr const char *kKeywords[] = {
    "lineage",
    "crdroid",
    "aospa",
    "pixelexperience",
    "omnirom",
    "protonaosp",
};

constexpr const char *kExactServices[] = {
    "profile",
};

char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

bool equals_ci(const std::string &value, const char *needle) {
    if (!needle) return false;
    size_t i = 0;
    for (; needle[i] != '\0'; ++i) {
        if (i >= value.size() || ascii_lower(value[i]) != needle[i]) return false;
    }
    return i == value.size();
}

bool contains_ci(const std::string &value, const char *needle) {
    if (!needle || *needle == '\0') return false;
    const size_t needle_size = std::char_traits<char>::length(needle);
    if (needle_size > value.size()) return false;
    for (size_t i = 0; i + needle_size <= value.size(); ++i) {
        size_t j = 0;
        for (; j < needle_size; ++j) {
            if (ascii_lower(value[i + j]) != needle[j]) break;
        }
        if (j == needle_size) return true;
    }
    return false;
}
} // namespace

bool hide_service(const std::string &service_name) {
    for (const char *service : kExactServices) {
        if (equals_ci(service_name, service)) return true;
    }

    for (const char *keyword : kKeywords) {
        if (contains_ci(service_name, keyword)) return true;
    }
    return false;
}

bool contains_rom_keyword(const std::string &value) {
    for (const char *keyword : kKeywords) {
        if (contains_ci(value, keyword)) return true;
    }
    return false;
}

std::size_t scrub_rom_keywords(char *buffer, std::size_t length) {
    if (!buffer || length == 0) return 0;
    std::size_t hits = 0;
    for (const char *keyword : kKeywords) {
        const std::size_t keyword_length = std::char_traits<char>::length(keyword);
        if (keyword_length == 0 || keyword_length > length) continue;
        for (std::size_t i = 0; i + keyword_length <= length; ++i) {
            std::size_t j = 0;
            for (; j < keyword_length; ++j) {
                if (ascii_lower(buffer[i + j]) != keyword[j]) break;
            }
            if (j == keyword_length) {
                std::memset(buffer + i, '_', keyword_length);
                ++hits;
                i += keyword_length - 1;
            }
        }
    }
    return hits;
}
