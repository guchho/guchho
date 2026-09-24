#include "guchho/graph.hpp"

namespace guchho::graph {

    // Answers "which parts declare this top-level symbol?" for a JavaScript
    // payload. Two layers are consulted in order: first the linker's mutable
    // overlay, which records parts appended after linking began (generated
    // helpers, wrapper pieces, and any other synthetic part registered via
    // AddPartToFile); then the immutable map the parser filled in while the
    // file was first read. The overlay wins whenever it has an entry, because
    // it is strictly newer than the parser's view of the same symbol. When
    // neither layer knows the symbol, a shared empty vector is returned so
    // callers can iterate the result unconditionally without checking for a
    // null or dangling pointer.
    //
    // Input:  ref for a helper declared in part 2 of this file, with no
    //         overlay entry recorded yet
    // Output: { 2 } -- the parser's part list for that helper
    //
    // Input:  the same ref after the linker appended part 7 to the overlay
    // Output: { 2, 7 } -- the extended list from the overlay
    //
    // Input:  a ref that belongs to a symbol this file never declared
    // Output: an empty vector -- nothing depends on a missing declaration
    const std::vector<uint32_t>& JSRepr::TopLevelSymbolToParts(compiler::Ref ref) const
    {
        auto overlay_it = meta.top_level_symbol_to_parts_overlay.find(ref);
        if (overlay_it != meta.top_level_symbol_to_parts_overlay.end()) {
            return overlay_it->second;
        }

        auto parser_it = ast.top_level_symbol_to_parts_from_parser.find(ref);
        if (parser_it != ast.top_level_symbol_to_parts_from_parser.end()) {
            return parser_it->second;
        }

        static const std::vector<uint32_t> kEmpty;
        return kEmpty;
    }

}
