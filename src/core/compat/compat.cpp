#include "guchho/compat.hpp"
#include "guchho/compiler.hpp"

namespace guchho::compat {

    // Combines the numeric parts and pre-release suffix of a Semver value into
    // a single dotted version string. The numeric parts are joined with '.'
    // separators and the pre-release string is appended without any separator.
    //
    // Example:
    //   Semver{.parts = {1, 2, 3}, .pre_release = "-beta.1"}
    //   ToString() -> "1.2.3-beta.1"
    //
    //   Semver{.parts = {0, 1, 0}, .pre_release = ""}
    //   ToString() -> "0.1.0"
    //
    // Edge cases:
    //   - If 'parts' is empty, returns just the pre-release string (possibly empty).
    //   - Pre-release is appended verbatim; callers must include the leading '-'
    //     when constructing the Semver if they want it in the output.
    std::string Semver::ToString() const
    {
        std::string result;

        for (uint32_t part : parts) {
            if (!result.empty()) {
                result += '.';
            }

            result += std::to_string(part);
        }

        result += pre_release;

        return result;
    }

    // Compares a structured Version (major/minor/patch) against a Semver
    // whose numeric parts are stored in a vector. Returns a negative value if
    // 'a' < 'b', zero if equal, or a positive value if 'a' > 'b'.
    //
    // The comparison is performed left-to-right: major, then minor, then patch.
    // If the Semver has fewer parts than three, the missing parts are treated
    // as zero. When the numeric parts are identical and 'b' has a non-empty
    // pre-release string, 'a' is considered greater (i.e., "1.0.0" > "1.0.0-alpha").
    //
    // Example:
    //   CompareVersions({1, 2, 3}, Semver{.parts = {1, 2, 3}}) -> 0
    //   CompareVersions({1, 2, 4}, Semver{.parts = {1, 2, 3}}) -> 1
    //   CompareVersions({1, 2, 3}, Semver{.parts = {1, 2, 4}}) -> -1
    //
    // Edge cases:
    //   - Semver with empty parts: all numeric parts treated as zero.
    //   - Pre-release tie-breaking: any non-empty pre-release makes b < a
    //     when numeric parts match.
    int CompareVersions(Version a, Semver b) {
        int diff = static_cast<int>(a.major);
        if (!b.parts.empty()) {
            diff -= b.parts[0];
        }
        if (diff == 0) {
            diff = static_cast<int>(a.minor);
            if (b.parts.size() > 1) {
                diff -= b.parts[1];
            }
        }
        if (diff == 0) {
            diff = static_cast<int>(a.patch);
            if (b.parts.size() > 2) {
                diff -= b.parts[2];
            }
        }
        if (diff == 0 && !b.pre_release.empty()) {
            return 1; // "1.0.0" > "1.0.0-alpha"
        }
        return diff;
    }

    // Checks whether a given Semver version falls within any of the provided
    // version ranges. A version is considered supported if it is >= range.start
    // and < range.end. If range.end is a default-constructed Version (all
    // zeros), the upper bound is treated as unbounded (the range is open-ended
    // on the right).
    //
    // Example:
    //   IsVersionSupported({{.start={1,0,0}, .end={2,0,0}}}, Semver{.parts={1,5,0}})
    //   -> true   (1.5.0 is within [1.0.0, 2.0.0))
    //
    //   IsVersionSupported({{.start={1,0,0}, .end={2,0,0}}}, Semver{.parts={2,0,0}})
    //   -> false  (2.0.0 is not < 2.0.0)
    //
    //   IsVersionSupported({{.start={1,0,0}, .end={}}}, Semver{.parts={99,0,0}})
    //   -> true   (end is all-zeros, treated as unbounded)
    //
    // Edge cases:
    //   - Empty ranges vector: always returns false.
    //   - Version with pre-release: CompareVersions treats non-empty pre-release
    //     as less than the same version without pre-release, so "1.0.0-alpha"
    //     would be < start=1.0.0 and thus not supported by range [1.0.0, ...).
    //   - Multiple ranges: returns true on the first match, short-circuits.
    bool IsVersionSupported(const std::vector<VersionRange>& ranges, const Semver& version) {
        for (const auto& r : ranges) {
            if (CompareVersions(r.start, version) <= 0 &&
                (r.end == Version{} || CompareVersions(r.end, version) > 0)) {
                return true;
            }
        }
        return false;
    }

    // Maps a compiler SymbolKind to the corresponding JSFeature flag that
    // indicates the language feature required to support that symbol kind.
    // Private class members (fields, methods, accessors) and their static
    // variants each map to a distinct feature flag. Any symbol kind not
    // related to private members returns a zero value (no feature required).
    //
    // Example:
    //   SymbolFeature(compiler::SymbolKind::kPrivateField)
    //   -> JSFeature::kClassPrivateField
    //
    //   SymbolFeature(compiler::SymbolKind::kPrivateStaticMethod)
    //   -> JSFeature::kClassPrivateStaticMethod
    //
    //   SymbolFeature(compiler::SymbolKind::kPublicField)
    //   -> static_cast<JSFeature>(0)  (no private-member feature needed)
    //
    // Edge cases:
    //   - Private getters, setters, and getter/setter pairs all map to the same
    //     accessor feature flag regardless of whether the accessor is static or
    //     instance-level.
    //   - The default case covers all non-private symbol kinds and returns zero,
    //     which callers should interpret as "no special feature flag applies."
    JSFeature SymbolFeature(compiler::SymbolKind kind) {
        switch (kind) {
            case compiler::SymbolKind::kPrivateField:
                return JSFeature::kClassPrivateField;
            case compiler::SymbolKind::kPrivateMethod:
                return JSFeature::kClassPrivateMethod;
            case compiler::SymbolKind::kPrivateGet:
            case compiler::SymbolKind::kPrivateSet:
            case compiler::SymbolKind::kPrivateGetSetPair:
                return JSFeature::kClassPrivateAccessor;
            case compiler::SymbolKind::kPrivateStaticField:
                return JSFeature::kClassPrivateStaticField;
            case compiler::SymbolKind::kPrivateStaticMethod:
                return JSFeature::kClassPrivateStaticMethod;
            case compiler::SymbolKind::kPrivateStaticGet:
            case compiler::SymbolKind::kPrivateStaticSet:
            case compiler::SymbolKind::kPrivateStaticGetSetPair:
                return JSFeature::kClassPrivateStaticAccessor;
            default:
                return static_cast<JSFeature>(0);
        }
    }
}
