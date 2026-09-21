#pragma once

// It provides a GoogleTest(https://github.com/google/googletest) compatible surface (TEST, EXPECT_* and
// ASSERT_*) so test source files only need to swap their include, plus a
// --list-tests / --run= driver that CTest discovery uses to register one
// CTest test per TEST().
//
// The header provides three things:
//
//   1. Registration. A TEST(Suite, Name) block declares a plain function
//      and uses file-scope static objects to record it into a global
//      registry before main() runs, so tests are discovered with zero
//      configuration.
//
//   2. Assertions. EXPECT_* and ASSERT_* evaluate a condition and, on
//      failure, format a readable failure report (location, operation,
//      both operand expressions, both formatted values, plus any trailing
//      <<" streamed") message). A failing assertion also aborts the rest
//      of the test body so the report stays focused on the first root
//      cause. The CHECK_TYPES below cover boolean, relational and
//      floating-point comparisons, and every operand is rendered through
//      a type-aware formatter that quotes strings, shows characters with
//      their code point, and prints pointers as hex.
//
//   3. A driver. RunAll() parses a small set of flags so the same binary
//      can list its tests, run everything, run one named test, or run a
//      filtered subset. CTest discovers one test per TEST() by invoking
//      the binary with --list-tests and registers each line as its own
//      test case.
//
// State is intentionally process-global: RunAll() is called from a test
// main() (or from guchho's own generated test driver), runs the selected
// tests, prints a one-line summary, and returns an exit code suitable for
// the CI layer (0 when everything passed, 1 when anything failed).

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace guchho {
namespace test {

// TestAbort is an empty exception type raised by every failing assertion
// to unwind the rest of the test body. RunOne() catches it around the
// invocation of each test function, so an assertion failure anywhere in
// the call stack — including inside helpers called by the test — cleanly
// stops that test without unwinding the whole suite. The destructor of
// the assertion handle is allowed to throw this type (see CheckHandle),
// which is why no destructor in the hot path is marked noexcept.
struct TestAbort {};

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

// TestInfo is the immutable record each TEST() contributes to the global
// registry. Suite/name are the stringified macro arguments, fn is the test
// body function, and file/line are the exact location of the TEST() line,
// used when the suite has to reproduce a location for diagnostics.
struct TestInfo {
    const char* suite;
    const char* name;
    void (*fn)();
    const char* file;
    int line;
};

// Registry() returns the single process-wide table of registered tests.
// Entries are appended during static initialization (see TestRegistrar),
// so by the time main() runs the vector is complete and can be iterated in
// source order. The function-local static guarantees thread-safe
// construction the first time it is touched.
//
//   Registry() -> [ TestInfo{Suite "Scanner", Name "RejectsEmpty dir", ...}, ... ]
inline std::vector<TestInfo>& Registry() {
    static std::vector<TestInfo> tests;
    return tests;
}

// TestRegistrar is the file-scope object type whose constructor appends
// one TestInfo to the registry. A namespace-scope instance of it created
// by the TEST() macro runs as part of static initialization, i.e. before
// main() executes, so no manual registration call is ever needed.
class TestRegistrar {
public:
    explicit TestRegistrar(TestInfo info) {
        Registry().push_back(info);
    }
};

// TEST(Suite, Name) declares a test. It expands to:
//
//   - a forward declaration of a unique static void Suite_Name(),
//   - a file-scope TestInfo constructed from the stringified suite/name
//     plus __FILE__ and __LINE__,
//   - a file-scope TestRegistrar that pushes that info into the registry
//     during static initialization, and
//   - the opening of Suite_Name() itself.
//
// The registrar object is marked [[maybe_unused]] so TUs that include this
// header but register no tests (or disable an unused suite) do not produce
// warnings. Each TEST() therefore compiles to distinct mangled symbols and
// can sit in the same file as any number of sibling tests.
#define TEST(Suite, Name)                                                       \
    static void Suite##_##Name();                                               \
    static ::guchho::test::TestInfo Suite##_##Name##_info{                       \
        #Suite, #Name, &Suite##_##Name, __FILE__, __LINE__};                    \
    static ::guchho::test::TestRegistrar                                        \
        Suite##_##Name##_registrar [[maybe_unused]]{Suite##_##Name##_info};      \
    static void Suite##_##Name()

// ---------------------------------------------------------------------------
// Runner state
// ---------------------------------------------------------------------------

// RunState tracks the cumulative outcome of a RunAll() invocation. passed
// and failed count finished tests, current is the dotted name of the test
// being executed right now, and current_failed records whether that test
// produced any failing assertion or escaped with a non-TestAbort
// exception. failure_depth is reserved for nested assertion contexts and
// currently stays 0.
struct RunState {
    int passed = 0;
    int failed = 0;
    std::string current;
    bool current_failed = false;
    int failure_depth = 0;
};

// State() returns the single process-wide RunState. Like Registry() it is
// a function-local static, so it is constructed on first use and shared by
// the assertion machinery, RunOne(), RunAll(), and any finish hook. The
// framework is single-threaded by design: tests run sequentially in one
// thread, so no locking is required around this state.
inline RunState& State() {
    static RunState state;
    return state;
}

// FinishHook is a callback type a harness can install to run exactly once
// after the selected tests complete but while still inside main().
// guchho's snapshot harness uses it to flush and validate its recorded
// state; doing so here is safe because RunAll() is guaranteed to be off
// the stack before it returns, unlike a static-destructor or atexit hook.
using FinishHook = void (*)();
inline FinishHook& FinishHookRef() {
    static FinishHook hook = nullptr;
    return hook;
}
inline void SetFinishHook(FinishHook f) {
    FinishHookRef() = f;
}

// ---------------------------------------------------------------------------
// Value formatting
// ---------------------------------------------------------------------------

// Streamable is a concept that an operand type satisfies when it can be
// written to an std::ostream. It is used to decide whether a value can be
// rendered through operator<< for failure reports. The constraint checks
// against a const-qualified value of the decayed type so stream operators
// that take const references (the common case) are accepted even though
// the operands arrive as const refs.
template <typename T>
concept Streamable =
    requires(std::ostream& out, const std::type_identity_t<T>& value) {
        out << value;
    };

// Quote() renders a std::string as a double-quoted, escaped literal so
// assertion output stays unambiguous when values contain quotes or
// control characters.
//
//   Quote("a\"b\\c")  ->  "\"a\\\"b\\\\c\""   (quotes are part of the text)
//   Quote("hi")       ->  "\"hi\""
//   Quote("\n\t")     ->  "\"\\n\\t\""
//
// Each byte is inspected as an unsigned char so bytes >= 0x80 (UTF-8
// continuation/tail bytes) are treated as opaque data, not as negative
// chars. Only backslash, double quote, newline, tab and carriage return
// are rewritten; every other byte — including all non-ASCII bytes —
// passes through verbatim, keeping UTF-8 text intact.
inline std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (const char raw : s) {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            default:   out += raw;    break;
        }
    }
    out += "\"";
    return out;
}

// AppendUTF8() appends the UTF-8 encoding of a single Unicode code point
// (cp) to the end of out. It selects the 1, 2, 3 or 4-byte encodings
// defined by RFC 3629 based on the code point's magnitude, and is the
// shared low-level append routine used by the UTF-16/UTF-8 conversion
// path in FromUTF16().
//
//   AppendUTF8(out, 0x41)    ->  out ends with 'A'      (1 byte)
//   AppendUTF8(out, 0x20AC)  ->  out ends with 0xE2 0x82 0xAC  ("€", 3 bytes)
//   AppendUTF8(out, 0x1F600) ->  out ends with 0xF0 0x9F 0x98 0x80 ("\U0001F600",
//                                                                   4 bytes)
//
// Only the appending is done here; out is never resized or cleared, and
// invalid code points (surrogate halves or values above 0x10FFFF) are not
// screened — producers such as FromUTF16() are responsible for that.
inline void AppendUTF8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// FromUTF16() decodes a UTF-16 code-unit sequence into a UTF-8 std::string.
// It walks the input left to right, combining a high surrogate (0xD800..0xDBFF)
// with the following low surrogate (0xDC00..0xDFFF) into a single code point
// (which then needs one UTF-8 entry), and otherwise encodes each code unit
// on its own.
//
//   FromUTF16(u"hi")                    ->  "hi"
//   FromUTF16(u"\uD83D\uDE00")          ->  "\xF0\x9F\x98\x80"  ("\U0001F600")
//   FromUTF16(u"\u00E9")                ->  "\xC3\xA9"          ("é", 2 bytes)
//
// Edge case: an unpaired surrogate at the end of the input (or any
// surrogate without a valid partner) is emitted as its own scalar value
// rather than crashing, so malformed UTF-16 from a misbehaving producer
// still produces a visible, lossy representation instead of undefined
// behavior.
inline std::string FromUTF16(std::u16string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        const char16_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() &&
            s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            const char32_t cp =
                0x10000 +
                ((static_cast<char32_t>(c) - 0xD800) << 10) +
                (static_cast<char32_t>(s[i + 1]) - 0xDC00);
            AppendUTF8(out, cp);
            ++i;
        } else {
            AppendUTF8(out, static_cast<char32_t>(c));
        }
    }
    return out;
}

// ValueText() renders any value used as an operand in a comparison
// assertion into a human-readable string for the failure report. The
// dispatch happens at compile time through decayed type checks, in this
// order:
//
//   ValueText(true)                  ->  "true"
//   ValueText('a')                   ->  "'a' (97)"
//   ValueText(0x20AC)                ->  "8364"            (char16_t/char32_t/wchar_t)
//   ValueText(someEnumValue)         ->  "<2>"             (any enum)
//   ValueText(u"hi")                 ->  "\"hi\""          (UTF-16, quoted)
//   ValueText(std::string("hi"))     ->  "\"hi\""
//   ValueText("hi")                  ->  "\"hi\""          (C string, quoted)
//   ValueText(static_cast<void*>(p)) ->  "0x...hex..."     (non-null pointer)
//   ValueText(nullptr)               ->  "null"
//   ValueText(3.14)                  ->  "3.14"            (any streamable type)
//   ValueText(someOpaqueType{})      ->  "<unprintable>"   (not streamable)
//
// The null checks run before streaming so null doesn't crash or format
// oddly; enums print their underlying value; strings are always quoted so
// empty strings stay visibly distinct; pointers are hex without a "0"
// prefix on null. The trailing fallback keeps the report readable even for
// types the harness knows nothing about.
template <typename T>
std::string ValueText(const T& value) {
    using D = std::decay_t<T>;
    if constexpr (std::is_same_v<D, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::is_same_v<D, char>) {
        std::ostringstream os;
        os << '\'' << value << "' (" << static_cast<int>(value) << ')';
        return os.str();
    } else if constexpr (std::is_same_v<D, char16_t> ||
                         std::is_same_v<D, char32_t> ||
                         std::is_same_v<D, wchar_t>) {
        return std::to_string(static_cast<long long>(value));
    } else if constexpr (std::is_enum_v<D>) {
        return "<" + std::to_string(static_cast<long long>(value)) + ">";
    } else if constexpr (std::is_same_v<D, std::u16string> ||
                         std::is_same_v<D, std::u16string_view>) {
        return Quote(FromUTF16(value));
    } else if constexpr (std::is_same_v<D, std::string> ||
                         std::is_same_v<D, std::string_view>) {
        return Quote(std::string(value));
    } else if constexpr (std::is_array_v<T> && std::is_same_v<std::remove_extent_t<T>, char16_t>) {
        return Quote(FromUTF16(value));
    } else if constexpr (std::is_array_v<T> && !std::is_same_v<std::remove_extent_t<T>, char16_t>) {
        return Quote(std::string(value));
    } else if constexpr (std::is_same_v<D, char16_t*> ||
                         std::is_same_v<D, const char16_t*>) {
        return value ? Quote(FromUTF16(value)) : std::string("null");
    } else if constexpr (std::is_same_v<D, const char*> ||
                         std::is_same_v<D, char*>) {
        return value ? Quote(std::string(value)) : std::string("null");
    } else if constexpr (std::is_same_v<D, std::nullptr_t>) {
        return std::string("null");
    } else if constexpr (std::is_pointer_v<D>) {
        if (!value) {
            return std::string("null");
        }
        std::ostringstream os;
        os << "0x" << std::hex
           << static_cast<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(value));
        return os.str();
    } else if constexpr (Streamable<D>) {
        std::ostringstream os;
        os << value;
        return os.str();
    } else {
        return std::string("<unprintable>");
    }
}

// ---------------------------------------------------------------------------
// Comparison helpers
// ---------------------------------------------------------------------------

namespace detail {

// Op enumerates the six relational operators the assertion macros map to.
// Keeping them as a strongly typed compile-time tag lets one templated
// Compare() cover EXPECT_EQ/NE/LT/LE/GT/GE without any runtime dispatch.
enum class Op { kEq, kNe, kLt, kLe, kGt, kGe };

// Compare() evaluates the requested relation between two arbitrary-typed
// operands a and b. It intentionally derives the loose orderings from
// operator< only, so types implementing just < (the majority) work with
// the full set:
//
//   Compare<kEq>(2, 2) -> true     Compare<kLt>(2, 3) -> true
//   Compare<kNe>(2, 3) -> true     Compare<kLe>(3, 3) -> true
//   Compare<kGt>(3, 2) -> true     Compare<kGe>(2, 3) -> false
//
// kGe/kLe are expressed as the negation of the mirrored strict ordering
// (le(a, b) == !(b < a)), which also happens to be behaviour that breaks
// symmetric for NaN-like values — an acceptable cost rarely observed with
// the integral/string/pointer operands guchho's suites compare.
template <Op op, typename A, typename B>
bool Compare(const A& a, const B& b) {
    using C = std::common_type_t<std::decay_t<A>, std::decay_t<B>>;
    const C ca = static_cast<C>(a);
    const C cb = static_cast<C>(b);
    if constexpr (op == Op::kEq) {
        return ca == cb;
    } else if constexpr (op == Op::kNe) {
        return !(ca == cb);
    } else if constexpr (op == Op::kLt) {
        return ca < cb;
    } else if constexpr (op == Op::kLe) {
        return !(cb < ca);
    } else if constexpr (op == Op::kGt) {
        return cb < ca;
    } else {
        return !(ca < cb);
    }
}

// OrderedDoubleBits() maps the 64 bit pattern of a double onto a signed
// integer axis that follows IEEE-754 magnitude order. Positive numbers
// keep their raw bit pattern (interpreted from low magnitude to high);
// negative numbers reflect their bits around zero so that -pow -> very
// negative and -tiny -> negative just below zero.
//
//   OrderedDoubleBits(0.0)     ->  0
//   OrderedDoubleBits(1.0)     ->  0x3FF0000000000000 (positive axis)
//   OrderedDoubleBits(-1.0)    ->  -(0x3FF0000000000000)
//
// Negative and positive halves are therefore directly comparable, which
// lets DoubleNearULP() measure distance across the sign boundary and even
// treat -0.0 and +0.0 as one ULP apart (the tiny 1-step gap), matching
// how guchho's snapshot hashing treats bit-equivalent doubles.
inline std::int64_t OrderedDoubleBits(double d) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &d, sizeof(d));
    const std::uint64_t sign = std::uint64_t{1} << 63;
    const std::uint64_t mask = sign - 1;
    if ((bits & sign) != 0) {
        return -static_cast<std::int64_t>(bits & mask);
    }
    return static_cast<std::int64_t>(bits);
}

// DoubleNearULP() reports whether two doubles are within max_ulp "units in
// the last place" of each other, i.e. whether their bit patterns differ by
// at most max_ulp steps on the magnitude axis created by
// OrderedDoubleBits(). This is the tolerance test EXPECT_DOUBLE_EQ uses —
// it measures the representation gap rather than an absolute difference,
// so it is scale-independent.
//
//   DoubleNearULP(1.0, nextafter(1.0, 2.0), 4)  ->  true   (1 ULP apart)
//
// Edge cases: both NaNs compare equal (signaled as such), a single NaN
// compares false against anything, infinities compare like very large
// values, and -0.0/+0.0 count as 1 ULP apart, which callers typically
// tolerate because the gap is the smallest representable one.
inline bool DoubleNearULP(double a, double b, std::uint64_t max_ulp) {
    if (std::isnan(a) || std::isnan(b)) {
        return std::isnan(a) && std::isnan(b);
    }
    const std::int64_t int_a = OrderedDoubleBits(a);
    const std::int64_t int_b = OrderedDoubleBits(b);
    const std::uint64_t diff = int_a > int_b
        ? static_cast<std::uint64_t>(int_a - int_b)
        : static_cast<std::uint64_t>(int_b - int_a);
    return diff <= max_ulp;
}

// DoubleNearlyEqual() is the convenience predicate for EXPECT_DOUBLE_EQ /
// ASSERT_DOUBLE_EQ. It treats two doubles as equal when they are within 4
// ULPs, a tolerance chosen to absorb benign optimizer/accumulator rounding
// while still rejecting genuinely different values. A NaN never compares
// equal to a number, but two NaNs do compare equal (see DoubleNearULP).
inline bool DoubleNearlyEqual(double a, double b) {
    return DoubleNearULP(a, b, 4);
}

// NearWithin() is the absolute-difference predicate behind EXPECT_NEAR /
// ASSERT_NEAR. It returns true when |a - b| <= eps:
//
//   NearWithin(1.0, 1.05, 0.1)  ->  true      NearWithin(1.0, 1.2, 0.1)  ->  false
//
// It differs from the ULP path in that the tolerance is a literal range,
// so it is the right choice when the acceptable error is known in advance
// (e.g. percentile timings in guchho's benchmark smoke tests). A NaN
// operand makes the result false, and eps is compared >=, so an eps of 0
// still accepts bit-identical values.
inline bool NearWithin(double a, double b, double eps) {
    if (std::isnan(a) || std::isnan(b)) {
        return false;
    }
    double diff = a - b;
    if (diff < 0) {
        diff = -diff;
    }
    return diff <= eps;
}

// StreamText() renders value through operator<< when the type supports it,
// and falls back to "<unprintable>" otherwise. It backs the
// CheckHandle::operator<< streaming sugar, so trailing
//   EXPECT_EQ(a, b) << someValue
// messages are always text even when someValue has no stream operator.
//
//   StreamText(1234) -> "1234"     StreamText(Opaque{}) -> "<unprintable>"
template <typename T>
std::string StreamText(const T& value) {
    using D = std::decay_t<T>;
    if constexpr (Streamable<D>) {
        std::ostringstream os;
        os << value;
        return os.str();
    } else {
        return std::string("<unprintable>");
    }
}

// CheckHandle is the object every assertion expression returns. A default-
// constructed instance is the "passed" case and does nothing when it goes
// out of scope at the end of the statement. A failed instance carries the
// location, the op-name, both operand expressions and both pre-formatted
// values; when the statement ends and its destructor runs it formats and
// prints the whole report, then aborts the current test with TestAbort.
//
// The destructor is the reporting point (instead of the assert call)
// because it captures streamed extras:
//
//   EXPECT_EQ(sum, 100) << "sum=" << sum;
//
//   appends "sum=..." via operator<< before the file-scope temporary dies
//   and Finalize() runs. The destructor is deliberately noexcept(false) so
//   it can throw TestAbort straight through any intermediate frames. A
//   duplicate handle would print a second report when it left scope, so
//   the class keeps itself single-owned: only the original object
//   finalizes, and exactly one report is produced per failure.
//
// Note that a failed EXPECT_* behaves like ASSERT_* here: the throw
// stops the rest of the test body so later assertions do not cascade a
// second report on top of the true root cause.
class CheckHandle {
public:
    CheckHandle() = default;

    CheckHandle(const char* file, int line, const char* opname,
                const char* lhs_expr, const char* rhs_expr,
                std::string lhs_val, std::string rhs_val, bool fatal)
        : failed_(true),
          fatal_(fatal),
          where_(std::string(file) + ":" + std::to_string(line)),
          opname_(opname ? opname : ""),
          lhs_expr_(lhs_expr ? lhs_expr : ""),
          rhs_expr_(rhs_expr ? rhs_expr : ""),
          lhs_val_(std::move(lhs_val)),
          rhs_val_(std::move(rhs_val)) {}

    CheckHandle(const CheckHandle&) = delete;
    CheckHandle& operator=(const CheckHandle&) = delete;

    ~CheckHandle() noexcept(false) {
        if (failed_) {
            Finalize();
        }
    }

    template <typename T>
    CheckHandle& operator<<(const T& value) {
        if (failed_) {
            extra_ += detail::StreamText(value);
        }
        return *this;
    }

private:
    void Finalize() {
        std::ostringstream os;
        os << where_ << ": Failure: " << opname_ << '\n';
        if (!lhs_expr_.empty()) {
            os << "  Expected: " << lhs_expr_ << '\n';
        }
        if (!rhs_expr_.empty()) {
            os << "  Which is: " << rhs_expr_ << '\n';
        }
        if (!lhs_val_.empty()) {
            os << "  LHS      = " << lhs_val_ << '\n';
        }
        if (!rhs_val_.empty()) {
            os << "  RHS      = " << rhs_val_ << '\n';
        }
        if (!extra_.empty()) {
            os << "  Extra message: " << extra_ << '\n';
        }
        std::printf("%s", os.str().c_str());

        State().current_failed = true;
        throw TestAbort{};
    }

    bool failed_ = false;
    bool fatal_ = false;
    std::string where_;
    std::string opname_;
    std::string lhs_expr_;
    std::string rhs_expr_;
    std::string lhs_val_;
    std::string rhs_val_;
    std::string extra_;
};

}  // namespace detail

// ---------------------------------------------------------------------------
// Assertion entry points
// ---------------------------------------------------------------------------

// Expect() is the shared engine for all relational comparisons
// (EXPECT_EQ/NE/LT/LE/GT/GE and their ASSERT_ twins). It runs the
// compile-time-selected comparison and returns a default CheckHandle on
// success; on failure it pre-formats both operands through ValueText() so
// the report is built before any side effects of the enclosing statement
// finish. lhs_expr/rhs_expr are the tokenized operands ("sum", "100")
// while lhs_val/rhs_val are their rendered values, which is why a report
// shows both the source expression and its evaluated text.
template <detail::Op op, typename A, typename B>
detail::CheckHandle Expect(const A& a, const B& b, const char* lhs_expr,
                           const char* rhs_expr, const char* file, int line,
                           bool fatal, const char* opname) {
    if (detail::Compare<op>(a, b)) {
        return detail::CheckHandle{};
    }
    return detail::CheckHandle(
        file, line, opname, lhs_expr, rhs_expr, ValueText(a), ValueText(b),
        fatal);
}

// ExpectTrue() is the predicate engine behind EXPECT_TRUE/ASSERT_TRUE and
// EXPECT_FALSE/ASSERT_FALSE. On success it returns the inert CheckHandle;
// on failure it builds a handle with only a left-side expression and the
// literal value that was found ("false" for *_TRUE, "true" for *_FALSE).
// The condition arrives already cast to bool by the macro, so expressions
// with a conversion operator are tested for truthiness exactly as the
// caller wrote them.
inline detail::CheckHandle ExpectTrue(bool condition, const char* expr,
                                      const char* file, int line, bool fatal,
                                      const char* opname) {
    if (condition) {
        return detail::CheckHandle{};
    }
    return detail::CheckHandle(file, line, opname, expr, nullptr, "false", "",
                               fatal);
}

// ExpectFalse() is the inverse of ExpectTrue: it passes when condition is
// false and otherwise reports a handle whose found value is the literal
// "true". It shares the same single-operand report shape, so output stays
//
//   file:line: Failure: EXPECT_FALSE
//     Expected: (flags & kWrite)     Which is: true
//
// even though the macro writes the intent as "expected false".
inline detail::CheckHandle ExpectFalse(bool condition, const char* expr,
                                       const char* file, int line, bool fatal,
                                       const char* opname) {
    if (!condition) {
        return detail::CheckHandle{};
    }
    return detail::CheckHandle(file, line, opname, expr, nullptr, "true", "",
                               fatal);
}

// ExpectDoubleEq() backs EXPECT_DOUBLE_EQ/ASSERT_DOUBLE_EQ. It collapses
// both operands to double and compares them with the ULP-based
// DoubleNearlyEqual() (4 ULPs) instead of operator==, so tests on guchho's
// hashing and size arithmetic tolerate benign floating-point rounding
// while still flagging genuinely distinct values. The report shows the
// original ValueText() rendering of each operand, not their double casts.
template <typename A, typename B>
detail::CheckHandle ExpectDoubleEq(const A& a, const B& b, const char* lhs_expr,
                                   const char* rhs_expr, const char* file,
                                   int line, bool fatal, const char* opname) {
    const double da = static_cast<double>(a);
    const double db = static_cast<double>(b);
    if (detail::DoubleNearlyEqual(da, db)) {
        return detail::CheckHandle{};
    }
    return detail::CheckHandle(file, line, opname, lhs_expr, rhs_expr,
                               ValueText(a), ValueText(b), fatal);
}

// ExpectNear() backs EXPECT_NEAR/ASSERT_NEAR. It compares |a - b| <= eps
// with an absolute tolerance:
//
//   ExpectNear(1.0, 1.01, 0.02, ...) -> passes
//   ExpectNear(1.0, 1.5, 0.02, ...)  -> fails, report shows both values
//
// All three operands collapse to double, and eps participates in the
// comparison as a literal bound rather than an ULP count, so it is the
// tool of choice for acceptance ranges expressed in real-world units.
template <typename A, typename B, typename E>
detail::CheckHandle ExpectNear(const A& a, const B& b, const E& eps,
                               const char* lhs_expr, const char* rhs_expr,
                               const char* file, int line, bool fatal,
                               const char* opname) {
    if (detail::NearWithin(static_cast<double>(a), static_cast<double>(b),
                           static_cast<double>(eps))) {
        return detail::CheckHandle{};
    }
    return detail::CheckHandle(file, line, opname, lhs_expr, rhs_expr,
                               ValueText(a), ValueText(b), fatal);
}

// ---------------------------------------------------------------------------
// Assertion macros
// ---------------------------------------------------------------------------

// The EXPECT_* family is the non-required-reporting half of the surface:
//
//   EXPECT_TRUE(x)              asserts x converts to true
//   EXPECT_FALSE(x)             asserts x converts to false
//   EXPECT_EQ(a, b)             asserts a == b
//   EXPECT_NE(a, b)             asserts a != b
//   EXPECT_LT(a, b)             asserts a <  b
//   EXPECT_LE(a, b)             asserts a <= b
//   EXPECT_GT(a, b)             asserts a >  b
//   EXPECT_GE(a, b)             asserts a >= b
//   EXPECT_DOUBLE_EQ(a, b)      asserts the doubles are within 4 ULPs
//   EXPECT_NEAR(a, b, eps)      asserts |a - b| <= eps
//
// Every macro forwards its arguments as the actual operands, their
// stringized source text (#a / #b — the report echoes the literal
// expression), __FILE__ and __LINE__, plus the op-name and the fatal flag
// (false here). The expectation of a failing EXPECT_* is naturally to keep
// going, but in this harness a failure terminates the current test via
// CheckHandle's destructor (see CheckHandle), so the report is always the
// first root cause rather than a cascade.
#define EXPECT_TRUE(x)                                                         \
    ::guchho::test::ExpectTrue(static_cast<bool>(x), #x, __FILE__, __LINE__,   \
                               false, "EXPECT_TRUE")
#define EXPECT_FALSE(x)                                                        \
    ::guchho::test::ExpectFalse(static_cast<bool>(x), #x, __FILE__, __LINE__,  \
                                false, "EXPECT_FALSE")
#define EXPECT_EQ(a, b)                                                        \
    ::guchho::test::Expect<::guchho::test::detail::Op::kEq>(                    \
        a, b, #a, #b, __FILE__, __LINE__, false, "EXPECT_EQ")
#define EXPECT_NE(a, b)                                                        \
    ::guchho::test::Expect<::guchho::test::detail::Op::kNe>(                    \
        a, b, #a, #b, __FILE__, __LINE__, false, "EXPECT_NE")
#define EXPECT_LT(a, b)                                                        \
    ::guchho::test::Expect<::guchho::test::detail::Op::kLt>(                    \
        a, b, #a, #b, __FILE__, __LINE__, false, "EXPECT_LT")
#define EXPECT_LE(a, b)                                                        \
    ::guchho::test::Expect<::guchho::test::detail::Op::kLe>(                    \
        a, b, #a, #b, __FILE__, __LINE__, false, "EXPECT_LE")
#define EXPECT_GT(a, b)                                                        \
    ::guchho::test::Expect<::guchho::test::detail::Op::kGt>(                    \
        a, b, #a, #b, __FILE__, __LINE__, false, "EXPECT_GT")
#define EXPECT_GE(a, b)                                                        \
    ::guchho::test::Expect<::guchho::test::detail::Op::kGe>(                    \
        a, b, #a, #b, __FILE__, __LINE__, false, "EXPECT_GE")
#define EXPECT_DOUBLE_EQ(a, b)                                                 \
    ::guchho::test::ExpectDoubleEq(a, b, #a, #b, __FILE__, __LINE__, false,    \
                                   "EXPECT_DOUBLE_EQ")
#define EXPECT_NEAR(a, b, eps)                                                 \
    ::guchho::test::ExpectNear(a, b, eps, #a, #b, __FILE__, __LINE__, false,   \
                               "EXPECT_NEAR")

// The ASSERT_* family is the counterpart to EXPECT_* with the same
// signatures and semantics but the fatal flag set to true:
//
//   ASSERT_TRUE / ASSERT_FALSE / ASSERT_EQ / ASSERT_NE / ASSERT_LT /
//   ASSERT_LE / ASSERT_GT / ASSERT_GE / ASSERT_DOUBLE_EQ / ASSERT_NEAR
//
// The fatal flag is carried from the macro into CheckHandle so future
// policy changes (e.g. letting EXPECT failures continue the test and only
// ASSERT aborting it) can be made in one place; today both report and
// abort identically, so ASSERT_* mainly documents intent to readers
// (failures are unrecoverable preconditions) and keeps the mental model of
// the surface consistent.
#define ASSERT_TRUE(x)                                                         \
    ::guchho::test::ExpectTrue(static_cast<bool>(x), #x, __FILE__, __LINE__,   \
                               true, "ASSERT_TRUE")
#define ASSERT_FALSE(x)                                                        \
    ::guchho::test::ExpectFalse(static_cast<bool>(x), #x, __FILE__, __LINE__,  \
                                true, "ASSERT_FALSE")
#define ASSERT_EQ(a, b)                                                        \
    ::guchho::test::Expect<::guchho::test::detail::Op::kEq>(                    \
        a, b, #a, #b, __FILE__, __LINE__, true, "ASSERT_EQ")
#define ASSERT_NE(a, b)                                                        \
    ::guchho::test::Expect<::guchho::test::detail::Op::kNe>(                    \
        a, b, #a, #b, __FILE__, __LINE__, true, "ASSERT_NE")
#define ASSERT_LT(a, b)                                                        \
    ::guchho::test::Expect<::guchho::test::detail::Op::kLt>(                    \
        a, b, #a, #b, __FILE__, __LINE__, true, "ASSERT_LT")
#define ASSERT_LE(a, b)                                                        \
    ::guchho::test::Expect<::guchho::test::detail::Op::kLe>(                    \
        a, b, #a, #b, __FILE__, __LINE__, true, "ASSERT_LE")
#define ASSERT_GT(a, b)                                                        \
    ::guchho::test::Expect<::guchho::test::detail::Op::kGt>(                    \
        a, b, #a, #b, __FILE__, __LINE__, true, "ASSERT_GT")
#define ASSERT_GE(a, b)                                                        \
    ::guchho::test::Expect<::guchho::test::detail::Op::kGe>(                    \
        a, b, #a, #b, __FILE__, __LINE__, true, "ASSERT_GE")
#define ASSERT_DOUBLE_EQ(a, b)                                                 \
    ::guchho::test::ExpectDoubleEq(a, b, #a, #b, __FILE__, __LINE__, true,     \
                                   "ASSERT_DOUBLE_EQ")
#define ASSERT_NEAR(a, b, eps)                                                 \
    ::guchho::test::ExpectNear(a, b, eps, #a, #b, __FILE__, __LINE__, true,    \
                               "ASSERT_NEAR")

// ---------------------------------------------------------------------------
// Test driver
// ---------------------------------------------------------------------------

// FullName() joins a TestInfo's suite and name with a dot to form the
// canonical test identifier used everywhere in the driver:
//
//   FullName({suite "Scanner", name "RejectsNoInput"}) -> "Scanner.RejectsNoInput"
//
// This exact string is what --run= selects against and what --list-tests
// prints, so a name printed by the binary can be fed straight back into
// --run= to re-execute that single test.
inline std::string FullName(const TestInfo& info) {
    return std::string(info.suite) + "." + info.name;
}

// RunOne() executes a single registered test and accounts for its result.
//
// Flow:
//   [ RUN      ] Scanner.RejectsNoInput
//   [       OK ] Scanner.RejectsNoInput        (all assertions passed)
//   -- or --
//   [ RUN      ] Scanner.RejectsNoInput
//   file.cpp:42: Failure: EXPECT_EQ ...        (report printed by the
//                                       failing CheckHandle destructor)
//   [  FAILED  ] Scanner.RejectsNoInput
//
// The test body is wrapped in try/catch for three outcomes: TestAbort
// (an assertion already reported and threw — nothing to add), a
// std::exception (reported as an aborted test), and anything else (an
// "unknown exception" abort). Only the non-TestAbort cases need to set
// current_failed themselves; the assertion path sets it via Finalize().
// The choice between [ OK ] and [ FAILED ] is then folded into the
// process-wide counters.
inline void RunOne(const TestInfo& info) {
    RunState& state = State();
    state.current = FullName(info);
    state.current_failed = false;

    std::printf("[ RUN      ] %s\n", state.current.c_str());
    try {
        info.fn();
    } catch (const TestAbort&) {
    } catch (const std::exception& e) {
        std::printf("%s: Aborted: uncaught exception: %s\n",
                    state.current.c_str(), e.what());
        state.current_failed = true;
    } catch (...) {
        std::printf("%s: Aborted: unknown exception\n", state.current.c_str());
        state.current_failed = true;
    }

    if (state.current_failed) {
        std::printf("[  FAILED  ] %s\n", state.current.c_str());
        ++state.failed;
    } else {
        std::printf("[       OK ] %s\n", state.current.c_str());
        ++state.passed;
    }
}

// RunAll() is the driver entry point test mains call. It parses the
// command line into a selection policy, then runs the matching tests and
// returns the process exit code.
//
// Recognized arguments:
//   --list-tests           print "Suite.Name" per registered test, no run
//   --run=Suite.Name       run exactly that one test
//   --filter=<substr>      run every test whose full name contains <substr>
//   --gtest_filter=<substr>  same as --filter (compat spelling)
//   -f<substr>             short alias for a filter
//   anything else          ignored, so tools like ctest can append flags
//
// Exit codes: 0 when every selected test passed; 1 when any failed; 1 when
// no selection was requested and nothing matched; 0 when a --run/--filter
// selection simply matched nothing. After the run it prints a summary line
//
//   ==== 12 test(s) run, 11 passed, 1 failed ====
//
// then invokes the finish hook (if installed) while still on the main
// stack, and reports the aggregate failure state.
inline int RunAll(int argc, char** argv) {
    std::vector<std::string> args;
    args.reserve(argc > 0 ? static_cast<size_t>(argc - 1) : 0);
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    std::vector<std::string> diagnostics;
    bool list_tests = false;
    bool single_run = false;
    std::string run_name;
    std::string filter;

    for (const std::string& arg : args) {
        if (arg == "--list-tests") {
            list_tests = true;
        } else if (arg.rfind("--run=", 0) == 0) {
            single_run = true;
            run_name = arg.substr(6);
        } else if (arg.rfind("--filter=", 0) == 0) {
            filter = arg.substr(9);
        } else if (arg.rfind("--gtest_filter=", 0) == 0) {
            filter = arg.substr(15);
        } else if (arg.rfind("-f", 0) == 0) {
            filter = arg.substr(2);
        } else {
        }
    }

    const std::vector<TestInfo>& registry = Registry();

    if (list_tests) {
        for (const TestInfo& info : registry) {
            std::printf("%s.%s\n", info.suite, info.name);
        }
        return 0;
    }

    std::vector<const TestInfo*> selected;
    for (const TestInfo& info : registry) {
        const std::string full = FullName(info);
        bool match = true;
        if (single_run) {
            match = full == run_name;
        } else if (!filter.empty()) {
            match = full.find(filter) != std::string::npos;
        }
        if (match) {
            selected.push_back(&info);
        }
    }

    if (selected.empty()) {
        std::printf("No tests found.\n");
        return filter.empty() && !single_run ? 1 : 0;
    }

    RunState& state = State();
    for (const TestInfo* info : selected) {
        RunOne(*info);
    }

    const int total = state.passed + state.failed;
    std::printf("\n==== %d test(s) run, %d passed, %d failed ====\n", total,
                state.passed, state.failed);
    if (FinishHookRef() != nullptr) {
        FinishHookRef()();
    }
    return state.failed == 0 ? 0 : 1;
}

// RandomSeed() yields a stable pseudo-random 32-bit seed for tests that
// need scratch data isolated per process. The seed is computed once from
// the high-resolution clock plus the address of the local, so two runs of
// the same binary get different namespaces while a single run holds one
// value for all tests (important for reproducible within-run behavior).
//
//   RandomSeed() -> e.g. 0x9E3779B9 (varies per process, constant per run)
//
// Tests use it to derive unique temporary paths so concurrent guchho
// builds under the same sandbox do not collide on generated fixtures.
inline std::uint32_t RandomSeed() {
    static const std::uint32_t seed = [] {
        const auto now =
            std::chrono::high_resolution_clock::now().time_since_epoch().count();
        return static_cast<std::uint32_t>(
            now ^ static_cast<std::int64_t>(
                      reinterpret_cast<std::uintptr_t>(&now)));
    }();
    return seed;
}

}  // namespace test
}  // namespace guchho