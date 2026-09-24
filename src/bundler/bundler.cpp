////////////////////////////////////////////////////////////////////////////////
// Bundler shared helpers
//
// Small utilities shared by the bundler translation units: script-type checks,
// repr casting, Windows path canonicalization, unique-key generation and a few
// string predicates. They are external (declared in the internal section of
// guchho/bundler.hpp) because the bundler is split across several files.
////////////////////////////////////////////////////////////////////////////////

#include "guchho/bundler.hpp"

#include <random>


namespace guchho::bundler {


    // Returns true when the <script> element carries a "type" attribute equal to
    // "module", i.e. it is an ES module script.
    //
    // Input : an HTML element.
    // Output: true for "<script type=module>", false otherwise.
    bool IsHtmlModuleScript(const html::Node& element) {
        if (element.tag_name != "script") {
            return false;
        }
        for (const html::Attribute& attr : element.attrs) {
            if (attr.name == "type" && attr.value == "module") {
                return true;
            }
        }
        return false;
    }

    // Returns the JSRepr inside an input file repr, or null when the repr does not
    // hold a JS repr (e.g. it is CSS or JSON).
    //
    // Input : an input file repr.
    // Output: a borrowed JSRepr pointer, or nullptr.
    graph::JSRepr* TryJSRepr(graph::InputFileRepr& repr) {
        if (auto* ptr =
                std::get_if<std::shared_ptr<graph::JSRepr>>(&repr);
            ptr != nullptr && *ptr != nullptr)
        {
            return ptr->get();
        }
        return nullptr;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Free helpers
    //
    // General-purpose string and path utilities used throughout the bundler.
    ////////////////////////////////////////////////////////////////////////////////

    // Normalizes an absolute path for use as a Windows identity key: backslashes
    // become forward slashes and drive letters are lowercased, so different
    // spellings of the same file map to the same key.
    //
    // Input : "C:\\Foo\\Bar.js".
    // Output: "c:/foo/bar.js".
    std::string CanonicalFileSystemPathForWindows(const std::string& abs_path) {
        std::string result;
        result.reserve(abs_path.size());
        for (char c : abs_path) {
            if (c == '\\') {
                result.push_back('/');
            } else if (c >= 'A' && c <= 'Z') {
                result.push_back(static_cast<char>(c - 'A' + 'a'));
            } else {
                result.push_back(c);
            }
        }
        return result;
    }

    // Generates a fresh 16-character base64url prefix used to make output keys
    // unique. The random bytes are chosen so the result never needs escaping when
    // embedded in a string.
    //
    // Input : none.
    // Output: a 16-character base64url string.
    std::string GenerateUniqueKeyPrefix() {
        std::random_device device;
        std::mt19937_64 engine(device());
        std::uniform_int_distribution<int> byte(0, 255);
        std::string data(12, '\0');
        for (char& c : data) c = static_cast<char>(byte(engine));

        return helpers::Base64URLEncode(data);
    }

    // Returns true when every byte is printable ASCII (0x20..0x7E), which the
    // printer uses to decide whether output can be emitted as-is.
    //
    // Input : a byte string.
    // Output: true when all bytes are printable ASCII.
    bool IsASCIIOnly(std::string_view text) {
        for (char c : text) {
            if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) {
                return false;
            }
        }
        return true;
    }

    // Returns true when text ends with the given suffix.
    //
    // Input : "hello.js", ".js".
    // Output: true.
    bool EndsWith(const std::string& text, const char* suffix) {
        size_t len = std::strlen(suffix);
        return text.size() >= len && text.compare(text.size() - len, len, suffix) == 0;
    }

}

