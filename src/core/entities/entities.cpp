// Named character reference decoder. See "guchho/entities.hpp". Data lives in
// the generated "entities_table.cpp" (see tools/generate_entities.py); this
// file only implements the longest-match lookup against it.

#include "guchho/entities.hpp"

#include <algorithm>
#include <string>

namespace guchho::entities {

// ASCII-only alphanumeric test used by the attribute-value quirk below.
inline bool IsAsciiAlphanumeric(char16_t c) {
    return (c >= u'0' && c <= u'9') || (c >= u'A' && c <= u'Z') ||
           (c >= u'a' && c <= u'z');
}

size_t DecodeNamedEntity(std::u16string_view input, bool in_attribute,
                         uint32_t (&codepoints)[2], size_t& cp_count,
                         bool& matched_semicolon) {
    cp_count = 0;
    matched_semicolon = false;
    if (input.empty()) return 0;

    const size_t max_len = std::min(kMaxEntityNameLength, input.size());
    for (size_t len = max_len; len >= 1; --len) {
        // Entity names are pure ASCII; skip any candidate containing
        // non-ASCII units.
        std::string candidate;
        candidate.reserve(len);
        bool is_ascii = true;
        for (size_t i = 0; i < len; ++i) {
            if (input[i] >= 0x80) {
                is_ascii = false;
                break;
            }
            candidate.push_back(static_cast<char>(input[i]));
        }
        if (!is_ascii) continue;

        const auto* it = std::lower_bound(
            kEntities, kEntities + kEntityCount, candidate,
            [](const EntityEntry& e, const std::string& c) {
                return e.name < c;
            });
        if (it == kEntities + kEntityCount || it->name != candidate) continue;

        // Candidate matched. Apply the attribute-value historical quirk:
        // a semicolon-less match followed by '=' or alphanumeric is not
        // consumed.
        if (in_attribute && !candidate.ends_with(';') && len < input.size()) {
            const char16_t next = input[len];
            if (next == u'=' || IsAsciiAlphanumeric(next)) continue;
        }

        cp_count = it->count;
        codepoints[0] = it->codepoints[0];
        codepoints[1] = it->codepoints[1];
        matched_semicolon = candidate.ends_with(';');
        return len;
    }
    return 0;
}

}  // namespace guchho::entities