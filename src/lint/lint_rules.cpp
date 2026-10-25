#include "guchho/lint.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace guchho::lint {

namespace {

constexpr std::array<RuleInfo, kRuleCount> kRules{{
    {Rule::kNoUndef, "no-undef", Severity::kError, false},
    {Rule::kNoUnusedVars, "no-unused-vars", Severity::kWarning, false},
    {Rule::kNoRedeclare, "no-redeclare", Severity::kError, false},
    {Rule::kNoUseBeforeDefine, "no-use-before-define", Severity::kOff, false},
    {Rule::kNoShadow, "no-shadow", Severity::kOff, false},
    {Rule::kNoUnreachable, "no-unreachable", Severity::kError, false},
    {Rule::kNoDuplicateImports, "no-duplicate-imports", Severity::kError, false},
    {Rule::kNoConstantCondition, "no-constant-condition", Severity::kWarning, false},
    {Rule::kValidTypeof, "valid-typeof", Severity::kError, false},
    {Rule::kNoConsole, "no-console", Severity::kOff, false},
    {Rule::kEqeqeq, "eqeqeq", Severity::kWarning, true},
    {Rule::kNoDebugger, "no-debugger", Severity::kError, true},
    {Rule::kNoDuplicateCase, "no-duplicate-case", Severity::kError, false},
    {Rule::kNoEmpty, "no-empty", Severity::kWarning, false},
    {Rule::kImportNoUnresolved, "import/no-unresolved", Severity::kError, false},
}};

std::string Lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

size_t EditDistance(std::string_view a, std::string_view b) {
    std::vector<size_t> prev(b.size() + 1);
    std::vector<size_t> curr(b.size() + 1);
    for (size_t j = 0; j <= b.size(); j++) prev[j] = j;
    for (size_t i = 1; i <= a.size(); i++) {
        curr[0] = i;
        for (size_t j = 1; j <= b.size(); j++) {
            size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, curr);
    }
    return prev[b.size()];
}

} // namespace

std::optional<Severity> ParseSeverity(std::string_view text) {
    const std::string lowered = Lower(text);
    if (lowered == "off" || lowered == "0") return Severity::kOff;
    if (lowered == "warn" || lowered == "warning" || lowered == "1") {
        return Severity::kWarning;
    }
    if (lowered == "error" || lowered == "2") return Severity::kError;
    return std::nullopt;
}

std::string_view SeverityName(Severity severity) {
    switch (severity) {
    case Severity::kOff: return "off";
    case Severity::kWarning: return "warning";
    case Severity::kError: return "error";
    }
    return "warning";
}

const std::array<RuleInfo, kRuleCount>& AllRules() {
    return kRules;
}

std::optional<Rule> RuleFromName(std::string_view name) {
    for (const RuleInfo& info : kRules) {
        if (info.name == name) return info.id;
    }
    return std::nullopt;
}

std::string_view RuleName(Rule rule) {
    const size_t index = static_cast<size_t>(rule);
    if (index >= kRules.size()) return "";
    return kRules[index].name;
}

std::string SuggestRuleName(std::string_view name) {
    std::string best;
    size_t best_distance = std::string::npos;
    for (const RuleInfo& info : kRules) {
        const size_t distance = EditDistance(name, info.name);
        if (distance < best_distance) {
            best_distance = distance;
            best = std::string(info.name);
        }
    }
    if (best.empty() || best_distance > 3) return {};
    return best;
}

Severity DefaultSeverity(Rule rule) {
    const size_t index = static_cast<size_t>(rule);
    if (index >= kRules.size()) return Severity::kOff;
    return kRules[index].default_severity;
}

} // namespace guchho::lint
