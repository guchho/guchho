////////////////////////////////////////////////////////////////////////////////
// The mangle cache file
//
// Property shortening turns readable property names into one or two letters,
// and the same property has to keep the same short name in every build of a
// project. A name that moved between two builds invalidates every cached copy
// of the previous output — a browser cache, a service worker copy, a client
// still holding the old bundle — and forces the whole application to reload
// over a change that touched a single property.
//
// Guchho therefore keeps one small JSON file, named by the build's
// "--mangle-cache" flag, that maps each original property name to the decision
// recorded for it. This file is the whole of that feature on the command line:
// it reads the file into the shape the build wants, and formats the build's
// decisions back into the file's text form. Choosing the short names is not
// done here — the linker does that, through the "mangle_cache" field of the
// build options, which the two functions below are the file side of.
//
// The file is a flat object whose keys are original property names and whose
// values are either a string or the literal false:
//
//     {
//       "firstName": "a",
//       "apiKey": false
//     }
//
// A key is the part the build acts on. A property named in this file already
// has a short name, so the linker keeps the assignment it has and will not
// hand that name to a different property. The two value forms record that
// decision differently rather than asking for different treatment, and either
// one is accepted in a file a person wrote by hand.
//
// Both directions are deliberately uneventful. Names are written back in the
// order they were read, so a build that decided nothing new rewrites the file
// byte for byte, and names decided since then are placed to match the shape
// the file already had. A cache file is meant to live next to the code it
// describes and to survive being committed, and a file that reshuffles itself
// on every build is a file nobody commits.
////////////////////////////////////////////////////////////////////////////////

#include "guchho/cli.hpp"
#include "guchho/helpers.hpp"
#include "guchho/logger.hpp"
#include "guchho/resolver.hpp"
#include "guchho/filesystem.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_helpers.hpp"
#include "guchho/javascript/js_parser.hpp"
#include "guchho/javascript/js_lexer.hpp"

#include <algorithm>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <system_error>

namespace guchho::cli {

    ////////////////////////////////////////////////////////////////////////////////
    // Telling "there is no cache file yet" apart from "the cache file is broken"
    //
    // A cache is optional, so running without one is the ordinary first build
    // of a project and must pass in silence. Anything else that goes wrong with
    // the read is a real problem the person running the build has to see. The
    // two are separated by comparing the canonical error code against the one
    // the platform uses for a missing file, never by matching message text.
    //
    // Input : a read failure carrying std::errc::no_such_file_or_directory.
    // Output: true, and the caller starts from an empty cache and logs nothing.
    //
    // Input : a read failure carrying any other error code, such as a
    //         permission problem.
    // Output: false, and the caller reports the file and the reason.
    ////////////////////////////////////////////////////////////////////////////////
    static bool IsENOENT(const std::optional<std::errc>& error)
    {
        return error == std::errc::no_such_file_or_directory;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Reading the cache file
    //
    // Turns the file on disk into the two containers a build needs: the mapping
    // itself, and the keys in the order the file listed them, which is what lets
    // the file be written back without reshuffling.
    //
    // The read goes through the filesystem::Fs it is given rather than around
    // it, so a run against an in-memory tree finds exactly the file a run
    // against the disk would. "os_args" is not scanned for anything; it only
    // builds a log, so a complaint about this one file is printed with the same
    // colour and verbosity as the rest of the run.
    //
    // Every way this can fail ends the same way: the error is logged and the
    // result is empty. That is on purpose. A file that is only half understood
    // is worse than no file, because the build would carry on and shorten a
    // different set of properties than the last build did, then write that back
    // over the file — turning a readable complaint into a silent loss.
    //
    // Input : a file containing
    //             { "firstName": "a", "apiKey": false }
    // Output: a cache holding "firstName" mapped to "a" and "apiKey" mapped to
    //         nothing at all, an order holding both names as they were
    //         written, and nothing logged.
    //
    // Input : a file containing { "firstName": true }
    // Output: an error pointing at the literal "true" and naming "firstName" as
    //         the key that needs a string or false, and both containers empty.
    //
    // Input : a file containing [ "firstName" ]
    // Output: an error at the opening bracket saying a top-level object was
    //         expected, and both containers empty.
    //
    // Input : no file at that path.
    // Output: both containers empty and nothing logged, which is how a first
    //         build starts its cache.
    ////////////////////////////////////////////////////////////////////////////////
    MangleCacheResult parseMangleCache(
        const std::vector<std::string>& os_args,
        filesystem::Fs& fs,
        const std::string& abs_path)
    {
        logger::Log log = logger::NewStderrLog(logger::OutputOptionsForArgs(os_args));

        // A path to put in a message, not to open. The cache is named by the
        // person running the build, so it is shown the way they wrote it:
        // relative to the working directory when it can be made relative, and
        // spelled with forward slashes either way, so the same mistake reports
        // identically on every platform. A path with no common ancestor with
        // the working directory keeps its absolute form.
        std::string pretty_path = abs_path;
        if (auto rel = fs.Rel(fs.Cwd(), abs_path)) {
            pretty_path = *rel;
        }
        for (auto& c : pretty_path) {
            if (c == '\\') c = '/';
        }

        // A cache that is simply not there yet is the expected case and is
        // answered with silence, so the build starts a fresh one.
        auto read_result = fs.ReadFile(abs_path);
        if (!read_result.Ok()) {
            if (IsENOENT(read_result.canonical_error)) {
                return MangleCacheResult{};
            }

            // Every other reason the file could not be read is the user's to
            // fix, and the reason is worth more than the file name alone.
            log.AddError(nullptr, logger::Range{},
                guchho::logger::FormatMsg(guchho::logger::MsgCat::kCLI_FailedToReadMangleCache, pretty_path, read_result.original_error));
            return MangleCacheResult{};
        }

        // The contents are parsed with Guchho's own JSON parser rather than by
        // hand, so a file with a mistake in it is reported with the line and
        // column a person can act on. The file is presented to the parser as an
        // ordinary "file" source, which is also what gives the resulting
        // diagnostics a name to print.
        logger::Path key_path;
        key_path.text = abs_path;
        key_path.namespace_ = "file";

        logger::Source source;
        source.key_path = key_path;
        source.pretty_paths = resolver::MakePrettyPaths(fs, key_path);
        source.contents = std::move(read_result.value);

        auto [result, ok] = javascript::Parser::ParseJSON(log, source, javascript::JSONOptions{});
        if (!ok || log.has_errors()) {
            // Stop here rather than carry on: a build that cannot read the file
            // must not reach the point where it writes one back, or a typo in
            // the cache would be replaced by an empty cache and quietly cost
            // every recorded name.
            return MangleCacheResult{};
        }

        logger::LineColumnTracker tracker(&source);

        // The file is one object of decisions. Anything else — an array, a bare
        // string, a number — means the file is not a cache file, and the result
        // points at the start of what was found instead.
        auto* root = javascript::Get<javascript::EObject>(result.data);
        if (!root) {
            log.AddError(&tracker, logger::Range{.loc = result.loc},
                guchho::logger::FormatMsg(guchho::logger::MsgCat::kCLI_ExpectedTopLevelObject));
            return MangleCacheResult{};
        }

        // Two containers are filled from one pass over the properties: the
        // mapping to build with, and the key order to write back later. Both
        // are sized up front, because a project with a few thousand renamed
        // properties should not rehash while it reads its own file.
        std::unordered_map<std::string, std::optional<std::string>> mangle_cache;
        std::vector<std::string> order;
        mangle_cache.reserve(root->properties.size());
        order.reserve(root->properties.size());

        // One property is one name and one decision. The key is always a string;
        // the value is either a string or false. A true is almost always a
        // hand-editing slip, and it is called out at the literal itself rather
        // than at the line, because the key alone would leave the reader
        // guessing which of the two values was wrong.
        for (auto& property : root->properties) {
            auto* key_str = javascript::Get<javascript::EString>(property.key.data);
            std::string key = helpers::UTF16ToString(key_str->value);
            order.push_back(key);

            auto* bool_val = javascript::Get<javascript::EBoolean>(property.value_or_nil.data);
            auto* str_val = javascript::Get<javascript::EString>(property.value_or_nil.data);

            if (bool_val) {
                if (bool_val->value) {
                    log.AddError(&tracker,
                        javascript::RangeOfIdentifier(source, property.value_or_nil.loc),
                        guchho::logger::FormatMsg(guchho::logger::MsgCat::kCLI_ExpectedKeyStringOrFalse, key));
                } else {
                    mangle_cache[key] = std::nullopt;
                }
            } else if (str_val) {
                mangle_cache[key] = helpers::UTF16ToString(str_val->value);
            } else {
                log.AddError(&tracker, logger::Range{.loc = property.value_or_nil.loc},
                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kCLI_ExpectedKeyStringOrFalse, key));
            }
        }

        // A single bad entry voids the whole file. Half a mapping would shorten
        // some properties and leave others alone, which is a result nobody
        // asked for and cannot tell apart from a deliberate choice later.
        if (log.has_errors()) {
            return MangleCacheResult{};
        }
        return MangleCacheResult{std::move(mangle_cache), std::move(order)};
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Writing the cache file
    //
    // Renders a mapping back into the text form of the file. Nothing is stored
    // here: the caller decides where the bytes are written, which is what lets
    // a build keep the file beside its configuration and lets a test compare
    // the string directly.
    //
    // The output is meant to survive being committed and being read by a
    // person, so two properties govern it. Names appear in the order they were
    // read, which makes a build that decided nothing new rewrite the file byte
    // for byte. And names decided during this build are added in the style the
    // file already uses: merged into one sorted list when the names on file
    // were sorted, or gathered into a sorted block at the end when they were
    // not, so a hand-ordered file keeps its shape instead of being flattened.
    //
    // "ascii_only" is the escape hatch for a file that has to survive a
    // terminal, an editor or a version control system with a different opinion
    // about encoding: every name and value is then written with anything
    // outside ASCII escaped, at the cost of a file that is harder to read.
    //
    // The text is assembled through a joiner rather than by appending to a
    // string, so the many small pieces — indentation, a quoted name, a colon, a
    // quoted value — are collected first and joined once at the end.
    //
    // Input : a cache holding "firstName" mapped to "a" and "apiKey" mapped to
    //         nothing, an original order of { "firstName", "apiKey" }, and
    //         ascii_only = false.
    // Output: the text
    //             {
    //               "firstName": "a",
    //               "apiKey": false
    //             }
    //         with two space indentation, one name per line and a trailing
    //         newline, both names in the order they were read in.
    //
    // Input : a cache holding "firstName" mapped to "a" plus two new names
    //         "zebra" and "colour" mapped to "b" and "c", an original order of
    //         { "firstName" }, and ascii_only = false.
    // Output: the original name first and the two new ones after it, sorted
    //         between themselves, as
    //             {
    //               "firstName": "a",
    //               "colour": "c",
    //               "zebra": "b"
    //             }
    //         so the one line a person wrote stays where they wrote it.
    //
    // Input : an empty cache with an empty order.
    // Output: "{}\n" — an empty object on one line, with no blank line where
    //         the names would have been.
    ////////////////////////////////////////////////////////////////////////////////
    std::string printMangleCache(
        const std::unordered_map<std::string, std::optional<std::string>>& mangle_cache,
        const std::vector<std::string>& original_order,
        bool ascii_only)
    {
        helpers::Joiner j;
        j.AddString("{");

        // The order to write the names in. As long as the build did not decide
        // anything the file did not already have, the order it was read in is
        // used unchanged. Once there are names the file does not list, the
        // shape of what is already there decides where they are placed.
        std::vector<std::string> order;
        if (mangle_cache.size() > original_order.size()) {
            if (std::is_sorted(original_order.begin(), original_order.end())) {
                // A sorted file stays sorted, so the new names join the list
                // instead of landing after it.
                order.reserve(mangle_cache.size());
                for (auto& [key, _] : mangle_cache) {
                    order.push_back(key);
                }
                std::sort(order.begin(), order.end());
            } else {
                // A file in an order of somebody's own making keeps it, and the
                // new names are collected into a sorted block at the end. Only
                // the block is sorted, because sorting the whole list would
                // discard the arrangement that was there.
                std::unordered_set<std::string> original_keys(
                    original_order.begin(), original_order.end());
                order.reserve(mangle_cache.size());
                order.insert(order.end(), original_order.begin(), original_order.end());
                for (auto& [key, _] : mangle_cache) {
                    if (original_keys.find(key) == original_keys.end()) {
                        order.push_back(key);
                    }
                }
                std::sort(order.begin() + static_cast<ptrdiff_t>(original_order.size()), order.end());
            }
        } else {
            order = original_order;
        }

        // Now the object itself, one name per line, each on its own line with
        // the names in the order settled above.
        for (size_t i = 0; i < order.size(); ++i) {
            const std::string& key = order[i];
            // The separator doubles as the indentation, which is why the first
            // name gets a newline and no comma.
            if (i > 0) {
                j.AddString(",\n  ");
            } else {
                j.AddString("\n  ");
            }
            auto quoted_key = helpers::QuoteForJSON(key, ascii_only);
            j.AddBytes(std::span<const char>(quoted_key));

            // A name with a decision is written as that decision quoted, and a
            // name without one is written as false. A property that is listed
            // but was not shortened this build therefore stays listed: it is
            // recorded as deliberately left alone instead of being forgotten,
            // so a later build cannot shorten it to something new and break
            // whatever outside the bundle still reads it by name.
            auto it = mangle_cache.find(key);
            if (it != mangle_cache.end() && it->second.has_value()) {
                j.AddString(": ");
                auto quoted_val = helpers::QuoteForJSON(*it->second, ascii_only);
                j.AddBytes(std::span<const char>(quoted_val));
            } else {
                j.AddString(": false");
            }
        }

        // The closing brace sits on its own line, but only when there was
        // something above it to close.
        if (!order.empty()) {
            j.AddString("\n");
        }
        j.AddString("}\n");
        return j.Done();
    }

} // namespace guchho::cli
