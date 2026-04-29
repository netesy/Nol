#ifndef NOL_HPP
#define NOL_HPP

/*
    NOL - Notation Object Language
    Phase 1 Reference Header


    Goals:
    - Strong typed value tree
    - Parser + serializer ready structure
    - Header-only foundation

    Included:
    - Full lexer
    - Full parser core
    - Objects / arrays / strings / numbers / booleans / null
    - Sections [server] and nested [server.tls]
    - Arrays of objects [users.*]
    - Duplicate key detection
    - Comments (# and ## ##)
    - Pretty serializer

    C++17


    Phase 1 — nol.hpp Core Complete (real code)
    full lexer
    full parser
    sections
    arrays
    objects
    strings
    comments
    duplicate keys
    dates
    Phase 2
    interpolation
    env vars
    coercion
    Phase 3
    anchors
    merge
    formatter
    schema

*/

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <memory>
#include <string_view>

namespace NOL {

// ============================================================
// Unicode Normalization (NFC Hook)
// ============================================================

// NOTE:
// Real NFC requires ICU or utf8proc.
// This is a placeholder to enforce pipeline correctness.

inline std::string normalizeKey(const std::string& k) {
    return k; // TODO: plug ICU/utf8proc
}

// ============================================================
// Errors
// ============================================================

class Error : public std::runtime_error {
public:
    Error(const std::string& m) : std::runtime_error(m) {}
};

// ============================================================
// Value
// ============================================================

struct Date {
    std::string iso;
    Date(const std::string& s) : iso(s) {}
};

class Value;
using Array  = std::vector<Value>;
using Object = std::unordered_map<std::string, Value>;

// Modern API - Typed Access Result Template
template<typename T>
struct Result {
    T value;
    std::string error;

    bool ok() const { return error.empty(); }
};

class Value {
public:
    using Variant = std::variant<
        std::nullptr_t,
        bool,
        int64_t,
        double,
        std::string,
        Date,
        Array,
        Object
    >;

private:
    Variant data_;

public:
    Value() : data_(nullptr) {}
    Value(std::nullptr_t) : data_(nullptr) {}
    Value(bool v) : data_(v) {}
    Value(int v) : data_((int64_t)v) {}
    Value(int64_t v) : data_(v) {}
    Value(double v) : data_(v) {}
    Value(const char* s) : data_(std::string(s)) {}
    Value(const std::string& s) : data_(s) {}
    Value(const Date& d) : data_(d) {}
    Value(const Array& a) : data_(a) {}
    Value(const Object& o) : data_(o) {}

    bool isNull()   const { return std::holds_alternative<std::nullptr_t>(data_); }
    bool isBool()   const { return std::holds_alternative<bool>(data_); }
    bool isInt()    const { return std::holds_alternative<int64_t>(data_); }
    bool isFloat()  const { return std::holds_alternative<double>(data_); }
    bool isString() const { return std::holds_alternative<std::string>(data_); }
    bool isDate()   const { return std::holds_alternative<Date>(data_); }
    bool isArray()  const { return std::holds_alternative<Array>(data_); }
    bool isObject() const { return std::holds_alternative<Object>(data_); }

    bool asBool() const { return std::get<bool>(data_); }
    int64_t asInt() const { return std::get<int64_t>(data_); }
    double asFloat() const {
        if (isFloat()) return std::get<double>(data_);
        return (double)std::get<int64_t>(data_);
    }
    const std::string& asString() const { return std::get<std::string>(data_); }
    const Date& asDate() const { return std::get<Date>(data_); }
    Array& asArray() { return std::get<Array>(data_); }
    const Array& asArray() const { return std::get<Array>(data_); }
    Object& asObject() { return std::get<Object>(data_); }
    const Object& asObject() const { return std::get<Object>(data_); }

    Value& operator[](const std::string& key) {
        if (!isObject()) data_ = Object{};
        return std::get<Object>(data_)[key];
    }

    const Value& operator[](const std::string& key) const {
        const auto& obj = std::get<Object>(data_);
        auto it = obj.find(key);
        if (it == obj.end()) throw Error("Key not found: " + key);
        return it->second;
    }

    Value& operator[](size_t i) { return std::get<Array>(data_).at(i); }
    const Value& operator[](size_t i) const { return std::get<Array>(data_).at(i); }

    std::string dump(int indent = 2) const {
        std::ostringstream out;
        write(out, *this, indent, 0);
        return out.str();
    }

    // Modern API - Typed Access Methods
    Result<int64_t> getInt() const {
        if (!isInt()) return {0, "Expected int"};
        return {asInt(), ""};
    }

    Result<std::string> getString() const {
        if (!isString()) return {"", "Expected string"};
        return {asString(), ""};
    }

    Result<bool> getBool() const {
        if (!isBool()) return {false, "Expected bool"};
        return {asBool(), ""};
    }

    Result<double> getFloat() const {
        if (!isFloat()) return {0.0, "Expected float"};
        return {asFloat(), ""};
    }

    // Safe getters with defaults
    int64_t getIntOr(int64_t def) const {
        return isInt() ? asInt() : def;
    }

    std::string getStringOr(const std::string& def) const {
        return isString() ? asString() : def;
    }

    bool getBoolOr(bool def) const {
        return isBool() ? asBool() : def;
    }

    double getFloatOr(double def) const {
        return isFloat() ? asFloat() : def;
    }

    // Safe navigation
    const Value* maybe(const std::string& key) const {
        if (!isObject()) return nullptr;

        auto it = asObject().find(key);
        if (it == asObject().end()) return nullptr;

        return &it->second;
    }

    // Path-based access
    const Value* at(const std::string& path) const {
        const Value* cur = this;

        std::stringstream ss(path);
        std::string part;

        while (std::getline(ss, part, '.')) {
            if (!cur->isObject()) return nullptr;

            auto it = cur->asObject().find(part);
            if (it == cur->asObject().end()) return nullptr;

            cur = &it->second;
        }

        return cur;
    }

private:
    static void pad(std::ostringstream& out, int n) {
        for (int i = 0; i < n; ++i) out << ' ';
    }

    static std::string esc(const std::string& s) {
        std::string r;
        for (char c : s) {
            switch (c) {
                case '\n': r += "\\n"; break;
                case '\t': r += "\\t"; break;
                case '\"': r += "\\\""; break;
                case '\\': r += "\\\\"; break;
                default: r += c;
            }
        }
        return r;
    }

    static void write(std::ostringstream& out,
                      const Value& v,
                      int indent,
                      int depth)
    {
        if (v.isNull()) out << "null";
        else if (v.isBool()) out << (v.asBool() ? "true" : "false");
        else if (v.isInt()) out << v.asInt();
        else if (v.isFloat()) out << v.asFloat();
        else if (v.isString()) out << "\"" << esc(v.asString()) << "\"";
        else if (v.isDate()) out << "\"" << v.asDate().iso << "\"";
        else if (v.isArray()) {
            out << "[";
            const auto& a = v.asArray();
            for (size_t i = 0; i < a.size(); ++i) {
                if (i) out << ",";
                if (indent) {
                    out << "\n";
                    pad(out, (depth + 1) * indent);
                }
                write(out, a[i], indent, depth + 1);
            }
            if (!a.empty() && indent) {
                out << "\n";
                pad(out, depth * indent);
            }
            out << "]";
        }
        else if (v.isObject()) {
            out << "{";
            bool first = true;
            
            // Deterministic serialization: sort keys lexicographically
            std::vector<std::pair<std::string, Value>> items;
            for (const auto& kv : v.asObject()) {
                items.emplace_back(kv.first, kv.second);
            }

            std::sort(items.begin(), items.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });

            for (auto& kv : items) {
                if (!first) out << ",";
                first = false;

                if (indent) {
                    out << "\n";
                    pad(out, (depth + 1) * indent);
                }

                out << kv.first << ": ";
                write(out, kv.second, indent, depth + 1);
            }
            if (!v.asObject().empty() && indent) {
                out << "\n";
                pad(out, depth * indent);
            }
            out << "}";
        }
    }
};

// ============================================================
// Lexer
// ============================================================

enum class Tok {
    End,
    Identifier,
    String,
    Number,
    True,
    False,
    Null,
    Date,

    Colon,
    Comma,
    Dot,
    Star,
    Dollar,
    LAngle,
    RAngle,
    QuestionQuestion,
    Ampersand,
    LShift,

    LBrace,
    RBrace,
    LBracket,
    RBracket,

    NewLine
};

struct Token {
    Tok type;
    std::string text;
    int line;
    int col;
};

class Lexer {
    const std::string src;
    size_t pos = 0;
    int line = 1;
    int col = 1;

public:
    Lexer(const std::string& s) : src(s) {}

    Token next() {
        skipSpaces();

        if (eof()) return token(Tok::End, "");

        char c = peek();

        if (c == '\n') {
            get();
            return token(Tok::NewLine, "\n");
        }

        if (c == '#') {
            skipComment();
            return next();
        }

        if (c == '?') {
            get();
            if (!eof() && peek() == '?') {
                get();
                return token(Tok::QuestionQuestion, "??");
            }
            throw Error("Unexpected character");
        }

        if (std::isalpha((unsigned char)c) || c == '_' || c == '-') {
            return identifier();
        }

        if (std::isdigit((unsigned char)c) || c == '-') {
            return number();
        }

        if (c == '"' || c == '\'') {
            return string();
        }

        switch (c) {
            case ':': get(); return token(Tok::Colon, ":");
            case ',': get(); return token(Tok::Comma, ",");
            case '.': get(); return token(Tok::Dot, ".");
            case '*': get(); return token(Tok::Star, "*");
            case '{': get(); return token(Tok::LBrace, "{");
            case '}': get(); return token(Tok::RBrace, "}");
            case '[': get(); return token(Tok::LBracket, "[");
            case ']': get(); return token(Tok::RBracket, "]");
        }

        throw Error("Unexpected character");
    }

private:
    bool eof() const { return pos >= src.size(); }

    char peek() const { return src[pos]; }

    char get() {
        char c = src[pos++];
        if (c == '\n') {
            line++;
            col = 1;
        } else col++;
        return c;
    }

    Token token(Tok t, const std::string& s) {
        return {t, s, line, col};
    }

    void skipSpaces() {
        while (!eof()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\r')
                get();
            else
                break;
        }
    }

    void skipComment() {
        get();

        if (!eof() && peek() == '#') {
            get();
            while (!eof()) {
                if (peek() == '#') {
                    get();
                    if (!eof() && peek() == '#') {
                        get();
                        return;
                    }
                } else get();
            }
            throw Error("Unclosed block comment");
        } else {
            while (!eof() && peek() != '\n') get();
        }
    }

    Token identifier() {
        std::string s;
        while (!eof()) {
            char c = peek();
            if (std::isalnum((unsigned char)c) || c == '_' || c == '-')
                s += get();
            else break;
        }

        if (s == "true") return token(Tok::True, s);
        if (s == "false") return token(Tok::False, s);
        if (s == "null") return token(Tok::Null, s);

        if (s == "yes" || s == "no" || s == "on" || s == "off")
            throw Error("Invalid boolean literal: " + s);

        
        return token(Tok::Identifier, s);
    }

    Token number() {
        std::string s;
        bool dot = false;

        if (peek() == '-') s += get();

        if (!std::isdigit((unsigned char)peek()))
            throw Error("Invalid number");

        while (!eof()) {
            char c = peek();

            if (std::isdigit((unsigned char)c)) {
                s += get();
            }
            else if (c == '.' && !dot) {
                dot = true;
                s += get();

                if (eof() || !std::isdigit((unsigned char)peek()))
                    throw Error("Invalid float format");
            }
            else break;
        }

        // Enforce float grammar strictly
        if (dot) {
            // Simple validation for float format (no regex to avoid <regex> dependency)
            bool has_digits_before = false;
            bool has_digits_after = false;
            size_t dot_pos = s.find('.');
            
            for (size_t i = 0; i < dot_pos; i++) {
                if (std::isdigit((unsigned char)s[i])) {
                    has_digits_before = true;
                    break;
                }
            }
            
            for (size_t i = dot_pos + 1; i < s.length(); i++) {
                if (std::isdigit((unsigned char)s[i])) {
                    has_digits_after = true;
                    break;
                }
            }
            
            if (!has_digits_before || !has_digits_after)
                throw Error("Invalid float literal: " + s);
        }

        return token(Tok::Number, s);
    }

    Token string() {
        char q = get();
        std::string s;

        while (!eof()) {
            char c = get();

            if (c == q)
                return token(Tok::String, s);

            if (c == '\\') {
                if (eof()) throw Error("Invalid escape");

                char n = get();
                switch (n) {
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case 'r': s += '\r'; break;
                    case '\\': s += '\\'; break;
                    case '"': s += '"'; break;
                    case '\'': s += '\''; break;
                    default:
                        throw Error("Invalid escape sequence");
                }
            } else {
                s += c;
            }
        }

        throw Error("Unclosed string");
    }
};

// ============================================================
// Parser
// ============================================================

class Parser {
    Lexer lex;
    Token cur;
    Value* root_ = nullptr;
    std::unordered_set<std::string> section_paths_;

public:
    Parser(const std::string& s) : lex(s) {
        cur = lex.next();
    }

    Value parse() {
        Value root(Object{});
        root_ = &root;
        std::vector<std::string> section;

        while (cur.type != Tok::End) {
            skipLines();

            if (cur.type == Tok::LBracket) {
                section = parseSection(root);
            }
            else if (cur.type == Tok::Identifier || cur.type == Tok::String) {
                parsePair(target(root, section));
            }
            else if (cur.type != Tok::End) {
                error("Unexpected token");
            }

            skipLines();
        }

        return root;
    }

private:

    void next() { cur = lex.next(); }

    void skipLines() {
        while (cur.type == Tok::NewLine)
            next();
    }

    void checkProhibitedFeatures() {
        // NOL prohibits all Extended Mode features
        if (cur.type == Tok::Dollar) {
            error("Environment variable syntax not allowed in NOL");
        }
        if (cur.type == Tok::Ampersand) {
            error("Anchor syntax not allowed in NOL");
        }
        if (cur.type == Tok::LShift) {
            error("Merge syntax not allowed in NOL");
        }
        if (cur.type == Tok::LAngle) {
            error("Type coercion not allowed in NOL");
        }
    }

    [[noreturn]] void error(const std::string& s) {
        throw Error(s);
    }

    Value& target(Value& root, const std::vector<std::string>& path) {
        Value* node = &root;
        for (auto& p : path)
            node = &((*node)[p]);
        return *node;
    }

    std::string joinPath(const std::vector<std::string>& p) {
        std::string r;
        for (size_t i = 0; i < p.size(); i++) {
            if (i) r += ".";
            r += p[i];
        }
        return r;
    }

    std::vector<std::string> parseSection(Value& root) {
        expect(Tok::LBracket);
        next();

        std::vector<std::string> path;

        while (true) {
            if (cur.type != Tok::Identifier)
                error("Expected section name");

            std::string sectionName = cur.text;
            path.push_back(sectionName);
            next();

            if (cur.type == Tok::Dot) {
                next();

                if (cur.type == Tok::Star) {
                    next();
                    expect(Tok::RBracket);
                    next();

                    // array of objects
                    Value& arr = target(root, path);
                    if (!arr.isArray())
                        arr = Value(Array{});
                    arr.asArray().push_back(Value(Object{}));
                    return path;
                }

                continue;
            }

            break;
        }

        expect(Tok::RBracket);
        next();

        auto key = joinPath(path);

        if (section_paths_.count(key))
            error("Duplicate section: " + key);

        section_paths_.insert(key);

        // ensure object path exists
        target(root, path);

        return path;
    }

    void parsePair(Value& obj) {
        std::string key = cur.text;
        next();

        expect(Tok::Colon);
        next();

        if (!obj.isObject()) obj = Value(Object{});

        auto& map = obj.asObject();

        auto nk = normalizeKey(key);

        if (section_paths_.count(nk))
            error("Section/value collision: " + nk);

        if (map.find(nk) != map.end())
            error("Duplicate key: " + nk);

        map[nk] = parseValue();
    }

    Value parseValue() {
        // Check for prohibited features
        checkProhibitedFeatures();
        
        switch (cur.type) {
            case Tok::String: {
                std::string s = cur.text;
                next();
                return s;
            }
            case Tok::Number: {
                std::string n = cur.text;
                next();

                // Check for integer overflow
                try {
                    if (n.find('.') != std::string::npos) {
                        double val = std::stod(n);
                        // Check for NaN/Inf
                        if (std::isnan(val) || std::isinf(val)) {
                            error("NaN and Infinity not allowed");
                        }
                        return val;
                    } else {
                        try {
                            return (int64_t)std::stoll(n);
                        } catch (...) {
                            error("Integer overflow");
                        }
                    }
                } catch (const std::out_of_range&) {
                    error("Integer overflow");
                }
            }
            case Tok::Identifier: {
                std::string s = cur.text;
                next();
                
                // Check if this is a date (YYYY-MM-DD)
                if (s.length() == 10 && s[4] == '-' && s[7] == '-') {
                    bool valid_date = true;
                    for (int i = 0; i < 10; i++) {
                        if (i == 4 || i == 7) continue;
                        if (!std::isdigit((unsigned char)s[i])) {
                            valid_date = false;
                            break;
                        }
                    }
                    if (valid_date) {
                        return Date(s);
                    }
                }
                
                error("Unexpected identifier: " + s);
            }
            case Tok::Date: {
                std::string d = cur.text;
                next();
                return Date(d);
            }
            case Tok::True:
                next(); return true;

            case Tok::False:
                next(); return false;

            case Tok::Null:
                next(); return nullptr;

            case Tok::LBracket:
                return parseArray();

            case Tok::LBrace:
                return parseObject();

            default:
                error("Invalid value");
        }
    }

    Value parseArray() {
        next();

        Array arr;

        while (cur.type != Tok::RBracket) {
            skipLines();
            arr.push_back(parseValue());
            skipLines();

            if (cur.type == Tok::Comma) next();
            else if (cur.type != Tok::RBracket)
                error("Expected , or ]");
        }

        next();
        return arr;
    }

    Value parseObject() {
        next();

        Object obj;

        while (cur.type != Tok::RBrace) {
            skipLines();

            if (cur.type != Tok::Identifier && cur.type != Tok::String)
                error("Expected key");

            std::string key = cur.text;
            next();

            expect(Tok::Colon);
            next();

            auto nk = normalizeKey(key);

            if (obj.find(nk) != obj.end())
                error("Duplicate key: " + nk);

            obj[nk] = parseValue();

            skipLines();

            if (cur.type == Tok::Comma)
                next();
            else if (cur.type != Tok::RBrace)
                error("Expected , or }");
        }

        next();
        return obj;
    }

    void expect(Tok t) {
        if (cur.type != t)
            error("Unexpected token");
    }
};

// ============================================================
// Modern API - Result-Based Parsing
// ============================================================

struct ParseError {
    std::string message;
    int line = 0;
    int col = 0;
};

struct ParseResult {
    Value value;
    std::unique_ptr<ParseError> error;

    bool ok() const { return !error; }
};

// ============================================================
// Public API - Ergonomic Interface
// ============================================================

// Modern API - Result-Based Parsing Functions
inline ParseResult parse(const std::string& input) {
    try {
        return { Parser(input).parse(), nullptr };
    } catch (const Error& e) {
        return { {}, std::make_unique<ParseError>(ParseError{ e.what(), 0, 0 }) };
    }
}

inline ParseResult parseFile(const std::string& path) {
    std::ifstream in(path);
    if (!in)
        return { {}, std::make_unique<ParseError>(ParseError{"Cannot open file", 0, 0}) };

    std::stringstream ss;
    ss << in.rdbuf();

    return parse(ss.str());
}


inline std::string dump(const Value& value, int indent = 2) {
    return value.dump(indent);
}

// Modern API - Document Wrapper
class Document {
    Value root_;

public:
    static ParseResult parse(const std::string& input) {
        return NOL::parse(input);
    }

    Document() : root_(Object{}) {}
    explicit Document(const Value& v) : root_(std::move(v)) {}

    // Ergonomic access methods - no need to call root()
    const Value& get() const { return root_; }
    Value& get() { return root_; }
    
    // Direct key/value access
    const Value& get(const std::string& key) const {
        return root_[key];
    }
    
    Value& get(const std::string& key) {
        return root_[key];
    }
    
    // Legacy root() method for backward compatibility
    const Value& root() const { return root_; }
    Value& root() { return root_; }

    // Path-based access
    const Value* at(const std::string& path) const {
        return root_.at(path);
    }

    bool has(const std::string& key) const {
        if (!root_.isObject()) return false;
        return root_.asObject().count(key);
    }

    std::string dump(int indent = 2) const {
        return root_.dump(indent);
    }
    
    // Helper to resolve dot notation paths
    const Value* resolvePath(const std::string& path) const {
        if (path.find('.') == std::string::npos) {
            // Simple key - direct access
            return root_.maybe(path);
        }
        
        // Dot notation path
        return root_.at(path);
    }
    
    // Direct typed access methods with dot notation support
    Result<int64_t> getInt(const std::string& key) const {
        if (auto value = resolvePath(key)) {
            return value->getInt();
        }
        return {0, "Key not found: " + key};
    }
    
    Result<std::string> getString(const std::string& key) const {
        if (auto value = resolvePath(key)) {
            return value->getString();
        }
        return {"", "Key not found: " + key};
    }
    
    Result<bool> getBool(const std::string& key) const {
        if (auto value = resolvePath(key)) {
            return value->getBool();
        }
        return {false, "Key not found: " + key};
    }
    
    Result<double> getFloat(const std::string& key) const {
        if (auto value = resolvePath(key)) {
            return value->getFloat();
        }
        return {0.0, "Key not found: " + key};
    }
    
    // Safe getters with defaults and dot notation support
    int64_t getIntOr(const std::string& key, int64_t def) const {
        if (auto value = resolvePath(key)) {
            return value->getIntOr(def);
        }
        return def;
    }
    
    std::string getStringOr(const std::string& key, const std::string& def) const {
        if (auto value = resolvePath(key)) {
            return value->getStringOr(def);
        }
        return def;
    }
    
    bool getBoolOr(const std::string& key, bool def) const {
        if (auto value = resolvePath(key)) {
            return value->getBoolOr(def);
        }
        return def;
    }
    
    double getFloatOr(const std::string& key, double def) const {
        if (auto value = resolvePath(key)) {
            return value->getFloatOr(def);
        }
        return def;
    }
    
    // Safe navigation with dot notation support
    const Value* maybe(const std::string& key) const {
        return resolvePath(key);
    }
};

// Modern API - Builder Pattern for Config Generation
class Builder {
    Value root_{Object{}};

public:
    Builder& set(const std::string& key, Value v) {
        root_[key] = std::move(v);
        return *this;
    }

    Builder& set(const std::string& key, const std::string& v) {
        root_[key] = v;
        return *this;
    }

    Builder& set(const std::string& key, int64_t v) {
        root_[key] = v;
        return *this;
    }

    Builder& set(const std::string& key, double v) {
        root_[key] = v;
        return *this;
    }

    Builder& set(const std::string& key, bool v) {
        root_[key] = v;
        return *this;
    }

    Value build() { return root_; }
};

// Modern API - Serialization Functions
inline std::string stringify(const Value& v, int indent = 2) {
    return v.dump(indent);
}

inline bool writeFile(const std::string& path, const Value& v) {
    std::ofstream out(path);
    if (!out) return false;
    out << stringify(v);
    return true;
}

} // namespace NOL

#endif