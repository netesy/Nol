#ifndef NOLE_HPP
#define NOLE_HPP
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <stdexcept>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace NOLE {
class Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;
class Value {
public:
    enum Type { Null, Bool, Int, Float, String, ArrayType, ObjectType };
    std::variant<std::nullptr_t, bool, int64_t, double, std::string, Array, Object> data;
    Value() : data(nullptr) {}
    Value(std::nullptr_t) : data(nullptr) {}
    Value(bool b) : data(b) {}
    Value(int64_t i) : data(i) {}
    Value(int i) : data((int64_t)i) {}
    Value(double d) : data(d) {}
    Value(const char* s) : data(std::string(s)) {}
    Value(std::string s) : data(std::move(s)) {}
    Value(Array a) : data(std::move(a)) {}
    Value(Object o) : data(std::move(o)) {}
    Type type() const { return (Type)data.index(); }
    bool isNull() const { return type() == Null; }
    bool isBool() const { return type() == Bool; }
    bool isInt() const { return type() == Int; }
    bool isFloat() const { return type() == Float; }
    bool isString() const { return type() == String; }
    bool isArray() const { return type() == ArrayType; }
    bool isObject() const { return type() == ObjectType; }
    int64_t asInt() const { return std::get<int64_t>(data); }
    double asFloat() const { return std::get<double>(data); }
    bool asBool() const { return std::get<bool>(data); }
    const std::string& asString() const { return std::get<std::string>(data); }
    const Array& asArray() const { return std::get<Array>(data); }
    const Object& asObject() const { return std::get<Object>(data); }
    Object& asObject() { return std::get<Object>(data); }
    Value& operator[](const std::string& key) { return std::get<Object>(data)[key]; }
    const Value& operator[](const std::string& key) const { return std::get<Object>(data).at(key); }
    std::string dump(int indent = 2, int level = 0, bool root = false) const {
        if (isNull()) return "null"; if (isBool()) return asBool() ? "true" : "false";
        if (isInt()) return std::to_string(asInt());
        if (isFloat()) { std::ostringstream oss; oss << std::fixed << std::setprecision(5) << asFloat(); std::string s = oss.str(); while (s.size() > 1 && s.back() == '0') s.pop_back(); if (s.back() == '.') s += '0'; return s; }
        if (isString()) return root ? asString() : "\"" + asString() + "\"";
        if (isArray()) { std::string s = "["; for (size_t i = 0; i < asArray().size(); ++i) { s += asArray()[i].dump(indent, level + 1); if (i + 1 < asArray().size()) s += ", "; } return s + "]"; }
        if (isObject()) {
            std::string pad(level * indent, ' '), next_pad((level + 1) * indent, ' '); std::string s = root ? "" : "{";
            bool first = true; for (auto const& [k, v] : asObject()) { if (k.empty() || (k[0] == '_' && !root)) continue; if (!first && !root) s += ","; if (!root) s += "\n" + next_pad; s += k + ": " + v.dump(indent, level + 1); if (root) s += "\n"; first = false; }
            return root ? s : (asObject().empty() ? "{}" : s + "\n" + pad + "}");
        }
        return "";
    }
};

struct Parser {
    std::string text; size_t pos = 0; std::chrono::steady_clock::time_point start; int depth = 0; bool nole;
    Parser(std::string t, bool n) : text(std::move(t)), start(std::chrono::steady_clock::now()), nole(n) {}
    char peek() { return pos < text.size() ? text[pos] : '\0'; }
    char advance() { return pos < text.size() ? text[pos++] : '\0'; }
    void skip_ws() { while (pos < text.size()) { if (isspace((unsigned char)text[pos])) advance(); else if (text[pos] == '#') { advance(); if (peek() == '#') { advance(); while (pos + 1 < text.size() && !(text[pos] == '#' && text[pos + 1] == '#')) advance(); if (pos < text.size()) { advance(); advance(); } } else { while (pos < text.size() && text[pos] != '\n') advance(); } } else break; } }
    std::string read_key() { skip_ws(); std::string k; if (peek() == '"' || peek() == '\'') { char q = advance(); while (peek() && peek() != q) { if (peek() == '\\') { advance(); char e = advance(); if (e == 'n') k += '\n'; else k += e; } else k += advance(); } if (peek() == q) advance(); } else { while (isalnum((unsigned char)peek()) || peek() == '_' || peek() == '-') k += advance(); } return k; }
    void parse_pair(Object& o) {
        skip_ws(); if (nole && peek() == '&') { advance(); std::string n = read_key(); skip_ws(); Value val; if (peek() == ':') { advance(); val = parse_value(); } else val = nullptr; if (o.find("_anchors") == o.end()) o["_anchors"] = Array{}; std::get<Array>(o["_anchors"].data).push_back(Object{{"name", n}, {"value", val}}); return; }
        bool is_m = false; if (nole && peek() == '<' && pos + 1 < text.size() && text[pos + 1] == '<') { advance(); advance(); is_m = true; }
        std::string k = is_m ? "<<" : read_key(); skip_ws(); if (peek() == ':') advance(); Value v = parse_value();
        if (is_m) { if (o.find("<<") == o.end()) o["<<"] = Array{}; std::get<Array>(o["<<"].data).push_back(v); }
        else { if (o.count(k)) throw std::runtime_error("Dup: " + k); o[k] = v; }
    }
    Value parse_value() {
        skip_ws(); if (std::chrono::steady_clock::now() - start > std::chrono::seconds(1)) throw std::runtime_error("Timeout"); if (++depth > 100) throw std::runtime_error("Depth");
        char c = peek(); Value r;
        if (c == '{') { advance(); Object o; while (peek() && peek() != '}') { skip_ws(); if (peek() == '}') break; parse_pair(o); skip_ws(); if (peek() == ',') advance(); } if (peek() == '}') advance(); r = o; }
        else if (c == '[') { advance(); Array a; while (peek() && peek() != ']') { skip_ws(); if (peek() == ']') break; a.push_back(parse_value()); skip_ws(); if (peek() == ',') advance(); } if (peek() == ']') advance(); r = a; }
        else if (nole && (c == '*' || c == '<')) { char t = advance(); if (t == '<') { std::string b; while (peek() && peek() != '>') b += advance(); if (peek() == '>') advance(); r = Object{{"_coerce", Object{{"type", b}, {"value", parse_value()}}}}; } else { std::string b; while (isalnum((unsigned char)peek()) || peek() == '_' || peek() == '-') b += advance(); r = "*" + b; } }
        else if (c == '"' || c == '\'') { char q = advance(); std::string b; while (peek() && peek() != q) { if (peek() == '\\') { advance(); char e = advance(); if (e == 'n') b += '\n'; else b += e; } else b += advance(); } if (peek() == q) advance(); r = b; }
        else if (isdigit((unsigned char)c) || c == '-') { std::string b; while (isdigit((unsigned char)peek()) || strchr(".-eE+", peek())) b += advance(); if (strchr(b.c_str(), '.')) r = std::stod(b); else r = (int64_t)std::stoll(b); }
        else { std::string b; while (isalpha((unsigned char)peek())) b += advance(); if (b == "true") r = true; else if (b == "false") r = false; else if (b == "null") r = nullptr; }
        depth--; return r;
    }
    Value parse() { Object root; while (pos < text.size()) { skip_ws(); if (!peek()) break; if (peek() == '[') { advance(); bool is_a = false; if (peek() == '*') { advance(); is_a = true; } std::string path; while (peek() && peek() != ']') path += advance(); if (peek() == ']') advance(); size_t s = 0, e; Object* curr = &root; while (true) { e = path.find('.', s); std::string p = path.substr(s, e - s); bool last = (e == std::string::npos); if (curr->find(p) == curr->end()) (*curr)[p] = last && is_a ? Value(Array{}) : Value(Object{}); if (last) { if (is_a) { std::get<Array>((*curr)[p].data).push_back(Object{}); parse_into(std::get<Object>(std::get<Array>((*curr)[p].data).back().data)); } else parse_into(std::get<Object>((*curr)[p].data)); break; } curr = &std::get<Object>((*curr)[p].data); s = e + 1; } } else parse_pair(root); } return root; }
    void parse_into(Object& o) { while (pos < text.size()) { skip_ws(); if (peek() == '[' || !peek()) break; parse_pair(o); } }
};

struct Evaluator {
    Object anchors; std::unordered_set<std::string> app_env, doc_env; Value root_val;
    Evaluator(std::vector<std::string> ae) : app_env(ae.begin(), ae.end()) {}
    Value evaluate(Value v) { root_val = collect_meta(std::move(v)); root_val = resolve_merges(std::move(root_val), 0); root_val = resolve_env(std::move(root_val)); Value clone = root_val; root_val = resolve_interp(std::move(root_val), clone, 0); return resolve_coerce(std::move(root_val)); }
    Value collect_meta(Value v) {
        if (!v.isObject()) return v; Object& o = v.asObject();
        if (o.count("_anchors")) { Array ans = std::get<Array>(o["_anchors"].data); o.erase("_anchors"); for (auto& av : ans) { Object& a = av.asObject(); std::string n = a["name"].asString(); Value val = a["value"]; anchors[n] = collect_meta(val.isNull() ? v : val); } }
        if (o.count("_env")) { Value& e = o["_env"]; if (e.isObject() && e.asObject().count("allowed")) { for (auto& x : e.asObject().at("allowed").asArray()) if (x.isString()) doc_env.insert(x.asString()); } }
        Object res; for (auto& [k, val] : o) res[k] = collect_meta(std::move(val)); return res;
    }
    Value resolve_merges(Value v, int depth) {
        if (!v.isObject() || depth > 20) return v; Object& o = v.asObject();
        if (o.count("<<")) { Array ma = o["<<"].isArray() ? o["<<"].asArray() : Array{o["<<"]}; o.erase("<<"); for (auto& m : ma) { Value rm = m; if (rm.isString() && !rm.asString().empty() && rm.asString()[0] == '*') { std::string an = rm.asString().substr(1); if (anchors.count(an)) rm = anchors[an]; } rm = resolve_merges(rm, depth + 1); if (rm.isObject()) for (auto& [mk, mv] : rm.asObject()) if (mk.find('_') != 0 && !o.count(mk)) o[mk] = mv; } }
        Object res; for (auto& [k, val] : o) res[k] = resolve_merges(std::move(val), depth); return res;
    }
    Value resolve_env(Value v) {
        if (v.isObject()) { Object& o = v.asObject(); if (o.size() == 1 && o.count("env") && o["env"].isString()) { std::string var = o["env"].asString(); if (app_env.empty() && doc_env.empty() || app_env.count(var) || doc_env.count(var)) { char* val = getenv(var.c_str()); return val ? std::string(val) : ""; } } Object res; for (auto& [k, val] : o) res[k] = resolve_env(std::move(val)); return res; }
        if (v.isArray()) { Array res; for (auto& x : v.asArray()) res.push_back(resolve_env(std::move(x))); return res; } return v;
    }
    Value resolve_interp(Value v, const Value& root, int depth) {
        if (v.isString() && v.asString().find("${") != std::string::npos && depth < 50) { std::string s = v.asString(), res; size_t i = 0; while (i < s.size()) { if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '{') { size_t end = s.find('}', i + 2); if (end == std::string::npos) break; std::string path = s.substr(i + 2, end - i - 2); size_t ps = 0, pe; const Value* curr = &root; while (true) { pe = path.find('.', ps); std::string p = path.substr(ps, pe - ps); if (!curr->isObject() || !curr->asObject().count(p)) throw std::runtime_error("Undef: " + p); curr = &curr->asObject().at(p); if (pe == std::string::npos) break; ps = pe + 1; } std::string vs = curr->isString() ? curr->asString() : curr->dump(2, 0, true); if (vs.find("${") != std::string::npos) vs = resolve_interp(vs, root, depth + 1).asString(); res += vs; i = end + 1; } else res += s[i++]; } return res; }
        if (v.isObject()) { Object res; for (auto& [k, val] : v.asObject()) res[k] = resolve_interp(std::move(val), root, depth); return res; }
        if (v.isArray()) { Array res; for (auto& x : v.asArray()) res.push_back(resolve_interp(std::move(x), root, depth)); return res; } return v;
    }
    Value resolve_coerce(Value v) {
        if (v.isObject()) { Object& o = v.asObject(); if (o.count("_coerce")) { Value c = std::move(o["_coerce"]); o.erase("_coerce"); if (c.isObject() && c.asObject().count("type") && c.asObject().count("value")) { std::string t = c.asObject().at("type").asString(); Value val = resolve_coerce(std::move(c.asObject().at("value"))); std::string s = val.isString() ? val.asString() : val.dump(2, 0, false); if (t == "int") return (int64_t)std::stoll(s); if (t == "float") return std::stod(s); if (t == "bool") return s == "true" || s == "TRUE"; return s; } } Object res; for (auto& [k, val] : o) res[k] = resolve_coerce(std::move(val)); return res; }
        if (v.isArray()) { Array res; for (auto& x : v.asArray()) res.push_back(resolve_coerce(std::move(x))); return res; } return v;
    }
};

inline Value parse(std::string text, std::vector<std::string> ae = {}) { Parser p(std::move(text), true); return Evaluator(ae).evaluate(p.parse()); }
}
#endif
