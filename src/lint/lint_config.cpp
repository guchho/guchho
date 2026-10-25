#include "guchho/lint.hpp"

#include "guchho/helpers.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_parser.hpp"
#include "guchho/logger.hpp"

#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace guchho::lint {

namespace {

using javascript::EArray;
using javascript::EBoolean;
using javascript::ENull;
using javascript::ENumber;
using javascript::EObject;
using javascript::EString;
using javascript::Expr;
using javascript::Property;

const std::vector<std::string>& ConfigFileNames() {
    static const std::vector<std::string> names = {
        "guchho.lint.config.js",
        "guchho.lint.config.mjs",
        "guchho.lint.config.cjs",
        "guchho.lint.config.json",
    };
    return names;
}

std::string JoinPath(filesystem::Fs& fs, const std::string& dir,
                     const std::string& name) {
    if (dir.empty()) return name;
    if (fs.IsAbs(name)) return name;
    return fs.Join({dir, name});
}

std::string ConfigDisplayPath(filesystem::Fs& fs, const std::string& cwd,
                              const std::string& path) {
    if (path.empty()) return path;
    if (auto rel = fs.Rel(cwd, path); rel && !rel->empty() &&
        !rel->starts_with("..")) {
        return *rel;
    }
    return path;
}

const Expr* PropertyValue(const std::shared_ptr<EObject>& object, const std::string& name) {
    if (object == nullptr) return nullptr;
    for (const Property& property : object->properties) {
        const auto* key = std::get_if<std::shared_ptr<EString>>(&property.key.data);
        if (key == nullptr || *key == nullptr) continue;
        if (helpers::UTF16ToString((*key)->value) == name) {
            return &property.value_or_nil;
        }
    }
    return nullptr;
}

bool ExprString(const Expr& expr, std::string& out) {
    const auto* value = std::get_if<std::shared_ptr<EString>>(&expr.data);
    if (value == nullptr || *value == nullptr) return false;
    out = helpers::UTF16ToString((*value)->value);
    return true;
}

bool ExprBool(const Expr& expr, bool& out) {
    const auto* value = std::get_if<std::shared_ptr<EBoolean>>(&expr.data);
    if (value == nullptr || *value == nullptr) return false;
    out = (*value)->value;
    return true;
}

bool ExprNumber(const Expr& expr, double& out) {
    const auto* value = std::get_if<std::shared_ptr<ENumber>>(&expr.data);
    if (value == nullptr || *value == nullptr) return false;
    out = (*value)->value;
    return true;
}

std::string LocationPrefix(const std::string& display_path,
                           const std::string& source, int32_t offset) {
    detail::SourcePosition position = detail::PositionForOffset(source, offset);
    return display_path + ":" + std::to_string(position.line) + ":" +
           std::to_string(position.column) + ": ";
}

bool ReadTextFile(filesystem::Fs& fs, const std::string& path,
                  std::string& contents, std::string& error) {
    filesystem::FsResult<std::string> result = fs.ReadFile(path);
    if (!result.Ok()) {
        error = "cannot read " + path + ": " + result.original_error;
        return false;
    }
    contents = std::move(result.value);
    return true;
}

bool ParseConfigJson(filesystem::Fs& fs, const std::string& cwd,
                     const std::string& path, javascript::Expr& out,
                     std::string& source_out, std::string& error) {
    std::string text;
    if (!ReadTextFile(fs, path, text, error)) return false;

    logger::Source source;
    source.key_path.text = path;
    source.key_path.namespace_ = "file";
    source.pretty_paths.abs = path;
    source.pretty_paths.rel = ConfigDisplayPath(fs, cwd, path);
    source.identifier_name = ConfigDisplayPath(fs, cwd, path);
    source.contents = text;
    source.index = 0;

    logger::Log log = logger::NewDeferLog(
        logger::DeferLogKind::kDeferLogNoVerboseOrDebug, {});
    javascript::JSONOptions options;
    auto result = javascript::Parser::ParseJSON(log, source, options);
    std::vector<logger::Msg> messages = log.done();
    if (!result.second) {
        std::string message = "invalid JSON";
        int32_t offset = 0;
        for (const logger::Msg& msg : messages) {
            if (msg.kind != logger::MsgKind::kError) continue;
            message = msg.data.text;
            if (msg.data.location != nullptr) {
                detail::SourcePosition position{
                    msg.data.location->line,
                    msg.data.location->column,
                };
                error = ConfigDisplayPath(fs, cwd, path) + ":" +
                        std::to_string(position.line) + ":" +
                        std::to_string(position.column) + ": " + message;
                source_out = std::move(text);
                return false;
            }
            break;
        }
        error = LocationPrefix(ConfigDisplayPath(fs, cwd, path), text, offset) +
                message;
        return false;
    }

    source_out = std::move(text);
    out = std::move(result.first);
    return true;
}

std::string NodeBinary() {
#ifdef _WIN32
    char*  value = nullptr;
    size_t size  = 0;
    if (_dupenv_s(&value, &size, "GUCHHO_NODE_BIN") != 0 || value == nullptr) {
        return "node";
    }
    std::string result = value;
    std::free(value);
    return result.empty() ? "node" : result;
#else
    if (const char* value = std::getenv("GUCHHO_NODE_BIN")) {
        if (*value != '\0') return value;
    }
    return "node";
#endif
}

std::string BuildConfigScript(const std::string& config_path) {
    const std::string quoted = helpers::QuoteForJSON(config_path, false);
    return "const { pathToFileURL } = require('node:url');\n"
           "const configPath = " + quoted + ";\n"
           "(async () => {\n"
           "  const mod = await import(pathToFileURL(configPath).href);\n"
           "  const config = (mod && 'default' in mod) ? mod.default : mod;\n"
           "  process.stdout.write(JSON.stringify(config));\n"
           "})().catch((err) => {\n"
           "  process.stderr.write((err && err.stack) ? err.stack : String(err));\n"
           "  process.exitCode = 1;\n"
           "});\n";
}

bool LoadJsConfigText(filesystem::Fs& fs, const std::string& cwd,
                      const std::string& path, std::string& text,
                      std::string& error) {
    const std::string display = ConfigDisplayPath(fs, cwd, path);
    helpers::ProcessResult run = helpers::RunProcess(
        {NodeBinary(), "-e", BuildConfigScript(path)}, fs.Dir(path));
    if (!run.started) {
        error = display + ": failed to start Node (" + NodeBinary() + ")";
        return false;
    }
    if (run.exit_code != 0) {
        std::string detail = run.stderr_data;
        while (!detail.empty() &&
               (detail.back() == '\n' || detail.back() == '\r')) {
            detail.pop_back();
        }
        error = display + ": failed to evaluate configuration" +
                (detail.empty() ? "" : ": " + detail);
        return false;
    }
    if (run.stdout_data.empty()) {
        error = display + ": configuration did not produce a value";
        return false;
    }
    text = std::move(run.stdout_data);
    return true;
}

bool ParseJsonText(const std::string& text, const std::string& display_path,
                   javascript::Expr& out, std::string& error) {
    logger::Source source;
    source.key_path.text = display_path;
    source.key_path.namespace_ = "file";
    source.pretty_paths.abs = display_path;
    source.pretty_paths.rel = display_path;
    source.identifier_name = display_path;
    source.contents = text;
    source.index = 0;

    logger::Log log = logger::NewDeferLog(
        logger::DeferLogKind::kDeferLogNoVerboseOrDebug, {});
    auto result = javascript::Parser::ParseJSON(log, source, javascript::JSONOptions{});
    std::vector<logger::Msg> messages = log.done();
    if (!result.second) {
        std::string message = "invalid configuration JSON";
        int line = 1;
        int column = 1;
        for (const logger::Msg& msg : messages) {
            if (msg.kind != logger::MsgKind::kError) continue;
            message = msg.data.text;
            if (msg.data.location != nullptr) {
                line = msg.data.location->line;
                column = msg.data.location->column;
            }
            break;
        }
        error = display_path + ":" + std::to_string(line) + ":" +
                std::to_string(column) + ": " + message;
        return false;
    }
    out = std::move(result.first);
    return true;
}

bool ParseRuleSeverity(const Expr& value, Severity& out) {
    std::string text;
    if (ExprString(value, text)) {
        if (auto parsed = ParseSeverity(text)) {
            out = *parsed;
            return true;
        }
        return false;
    }
    double number = 0.0;
    if (ExprNumber(value, number) &&
        (number == 0.0 || number == 1.0 || number == 2.0)) {
        out = static_cast<Severity>(static_cast<uint8_t>(number));
        return true;
    }
    return false;
}

struct Loader {
    filesystem::Fs& fs;
    std::string cwd;
    Config* out{};
    std::string* error{};
    std::vector<std::string> stack{};
    int depth{};
    std::string config_path{};
};

bool ApplyConfigObject(Loader& loader, const Expr& root,
                       const std::string& path, const std::string& source);

bool LoadOne(Loader& loader, const std::string& path, bool explicit_file,
             std::string& error) {
    if (loader.depth >= 32) {
        error = ConfigDisplayPath(loader.fs, loader.cwd, path) +
                ": configuration extends chain is too deep";
        return false;
    }
    for (const std::string& active : loader.stack) {
        if (active == path) {
            error = ConfigDisplayPath(loader.fs, loader.cwd, path) +
                    ": configuration extends cycle detected";
            return false;
        }
    }

    const bool is_json = path.ends_with(".json");
    javascript::Expr root;
    std::string source;
    std::string display = ConfigDisplayPath(loader.fs, loader.cwd, path);

    if (is_json) {
        if (!ParseConfigJson(loader.fs, loader.cwd, path, root, source, error)) {
            return false;
        }
    } else {
        std::string text;
        if (!LoadJsConfigText(loader.fs, loader.cwd, path, text, error)) {
            return false;
        }
        if (!ParseJsonText(text, display, root, error)) return false;
        source = std::move(text);
    }

    loader.stack.push_back(path);
    loader.depth++;
    const bool ok = ApplyConfigObject(loader, root, path, source);
    loader.depth--;
    loader.stack.pop_back();
    if (!ok) return false;

    loader.config_path = path;
    (void)explicit_file;
    return true;
}

bool ApplyConfigObject(Loader& loader, const Expr& root,
                       const std::string& path, const std::string& source) {
    const std::string display = ConfigDisplayPath(loader.fs, loader.cwd, path);
    auto fail = [&](const Expr& node, const std::string& message) {
        *loader.error = LocationPrefix(display, source, node.loc.start) + message;
        return false;
    };

    const auto* object = std::get_if<std::shared_ptr<EObject>>(&root.data);
    if (object == nullptr || *object == nullptr) {
        return fail(root, "configuration root must be an object");
    }

    static const std::unordered_set<std::string> known_fields = {
        "$schema", "extends", "include", "files", "exclude", "ignores",
        "rules", "globals", "platform", "languageOptions",
        "reportUnusedDisableDirectives",
    };
    for (const Property& property : (*object)->properties) {
        const auto* key = std::get_if<std::shared_ptr<EString>>(&property.key.data);
        if (key == nullptr || *key == nullptr) continue;
        const std::string name = helpers::UTF16ToString((*key)->value);
        if (name == "overrides") {
            return fail(property.value_or_nil,
                        "\"overrides\" is not supported; use include/exclude instead");
        }
        if (!known_fields.contains(name)) {
            return fail(property.value_or_nil,
                        "unknown configuration field \"" + name + "\"");
        }
    }

    if (const Expr* node = PropertyValue(*object, "extends")) {
        std::vector<const Expr*> entries;
        if (const auto* array = std::get_if<std::shared_ptr<EArray>>(&node->data);
            array != nullptr && *array != nullptr) {
            for (const Expr& item : (*array)->items) entries.push_back(&item);
        } else {
            entries.push_back(node);
        }
        for (const Expr* entry : entries) {
            std::string target;
            if (!ExprString(*entry, target) || target.empty()) {
                return fail(*entry, "\"extends\" must be a path string or an array of them");
            }
            const std::string resolved =
                JoinPath(loader.fs, loader.fs.Dir(path), target);
            if (!LoadOne(loader, resolved, true, *loader.error)) return false;
        }
    }

    auto apply_string_array = [&](const std::string& field,
                                  std::vector<std::string>& out) -> bool {
        const Expr* node = PropertyValue(*object, field);
        if (node == nullptr) return true;
        const auto* array = std::get_if<std::shared_ptr<EArray>>(&node->data);
        if (array == nullptr || *array == nullptr) {
            return fail(*node, "\"" + field + "\" must be an array of strings");
        }
        std::vector<std::string> values;
        for (const Expr& item : (*array)->items) {
            std::string value;
            if (!ExprString(item, value)) {
                return fail(item, "\"" + field + "\" must contain only strings");
            }
            values.push_back(std::move(value));
        }
        out = std::move(values);
        return true;
    };

    std::vector<std::string> includes;
    std::vector<std::string> files;
    std::vector<std::string> excludes;
    std::vector<std::string> ignores;
    if (!apply_string_array("include", includes)) return false;
    if (!apply_string_array("files", files)) return false;
    if (!apply_string_array("exclude", excludes)) return false;
    if (!apply_string_array("ignores", ignores)) return false;

    if (PropertyValue(*object, "include") != nullptr ||
        PropertyValue(*object, "files") != nullptr) {
        includes.insert(includes.end(), files.begin(), files.end());
        loader.out->include = std::move(includes);
    }
    if (PropertyValue(*object, "exclude") != nullptr ||
        PropertyValue(*object, "ignores") != nullptr) {
        excludes.insert(excludes.end(), ignores.begin(), ignores.end());
        loader.out->exclude = std::move(excludes);
    }

    for (const std::string& pattern : loader.out->include) {
        std::string glob_error;
        if (!detail::ValidateGlobPattern(pattern, glob_error)) {
            *loader.error = display + ": invalid include pattern \"" + pattern +
                            "\": " + glob_error;
            return false;
        }
    }
    for (const std::string& pattern : loader.out->exclude) {
        std::string glob_error;
        if (!detail::ValidateGlobPattern(pattern, glob_error)) {
            *loader.error = display + ": invalid exclude pattern \"" + pattern +
                            "\": " + glob_error;
            return false;
        }
    }

    if (const Expr* node = PropertyValue(*object, "rules")) {
        const auto* rules = std::get_if<std::shared_ptr<EObject>>(&node->data);
        if (rules == nullptr || *rules == nullptr) {
            return fail(*node, "\"rules\" must be an object");
        }
        for (const Property& property : (*rules)->properties) {
            const auto* key = std::get_if<std::shared_ptr<EString>>(&property.key.data);
            if (key == nullptr || *key == nullptr) continue;
            const std::string name = helpers::UTF16ToString((*key)->value);
            std::optional<Rule> rule = RuleFromName(name);
            if (!rule) {
                std::string message = "unknown rule \"" + name + "\"";
                if (std::string suggestion = SuggestRuleName(name); !suggestion.empty()) {
                    message += "; did you mean \"" + suggestion + "\"?";
                }
                return fail(property.value_or_nil, message);
            }

            const Expr* severity_node = &property.value_or_nil;
            if (const auto* array =
                    std::get_if<std::shared_ptr<EArray>>(&property.value_or_nil.data);
                array != nullptr && *array != nullptr) {
                if ((*array)->items.empty()) {
                    return fail(property.value_or_nil,
                                "rule \"" + name + "\" needs a severity");
                }
                if ((*array)->items.size() > 1) {
                    return fail(property.value_or_nil,
                                "rule \"" + name + "\" does not accept options");
                }
                severity_node = &(*array)->items.front();
            }

            Severity severity = Severity::kOff;
            if (!ParseRuleSeverity(*severity_node, severity)) {
                return fail(*severity_node,
                            "invalid severity for rule \"" + name +
                            "\"; use \"off\", \"warn\", or \"error\"");
            }
            loader.out->rules[name] = severity;
        }
    }

    if (const Expr* node = PropertyValue(*object, "globals")) {
        const auto* globals = std::get_if<std::shared_ptr<EObject>>(&node->data);
        if (globals == nullptr || *globals == nullptr) {
            return fail(*node, "\"globals\" must be an object");
        }
        for (const Property& property : (*globals)->properties) {
            const auto* key = std::get_if<std::shared_ptr<EString>>(&property.key.data);
            if (key == nullptr || *key == nullptr) continue;
            const std::string name = helpers::UTF16ToString((*key)->value);
            bool writable = false;
            std::string value;
            if (ExprBool(property.value_or_nil, writable)) {
                loader.out->globals[name] = writable;
                continue;
            }
            if (!ExprString(property.value_or_nil, value)) {
                return fail(property.value_or_nil,
                            "global \"" + name +
                            "\" must be true, false, \"readonly\", or \"writable\"");
            }
            if (value == "readonly" || value == "readable") {
                loader.out->globals[name] = false;
            } else if (value == "writable" || value == "writeable") {
                loader.out->globals[name] = true;
            } else if (value == "off") {
                loader.out->globals.erase(name);
            } else {
                return fail(property.value_or_nil,
                            "global \"" + name +
                            "\" must be true, false, \"readonly\", or \"writable\"");
            }
        }
    }

    if (const Expr* node = PropertyValue(*object, "platform")) {
        std::string value;
        if (!ExprString(*node, value)) {
            return fail(*node, "\"platform\" must be a string");
        }
        if (value == "browser") {
            loader.out->platform = config::Platform::kBrowser;
        } else if (value == "node") {
            loader.out->platform = config::Platform::kNode;
        } else if (value == "neutral") {
            loader.out->platform = config::Platform::kNeutral;
        } else {
            return fail(*node,
                        "invalid platform \"" + value +
                        "\"; use \"browser\", \"node\", or \"neutral\"");
        }
    }

    if (const Expr* node = PropertyValue(*object, "reportUnusedDisableDirectives")) {
        bool value = false;
        if (!ExprBool(*node, value)) {
            return fail(*node, "\"reportUnusedDisableDirectives\" must be a boolean");
        }
        if (value) {
            return fail(*node,
                        "\"reportUnusedDisableDirectives\" is not supported; "
                        "disable comments are not interpreted");
        }
        loader.out->report_unused_disable_directives = value;
    }

    if (const Expr* node = PropertyValue(*object, "languageOptions")) {
        const auto* language = std::get_if<std::shared_ptr<EObject>>(&node->data);
        if (language == nullptr || *language == nullptr) {
            return fail(*node, "\"languageOptions\" must be an object");
        }
        for (const Property& property : (*language)->properties) {
            const auto* key = std::get_if<std::shared_ptr<EString>>(&property.key.data);
            if (key == nullptr || *key == nullptr) continue;
            const std::string name = helpers::UTF16ToString((*key)->value);
            if (name == "sourceType") {
                std::string value;
                if (!ExprString(property.value_or_nil, value) ||
                    (value != "module" && value != "script" && value != "commonjs")) {
                    return fail(property.value_or_nil,
                                "\"languageOptions.sourceType\" must be \"module\", "
                                "\"script\", or \"commonjs\"");
                }
                if (value != "module") {
                    return fail(property.value_or_nil,
                                "\"languageOptions.sourceType\" only supports "
                                "\"module\"; module kind is inferred from the "
                                "file extension");
                }
            } else if (name == "ecmaVersion") {
                std::string value;
                double number = 0.0;
                if (!ExprString(property.value_or_nil, value) &&
                    !ExprNumber(property.value_or_nil, number)) {
                    return fail(property.value_or_nil,
                                "\"languageOptions.ecmaVersion\" must be a number or "
                                "\"latest\"");
                }
                if (!ExprString(property.value_or_nil, value) || value != "latest") {
                    return fail(property.value_or_nil,
                                "\"languageOptions.ecmaVersion\" only supports "
                                "\"latest\"");
                }
            } else {
                return fail(property.value_or_nil,
                            "unknown language option \"" + name + "\"");
            }
        }
    }

    return true;
}

bool FindConfig(filesystem::Fs& fs, const std::string& cwd, std::string& found) {
    std::string dir = cwd;
    for (int depth = 0; depth < 256; depth++) {
        filesystem::FsResult<filesystem::DirEntries> entries =
            fs.ReadDirectory(dir);
        if (entries.Ok()) {
            for (const std::string& name : ConfigFileNames()) {
                auto [entry, different_case] = entries.value.Get(name);
                (void)different_case;
                if (entry != nullptr &&
                    entry->Kind(fs) == filesystem::EntryKind::kFile) {
                    found = fs.Join({dir, name});
                    return true;
                }
            }
        }
        const std::string parent = fs.Dir(dir);
        if (parent.empty() || parent == dir) break;
        dir = parent;
    }
    return false;
}

} // namespace

Config DefaultConfig() {
    Config config;
    for (const RuleInfo& info : AllRules()) {
        config.rules[std::string(info.name)] = info.default_severity;
    }
    config.include.clear();
    config.exclude.clear();
    config.globals.clear();
    config.platform = config::Platform::kBrowser;
    config.report_unused_disable_directives = false;
    config.path.clear();
    return config;
}

bool LoadConfig(filesystem::Fs& fs, const std::string& cwd,
                const std::string& explicit_path, Config& out,
                std::string& error) {
    out = DefaultConfig();
    error.clear();

    std::string path;
    bool explicit_file = false;
    if (!explicit_path.empty()) {
        path = fs.IsAbs(explicit_path)
                   ? explicit_path
                   : fs.Join({cwd, explicit_path});
        std::optional<std::string> absolute = fs.Abs(path);
        if (absolute) path = *absolute;
        explicit_file = true;

        bool exists = false;
        filesystem::FsResult<filesystem::DirEntries> entries =
            fs.ReadDirectory(fs.Dir(path));
        if (entries.Ok()) {
            auto [entry, different_case] = entries.value.Get(fs.Base(path));
            (void)different_case;
            exists = entry != nullptr &&
                     entry->Kind(fs) == filesystem::EntryKind::kFile;
        }
        if (!exists) {
            error = "cannot find lint configuration " + path;
            return false;
        }
    } else {
        FindConfig(fs, cwd, path);
    }

    if (path.empty()) return true;

    Loader loader{fs, cwd, &out, &error};
    if (!LoadOne(loader, path, explicit_file, error)) return false;
    out.path = loader.config_path.empty() ? path : loader.config_path;
    return true;
}

} // namespace guchho::lint
