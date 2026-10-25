#include "guchho/lint.hpp"

#include "guchho/helpers.hpp"
#include "guchho/javascript/js_parser.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace guchho::lint::detail {

namespace {

using javascript::EArray;
using javascript::EArrow;
using javascript::EBigInt;
using javascript::EBoolean;
using javascript::EClass;
using javascript::EIf;
using javascript::EImportIdentifier;
using javascript::EInlinedEnum;
using javascript::EAnnotation;
using javascript::EAwait;
using javascript::EBinary;
using javascript::EBinary;
using javascript::ECall;
using javascript::EDot;
using javascript::EFunction;
using javascript::EIdentifier;
using javascript::EIndex;
using javascript::EJSXElement;
using javascript::EMissing;
using javascript::ENew;
using javascript::ENull;
using javascript::ENumber;
using javascript::EObject;
using javascript::ERegExp;
using javascript::ESpread;
using javascript::EString;
using javascript::ETemplate;
using javascript::EUndefined;
using javascript::EUnary;
using javascript::EYield;
using javascript::LintData;
using javascript::SBlock;
using javascript::SBreak;
using javascript::SClass;
using javascript::SContinue;
using javascript::SDebugger;
using javascript::SDoWhile;
using javascript::SEnum;
using javascript::SExportDefault;
using javascript::SExportEquals;
using javascript::SExpr;
using javascript::SFor;
using javascript::SForIn;
using javascript::SForOf;
using javascript::SFunction;
using javascript::SIf;
using javascript::SLabel;
using javascript::SLazyExport;
using javascript::SLocal;
using javascript::SNamespace;
using javascript::SReturn;
using javascript::SSwitch;
using javascript::SThrow;
using javascript::STry;
using javascript::SWhile;
using javascript::SWith;

template <typename T>
const T* Get(const javascript::Expr& expr) {
    const auto* value = std::get_if<std::shared_ptr<T>>(&expr.data);
    return value != nullptr && *value != nullptr ? value->get() : nullptr;
}

template <typename T>
const T* GetStmt(const javascript::Stmt& stmt) {
    const auto* value = std::get_if<std::shared_ptr<T>>(&stmt.data);
    return value != nullptr && *value != nullptr ? value->get() : nullptr;
}

bool ExprPresent(const javascript::Expr& expr) {
    if (std::holds_alternative<std::shared_ptr<EMissing>>(expr.data)) return false;
    return std::visit([](const auto& pointer) { return pointer != nullptr; },
                      expr.data);
}

std::string Slice(const std::string& source, logger::Loc loc, int32_t len) {
    if (loc.start < 0 || static_cast<size_t>(loc.start) > source.size()) {
        return {};
    }
    const size_t start = static_cast<size_t>(loc.start);
    const size_t count = len < 0
                             ? source.size() - start
                             : std::min<size_t>(static_cast<size_t>(len),
                                                source.size() - start);
    return source.substr(start, count);
}

int32_t IdentifierLength(const std::string& source, logger::Loc loc) {
    if (loc.start < 0 || static_cast<size_t>(loc.start) >= source.size()) return 1;
    size_t i = static_cast<size_t>(loc.start);
    while (i < source.size()) {
        const unsigned char c = static_cast<unsigned char>(source[i]);
        if (std::isalnum(c) || c == '_' || c == '$' || c >= 0x80) {
            i++;
        } else {
            break;
        }
    }
    return i == static_cast<size_t>(loc.start)
               ? 1
               : static_cast<int32_t>(i - static_cast<size_t>(loc.start));
}

bool IsComparisonOp(javascript::OpCode op) {
    switch (op) {
    case javascript::OpCode::kBinOpLooseEq:
    case javascript::OpCode::kBinOpLooseNe:
    case javascript::OpCode::kBinOpStrictEq:
    case javascript::OpCode::kBinOpStrictNe:
        return true;
    default:
        return false;
    }
}

void SkipString(const std::string& source, size_t& i) {
    const char quote = source[i++];
    while (i < source.size()) {
        if (source[i] == '\\') {
            i += 2;
        } else if (source[i] == quote) {
            i++;
            return;
        } else {
            i++;
        }
    }
}

std::optional<logger::Range> FindLooseOperatorRange(const std::string& source,
                                                    logger::Loc left_start,
                                                    logger::Loc right_start) {
    if (left_start.start < 0 || right_start.start <= left_start.start) {
        return std::nullopt;
    }
    const size_t begin = static_cast<size_t>(left_start.start);
    const size_t end = std::min<size_t>(static_cast<size_t>(right_start.start),
                                        source.size());
    int depth = 0;
    std::optional<size_t> found;
    for (size_t i = begin; i < end;) {
        const char c = source[i];
        if (c == '/' && i + 1 < end && source[i + 1] == '/') {
            i += 2;
            while (i < end && source[i] != '\n') i++;
            continue;
        }
        if (c == '/' && i + 1 < end && source[i + 1] == '*') {
            i += 2;
            while (i + 1 < end && !(source[i] == '*' && source[i + 1] == '/')) i++;
            i = std::min(end, i + 2);
            continue;
        }
        if (c == '\'' || c == '"' || c == '`') {
            SkipString(source, i);
            continue;
        }
        if (c == '(' || c == '[' || c == '{') {
            depth++;
            i++;
            continue;
        }
        if (c == ')' || c == ']' || c == '}') {
            depth = std::max(0, depth - 1);
            i++;
            continue;
        }
        if (depth == 0 && (c == '=' || c == '!') && i + 1 < end &&
            source[i + 1] == '=') {
            const bool loose = i + 2 >= end || source[i + 2] != '=';
            const bool is_eq = c == '=';
            const bool valid = is_eq || (i == begin || source[i - 1] != '!');
            if (loose && valid) {
                if (c == '!' && i > begin && source[i - 1] == '=') {
                    i += 2;
                    continue;
                }
                found = i;
                i += 2;
                continue;
            }
        }
        i++;
    }
    if (!found) return std::nullopt;
    return logger::Range{logger::Loc{static_cast<int32_t>(*found)}, 2};
}

std::string Utf16ToUtf8(const std::u16string& value) {
    return helpers::UTF16ToString(value);
}

bool ConstantExpr(const javascript::Expr& expr);

bool ConstantUnary(javascript::OpCode op, const javascript::Expr& value) {
    switch (op) {
    case javascript::OpCode::kUnOpPos:
    case javascript::OpCode::kUnOpNeg:
    case javascript::OpCode::kUnOpCpl:
    case javascript::OpCode::kUnOpNot:
    case javascript::OpCode::kUnOpVoid:
    case javascript::OpCode::kUnOpTypeof:
        return ConstantExpr(value);
    default:
        return false;
    }
}

bool ConstantExpr(const javascript::Expr& expr) {
    if (!ExprPresent(expr)) return false;
    if (Get<EBoolean>(expr) || Get<ENull>(expr) || Get<EUndefined>(expr) ||
        Get<ENumber>(expr) || Get<EBigInt>(expr) || Get<EString>(expr) ||
        Get<ERegExp>(expr) || Get<EArray>(expr) || Get<EObject>(expr) ||
        Get<EFunction>(expr) || Get<EArrow>(expr) || Get<EClass>(expr)) {
        return true;
    }
    if (const auto* tmpl = Get<ETemplate>(expr)) {
        if (tmpl->parts.empty()) return true;
        for (const javascript::TemplatePart& part : tmpl->parts) {
            if (!ConstantExpr(part.value)) return false;
        }
        return true;
    }
    if (const auto* unary = Get<EUnary>(expr)) {
        return ConstantUnary(unary->op, unary->value);
    }
    if (const auto* binary = Get<EBinary>(expr)) {
        switch (binary->op) {
        case javascript::OpCode::kBinOpAdd:
        case javascript::OpCode::kBinOpSub:
        case javascript::OpCode::kBinOpMul:
        case javascript::OpCode::kBinOpDiv:
        case javascript::OpCode::kBinOpRem:
        case javascript::OpCode::kBinOpPow:
        case javascript::OpCode::kBinOpLt:
        case javascript::OpCode::kBinOpLe:
        case javascript::OpCode::kBinOpGt:
        case javascript::OpCode::kBinOpGe:
        case javascript::OpCode::kBinOpIn:
        case javascript::OpCode::kBinOpInstanceof:
        case javascript::OpCode::kBinOpShl:
        case javascript::OpCode::kBinOpShr:
        case javascript::OpCode::kBinOpUShr:
        case javascript::OpCode::kBinOpLooseEq:
        case javascript::OpCode::kBinOpLooseNe:
        case javascript::OpCode::kBinOpStrictEq:
        case javascript::OpCode::kBinOpStrictNe:
        case javascript::OpCode::kBinOpNullishCoalescing:
        case javascript::OpCode::kBinOpLogicalOr:
        case javascript::OpCode::kBinOpLogicalAnd:
        case javascript::OpCode::kBinOpBitwiseOr:
        case javascript::OpCode::kBinOpBitwiseAnd:
        case javascript::OpCode::kBinOpBitwiseXor:
            return ConstantExpr(binary->left) && ConstantExpr(binary->right);
        default:
            return false;
        }
    }
    if (const auto* conditional = Get<EIf>(expr)) {
        return ConstantExpr(conditional->test) &&
               ConstantExpr(conditional->yes) &&
               ConstantExpr(conditional->no);
    }
    if (const auto* annotation = Get<EAnnotation>(expr)) {
        return ConstantExpr(annotation->value);
    }
    if (const auto* inlined = Get<EInlinedEnum>(expr)) {
        return ConstantExpr(inlined->value);
    }
    return false;
}

bool IsConstantCondition(const javascript::Expr& expr) {
    return ExprPresent(expr) && ConstantExpr(expr);
}

bool IsStringLiteral(const javascript::Expr& expr, std::string& out) {
    if (const auto* string = Get<EString>(expr)) {
        out = Utf16ToUtf8(string->value);
        return true;
    }
    return false;
}

bool IsTypeofExpr(const javascript::Expr& expr) {
    const auto* unary = Get<EUnary>(expr);
    return unary != nullptr && unary->op == javascript::OpCode::kUnOpTypeof;
}

bool IsStringLikeExpr(const javascript::Expr& expr) {
    return Get<EString>(expr) != nullptr || IsTypeofExpr(expr);
}

enum class LiteralCategory {
    kNone,
    kBoolean,
    kNull,
    kUndefined,
    kNumber,
    kBigInt,
    kString,
    kRegExp,
};

LiteralCategory LiteralCategoryOf(const javascript::Expr& expr) {
    if (Get<EBoolean>(expr) != nullptr) return LiteralCategory::kBoolean;
    if (Get<ENull>(expr) != nullptr) return LiteralCategory::kNull;
    if (Get<EUndefined>(expr) != nullptr) return LiteralCategory::kUndefined;
    if (Get<ENumber>(expr) != nullptr) return LiteralCategory::kNumber;
    if (Get<EBigInt>(expr) != nullptr) return LiteralCategory::kBigInt;
    if (Get<EString>(expr) != nullptr) return LiteralCategory::kString;
    if (Get<ERegExp>(expr) != nullptr) return LiteralCategory::kRegExp;
    return LiteralCategory::kNone;
}

bool StrictEqualityFixIsSafe(const javascript::Expr& left,
                             const javascript::Expr& right) {
    if ((IsTypeofExpr(left) && IsStringLikeExpr(right)) ||
        (IsTypeofExpr(right) && IsStringLikeExpr(left))) {
        return true;
    }
    const LiteralCategory left_kind = LiteralCategoryOf(left);
    const LiteralCategory right_kind = LiteralCategoryOf(right);
    return left_kind != LiteralCategory::kNone && left_kind == right_kind;
}

bool IsValidTypeofString(const std::string& value) {
    static const std::unordered_set<std::string> values = {
        "undefined", "object", "boolean", "number", "string",
        "function", "symbol", "bigint",
    };
    return values.contains(value);
}

bool CaseKey(const javascript::Expr& expr, std::string& key) {
    if (const auto* string = Get<EString>(expr)) {
        key = "s:" + Utf16ToUtf8(string->value);
        return true;
    }
    if (const auto* number = Get<ENumber>(expr)) {
        if (std::isnan(number->value)) return false;
        key = "n:" + std::to_string(number->value);
        return true;
    }
    if (const auto* bigint = Get<EBigInt>(expr)) {
        key = "b:" + bigint->value;
        return true;
    }
    if (const auto* boolean = Get<EBoolean>(expr)) {
        key = boolean->value ? "bool:true" : "bool:false";
        return true;
    }
    if (Get<ENull>(expr)) {
        key = "null";
        return true;
    }
    if (Get<EUndefined>(expr)) {
        key = "undefined";
        return true;
    }
    return false;
}

bool IsConsoleRoot(const std::string& source, const javascript::Expr& expr,
                   logger::Loc& root_loc) {
    if (Get<EIdentifier>(expr) != nullptr) {
        root_loc = expr.loc;
        if (Slice(source, root_loc, 7) != "console") return false;
        const size_t next = static_cast<size_t>(root_loc.start) + 7;
        if (next < source.size()) {
            const unsigned char c = static_cast<unsigned char>(source[next]);
            if (std::isalnum(c) || c == '_' || c == '$') return false;
        }
        return true;
    }
    if (const auto* dot = Get<EDot>(expr)) {
        return IsConsoleRoot(source, dot->target, root_loc);
    }
    if (const auto* index = Get<EIndex>(expr)) {
        return IsConsoleRoot(source, index->target, root_loc);
    }
    if (const auto* call = Get<ECall>(expr)) {
        return IsConsoleRoot(source, call->target, root_loc);
    }
    return false;
}

struct Visitor {
    const std::string& path;
    const std::string& source;
    const Config& config;
    Analysis& analysis;
    std::unordered_set<int32_t> reported_console_roots{};
    std::unordered_set<std::string> seen{};

    Severity RuleSeverity(Rule rule) const {
        const std::string name(RuleName(rule));
        auto it = config.rules.find(name);
        return it == config.rules.end() ? DefaultSeverity(rule) : it->second;
    }

    void Add(Rule rule, logger::Range range, std::string message,
             std::vector<logger::Fix> fixes = {}) {
        const Severity severity = RuleSeverity(rule);
        if (severity == Severity::kOff) return;
        if (range.loc.start < 0) range.loc.start = 0;
        SourcePosition start = PositionForOffset(source, range.loc.start);
        SourcePosition end = PositionForOffset(
            source, range.loc.start + std::max(range.len, 0));
        const std::string key = std::to_string(static_cast<int>(rule)) + ":" +
                                std::to_string(start.line) + ":" +
                                std::to_string(start.column) + ":" + message;
        if (!seen.insert(key).second) return;

        Diagnostic diagnostic;
        diagnostic.path = path;
        diagnostic.rule_id = std::string(RuleName(rule));
        diagnostic.severity = severity;
        diagnostic.message = std::move(message);
        diagnostic.line = start.line;
        diagnostic.column = start.column;
        diagnostic.end_line = end.line;
        diagnostic.end_column = end.column;
        diagnostic.fixes = std::move(fixes);
        analysis.report.diagnostics.push_back(std::move(diagnostic));
    }

    void AddAt(Rule rule, logger::Loc loc, std::string message) {
        Add(rule, logger::Range{loc, IdentifierLength(source, loc)},
            std::move(message));
    }

    void VisitBinding(const javascript::Binding& binding) {
        if (const auto* array = std::get_if<std::shared_ptr<javascript::BArray>>(
                &binding.data);
            array != nullptr && *array != nullptr) {
            for (const javascript::ArrayBinding& item : (*array)->items) {
                if (ExprPresent(item.default_value_or_nil)) {
                    VisitExpr(item.default_value_or_nil);
                }
                VisitBinding(item.binding);
            }
            return;
        }
        if (const auto* object = std::get_if<std::shared_ptr<javascript::BObject>>(
                &binding.data);
            object != nullptr && *object != nullptr) {
            for (const javascript::PropertyBinding& property :
                 (*object)->properties) {
                if (property.is_computed) VisitExpr(property.key);
                if (ExprPresent(property.default_value_or_nil)) {
                    VisitExpr(property.default_value_or_nil);
                }
                VisitBinding(property.value);
            }
        }
    }

    void VisitArg(const javascript::Arg& arg) {
        VisitBinding(arg.binding);
        if (ExprPresent(arg.default_or_nil)) VisitExpr(arg.default_or_nil);
        for (const javascript::Decorator& decorator : arg.decorators) {
            VisitExpr(decorator.value);
        }
    }

    void VisitDecorator(const javascript::Decorator& decorator) {
        VisitExpr(decorator.value);
    }

    void VisitProperty(const javascript::Property& property) {
        for (const javascript::Decorator& decorator : property.decorators) {
            VisitDecorator(decorator);
        }
        if (property.class_static_block != nullptr) {
            VisitBlock(property.class_static_block->block, true,
                       property.class_static_block->loc);
        }
        VisitExpr(property.key);
        if (ExprPresent(property.value_or_nil)) VisitExpr(property.value_or_nil);
        if (ExprPresent(property.initializer_or_nil)) {
            VisitExpr(property.initializer_or_nil);
        }
    }

    void VisitClass(const javascript::Class& class_) {
        for (const javascript::Decorator& decorator : class_.decorators) {
            VisitDecorator(decorator);
        }
        if (ExprPresent(class_.extends_or_nil)) VisitExpr(class_.extends_or_nil);
        for (const javascript::Property& property : class_.properties) {
            VisitProperty(property);
        }
    }

    void VisitFn(const javascript::Fn& fn) {
        for (const javascript::Arg& arg : fn.args) {
            VisitArg(arg);
        }
        for (const javascript::Stmt& stmt : fn.body.block.stmts) {
            VisitStmt(stmt, false);
        }
        AnalyzeStmtList(fn.body.block.stmts);
    }

    void VisitBlock(const SBlock& block, bool report_empty, logger::Loc loc) {
        if (block.stmts.empty() && report_empty) {
            Add(Rule::kNoEmpty, logger::Range{loc, 1}, "Empty block statement.");
        }
        AnalyzeStmtList(block.stmts);
        for (const javascript::Stmt& stmt : block.stmts) {
            VisitStmt(stmt, true);
        }
    }

    void AnalyzeStmtList(const std::vector<javascript::Stmt>& stmts) {
        bool terminated = false;
        bool reported = false;
        for (const javascript::Stmt& stmt : stmts) {
            if (terminated && !reported) {
                Add(Rule::kNoUnreachable,
                    logger::Range{stmt.loc, 1}, "Unreachable code.");
                reported = true;
            }
            if (StmtTerminates(stmt)) terminated = true;
        }
    }

    bool StmtTerminates(const javascript::Stmt& stmt) {
        if (GetStmt<SReturn>(stmt) || GetStmt<SThrow>(stmt) ||
            GetStmt<SBreak>(stmt) || GetStmt<SContinue>(stmt)) {
            return true;
        }
        if (const auto* block = GetStmt<SBlock>(stmt)) {
            return BlockTerminates(*block);
        }
        if (const auto* conditional = GetStmt<SIf>(stmt)) {
            return conditional->no_or_nil != nullptr &&
                   StmtTerminates(*conditional->no_or_nil) &&
                   StmtTerminates(conditional->yes);
        }
        if (const auto* label = GetStmt<SLabel>(stmt)) {
            return StmtTerminates(label->stmt);
        }
        if (const auto* tried = GetStmt<STry>(stmt)) {
            if (tried->finally_block != nullptr &&
                BlockTerminates(tried->finally_block->block)) {
                return true;
            }
            if (!BlockTerminates(tried->block)) return false;
            if (tried->catch_block == nullptr) return true;
            return BlockTerminates(tried->catch_block->block);
        }
        return false;
    }

    bool BlockTerminates(const SBlock& block) {
        for (const javascript::Stmt& stmt : block.stmts) {
            if (StmtTerminates(stmt)) return true;
        }
        return false;
    }

    void VisitStmtList(const std::vector<javascript::Stmt>& stmts,
                       bool direct) {
        AnalyzeStmtList(stmts);
        for (const javascript::Stmt& stmt : stmts) {
            VisitStmt(stmt, direct);
        }
    }

    void VisitStmt(const javascript::Stmt& stmt, bool direct) {
        if (std::holds_alternative<std::shared_ptr<SDebugger>>(stmt.data)) {
            std::vector<logger::Fix> fixes;
            if (direct) {
                const size_t begin = static_cast<size_t>(std::max(stmt.loc.start, 0));
                size_t end = begin;
                if (source.compare(end, 8, "debugger") == 0) end += 8;
                while (end < source.size() &&
                       (source[end] == ' ' || source[end] == '\t')) {
                    end++;
                }
                if (end < source.size() && source[end] == ';') end++;
                size_t line_end = end;
                while (line_end < source.size() &&
                       (source[line_end] == ' ' || source[line_end] == '\t' ||
                        source[line_end] == '\r')) {
                    line_end++;
                }
                if (line_end < source.size() && source[line_end] == '\n') {
                    end = line_end + 1;
                } else if (line_end == source.size()) {
                    end = line_end;
                }
                const int32_t len = static_cast<int32_t>(end - begin);
                if (len > 0) {
                    fixes.push_back(logger::Fix{
                        logger::Range{stmt.loc, len}, "",
                        logger::FixSafety::kSafe,
                        "Remove the debugger statement",
                    });
                }
            }
            Add(Rule::kNoDebugger, logger::Range{stmt.loc, 8},
                "Unexpected debugger statement.", std::move(fixes));
            return;
        }

        if (const auto* block = GetStmt<SBlock>(stmt)) {
            VisitBlock(*block, true, stmt.loc);
            return;
        }
        if (const auto* expr = GetStmt<SExpr>(stmt)) {
            VisitExpr(expr->value);
            return;
        }
        if (const auto* conditional = GetStmt<SIf>(stmt)) {
            VisitExpr(conditional->test);
            if (IsConstantCondition(conditional->test)) {
                Add(Rule::kNoConstantCondition,
                    logger::Range{conditional->test.loc, 1},
                    "Unexpected constant condition.");
            }
            VisitStmt(conditional->yes, false);
            if (conditional->no_or_nil != nullptr) {
                VisitStmt(*conditional->no_or_nil, false);
            }
            return;
        }
        if (const auto* loop = GetStmt<SFor>(stmt)) {
            if (loop->init_or_nil != nullptr) VisitStmt(*loop->init_or_nil, false);
            if (ExprPresent(loop->test_or_nil)) {
                VisitExpr(loop->test_or_nil);
                if (IsConstantCondition(loop->test_or_nil)) {
                    Add(Rule::kNoConstantCondition,
                        logger::Range{loop->test_or_nil.loc, 1},
                        "Unexpected constant condition.");
                }
            }
            if (ExprPresent(loop->update_or_nil)) VisitExpr(loop->update_or_nil);
            VisitStmt(loop->body, false);
            return;
        }
        if (const auto* loop = GetStmt<SForIn>(stmt)) {
            if (loop->init != nullptr) VisitStmt(*loop->init, false);
            VisitExpr(loop->value);
            VisitStmt(loop->body, false);
            return;
        }
        if (const auto* loop = GetStmt<SForOf>(stmt)) {
            if (loop->init != nullptr) VisitStmt(*loop->init, false);
            VisitExpr(loop->value);
            VisitStmt(loop->body, false);
            return;
        }
        if (const auto* loop = GetStmt<SDoWhile>(stmt)) {
            VisitStmt(loop->body, false);
            VisitExpr(loop->test);
            if (IsConstantCondition(loop->test)) {
                Add(Rule::kNoConstantCondition,
                    logger::Range{loop->test.loc, 1},
                    "Unexpected constant condition.");
            }
            return;
        }
        if (const auto* loop = GetStmt<SWhile>(stmt)) {
            VisitExpr(loop->test);
            if (IsConstantCondition(loop->test)) {
                Add(Rule::kNoConstantCondition,
                    logger::Range{loop->test.loc, 1},
                    "Unexpected constant condition.");
            }
            VisitStmt(loop->body, false);
            return;
        }
        if (const auto* with = GetStmt<SWith>(stmt)) {
            VisitExpr(with->value);
            VisitStmt(with->body, false);
            return;
        }
        if (const auto* tried = GetStmt<STry>(stmt)) {
            VisitBlock(tried->block, true, tried->block_loc);
            if (tried->catch_block != nullptr) {
                VisitBinding(tried->catch_block->binding_or_nil);
                VisitBlock(tried->catch_block->block, true,
                           tried->catch_block->block_loc);
            }
            if (tried->finally_block != nullptr) {
                VisitBlock(tried->finally_block->block, true,
                           tried->finally_block->loc);
            }
            return;
        }
        if (const auto* switch_stmt = GetStmt<SSwitch>(stmt)) {
            VisitExpr(switch_stmt->test);
            std::unordered_set<std::string> keys;
            for (const javascript::Case& case_ : switch_stmt->cases) {
                if (ExprPresent(case_.value_or_nil)) {
                    VisitExpr(case_.value_or_nil);
                    std::string key;
                    if (CaseKey(case_.value_or_nil, key) &&
                        !keys.insert(key).second) {
                        Add(Rule::kNoDuplicateCase,
                            logger::Range{case_.value_or_nil.loc, 1},
                            "Duplicate case label.");
                    }
                }
                AnalyzeStmtList(case_.body);
                for (const javascript::Stmt& body_stmt : case_.body) {
                    VisitStmt(body_stmt, true);
                }
            }
            return;
        }
        if (const auto* local = GetStmt<SLocal>(stmt)) {
            for (const javascript::Decl& decl : local->decls) {
                VisitBinding(decl.binding);
                if (ExprPresent(decl.value_or_nil)) VisitExpr(decl.value_or_nil);
            }
            return;
        }
        if (const auto* fn = GetStmt<SFunction>(stmt)) {
            VisitFn(fn->fn);
            return;
        }
        if (const auto* class_ = GetStmt<SClass>(stmt)) {
            VisitClass(class_->class_);
            return;
        }
        if (const auto* label = GetStmt<SLabel>(stmt)) {
            VisitStmt(label->stmt, false);
            return;
        }
        if (const auto* exported = GetStmt<SExportDefault>(stmt)) {
            VisitStmt(exported->value, false);
            return;
        }
        if (const auto* exported = GetStmt<SExportEquals>(stmt)) {
            VisitExpr(exported->value);
            return;
        }
        if (const auto* exported = GetStmt<SLazyExport>(stmt)) {
            VisitExpr(exported->value);
            return;
        }
        if (const auto* returned = GetStmt<SReturn>(stmt)) {
            if (ExprPresent(returned->value_or_nil)) {
                VisitExpr(returned->value_or_nil);
            }
            return;
        }
        if (const auto* thrown = GetStmt<SThrow>(stmt)) {
            VisitExpr(thrown->value);
            return;
        }
        if (const auto* enum_ = GetStmt<SEnum>(stmt)) {
            for (const javascript::EnumValue& value : enum_->values) {
                if (ExprPresent(value.value_or_nil)) VisitExpr(value.value_or_nil);
            }
            return;
        }
        if (const auto* namespace_ = GetStmt<SNamespace>(stmt)) {
            AnalyzeStmtList(namespace_->stmts);
            for (const javascript::Stmt& inner : namespace_->stmts) {
                VisitStmt(inner, true);
            }
            return;
        }
    }

    void VisitExpr(const javascript::Expr& expr) {
        if (!ExprPresent(expr)) return;

        if (const auto* array = Get<EArray>(expr)) {
            for (const javascript::Expr& item : array->items) VisitExpr(item);
            return;
        }
        if (const auto* unary = Get<EUnary>(expr)) {
            VisitExpr(unary->value);
            return;
        }
        if (const auto* binary = Get<EBinary>(expr)) {
            VisitExpr(binary->left);
            VisitExpr(binary->right);
            const bool loose = binary->op == javascript::OpCode::kBinOpLooseEq ||
                               binary->op == javascript::OpCode::kBinOpLooseNe;
            if (loose) {
                const bool is_eq = binary->op == javascript::OpCode::kBinOpLooseEq;
                const std::string expected = is_eq ? "===" : "!==";
                const std::string actual = is_eq ? "==" : "!=";
                std::vector<logger::Fix> fixes;
                if (auto op_range = FindLooseOperatorRange(
                        source, binary->left.loc, binary->right.loc)) {
                    if (StrictEqualityFixIsSafe(binary->left, binary->right)) {
                        fixes.push_back(logger::Fix{
                            *op_range, expected, logger::FixSafety::kSafe,
                            "Replace '" + actual + "' with '" + expected + "'",
                        });
                    }
                    Add(Rule::kEqeqeq, *op_range,
                        "Expected '" + expected + "' and instead saw '" + actual +
                            "'.",
                        std::move(fixes));
                } else {
                    Add(Rule::kEqeqeq,
                        logger::Range{binary->left.loc,
                                      IdentifierLength(source, binary->left.loc)},
                        "Expected '" + expected + "' and instead saw '" + actual +
                            "'.");
                }
            }
            if (IsComparisonOp(binary->op)) {
                const EUnary* typeof_side = Get<EUnary>(binary->left);
                const javascript::Expr* other = &binary->right;
                if (typeof_side == nullptr ||
                    typeof_side->op != javascript::OpCode::kUnOpTypeof) {
                    typeof_side = Get<EUnary>(binary->right);
                    other = &binary->left;
                }
                if (typeof_side != nullptr &&
                    typeof_side->op == javascript::OpCode::kUnOpTypeof) {
                    std::string value;
                    if (IsStringLiteral(*other, value) &&
                        !IsValidTypeofString(value)) {
                        Add(Rule::kValidTypeof,
                            logger::Range{other->loc,
                                          IdentifierLength(source, other->loc)},
                            "Invalid typeof comparison value \"" + value + "\".");
                    }
                }
            }
            return;
        }
        if (const auto* new_expr = Get<ENew>(expr)) {
            VisitExpr(new_expr->target);
            for (const javascript::Expr& arg : new_expr->args) VisitExpr(arg);
            return;
        }
        if (const auto* call = Get<ECall>(expr)) {
            logger::Loc root_loc;
            if (IsConsoleRoot(source, call->target, root_loc) &&
                reported_console_roots.insert(root_loc.start).second) {
                Add(Rule::kNoConsole, logger::Range{root_loc, 7},
                    "Unexpected console statement.");
            }
            VisitExpr(call->target);
            for (const javascript::Expr& arg : call->args) VisitExpr(arg);
            return;
        }
        if (const auto* dot = Get<EDot>(expr)) {
            logger::Loc root_loc;
            if (IsConsoleRoot(source, dot->target, root_loc) &&
                reported_console_roots.insert(root_loc.start).second) {
                Add(Rule::kNoConsole, logger::Range{root_loc, 7},
                    "Unexpected console statement.");
            }
            VisitExpr(dot->target);
            return;
        }
        if (const auto* index = Get<EIndex>(expr)) {
            logger::Loc root_loc;
            if (IsConsoleRoot(source, index->target, root_loc) &&
                reported_console_roots.insert(root_loc.start).second) {
                Add(Rule::kNoConsole, logger::Range{root_loc, 7},
                    "Unexpected console statement.");
            }
            VisitExpr(index->target);
            VisitExpr(index->index);
            return;
        }
        if (const auto* arrow = Get<EArrow>(expr)) {
            for (const javascript::Arg& arg : arrow->args) VisitArg(arg);
            AnalyzeStmtList(arrow->body.block.stmts);
            for (const javascript::Stmt& stmt : arrow->body.block.stmts) {
                VisitStmt(stmt, false);
            }
            return;
        }
        if (const auto* fn = Get<EFunction>(expr)) {
            VisitFn(fn->fn);
            return;
        }
        if (const auto* class_ = Get<EClass>(expr)) {
            VisitClass(class_->class_);
            return;
        }
        if (const auto* jsx = Get<EJSXElement>(expr)) {
            if (ExprPresent(jsx->tag_or_nil)) VisitExpr(jsx->tag_or_nil);
            for (const javascript::Property& property : jsx->properties) {
                VisitProperty(property);
            }
            for (const javascript::Expr& child : jsx->nullable_children) {
                VisitExpr(child);
            }
            return;
        }
        if (const auto* object = Get<EObject>(expr)) {
            for (const javascript::Property& property : object->properties) {
                VisitProperty(property);
            }
            return;
        }
        if (const auto* spread = Get<ESpread>(expr)) {
            VisitExpr(spread->value);
            return;
        }
        if (const auto* tmpl = Get<ETemplate>(expr)) {
            if (ExprPresent(tmpl->tag_or_nil)) VisitExpr(tmpl->tag_or_nil);
            for (const javascript::TemplatePart& part : tmpl->parts) {
                VisitExpr(part.value);
            }
            return;
        }
        if (const auto* annotation = Get<EAnnotation>(expr)) {
            VisitExpr(annotation->value);
            return;
        }
        if (const auto* inlined = Get<EInlinedEnum>(expr)) {
            VisitExpr(inlined->value);
            return;
        }
        if (const auto* await = Get<EAwait>(expr)) {
            VisitExpr(await->value);
            return;
        }
        if (const auto* yield = Get<EYield>(expr)) {
            if (ExprPresent(yield->value_or_nil)) VisitExpr(yield->value_or_nil);
            return;
        }
        if (const auto* conditional = Get<EIf>(expr)) {
            VisitExpr(conditional->test);
            VisitExpr(conditional->yes);
            VisitExpr(conditional->no);
            return;
        }
        if (const auto* import_call = Get<javascript::EImportCall>(expr)) {
            VisitExpr(import_call->expr);
            if (ExprPresent(import_call->options_or_nil)) {
                VisitExpr(import_call->options_or_nil);
            }
            return;
        }
    }

    void VisitLintData(const LintData& data) {
        if (RuleSeverity(Rule::kNoRedeclare) != Severity::kOff) {
            for (const javascript::LintRedeclaration& redeclaration :
                 data.redeclarations) {
                Add(Rule::kNoRedeclare,
                    logger::Range{redeclaration.new_loc,
                                  IdentifierLength(source, redeclaration.new_loc)},
                    "\"" + redeclaration.name + "\" is already defined.");
            }
        }

        if (RuleSeverity(Rule::kNoUseBeforeDefine) != Severity::kOff) {
            std::unordered_map<uint64_t, logger::Loc> definitions;
            auto key = [](compiler::Ref ref) {
                return (static_cast<uint64_t>(ref.source_index) << 32) |
                       static_cast<uint64_t>(ref.inner_index);
            };
            for (const javascript::LintDeclaration& declaration :
                 data.declarations) {
                if (compiler::SymbolKindIsFunction(declaration.kind)) continue;
                const uint64_t ref = key(declaration.ref);
                auto it = definitions.find(ref);
                if (it == definitions.end() ||
                    declaration.loc.start < it->second.start) {
                    definitions[ref] = declaration.loc;
                }
            }
            for (const javascript::SourceReference& reference : data.references) {
                if (reference.mode != javascript::SourceReferenceMode::kRead) {
                    continue;
                }
                auto it = definitions.find(key(reference.ref));
                if (it == definitions.end()) continue;
                if (reference.loc.start < it->second.start) {
                    Add(Rule::kNoUseBeforeDefine,
                        logger::Range{reference.loc,
                                      IdentifierLength(source, reference.loc)},
                        "\"" + reference.name + "\" was used before it was defined.");
                }
            }
        }

        if (RuleSeverity(Rule::kNoShadow) != Severity::kOff) {
            for (const javascript::LintDeclaration& declaration :
                 data.declarations) {
                if (declaration.scope == nullptr) continue;
                for (javascript::Scope* scope = declaration.scope->parent;
                     scope != nullptr; scope = scope->parent) {
                    auto it = scope->members.find(declaration.name);
                    if (it == scope->members.end()) continue;
                    if (it->second.loc.start < 0) continue;
                    Add(Rule::kNoShadow,
                        logger::Range{declaration.loc,
                                      IdentifierLength(source, declaration.loc)},
                        "\"" + declaration.name +
                            "\" is already declared in the upper scope.");
                    break;
                }
            }
        }
    }

    void VisitImports(const javascript::AST& ast) {
        if (RuleSeverity(Rule::kNoDuplicateImports) != Severity::kOff) {
            std::vector<compiler::ImportRecord> imports;
            for (const compiler::ImportRecord& record : ast.import_records) {
                if (record.kind == compiler::ImportKind::kStmt) {
                    imports.push_back(record);
                }
            }
            std::sort(imports.begin(), imports.end(),
                      [](const compiler::ImportRecord& a,
                         const compiler::ImportRecord& b) {
                          return a.range.loc.start < b.range.loc.start;
                      });
            std::unordered_set<std::string> seen_paths;
            for (const compiler::ImportRecord& record : imports) {
                if (!seen_paths.insert(record.path.text).second) {
                    Add(Rule::kNoDuplicateImports, record.range,
                        "Module \"" + record.path.text +
                            "\" is imported multiple times.");
                }
            }
        }
    }
};

std::string Extension(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return {};
    }
    std::string extension = path.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

} // namespace

SourcePosition PositionForOffset(const std::string& source, int32_t offset) {
    if (offset < 0) offset = 0;
    if (static_cast<size_t>(offset) > source.size()) {
        offset = static_cast<int32_t>(source.size());
    }
    int line = 1;
    int column = 1;
    for (int32_t i = 0; i < offset; i++) {
        if (source[static_cast<size_t>(i)] == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }
    }
    return SourcePosition{line, column};
}

std::string RelativeDisplayPath(filesystem::Fs& fs, const std::string& cwd,
                                const std::string& path) {
    std::string result = path;
    if (auto rel = fs.Rel(cwd, path); rel && !rel->empty()) {
        result = *rel;
    }
    for (char& c : result) {
        if (c == '\\') c = '/';
    }
    return result;
}

Analysis AnalyzeSource(const std::string& path, const std::string& source,
                       const Config& config) {
    Analysis analysis;
    analysis.report.path = path;

    logger::Source parsed_source;
    parsed_source.contents = source;
    parsed_source.key_path.text = path;
    parsed_source.index = 0;

    logger::Log log = logger::NewDeferLog(
        logger::DeferLogKind::kDeferLogNoVerboseOrDebug, {});

    const std::string extension = Extension(path);
    config::Options build_options{};
    build_options.BuildMode = config::Mode::kBundle;
    build_options.OutputPlatform = config.platform;
    if (extension == ".cjs" || extension == ".cts") {
        build_options.ModuleTypeData.type = javascript::ModuleType::kCommonJS_CJS;
    } else {
        build_options.ModuleTypeData.type = javascript::ModuleType::kESM_MJS;
    }

    const Severity undef = config.rules.contains("no-undef")
                               ? config.rules.at("no-undef")
                               : DefaultSeverity(Rule::kNoUndef);
    const Severity unused = config.rules.contains("no-unused-vars")
                                ? config.rules.at("no-unused-vars")
                                : DefaultSeverity(Rule::kNoUnusedVars);
    build_options.Lint.UndefinedVariables =
        undef == Severity::kOff ? config::LintLevel::kOff
                                : config::LintLevel::kWarning;
    build_options.Lint.UnusedVariables =
        unused == Severity::kOff ? config::LintLevel::kOff
                                 : config::LintLevel::kWarning;
    build_options.Lint.UnusedParameters =
        unused == Severity::kOff ? config::LintLevel::kOff
                                 : config::LintLevel::kWarning;

    for (const auto& [name, writable] : config.globals) {
        (void)writable;
        build_options.Lint.Globals.insert(name);
    }

    javascript::Options options = javascript::OptionsFromConfig(&build_options);
    LintData lint_data;
    auto [ast, ok] = javascript::Parse(log, std::move(parsed_source), options,
                                      &lint_data);
    std::vector<logger::Msg> messages = log.done();

    std::unordered_set<std::string> seen;
    for (const logger::Msg& message : messages) {
        std::optional<Rule> rule;
        if (message.id == logger::MsgID::kJS_UndefinedVariable) {
            rule = Rule::kNoUndef;
        } else if (message.id == logger::MsgID::kJS_UnusedVariable ||
                   message.id == logger::MsgID::kJS_UnusedParameter) {
            rule = Rule::kNoUnusedVars;
        }

        if (rule.has_value()) {
            const Severity severity = config.rules.contains(
                                          std::string(RuleName(*rule)))
                                          ? config.rules.at(std::string(RuleName(*rule)))
                                          : DefaultSeverity(*rule);
            if (severity == Severity::kOff) continue;
            int line = 1;
            int column = 1;
            int length = 1;
            if (message.data.location != nullptr) {
                line = std::max(1, message.data.location->line);
                column = std::max(1, message.data.location->column + 1);
                length = std::max(1, message.data.location->length);
            }
            const std::string key = std::to_string(static_cast<int>(*rule)) + ":" +
                                    std::to_string(line) + ":" +
                                    std::to_string(column) + ":" +
                                    message.data.text;
            if (!seen.insert(key).second) continue;
            Diagnostic diagnostic;
            diagnostic.path = path;
            diagnostic.rule_id = std::string(RuleName(*rule));
            diagnostic.severity = severity;
            diagnostic.message = message.data.text;
            diagnostic.line = line;
            diagnostic.column = column;
            diagnostic.end_line = line;
            diagnostic.end_column = column + length;
            diagnostic.fixes = message.data.fixes;
            analysis.report.diagnostics.push_back(std::move(diagnostic));
            continue;
        }

        if (message.kind == logger::MsgKind::kError) {
            int line = 1;
            int column = 1;
            int length = 1;
            if (message.data.location != nullptr) {
                line = std::max(1, message.data.location->line);
                column = std::max(1, message.data.location->column + 1);
                length = std::max(1, message.data.location->length);
            }
            const std::string key = "syntax:" + std::to_string(line) + ":" +
                                    std::to_string(column) + ":" +
                                    message.data.text;
            if (!seen.insert(key).second) continue;
            Diagnostic diagnostic;
            diagnostic.path = path;
            diagnostic.rule_id = "syntax";
            diagnostic.severity = Severity::kError;
            diagnostic.message = message.data.text;
            diagnostic.line = line;
            diagnostic.column = column;
            diagnostic.end_line = line;
            diagnostic.end_column = column + length;
            diagnostic.is_syntax = true;
            analysis.report.diagnostics.push_back(std::move(diagnostic));
        }
    }

    if (ok) {
        analysis.imports = ast.import_records;
        Visitor visitor{path, source, config, analysis};
        visitor.VisitImports(ast);
        for (const javascript::Part& part : ast.parts) {
            visitor.AnalyzeStmtList(part.stmts);
            for (const javascript::Stmt& stmt : part.stmts) {
                visitor.VisitStmt(stmt, true);
            }
        }
        visitor.VisitLintData(lint_data);
    }

    std::sort(analysis.report.diagnostics.begin(),
              analysis.report.diagnostics.end(),
              [](const Diagnostic& a, const Diagnostic& b) {
                  if (a.line != b.line) return a.line < b.line;
                  if (a.column != b.column) return a.column < b.column;
                  if (a.rule_id != b.rule_id) return a.rule_id < b.rule_id;
                  return a.message < b.message;
              });

    for (const Diagnostic& diagnostic : analysis.report.diagnostics) {
        if (diagnostic.severity == Severity::kError) analysis.report.errors++;
        if (diagnostic.severity == Severity::kWarning) analysis.report.warnings++;
    }
    return analysis;
}

} // namespace guchho::lint::detail
