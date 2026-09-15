// Named HTML character reference decoding.
// ("src/core/entities/entities_table.cpp") separated from the decoder
// ("src/core/entities/entities.cpp").

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace guchho::entities {

struct EntityEntry {
    std::string_view name;
    std::array<uint32_t, 2> codepoints;
    size_t count;
};

// Generated table (entities_table.cpp), sorted by name byte-wise.
extern const size_t kEntityCount;
extern const size_t kMaxEntityNameLength;
extern const size_t kMaxEntityCodePoints;
extern const EntityEntry kEntities[];

// Longest-match decode of a named character reference.
// `input` is the text immediately following an '&' character. On success,
// fills `codepoints` with the replacement code points and returns the number
// of input units consumed (the entity name length). Returns 0 when no named
// reference matches at the start of `input`.
// When `in_attribute` is true, a match that does not end with ';' is rejected
// if it is followed by '=' or an alphanumeric character (historical quirk).
// `matched_semicolon` reports whether the matched name ended with ';'.
size_t DecodeNamedEntity(std::u16string_view input, bool in_attribute,
                         uint32_t (&codepoints)[2], size_t& cp_count,
                         bool& matched_semicolon);

}  // namespace guchho::entities