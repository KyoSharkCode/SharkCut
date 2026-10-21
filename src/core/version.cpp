#include "core/version.h"

#include <charconv>

namespace sharkcut {

std::optional<Version> parseVersion(std::string_view text) {
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) {
        text.remove_prefix(1);
    }
    Version version{0, 0, 0};
    std::size_t part = 0;
    const char* p = text.data();
    const char* end = text.data() + text.size();
    while (part < version.size()) {
        int value = 0;
        const auto [next, error] = std::from_chars(p, end, value);
        if (error != std::errc{} || value < 0) {
            break;
        }
        version[part++] = value;
        p = next;
        if (p == end || *p != '.') {
            break;
        }
        ++p;
    }
    if (part < 2) {
        return std::nullopt; // al menos "mayor.menor"
    }
    return version;
}

bool isNewerVersion(std::string_view latest, std::string_view current) {
    const auto a = parseVersion(latest);
    const auto b = parseVersion(current);
    return a && b && *a > *b;
}

} // namespace sharkcut
