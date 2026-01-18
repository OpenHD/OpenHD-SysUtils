#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <utility>

// -----------------------------------------------------------------------------
// Minimal JSON DOM & Parser
// -----------------------------------------------------------------------------

enum class JsonType { Null, Object, Array, String, Number, Boolean };

struct JsonValue;

using JsonObject = std::map<std::string, std::shared_ptr<JsonValue>>;
using JsonArray = std::vector<std::shared_ptr<JsonValue>>;

struct JsonValue {
    JsonType type = JsonType::Null;
    JsonObject object_val;
    JsonArray array_val;
    std::string string_val;
    double number_val = 0.0;
    bool bool_val = false;

    bool is_object() const { return type == JsonType::Object; }
    bool is_array() const { return type == JsonType::Array; }
    bool is_string() const { return type == JsonType::String; }
    bool is_number() const { return type == JsonType::Number; }
    bool is_bool() const { return type == JsonType::Boolean; }
    bool is_null() const { return type == JsonType::Null; }

    const JsonObject& as_object() const { return object_val; }
    const JsonArray& as_array() const { return array_val; }
    const std::string& as_string() const { return string_val; }
    double as_number() const { return number_val; }
    bool as_bool() const { return bool_val; }
};

class JsonParser {
public:
    explicit JsonParser(std::string input) : input_(std::move(input)), pos_(0) {}

    std::shared_ptr<JsonValue> parse() {
        skip_whitespace();
        if (pos_ >= input_.size()) return nullptr;
        return parse_value();
    }

private:
    std::string input_;
    size_t pos_;

    void skip_whitespace() {
        while (pos_ < input_.size()) {
            unsigned char c = static_cast<unsigned char>(input_[pos_]);
            if (std::isspace(c)) {
                pos_++;
            } else {
                break;
            }
        }
    }

    std::shared_ptr<JsonValue> parse_value() {
        skip_whitespace();
        if (pos_ >= input_.size()) return nullptr;

        char c = input_[pos_];
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') return parse_string();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number();
        if (c == 'n') {
            if (input_.compare(pos_, 4, "null") == 0) {
                pos_ += 4;
                return std::make_shared<JsonValue>();
            }
        }

        throw std::runtime_error("Unexpected character at pos " + std::to_string(pos_) +
                                 ": '" + std::string(1, c) + "'");
    }

    std::shared_ptr<JsonValue> parse_object() {
        auto val = std::make_shared<JsonValue>();
        val->type = JsonType::Object;
        pos_++; // skip '{'

        skip_whitespace();
        if (pos_ < input_.size() && input_[pos_] == '}') {
            pos_++;
            return val;
        }

        while (true) {
            skip_whitespace();
            if (pos_ >= input_.size()) throw std::runtime_error("Unexpected end in object");

            if (input_[pos_] != '"') {
                 throw std::runtime_error("Expected string key in object at pos " + std::to_string(pos_));
            }
            auto key_val = parse_string();
            std::string key = key_val->as_string();

            skip_whitespace();
            if (pos_ >= input_.size() || input_[pos_] != ':') throw std::runtime_error("Expected ':'");
            pos_++;

            val->object_val[key] = parse_value();

            skip_whitespace();
            if (pos_ >= input_.size()) throw std::runtime_error("Unexpected end in object");

            if (input_[pos_] == '}') {
                pos_++;
                break;
            }
            if (input_[pos_] == ',') {
                pos_++;
            } else {
                throw std::runtime_error("Expected ',' or '}' in object");
            }
        }
        return val;
    }

    std::shared_ptr<JsonValue> parse_array() {
        auto val = std::make_shared<JsonValue>();
        val->type = JsonType::Array;
        pos_++; // skip '['

        skip_whitespace();
        if (pos_ < input_.size() && input_[pos_] == ']') {
            pos_++;
            return val;
        }

        while (true) {
            val->array_val.push_back(parse_value());

            skip_whitespace();
            if (pos_ >= input_.size()) throw std::runtime_error("Unexpected end in array");

            if (input_[pos_] == ']') {
                pos_++;
                break;
            }
            if (input_[pos_] == ',') {
                pos_++;
            } else {
                throw std::runtime_error("Expected ',' or ']'");
            }
        }
        return val;
    }

    void append_utf8(std::string& out, int cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    std::shared_ptr<JsonValue> parse_string() {
        auto val = std::make_shared<JsonValue>();
        val->type = JsonType::String;
        pos_++; // skip '"'

        std::string res;
        while (pos_ < input_.size()) {
            char c = input_[pos_];
            if (c == '"') {
                pos_++;
                val->string_val = res;
                return val;
            }
            if (c == '\\') {
                pos_++;
                if (pos_ >= input_.size()) throw std::runtime_error("Unterminated escape sequence");
                char esc = input_[pos_];
                if (esc == '"') res += '"';
                else if (esc == '\\') res += '\\';
                else if (esc == '/') res += '/';
                else if (esc == 'b') res += '\b';
                else if (esc == 'f') res += '\f';
                else if (esc == 'n') res += '\n';
                else if (esc == 'r') res += '\r';
                else if (esc == 't') res += '\t';
                else if (esc == 'u') {
                    pos_++; // skip 'u'
                    if (pos_ + 4 > input_.size()) throw std::runtime_error("Incomplete unicode escape");
                    std::string hex = input_.substr(pos_, 4);
                    try {
                        int cp = std::stoi(hex, nullptr, 16);
                        append_utf8(res, cp);
                    } catch (...) {
                         throw std::runtime_error("Invalid unicode escape");
                    }
                    pos_ += 4;
                    // decrement because loop increments
                    pos_--;
                }
                else res += esc;
                pos_++;
            } else {
                res += c;
                pos_++;
            }
        }
        throw std::runtime_error("Unterminated string");
    }

    std::shared_ptr<JsonValue> parse_bool() {
        auto val = std::make_shared<JsonValue>();
        val->type = JsonType::Boolean;
        if (input_.compare(pos_, 4, "true") == 0) {
            val->bool_val = true;
            pos_ += 4;
        } else {
            val->bool_val = false;
            pos_ += 5;
        }
        return val;
    }

    std::shared_ptr<JsonValue> parse_number() {
        auto val = std::make_shared<JsonValue>();
        val->type = JsonType::Number;
        size_t start = pos_;
        if (pos_ < input_.size() && input_[pos_] == '-') pos_++;
        while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) pos_++;
        if (pos_ < input_.size() && input_[pos_] == '.') {
            pos_++;
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) pos_++;
        }
        if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            pos_++;
            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) pos_++;
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) pos_++;
        }

        try {
             val->number_val = std::stod(input_.substr(start, pos_ - start));
        } catch (...) {
             throw std::runtime_error("Invalid number format at pos " + std::to_string(start));
        }
        return val;
    }
};

// -----------------------------------------------------------------------------
// C++ Code Generator
// -----------------------------------------------------------------------------

std::string escape_cpp_string(const std::string& value) {
    std::string out;
    for (char ch : value) {
        if (ch == '\\') out += "\\\\";
        else if (ch == '"') out += "\\\"";
        else if (ch == '\n') out += "\\n";
        else if (ch == '\r') out += "\\r";
        else if (ch == '\t') out += "\\t";
        else out += ch;
    }
    return out;
}

void render_header(std::shared_ptr<JsonValue> root, std::ostream& out) {
    auto& obj = root->as_object();
    auto platforms = obj.at("platforms")->as_array();

    // Build map of key -> id
    std::map<std::string, int> platform_ids;

    out << "// Generated by tools/gen_platforms.cpp. Do not edit by hand.\n";
    out << "#pragma once\n\n";
    out << "#include <cstddef>\n";
    out << "#include <string>\n\n";
    out << "namespace sysutil {\n\n";

    // Generate constants
    for (const auto& p : platforms) {
        auto& entry = p->as_object();
        std::string key = entry.at("key")->as_string();
        int pid = static_cast<int>(entry.at("id")->as_number());
        platform_ids[key] = pid;
        out << "static constexpr int " << key << " = " << pid << ";\n";
    }

    out << "\nstruct PlatformTypeEntry {\n";
    out << "  int id;\n";
    out << "  const char* name;\n";
    out << "};\n\n";

    out << "inline constexpr PlatformTypeEntry kPlatformTypeEntries[] = {\n";
    for (const auto& p : platforms) {
        auto& entry = p->as_object();
        if (entry.find("display") == entry.end()) continue;
        int pid = static_cast<int>(entry.at("id")->as_number());
        std::string display = entry.at("display")->as_string();
        out << "  {" << pid << ", \"" << escape_cpp_string(display) << "\"},\n";
    }
    out << "};\n\n";

    out << "inline std::string platform_type_to_string(int platform_type) {\n";
    out << "  for (const auto& entry : kPlatformTypeEntries) {\n";
    out << "    if (entry.id == platform_type) {\n";
    out << "      return entry.name;\n";
    out << "    }\n";
    out << "  }\n";
    out << "  return \"ERR-UNDEFINED{\" + std::to_string(platform_type) + \"}\";\n";
    out << "}\n\n";

    // Detections
    if (obj.find("detections") != obj.end()) {
        auto detections = obj.at("detections")->as_array();

        out << "enum class ConditionKind {\n";
        out << "  FileExists,\n";
        out << "  FileContainsAny,\n";
        out << "  FileRegex,\n";
        out << "  ArchRegex,\n";
        out << "};\n\n";

        out << "struct DetectionCondition {\n";
        out << "  ConditionKind kind;\n";
        out << "  const char* path;\n";
        out << "  const char* pattern;\n";
        out << "  const char* group_equals;\n";
        out << "  const char* const* values;\n";
        out << "  std::size_t value_count;\n";
        out << "  bool case_insensitive;\n";
        out << "};\n\n";

        out << "struct DetectionRule {\n";
        out << "  int platform_id;\n";
        out << "  const DetectionCondition* conditions;\n";
        out << "  std::size_t condition_count;\n";
        out << "  const char* log;\n";
        out << "};\n\n";

        int rule_index = 0;
        for (const auto& rule_val : detections) {
            auto& rule = rule_val->as_object();
            // Process nested arrays/values first
            auto conditions = rule.at("conditions")->as_array();
            int cond_index = 0;
            for (const auto& cond_val : conditions) {
                auto& cond = cond_val->as_object();
                std::string type = cond.at("type")->as_string();
                if (type == "file_contains_any") {
                    auto values = cond.at("values")->as_array();
                    out << "inline constexpr const char* kRule" << rule_index << "Cond" << cond_index << "Values[] = {";
                    bool first = true;
                    for (const auto& v : values) {
                        if (!first) out << ", ";
                        out << "\"" << escape_cpp_string(v->as_string()) << "\"";
                        first = false;
                    }
                    out << "};\n";
                }
                cond_index++;
            }

            out << "\ninline constexpr DetectionCondition kRule" << rule_index << "Conditions[] = {\n";
            cond_index = 0;
            for (const auto& cond_val : conditions) {
                auto& cond = cond_val->as_object();
                std::string type = cond.at("type")->as_string();
                bool case_insensitive = false;
                if (cond.find("case_insensitive") != cond.end()) {
                    case_insensitive = cond.at("case_insensitive")->as_bool();
                }

                if (type == "file_exists") {
                    std::string path = cond.at("path")->as_string();
                    out << "  {ConditionKind::FileExists, \"" << escape_cpp_string(path) << "\", nullptr, nullptr, nullptr, 0, false},\n";
                } else if (type == "file_contains_any") {
                    std::string path = cond.at("path")->as_string();
                    auto values = cond.at("values")->as_array();
                    out << "  {ConditionKind::FileContainsAny, \"" << escape_cpp_string(path) << "\", nullptr, nullptr, "
                        << "kRule" << rule_index << "Cond" << cond_index << "Values, " << values.size() << ", "
                        << (case_insensitive ? "true" : "false") << "},\n";
                } else if (type == "file_regex") {
                    std::string path = cond.at("path")->as_string();
                    std::string pattern = cond.at("pattern")->as_string();
                    std::string group_expr = "nullptr";
                    if (cond.find("group_equals") != cond.end()) {
                        group_expr = "\"" + escape_cpp_string(cond.at("group_equals")->as_string()) + "\"";
                    }
                    out << "  {ConditionKind::FileRegex, \"" << escape_cpp_string(path) << "\", \""
                        << escape_cpp_string(pattern) << "\", " << group_expr << ", nullptr, 0, "
                        << (case_insensitive ? "true" : "false") << "},\n";
                } else if (type == "arch_regex") {
                    std::string pattern = cond.at("pattern")->as_string();
                    out << "  {ConditionKind::ArchRegex, nullptr, \"" << escape_cpp_string(pattern) << "\", nullptr, nullptr, 0, "
                        << (case_insensitive ? "true" : "false") << "},\n";
                }
                cond_index++;
            }
            out << "};\n";
            rule_index++;
        }

        out << "\ninline constexpr DetectionRule kDetectionRules[] = {\n";
        rule_index = 0;
        for (const auto& rule_val : detections) {
            auto& rule = rule_val->as_object();
            std::string key = rule.at("platform")->as_string();
            int pid = platform_ids[key];

            std::string log = "";
            if (rule.find("log") != rule.end()) log = rule.at("log")->as_string();
            std::string log_expr = log.empty() ? "\"\"" : "\"" + escape_cpp_string(log) + "\"";

            out << "  {" << pid << ", kRule" << rule_index << "Conditions, "
                << "sizeof(kRule" << rule_index << "Conditions) / sizeof(kRule" << rule_index << "Conditions[0]), "
                << log_expr << "},\n";
            rule_index++;
        }
        out << "};\n\n";
    }

    out << "}  // namespace sysutil\n";
}

#include <clocale>

int main(int argc, char* argv[]) {
    // Ensure standard C locale for consistent JSON parsing (e.g. decimal dots)
    std::setlocale(LC_ALL, "C");

    std::string input_path;
    std::string output_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc) {
            input_path = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        }
    }

    if (input_path.empty() || output_path.empty()) {
        std::cerr << "Usage: " << argv[0] << " --input <json> --output <header>" << std::endl;
        return 1;
    }

    std::ifstream ifs(input_path);
    if (!ifs) {
        std::cerr << "Failed to open input: " << input_path << std::endl;
        return 1;
    }
    std::stringstream buffer;
    buffer << ifs.rdbuf();

    // Ensure parser owns the data string
    std::string content = buffer.str();
    JsonParser parser(content);

    try {
        auto root = parser.parse();
        if (!root || !root->is_object()) {
            std::cerr << "Invalid JSON root" << std::endl;
            return 1;
        }

        std::ofstream ofs(output_path);
        if (!ofs) {
            std::cerr << "Failed to open output: " << output_path << std::endl;
            return 1;
        }
        render_header(root, ofs);

    } catch (const std::exception& e) {
        std::cerr << "JSON Parse error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
