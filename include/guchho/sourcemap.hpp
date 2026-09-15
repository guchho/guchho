#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "guchho/logger.hpp"
#include "guchho/compiler.hpp"

namespace guchho::helpers {
    class Joiner;
}

namespace guchho::sourcemap {

    // Represents a single point-to-point mapping between a location in the
    // generated output and the corresponding location in the original source.
    // Each mapping encodes which source file it refers to (source_index), the
    // exact line and column in both the generated and original files, and
    // optionally the name of the symbol at that original location.
    //
    // Example:
    //   Mapping{.generated_line = 0, .generated_column = 5,
    //           .source_index = 0, .original_line = 2, .original_column = 10}
    //   means "generated column 5 on line 0 came from original column 10 on
    //   line 2 of source file 0."
    struct Mapping {
        int32_t generated_line{};
        int32_t generated_column{};
        int32_t source_index{};
        int32_t original_line{};
        int32_t original_column{};
        compiler::Index32 original_name{};
    };

    // Holds the original source text for a single entry in the "sources" array
    // of a source map. The 'quoted' member stores the JSON-safe escaped form
    // suitable for embedding directly into the source map JSON output. The
    // 'value' member holds the decoded UTF-16 representation for internal use
    // by the source map builder when performing character-level column tracking.
    struct SourceContent {
        std::string quoted;
        std::u16string value;
    };

    // The fully parsed contents of a source map file, as produced by decoding
    // a JSON source map or by the source map builder during code generation.
    // 'sources' lists every original source file referenced by the map.
    // 'sources_content' optionally holds the full text of each source file.
    // 'mappings' contains all the point-to-point line/column correspondences.
    // 'names' lists every original symbol name referenced by the mappings.
    struct SourceMapData {
        std::vector<std::string> sources;
        std::vector<SourceContent> sources_content;
        std::vector<Mapping> mappings;
        std::vector<std::string> names;

        // Searches the mappings list for the entry whose generated line/column
        // best covers the requested position. Mappings are assumed to be sorted
        // by generated_line then generated_column in ascending order. The search
        // returns the last mapping whose generated position is <= the requested
        // position on the same line, or nullptr if no mapping exists for that
        // line or if the requested column precedes every mapping on that line.
        //
        // Example:
        //   If mappings contains {generated_line=0, generated_column=0} and
        //   {generated_line=0, generated_column=10}, then Find(0, 7) returns
        //   the first mapping (column 0) because 7 >= 0 and 7 < 10.
        //   Find(0, 15) returns the second mapping (column 10).
        //   Find(0, -1) returns nullptr because no mapping has column <= -1.
        //   Find(5, 0) returns nullptr if no mapping exists for line 5.
        const Mapping* Find(int32_t line, int32_t column) const;
    };

    // Tracks a position within a source file as a pair of line and column
    // numbers (both zero-based). This is used throughout the source map
    // machinery to represent offsets, shifts, and current positions without
    // having to convert back and forth between byte offsets and line/column
    // pairs constantly.
    struct LineColumnOffset {
        int lines{};
        int columns{};

        // Returns true when this offset precedes offset 'b' in the file. An
        // offset is considered to come before another if its line number is
        // strictly less, or if the lines are equal and its column is strictly
        // less. Equal offsets are not considered to come before one another.
        //
        // Example:
        //   LineColumnOffset{1, 5}.ComesBefore(LineColumnOffset{2, 0}) -> true
        //   LineColumnOffset{1, 5}.ComesBefore(LineColumnOffset{1, 3}) -> false
        //   LineColumnOffset{1, 5}.ComesBefore(LineColumnOffset{1, 5}) -> false
        bool ComesBefore(LineColumnOffset b) const;

        // Adds offset 'b' to this offset in place. Line numbers are summed
        // directly. Column arithmetic depends on whether 'b' spans multiple
        // lines: if b.lines > 0 then b.columns replaces this->columns
        // (since the cursor moved to a new line), otherwise b.columns are
        // added to this->columns.
        //
        // Example:
        //   {lines=1, columns=3}.Add({lines=0, columns=5}) -> {1, 8}
        //   {lines=1, columns=3}.Add({lines=2, columns=5}) -> {3, 5}
        void Add(LineColumnOffset b);

        // Advances this offset past the raw bytes in 'bytes'. Newline
        // characters (\n, \r, \r\n) reset the column to 0 and increment the
        // line counter. All other bytes increment the column by 1. The
        // implementation handles \r\n as a single line break (consuming both
        // characters).
        //
        // Example:
        //   offset = {0, 0}
        //   offset.AdvanceBytes("ab\ncd") -> offset becomes {1, 2}
        //   offset.AdvanceBytes("ef")     -> offset becomes {1, 4}
        void AdvanceBytes(std::string_view bytes);

        // Advances this offset past the text in 'text', counting each code
        // unit as one column unit. This is the primary method used when
        // tracking column positions through generated output that has already
        // been validated as ASCII-safe. Unlike AdvanceBytes, this method does
        // not inspect individual characters for newline detection; callers are
        // expected to pass only the portion of text that stays on the current
        // line.
        //
        // Example:
        //   offset = {0, 0}
        //   offset.AdvanceString("hello") -> offset becomes {0, 5}
        void AdvanceString(std::string_view text);
    };

    // Describes the spatial relationship between the original content and the
    // content after a chunk edit has been applied. 'before' is the offset in
    // the original file where the chunk was inserted/replaced, and 'after' is
    // the offset in the resulting file after the chunk's text has been
    // written. The difference between these two offsets tells the source map
    // finalizer how much to shift subsequent mappings.
    //
    // Example:
    //   If a chunk inserts 3 lines of output starting at original position
    //   {2, 0}, then before={2,0} and after={5,0} because 3 new lines were
    //   added. All mappings that originally pointed to line 2 or beyond must
    //   be shifted down by 3 lines.
    struct SourceMapShift {
        LineColumnOffset before;
        LineColumnOffset after;
    };

    // A decomposition of a source map's JSON output into three contiguous
    // text segments: the prefix (everything before the "mappings" string
    // value), the encoded VLQ mappings themselves, and the suffix (everything
    // after the mappings string value, including the closing quote and any
    // trailing JSON structure). This split allows the finalizer to splice
    // modified mappings back into the source map without re-serializing the
    // entire JSON document.
    struct SourceMapPieces {
        std::string prefix;
        std::string mappings;
        std::string suffix;

        // Returns true when the mappings segment is non-empty, indicating
        // that this chunk actually carries source map data that needs to be
        // incorporated into the final output.
        bool HasContent() const;

        // Reassembles the complete source map JSON string by applying the
        // given vector of source-map shifts to the stored VLQ mappings.
        // Each shift describes how much the generated line/column positions
        // have moved due to a chunk insertion or replacement. The shifts are
        // applied in order, and the resulting VLQ deltas are re-encoded to
        // reflect the corrected positions. The returned string is the full
        // source map ready for writing to disk.
        //
        // Example:
        //   If prefix = '{"version":3,"mappings":"', mappings = "AAAA",
        //   suffix = '"}', and shifts is empty, then Finalize returns
        //   '{"version":3,"mappings":"AAAA"}'.
        //   If shifts adds 2 to every generated line, the VLQ in 'mappings'
        //   is re-encoded accordingly.
        std::string Finalize(const std::vector<SourceMapShift>& shifts) const;
    };

    // Tracks the current cumulative position that the source map encoder
    // needs in order to compute VLQ-encoded deltas. Source maps encode
    // positions as deltas relative to the previous mapping, so this state
    // must be maintained across successive calls to AppendMapping. Every
    // field represents the absolute position that was last written; the next
    // mapping's delta is computed by subtracting these values from the new
    // mapping's absolute position.
    struct SourceMapState {
        int generated_line{};
        int generated_column{};
        int source_index{};
        int original_line{};
        int original_column{};
        int original_name{};
        bool has_original_name{};
    };

    // Precomputed per-line lookup table that converts a byte offset within a
    // source file into the column number a source map expects (which counts
    // code units, not bytes). For lines that contain no multi-byte UTF-8
    // sequences, the column equals the byte offset relative to the line start.
    // For lines with non-ASCII characters, 'columns_for_non_ascii' stores the
    // cumulative column count at each multi-byte character boundary so a
    // binary search can map a byte offset to the correct column in O(log n)
    // time. 'byte_offset_to_first_non_ascii' caches the byte position of the
    // first non-ASCII character on this line (or the line's byte length if
    // the line is pure ASCII) to allow a fast-path check.
    //
    // Example:
    //   For the line "café" (bytes: 63 61 66 C3 A9):
    //   columns_for_non_ascii = {3}  (column 3 is where the 2-byte é starts)
    //   byte_offset_to_first_non_ascii = 3
    //   byte_offset_to_start_of_line = 0
    //   Byte offset 1 -> column 1 ('a')
    //   Byte offset 3 -> column 3 (start of é)
    //   Byte offset 4 -> column 3 (second byte of é, same column)
    struct LineOffsetTable {
        std::vector<int32_t> columns_for_non_ascii;
        int32_t byte_offset_to_first_non_ascii{};
        int32_t byte_offset_to_start_of_line{};
    };

    // Holds the Base64-VLQ-encoded mapping data for a single generated output
    // chunk, along with the byte offset of the first name entry referenced by
    // any mapping in this chunk. The 'data' string is a sequence of VLQ
    // segments separated by ';' (one per generated line) and ',' (between
    // mappings on the same line). The 'first_name_offset' is used by the
    // joiner to adjust name indices when chunks are concatenated out of order.
    struct MappingsBuffer {
        std::string data;
        compiler::Index32 first_name_offset{};
    };

    // A single generated output chunk paired with all the source-map data
    // needed to incorporate it into the final source map. Each chunk knows its
    // own encoded VLQ mappings, the names it references, the cumulative
    // SourceMapState at its end position, the final generated column after the
    // chunk's text, and whether it should be ignored during finalization (for
    // example, chunks that contain only whitespace and no user-visible code).
    struct Chunk {
        MappingsBuffer buffer;
        std::vector<std::string> quoted_names;
        SourceMapState end_state{};
        int final_generated_column{};
        bool should_ignore{};
    };

    // Incrementally builds source-map mappings as Guchho emits output chunks.
    // The builder maintains internal state so that each call to
    // AddSourceMapping records the correct VLQ delta relative to the previous
    // mapping, optionally remapping the original location through an existing
    // input source map. When all mappings for a chunk have been recorded,
    // GenerateChunk produces a Chunk object containing the encoded VLQ data,
    // the referenced names, and the end-of-chunk state. The builder also
    // tracks per-line byte-to-column conversion tables so that multi-byte
    // UTF-8 characters are assigned the correct column numbers in the source
    // map.
    //
    // Example workflow:
    //   ChunkBuilder builder = MakeChunkBuilder(nullptr, tables, false);
    //   builder.AddSourceMapping({.line=0, .column=0}, "main", output_text);
    //   builder.UpdateGeneratedLineAndColumn(output_text);
    //   Chunk chunk = builder.GenerateChunk(output_text);
    //   // chunk.buffer.data now contains the VLQ-encoded mappings for this
    //   // chunk, and chunk.end_state reflects the position after output_text.
    struct ChunkBuilder {
        SourceMapData* input_source_map{};
        std::string source_map;
        std::vector<std::string> quoted_names;
        std::unordered_map<std::string, uint32_t> names_map;
        std::vector<LineOffsetTable> line_offset_tables;
        std::string prev_original_name;
        SourceMapState prev_state{};
        int last_generated_update{};
        int generated_column{};
        int prev_generated_len{};
        logger::Loc prev_original_loc{};
        compiler::Index32 first_name_offset{};
        bool has_prev_state{};
        bool ascii_only{};
        bool line_starts_with_mapping{};
        bool cover_lines_without_mappings{};

        // Records a source mapping that associates the given original source
        // location (original_loc) and symbol name (original_name) with the
        // current position in the generated output. If an input source map is
        // present, the original location is first remapped through it so that
        // the generated source map points to the true original source rather
        // than to an intermediate representation. The output string is used to
        // update the internal line/column counters.
        //
        // Example:
        //   builder.AddSourceMapping({.line=2, .column=0}, "foo",
        //                            "var x = 1;\n");
        //   This records that the output text "var x = 1;\n" originated from
        //   line 2, column 0 of the original source, and the symbol name was
        //   "foo". The builder's internal generated column advances by the
        //   length of the output string.
        void AddSourceMapping(logger::Loc original_loc, std::string_view original_name, const std::string& output);

        // Finalizes all mappings recorded since the last call to
        // GenerateChunk and returns a Chunk object. The returned Chunk
        // contains the VLQ-encoded mappings (buffer), the list of referenced
        // symbol names (quoted_names), the cumulative SourceMapState after the
        // chunk (end_state), and the final generated column position. After
        // this call, the builder's internal state is reset for the next chunk,
        // except for the running generated-column counter which continues
        // incrementing across chunks.
        //
        // Example:
        //   Chunk chunk = builder.GenerateChunk("console.log('hi');");
        //   // chunk.buffer.data might be "AAAA;ACAA" (two VLQ segments)
        //   // chunk.end_state.generated_column == 21 (length of output)
        Chunk GenerateChunk(const std::string& output);

        // Scans the output string character by character and advances the
        // builder's internal generated_line and generated_column counters to
        // account for the text that was written. Newline characters (\n, \r,
        // \r\n) reset the column to 0 and increment the line. This method
        // must be called after each chunk of output is emitted so that the
        // next AddSourceMapping call starts from the correct generated
        // position.
        //
        // Example:
        //   builder.generated_column = 0;
        //   builder.UpdateGeneratedLineAndColumn("line1\nline2\n");
        //   // generated_column becomes 0, generated_line becomes 2
        void UpdateGeneratedLineAndColumn(const std::string& output);

        // Appends a VLQ-encoded mapping segment for the current state,
        // computing the delta from prev_state and encoding it into the
        // internal VLQ buffer. If an input source map is present and the
        // original location resolves to a mapping in it, the original
        // line/column/source_index are taken from that remapped location
        // rather than from the raw original_loc. The original_name is
        // appended to the names list if it has not been seen before, and its
        // index is encoded into the VLQ segment.
        //
        // Example:
        //   If prev_state = {generated_line=0, generated_column=0} and the
        //   new mapping targets generated_column=5 with source_index=0,
        //   original_line=1, original_column=3, name="x", then the VLQ
        //   delta "EAAAA" (5, 0, 1, 3, 0) is appended.
        void AppendMapping(std::string_view original_name, const SourceMapState& current_state);

        // Appends a VLQ-encoded mapping segment for the current state
        // without performing any input-source-map remapping. This is used
        // when the caller has already resolved the correct original location
        // and wants to record it directly. The delta from prev_state is
        // computed and encoded identically to AppendMapping, but the source
        // index, original line, original column, and name index are taken
        // verbatim from current_state.
        //
        // Example:
        //   If current_state = {generated_line=0, generated_column=0,
        //                       source_index=0, original_line=5,
        //                       original_column=2}, then the VLQ segment
        //   "AAAAA" (all zeros) is appended and prev_state is updated to
        //   match current_state.
        void AppendMappingWithoutRemapping(const SourceMapState& current_state);
    };

    // Decodes a single Base64-VLQ (Variable-Length Quantity) value from the
    // encoded string starting at position 'start'. VLQ encoding uses 6 bits
    // per digit (stored as Base64 characters), with the high bit indicating
    // whether more digits follow. The decoded value is sign-extended: if the
    // low bit of the first digit is 1, the result is negative. Returns a
    // pair of (decoded_value, index_past_end) where index_past_end points to
    // the first character that was not consumed.
    //
    // Example:
    //   DecodeVLQ("A", 0) -> (0, 1)     // single digit, value 0
    //   DecodeVLQ("C", 0) -> (1, 1)     // single digit, value 1
    //   DecodeVLQ("D", 0) -> (-1, 1)    // single digit, sign bit set
    //   DecodeVLQ("gC", 0) -> (64, 2)   // two digits: (0<<5|2) * 2 = 64
    //   DecodeVLQ("abc", 1) -> (2, 3)   // decode starting at index 1
    std::pair<int, int> DecodeVLQ(std::string_view encoded, int start);

    // Decodes a single Base64-VLQ value from a UTF-16 character span. This
    // is the UTF-16 counterpart of DecodeVLQ, used when parsing source maps
    // that have been decoded from JSON into a UTF-16 string buffer. Returns a
    // tuple of (decoded_value, code_units_consumed, success). On failure
    // (invalid Base64 character or truncated input), success is false and the
    // other values are undefined.
    //
    // Example:
    //   DecodeVLQUTF16(u"A") -> (0, 1, true)
    //   DecodeVLQUTF16(u"C") -> (1, 1, true)
    //   DecodeVLQUTF16(u"D") -> (-1, 1, true)
    //   DecodeVLQUTF16(u"")  -> (0, 0, false)  // empty input
    std::tuple<int32_t, int, bool> DecodeVLQUTF16(std::span<const char16_t> encoded);

    // Precomputes per-line offset tables for the given source file contents.
    // Each entry in the returned vector corresponds to one line and stores
    // the byte-to-column mapping information needed by the source map builder
    // to convert byte offsets into the column numbers that source maps
    // require. 'approximate_line_count' is used only as a capacity hint for
    // the vector; the actual number of entries is determined by counting
    // newlines in 'contents'. Lines that contain only ASCII characters have
    // empty columns_for_non_ascii vectors (a fast-path optimization).
    //
    // Example:
    //   For contents = "abc\ndef\n" with approximate_line_count = 2:
    //   Result has 2 entries, one per line, each with
    //   columns_for_non_ascii = {} (pure ASCII) and
    //   byte_offset_to_first_non_ascii = 4 (pointing past the line).
    std::vector<LineOffsetTable> GenerateLineOffsetTables(std::string_view contents, int32_t approximate_line_count);

    // Factory function that creates a ChunkBuilder configured for the given
    // input source map (or nullptr for a fresh source map with no remapping).
    // The line_offset_tables parameter provides the precomputed byte-to-column
    // conversion data for all source files being processed. When ascii_only is
    // true, symbol names are restricted to ASCII characters (non-ASCII
    // characters are replaced with underscores) to avoid encoding issues in
    // the VLQ output.
    //
    // Example:
    //   auto tables = GenerateLineOffsetTables(src, 100);
    //   ChunkBuilder builder = MakeChunkBuilder(nullptr, tables, false);
    //   // builder is now ready to record mappings for source 'src'
    ChunkBuilder MakeChunkBuilder(SourceMapData* input_source_map, const std::vector<LineOffsetTable>& line_offset_tables, bool ascii_only);

    // Appends the VLQ-encoded mappings from a single Chunk to a Joiner
    // object, applying the shift between prev_end_state and start_state. The
    // shift represents how much the generated positions have moved since the
    // previous chunk was finalized, and it is used to adjust the VLQ deltas
    // so that the final source map reflects the correct absolute positions.
    // The buffer's VLQ segments are re-encoded with the corrected deltas
    // before being appended to the joiner.
    //
    // Example:
    //   If prev_end_state.generated_column = 10 and
    //   start_state.generated_column = 15, the shift is 5 columns. Any VLQ
    //   segment in buffer.data that references a generated column will have
    //   its first delta increased by 5 before being written.
    void AppendSourceMapChunk(helpers::Joiner& j, SourceMapState prev_end_state, SourceMapState start_state, const MappingsBuffer& buffer);

}
