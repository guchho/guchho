#include "guchho/sourcemap.hpp"
#include "guchho/helpers.hpp"
#include "guchho/compiler.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>


namespace guchho::sourcemap {
    namespace {

    // The 64 characters used in Base64 VLQ encoding as defined by the source
    // map specification. Each character maps to a 6-bit value (0-63) used
    // during VLQ decoding.
    constexpr std::string_view kBase64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    // Returns the 6-bit index (0-63) for a given Base64 character byte, or
    // -1 if the byte is not a valid Base64 character. A precomputed lookup
    // table is built once on first call for O(1) resolution.
    //
    // Example:
    //   Base64Index('A') => 0
    //   Base64Index('a') => 26
    //   Base64Index('+') => 62
    //   Base64Index('/') => 63
    //   Base64Index('!') => -1
    int Base64Index(uint8_t b) {
        static const auto kTable = [] {
            std::array<int8_t, 256> table{};
            table.fill(-1);
            for (int i = 0; i < 64; i++) {
                table[static_cast<uint8_t>(kBase64Chars[static_cast<size_t>(i)])] = static_cast<int8_t>(i);
            }
            return table;
        }();
        return kTable[b];
    }

    // Encodes a single signed integer as a Base64 VLQ segment and appends
    // the resulting characters to "encoded". Negative values are encoded by
    // setting the LSB of the first 5-bit group. Each subsequent group
    // carries a continuation bit in bit 5.
    //
    // The source map spec defines VLQ as variable-length: values that fit
    // in 5 bits (range -15 to 15) produce a single character; larger values
    // produce two or more characters with the continuation bit set.
    //
    // Example:
    //   EncodeVLQ(buf, 0)   => buf += "A"
    //   EncodeVLQ(buf, 1)   => buf += "C"
    //   EncodeVLQ(buf, -1)  => buf += "D"
    //   EncodeVLQ(buf, 15)  => buf += "e"
    //   EncodeVLQ(buf, 16)  => buf += "gB"  (two characters)
    void EncodeVLQ(std::string& encoded, int value) {
        int vlq;
        if (value < 0) {
            vlq = ((-value) << 1) | 1;
        } else {
            vlq = value << 1;
        }

        if ((vlq >> 5) == 0) {
            int digit = vlq & 31;
            encoded += kBase64Chars[static_cast<size_t>(digit)];
            return;
        }

        for (;;) {
            int digit = vlq & 31;
            vlq >>= 5;

            if (vlq != 0) {
                digit |= 32;
            }

            encoded += kBase64Chars[static_cast<size_t>(digit)];

            if (vlq == 0) {
                break;
            }
        }
    }

    // Appends a single source-map mapping segment to "buffer". The segment
    // encodes deltas from "prev_state" to "current_state" as VLQ values.
    // When "omit_source" is true, only the generated-column delta is
    // emitted (used for subsequent mappings on the same generated line that
    // share the same source location).
    //
    // A comma separator is inserted before the segment when the preceding
    // byte in the buffer is not a semicolon, quote, or zero (which would
    // indicate the buffer is empty or already has a separator).
    //
    // Returns the byte offset within "buffer" where the name VLQ was
    // written (if a name was present), or an invalid Index32 otherwise.
    //
    // Example:
    //   prev_state = {gen_col=0, src=0, line=0, col=0}
    //   current_state = {gen_col=5, src=1, line=2, col=3, name="x"}
    //   => buffer appends: "IALAAM" (column delta, source delta, etc.)
    //   => returns Index32 pointing at the "M" (name delta offset)
    compiler::Index32 AppendMappingToBuffer(std::string& buffer, uint8_t last_byte,
        const SourceMapState& prev_state, const SourceMapState& current_state, bool omit_source) {
        if (last_byte != 0 && last_byte != ';' && last_byte != '"') {
            buffer += ',';
        }

        EncodeVLQ(buffer, current_state.generated_column - prev_state.generated_column);

        if (!omit_source) {
            EncodeVLQ(buffer, current_state.source_index - prev_state.source_index);
            EncodeVLQ(buffer, current_state.original_line - prev_state.original_line);
            EncodeVLQ(buffer, current_state.original_column - prev_state.original_column);
        }

        compiler::Index32 name_offset{};
        if (current_state.has_original_name) {
            name_offset = compiler::Index32::Make(static_cast<uint32_t>(buffer.size()));
            EncodeVLQ(buffer, current_state.original_name - prev_state.original_name);
        }

        return name_offset;
    }

    }

// Performs a binary search over the sorted "mappings" vector to find the
// mapping whose generated line/column most closely covers the given
// (line, column) position. The search finds the last mapping whose
// generated position is <= the query position on the same line.
//
// Returns a pointer to the matching Mapping, or nullptr when no mapping
// exists for the requested line.
//
// Example:
//   mappings = [
//     {gen_line=0, gen_col=0, ...},
//     {gen_line=0, gen_col=10, ...},
//     {gen_line=1, gen_col=0, ...},
//   ]
//   Find(0, 5)  => &mappings[0]  (covers col 0-9 on line 0)
//   Find(0, 15) => &mappings[1]  (covers col 10+ on line 0)
//   Find(0, 0)  => &mappings[0]  (exact match)
//   Find(2, 0)  => nullptr       (line 2 not in mappings)
const Mapping* SourceMapData::Find(int32_t line, int32_t column) const {
    const auto& mappings_ref = this->mappings;

    int count = static_cast<int>(mappings_ref.size());
    int index = 0;

    while (count > 0) {
        int step = count / 2;
        int i = index + step;
        const auto& mapping = mappings_ref[static_cast<size_t>(i)];
        if (mapping.generated_line < line ||
            (mapping.generated_line == line && mapping.generated_column <= column)) {
            index = i + 1;
            count -= step + 1;
        } else {
            count = step;
        }
    }

    if (index > 0) {
        const auto& mapping = mappings_ref[static_cast<size_t>(index - 1)];
        if (mapping.generated_line == line) {
            return &mapping;
        }
    }

    return nullptr;
}

// Decodes a single Base64 VLQ value from "encoded" starting at byte
// position "start". Returns the decoded signed integer and the byte
// position immediately past the last consumed character.
//
// The decoding reads 5-bit groups, using bit 5 as a continuation flag.
// Once all groups are collected, bit 0 is extracted as the sign bit and
// the remaining bits are right-shifted to recover the original value.
//
// Example:
//   DecodeVLQ("A", 0)  => {0, 1}     (A encodes 0)
//   DecodeVLQ("C", 0)  => {1, 1}     (C encodes 1)
//   DecodeVLQ("D", 0)  => {-1, 1}    (D encodes -1)
//   DecodeVLQ("gB", 0) => {16, 2}    (gB encodes 16, two chars)
//   DecodeVLQ("!", 0)  => {0, 0}     (invalid char, returns 0 and
//                                     does not advance)
std::pair<int, int> DecodeVLQ(std::string_view encoded, int start) {
    int shift = 0;
    int vlq = 0;

    for (;;) {
        int index = Base64Index(static_cast<uint8_t>(encoded[static_cast<size_t>(start)]));
        if (index < 0) {
            break;
        }

        vlq |= (index & 31) << shift;
        start++;
        shift += 5;

        if ((index & 32) == 0) {
            break;
        }
    }

    int value = vlq >> 1;
    if ((vlq & 1) != 0) {
        value = -value;
    }

    return {value, start};
}

// Decodes a single Base64 VLQ value from a UTF-16 character span. This
// variant is used when source map data arrives as UTF-16 (e.g. from
// JavaScript string APIs) and avoids an intermediate UTF-8 conversion.
//
// Returns a tuple of (decoded_value, characters_consumed, success). On
// failure the third element is false and the first two are zero.
//
// Example:
//   input = {'C'}
//   => {1, 1, true}
//
//   input = {'g', 'B'}
//   => {16, 2, true}
//
//   input = {'!'}
//   => {0, 0, false}
std::tuple<int32_t, int, bool> DecodeVLQUTF16(std::span<const char16_t> encoded) {
    int n = static_cast<int>(encoded.size());
    if (n == 0) {
        return {0, 0, false};
    }

    int current = 0;
    int shift = 0;
    int32_t vlq = 0;

    for (;;) {
        if (current >= n) {
            return {0, 0, false};
        }

        int index = Base64Index(static_cast<uint8_t>(encoded[static_cast<size_t>(current)]));
        if (index < 0) {
            return {0, 0, false};
        }

        vlq |= (index & 31) << shift;
        current++;
        shift += 5;

        if ((index & 32) == 0) {
            break;
        }
    }

    int32_t value = vlq >> 1;
    if ((vlq & 1) != 0) {
        value = -value;
    }

    return {value, current, true};
}

// Returns true when this offset precedes "b" in file order. Comparison
// is lexicographic on (lines, columns): first by line number, then by
// column within the same line.
//
// Example:
//   {lines=0, columns=5}.ComesBefore({lines=0, columns=10}) => true
//   {lines=0, columns=10}.ComesBefore({lines=1, columns=0})  => true
//   {lines=1, columns=0}.ComesBefore({lines=0, columns=10})  => false
bool LineColumnOffset::ComesBefore(LineColumnOffset b) const {
    return lines < b.lines || (lines == b.lines && columns < b.columns);
}

// Adds offset "b" to this offset. When "b" is on the same line
// (b.lines == 0), only the column count is accumulated. When "b"
// advances to a new line, the line count increases and the column
// resets to b.columns.
//
// Example:
//   {lines=0, columns=3}.Add({lines=0, columns=2}) => {lines=0, columns=5}
//   {lines=0, columns=3}.Add({lines=2, columns=1}) => {lines=2, columns=1}
void LineColumnOffset::Add(LineColumnOffset b) {
    if (b.lines == 0) {
        columns += b.columns;
    } else {
        lines += b.lines;
        columns = b.columns;
    }
}

// Advances this offset by scanning through "bytes" as WTF-8 encoded text.
// Each code point is decoded to determine its UTF-16 column width: BMP
// characters count as 1 column, supplementary characters (above U+FFFF)
// count as 2 columns. Line breaks (\r, \n, U+2028, U+2029) reset the
// column to 0 and increment the line counter. A \r\n pair is treated as
// a single line break.
//
// Example:
//   offset = {lines=0, columns=0}
//   offset.AdvanceBytes("abc\ndef")
//   => {lines=1, columns=3}
//
//   offset = {lines=0, columns=0}
//   offset.AdvanceBytes("a\r\nb")
//   => {lines=1, columns=1}
//
//   offset = {lines=0, columns=0}
//   offset.AdvanceBytes("a\uD83D\uDE00b")  (emoji, 4 UTF-8 bytes)
//   => {lines=0, columns=3}  (1 + 2 + 1)
void LineColumnOffset::AdvanceBytes(std::string_view bytes) {
    int cols = columns;
    size_t i = 0;

    while (i < bytes.size()) {
        auto [c, width] = helpers::DecodeWTF8Rune(bytes.substr(i));

        switch (c) {
        case '\r': case '\n': case 0x2028: case 0x2029:
            if (c == '\r' && i + static_cast<size_t>(width) < bytes.size() && bytes[i + static_cast<size_t>(width)] == '\n') {
                cols++;
                i += static_cast<size_t>(width);
                continue;
            }

            lines++;
            cols = 0;
            break;

        default:
            if (c <= 0xFFFF) {
                cols++;
            } else {
                cols += 2;
            }
            break;
        }

        i += static_cast<size_t>(width);
    }

    columns = cols;
}

// Identical to AdvanceBytes but accepts a std::string_view of text. The
// two methods exist separately to preserve overload resolution for callers
// that hold either byte buffers or decoded text.
//
// Example:
//   offset = {lines=0, columns=0}
//   offset.AdvanceString("line1\nline2\nline3")
//   => {lines=2, columns=5}
void LineColumnOffset::AdvanceString(std::string_view text) {
    int cols = columns;
    size_t i = 0;

    while (i < text.size()) {
        auto [c, width] = helpers::DecodeWTF8Rune(text.substr(i));

        switch (c) {
        case '\r': case '\n': case 0x2028: case 0x2029:
            if (c == '\r' && i + static_cast<size_t>(width) < text.size() && text[i + static_cast<size_t>(width)] == '\n') {
                cols++;
                i += static_cast<size_t>(width);
                continue;
            }

            lines++;
            cols = 0;
            break;

        default:
            if (c <= 0xFFFF) {
                cols++;
            } else {
                cols += 2;
            }
            break;
        }

        i += static_cast<size_t>(width);
    }

    columns = cols;
}

// Returns true when this pieces object carries any data that would
// contribute to a finalized source map. An empty prefix, empty mappings,
// and empty suffix all mean the chunk is effectively a no-op.
//
// Example:
//   SourceMapPieces{"", "", ""}.HasContent() => false
//   SourceMapPieces{"{", "AAAA", "}"}.HasContent() => true
bool SourceMapPieces::HasContent() const {
    return !prefix.empty() || !mappings.empty() || !suffix.empty();
}

// Assembles the final source-map text for this chunk by concatenating the
// prefix, remapped mappings, and suffix. When "shifts" contains exactly
// one entry (the identity shift), a fast path simply concatenates the
// three strings. Otherwise each VLQ-encoded mapping segment is
// re-encoded to account for column deltas introduced by the shifts.
//
// The shift list describes how earlier edits displaced columns on each
// line. The function walks the mappings in parallel with the shift list,
// emitting unchanged runs until a boundary is crossed, at which point
// the column delta is adjusted.
//
// Example:
//   pieces = SourceMapPieces{"{", "AAAA,CACE;;", "}"}
//   shifts = [Shift{before={0,0}, after={0,0}}]  (identity)
//   => "{AAAA,CACE;;}"
//
//   shifts = [Shift{before={0,0}, after={0,0}},
//             Shift{before={1,0}, after={1,5}}]  (inserted 5 cols on line 1)
//   => "{AAAA,CACE;;}" with column deltas adjusted at the boundary
std::string SourceMapPieces::Finalize(const std::vector<SourceMapShift>& shifts) const {
    // Optimized path when there is exactly one shift (the identity),
    // meaning no column adjustments are needed.
    if (shifts.size() == 1) {
        std::string result;
        result.reserve(prefix.size() + mappings.size() + suffix.size());
        result += prefix;
        result += mappings;
        result += suffix;
        return result;
    }

    size_t start_of_run = 0;
    size_t current = 0;
    LineColumnOffset generated{};
    int prev_shift_column_delta = 0;
    helpers::Joiner j{};
    size_t shift_index = 0;

    j.AddString(prefix);

    while (current < mappings.size()) {
        if (mappings[current] == ';') {
            generated.lines++;
            generated.columns = 0;
            prev_shift_column_delta = 0;
            current++;
            continue;
        }

        size_t potential_end_of_run = current;

        auto [generated_column_delta, next] = DecodeVLQ(mappings, static_cast<int>(current));
        generated.columns += generated_column_delta;
        current = static_cast<size_t>(next);

        size_t potential_start_of_run = current;

        // Skip over the remaining VLQ segments of this mapping
        // (source index, original line, original column, and
        // optionally the original name).
        if (current < mappings.size()) {
            auto [_, n1] = DecodeVLQ(mappings, static_cast<int>(current));
            current = static_cast<size_t>(n1);
            auto [__, n2] = DecodeVLQ(mappings, static_cast<int>(current));
            current = static_cast<size_t>(n2);
            auto [___, n3] = DecodeVLQ(mappings, static_cast<int>(current));
            current = static_cast<size_t>(n3);

            if (current < mappings.size()) {
                auto [____, n4] = DecodeVLQ(mappings, static_cast<int>(current));
                current = static_cast<size_t>(n4);
            }
        }

        if (current < mappings.size() && mappings[current] == ',') {
            current++;
        }

        // Advance through any shift boundaries that this mapping has
        // crossed, recording whether a boundary was crossed and which
        // shift applies.
        bool did_cross_boundary = false;
        while (shift_index + 1 < shifts.size() && shifts[shift_index + 1].before.ComesBefore(generated)) {
            shift_index++;
            did_cross_boundary = true;
        }

        if (!did_cross_boundary) {
            continue;
        }

        const auto& shift = shifts[shift_index];
        if (shift.after.lines != generated.lines) {
            continue;
        }

        // Emit the raw VLQ text from the start of the current run up to
        // the end of the previous mapping.
        j.AddString(std::string_view(mappings).substr(start_of_run, potential_end_of_run - start_of_run));

        if (shift.before.lines != shift.after.lines) {
            // Unexpected line change when shifting source maps
        }

        // Re-encode the column delta with the shift adjustment applied.
        int shift_column_delta = shift.after.columns - shift.before.columns;
        std::string encoded;
        EncodeVLQ(encoded, generated_column_delta + shift_column_delta - prev_shift_column_delta);
        j.AddString(encoded);
        prev_shift_column_delta = shift_column_delta;

        start_of_run = potential_start_of_run;
    }

    j.AddString(std::string_view(mappings).substr(start_of_run));
    j.AddString(suffix);

    return j.Done();
}

// Appends a chunk's encoded mappings to a Joiner, rewriting the VLQ
// deltas to be relative to the previous chunk's end state rather than
// the chunk's own start state. This is necessary because Guchho builds
// source maps incrementally: each chunk records deltas from its own
// local origin, but the final source map needs deltas that chain
// continuously across all chunks.
//
// The function handles several preliminary steps:
//   1. Emits leading semicolons to advance to the correct generated line.
//   2. Strips any leading semicolons from the buffer data.
//   3. Decodes the first mapping segment to obtain the generated-column
//      delta, source index, original line, and original column.
//   4. Rewrites the segment using AppendMappingToBuffer with the
//      accumulated previous state.
//   5. If the chunk carries a named mapping, the name offset is adjusted
//      to reflect the rewritten position.
//
// Example:
//   prev_end_state = {gen_line=0, gen_col=0, src=0, line=0, col=0}
//   start_state    = {gen_line=1, gen_col=0, src=0, line=0, col=0}
//   buffer.data    = "AAAA"  (VLQ: gen_col_delta=0, src_delta=0, ...)
//   => emits ";AAAA" (semicolon for line advance, then the mapping)
void AppendSourceMapChunk(helpers::Joiner& j, SourceMapState prev_end_state, SourceMapState start_state, const MappingsBuffer& buffer) {
    if (start_state.generated_line != 0) {
        j.AddString(std::string(static_cast<size_t>(start_state.generated_line), ';'));
        prev_end_state.generated_column = 0;
    }

    int semicolons = 0;
    while (buffer.data[static_cast<size_t>(semicolons)] == ';') {
        semicolons++;
    }

    if (semicolons > 0) {
        j.AddString(std::string_view(buffer.data).substr(0, static_cast<size_t>(semicolons)));
        prev_end_state.generated_column = 0;
        start_state.generated_column = 0;
    }

    int source_index = 0;
    int original_line = 0;
    int original_column = 0;
    bool omit_source = false;

    auto [generated_column, i] = DecodeVLQ(buffer.data, semicolons);
    if (i == static_cast<int>(buffer.data.size()) ||
        buffer.data[static_cast<size_t>(i)] == ',' ||
        buffer.data[static_cast<size_t>(i)] == ';') {
        omit_source = true;
    } else {
        auto [si, n1] = DecodeVLQ(buffer.data, i);
        source_index = si;
        i = n1;
        auto [ol, n2] = DecodeVLQ(buffer.data, i);
        original_line = ol;
        i = n2;
        auto [oc, n3] = DecodeVLQ(buffer.data, i);
        original_column = oc;
        i = n3;
    }

    start_state.generated_column += generated_column;
    start_state.source_index += source_index;
    start_state.original_line += original_line;
    start_state.original_column += original_column;
    prev_end_state.has_original_name = false;

    std::string rewritten;
    AppendMappingToBuffer(rewritten, j.LastByte(), prev_end_state, start_state, omit_source);
    j.AddString(rewritten);

    if (buffer.first_name_offset.IsValid()) {
        size_t before = buffer.first_name_offset.GetIndex();
        auto [original_name, after] = DecodeVLQ(buffer.data, static_cast<int>(before));
        original_name += start_state.original_name - prev_end_state.original_name;
        j.AddString(std::string_view(buffer.data).substr(static_cast<size_t>(i), before - static_cast<size_t>(i)));
        std::string name_encoded;
        EncodeVLQ(name_encoded, original_name);
        j.AddString(name_encoded);
        j.AddString(std::string_view(buffer.data).substr(static_cast<size_t>(after)));
        return;
    }

    j.AddString(std::string_view(buffer.data).substr(static_cast<size_t>(i)));
}

// Builds per-line offset tables for the given source "contents". Each
// entry records the byte offset where its line begins, and for lines
// containing non-ASCII characters, a mapping from byte-offset-within-line
// to column number (accounting for multi-byte characters).
//
// The "approximate_line_count" hint is used to pre-reserve the output
// vector, avoiding repeated reallocations for large files.
//
// These tables allow O(log n) lookup of (line, column) from a byte
// offset, which is essential for converting AST node positions (stored
// as byte offsets) into source-map-compatible line/column pairs.
//
// Example:
//   contents = "abc\n日本語\nxyz"
//   => line_offset_tables = [
//       {byte_offset=0, columns_for_non_ascii=[]},
//       {byte_offset=4, columns_for_non_ascii=[0,1,2,3], ...},
//       {byte_offset=13, columns_for_non_ascii=[]},
//     ]
std::vector<LineOffsetTable> GenerateLineOffsetTables(std::string_view contents, int32_t approximate_line_count) {
    std::vector<int32_t> columns_for_non_ascii;
    int32_t byte_offset_to_first_non_ascii = 0;
    int line_byte_offset = 0;
    int32_t column = 0;
    size_t i = 0;

    std::vector<LineOffsetTable> line_offset_tables;
    line_offset_tables.reserve(static_cast<size_t>(approximate_line_count));

    while (i < contents.size()) {
        auto [c, width] = helpers::DecodeWTF8Rune(contents.substr(i));

        if (column == 0) {
            line_byte_offset = static_cast<int>(i);
        }

        // Lazily initialize the per-byte column mapping when the first
        // non-ASCII character is encountered on this line, then record the
        // logical column of every byte consumed by the current character.
        // ASCII bytes map 1:1 to their column; each byte of a multi-byte
        // character maps to the same column as the first byte.
        bool is_line_break = c == '\r' || c == '\n' || c == 0x2028 || c == 0x2029;
        if (!is_line_break) {
            if (c > 0x7F && columns_for_non_ascii.empty()) {
                byte_offset_to_first_non_ascii = static_cast<int32_t>(static_cast<int>(i) - line_byte_offset);
                columns_for_non_ascii = std::vector<int32_t>{};
            }
            // Record the logical column of every byte consumed by the current
            // character. This runs for the first non-ASCII character too (it
            // starts the table, so the table is still empty at this point).
            if (c > 0x7F || !columns_for_non_ascii.empty()) {
                for (int b = 0; b < width; b++) {
                    columns_for_non_ascii.push_back(column);
                }
            }
        }

        switch (c) {
        case '\r': case '\n': case 0x2028: case 0x2029:
            if (c == '\r' && i + static_cast<size_t>(width) < contents.size() && contents[i + static_cast<size_t>(width)] == '\n') {
                column++;
                i += static_cast<size_t>(width);
                continue;
            }

            line_offset_tables.push_back(LineOffsetTable{
                .columns_for_non_ascii = std::move(columns_for_non_ascii),
                .byte_offset_to_first_non_ascii = byte_offset_to_first_non_ascii,
                .byte_offset_to_start_of_line = static_cast<int32_t>(line_byte_offset),
            });
            byte_offset_to_first_non_ascii = 0;
            columns_for_non_ascii = {};
            column = 0;
            break;

        default:
            if (c <= 0xFFFF) {
                column++;
            } else {
                column += 2;
            }
            break;
        }

        i += static_cast<size_t>(width);
    }

    if (column == 0) {
        line_byte_offset = static_cast<int>(contents.size());
    }

    line_offset_tables.push_back(LineOffsetTable{
        .columns_for_non_ascii = std::move(columns_for_non_ascii),
        .byte_offset_to_first_non_ascii = byte_offset_to_first_non_ascii,
        .byte_offset_to_start_of_line = static_cast<int32_t>(line_byte_offset),
    });

    return line_offset_tables;
}

// Factory function that creates a ChunkBuilder pre-configured with the
// given input source map (or nullptr for a fresh mapping), pre-built
// line offset tables, and an ASCII-only flag that controls how names
// are quoted in the output.
//
// When "input_source_map" is non-null, AddSourceMapping will remap
// original positions through it before encoding. When null, positions
// are used as-is.
//
// When "ascii_only" is true, name strings are escaped to ensure all
// bytes are in the printable ASCII range.
//
// Example:
//   auto builder = MakeChunkBuilder(nullptr, tables, true);
//   builder.AddSourceMapping({.start=0}, "foo", "var foo = 1;");
//   auto chunk = builder.GenerateChunk("var foo = 1;");
//   chunk.buffer.data => "AAAA" (VLQ-encoded mapping)
ChunkBuilder MakeChunkBuilder(SourceMapData* input_source_map,
    const std::vector<LineOffsetTable>& line_offset_tables, bool ascii_only) {
    return ChunkBuilder{
        .input_source_map = input_source_map,
        .source_map = {},
        .quoted_names = {},
        .names_map = {},
        .line_offset_tables = line_offset_tables,
        .prev_original_name = {},
        .prev_state = {},
        .last_generated_update = 0,
        .generated_column = 0,
        .prev_generated_len = 0,
        .prev_original_loc = logger::Loc{.start = -1},
        .first_name_offset = {},
        .has_prev_state = false,
        .ascii_only = ascii_only,
        .line_starts_with_mapping = false,
        .cover_lines_without_mappings = input_source_map == nullptr,
    };
}

// Records a source mapping that associates an original source location
// with the current position in the generated output.
//
// Deduplication: if the original location has not changed since the last
// call and the output has not grown, the mapping is skipped. This avoids
// redundant mappings when the same original location spans multiple
// output chunks.
//
// When an input source map is present, the original position is first
// looked up via Find() to remap through the intermediate source map.
// If no mapping is found, the call is silently ignored.
//
// The function performs a binary search over line_offset_tables to
// convert the byte-offset-based original_loc into a (line, column) pair
// suitable for source-map encoding.
//
// Example:
//   builder = MakeChunkBuilder(nullptr, tables, false)
//   builder.AddSourceMapping({.start=5}, "x", "var x = 1;")
//   => records a mapping at the current generated_column for line/column
//     derived from byte offset 5
void ChunkBuilder::AddSourceMapping(logger::Loc original_loc, std::string_view original_name, const std::string& output) {
    if (original_loc.start == prev_original_loc.start &&
        (prev_generated_len == static_cast<int>(output.size()) || prev_original_name == original_name)) {
        return;
    }

    prev_original_loc = original_loc;
    prev_generated_len = static_cast<int>(output.size());
    prev_original_name = std::string(original_name);

    // Binary search to find which line contains the byte offset
    // stored in original_loc.start.
    const auto& lot = this->line_offset_tables;
    int count = static_cast<int>(lot.size());
    int original_line = 0;

    while (count > 0) {
        int step = count / 2;
        int i = original_line + step;

        if (lot[static_cast<size_t>(i)].byte_offset_to_start_of_line <= original_loc.start) {
            original_line = i + 1;
            count = count - step - 1;
        } else {
            count = step;
        }
    }

    original_line--;

    // Compute the column within the line by subtracting the line's byte
    // offset from the original_loc byte offset. If the line has a
    // non-ASCII column mapping, translate the raw byte column into the
    // logical column (where multi-byte characters count as 1 column).
    const auto& line = lot[static_cast<size_t>(original_line)];
    int original_column = static_cast<int>(original_loc.start - line.byte_offset_to_start_of_line);

    if (!line.columns_for_non_ascii.empty() && original_column >= static_cast<int>(line.byte_offset_to_first_non_ascii)) {
        size_t index = static_cast<size_t>(original_column - static_cast<int>(line.byte_offset_to_first_non_ascii));
        if (index < line.columns_for_non_ascii.size()) {
            original_column = line.columns_for_non_ascii[index];
        } else {
            original_column = line.columns_for_non_ascii.back();
        }
    }

    UpdateGeneratedLineAndColumn(output);

    // When cover_lines_without_mappings is enabled and we are mid-line
    // without a prior mapping, insert a mapping at column 0 so the
    // entire line is covered in the source map.
    if (cover_lines_without_mappings && !line_starts_with_mapping && generated_column > 0 && has_prev_state) {
        AppendMappingWithoutRemapping(SourceMapState{
            .generated_line = prev_state.generated_line,
            .generated_column = 0,
            .source_index = prev_state.source_index,
            .original_line = prev_state.original_line,
            .original_column = prev_state.original_column,
        });
    }

    AppendMapping(std::string(original_name), SourceMapState{
        .generated_line = prev_state.generated_line,
        .generated_column = generated_column,
        .original_line = original_line,
        .original_column = original_column,
    });

    line_starts_with_mapping = true;
}

// Finalizes the chunk builder and returns a Chunk containing the
// accumulated source-map data, quoted names, end state, and a flag
// indicating whether the chunk output is free of user-visible code
// (only semicolons, whitespace, and line terminators), meaning it can
// be ignored during finalization.
//
// The end_state reflects the generated line/column position after
// processing all output, which the caller uses as the prev_end_state
// when appending this chunk to the final source map.
//
// Example:
//   builder generated mappings for "var x = 1;\nvar y = 2;"
//   => Chunk{
//       buffer.data = "AAAA;AACA",
//       end_state = {gen_line=1, gen_col=10, ...},
//       should_ignore = false,
//     }
Chunk ChunkBuilder::GenerateChunk(const std::string& output) {
    UpdateGeneratedLineAndColumn(output);

    // A chunk can be ignored when it contains no user-visible code, i.e.
    // only semicolons, whitespace, and line terminators. Such chunks still
    // contribute semicolons for line boundaries but nothing meaningful.
    bool should_ignore = source_map.find_first_not_of(';') == std::string::npos;

    return Chunk{
        .buffer = MappingsBuffer{
            .data = source_map,
            .first_name_offset = first_name_offset,
        },
        .quoted_names = quoted_names,
        .end_state = prev_state,
        .final_generated_column = generated_column,
        .should_ignore = should_ignore,
    };
}

// Advances the generated line and column counters by scanning the
// output text from the last update position. Each line break (\r, \n,
// U+2028, U+2029) increments the generated line, resets the column to
// zero, and appends a semicolon to the source_map buffer. A \r\n pair
// is treated as a single line break.
//
// When cover_lines_without_mappings is enabled and the builder has not
// yet recorded a mapping for the current line, a zero-column mapping
// is automatically inserted at the line boundary to ensure every line
// has at least one source-map entry.
//
// Non-line-break characters advance the column counter by 1 for BMP
// characters or 2 for supplementary characters (above U+FFFF), matching
// the UTF-16 column width used by source maps.
//
// Example:
//   output = "abc\ndef\nghi"
//   => after processing: generated_line=2, generated_column=3,
//     source_map = ";;;AAAA;AACA;AACA"  (semicolons for line breaks)
void ChunkBuilder::UpdateGeneratedLineAndColumn(const std::string& output) {
    // The cursor may already be past the end of the supplied output when the
    // caller provides incremental fragments rather than the full accumulated
    // text. In that case there is nothing new to scan, so return early.
    if (static_cast<size_t>(last_generated_update) > output.size()) {
        return;
    }

    auto view = std::string_view(output).substr(static_cast<size_t>(last_generated_update));
    size_t i = 0;

    while (i < view.size()) {
        auto [c, width] = helpers::DecodeWTF8Rune(view.substr(i));

        switch (c) {
        case '\r': case '\n': case 0x2028: case 0x2029:
            if (c == '\r') {
                size_t newline_check = static_cast<size_t>(last_generated_update) + i + static_cast<size_t>(width);
                if (newline_check < output.size() && output[newline_check] == '\n') {
                    i += static_cast<size_t>(width);
                    continue;
                }
            }

            if (cover_lines_without_mappings && !line_starts_with_mapping && has_prev_state) {
                AppendMappingWithoutRemapping(SourceMapState{
                    .generated_line = prev_state.generated_line,
                    .generated_column = 0,
                    .source_index = prev_state.source_index,
                    .original_line = prev_state.original_line,
                    .original_column = prev_state.original_column,
                });
            }

            prev_state.generated_line++;
            prev_state.generated_column = 0;
            generated_column = 0;
            source_map += ';';
            line_starts_with_mapping = false;
            break;

        default:
            if (c <= 0xFFFF) {
                generated_column++;
            } else {
                generated_column += 2;
            }
            break;
        }

        i += static_cast<size_t>(width);
    }

    last_generated_update = static_cast<int>(output.size());
}

// Appends a mapping for the current state, optionally remapping through
// the input source map when one is present. When remapping, the
// original line/column from current_state are used to look up the
// corresponding entry in input_source_map, and the resulting
// source_index, original_line, original_column, and original_name
// fields are overwritten with the remapped values.
//
// If no remapping entry is found, the mapping is silently dropped.
//
// Name handling: when an original name is present, it is deduplicated
// through names_map. New names are appended to quoted_names (JSON-
// quoted) and their index is stored in the mapping's original_name
// field. The first name offset in the buffer is recorded for later
// adjustment during chunk finalization.
//
// Example:
//   input_source_map maps (line=5, col=10) => {src=0, line=2, col=3, name="foo"}
//   current_state = {gen_line=0, gen_col=0, original_line=5, original_column=10}
//   => mutable_state overwritten to {src=0, original_line=2, original_column=3}
//   => name "foo" added to quoted_names, mapping encoded with name index
void ChunkBuilder::AppendMapping(std::string_view original_name, const SourceMapState& current_state) {
    auto mutable_state = current_state;

    if (input_source_map != nullptr) {
        auto* mapping = input_source_map->Find(
            static_cast<int32_t>(mutable_state.original_line),
            static_cast<int32_t>(mutable_state.original_column));

        if (mapping == nullptr) {
            return;
        }

        mutable_state.source_index = static_cast<int>(mapping->source_index);
        mutable_state.original_line = static_cast<int>(mapping->original_line);
        mutable_state.original_column = static_cast<int>(mapping->original_column);

        if (mapping->original_name.IsValid()) {
            original_name = input_source_map->names[mapping->original_name.GetIndex()];
        }
    }

    if (!original_name.empty()) {
        auto it = names_map.find(std::string(original_name));
        if (it == names_map.end()) {
            uint32_t i = static_cast<uint32_t>(quoted_names.size());
            quoted_names.push_back(helpers::QuoteForJSON(original_name, ascii_only));
            names_map[std::string(original_name)] = i;
            mutable_state.original_name = static_cast<int>(i);
            mutable_state.has_original_name = true;
        } else {
            mutable_state.original_name = static_cast<int>(it->second);
            mutable_state.has_original_name = true;
        }
    }

    AppendMappingWithoutRemapping(mutable_state);
}

// Appends a mapping to the source_map buffer without any remapping. This
// is the low-level entry point used by both AppendMapping (after
// remapping) and the line-covering logic in UpdateGeneratedLineAndColumn.
//
// The function computes VLQ deltas between the previous state and the
// current state, appends them to the buffer via AppendMappingToBuffer,
// and updates prev_state. If the mapping carries no original name, the
// previous original_name is preserved across the state update so that
// subsequent mappings on the same line inherit the last-seen name.
//
// The first_name_offset is recorded when the first named mapping is
// appended, enabling the chunk finalizer to adjust the name offset
// after VLQ re-encoding.
//
// Example:
//   prev_state = {gen_col=0, src=0, line=0, col=0}
//   current_state = {gen_col=5, src=1, line=2, col=3, has_name=true}
//   => source_map appends "IALAAM"
//   => prev_state updated to current_state
//   => first_name_offset set to byte offset of the "M" character
void ChunkBuilder::AppendMappingWithoutRemapping(const SourceMapState& current_state) {
    uint8_t last_byte = 0;
    if (!source_map.empty()) {
        last_byte = static_cast<uint8_t>(source_map.back());
    }

    auto name_offset = AppendMappingToBuffer(source_map, last_byte, prev_state, current_state, false);
    int saved_original_name = prev_state.original_name;
    prev_state = current_state;

    if (!current_state.has_original_name) {
        prev_state.original_name = saved_original_name;
    } else if (!first_name_offset.IsValid()) {
        first_name_offset = name_offset;
    }

    has_prev_state = true;
}

}
