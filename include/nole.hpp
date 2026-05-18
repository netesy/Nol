#ifndef NOLE_HPP
#define NOLE_HPP
#include "nol.hpp"
#include <unordered_set>

namespace NOLE {
using namespace NOL;

class Evaluator {
    Object anchors;
    std::unordered_set<std::string> app_env, doc_env;
    Value root_val;
public:
    Evaluator(std::vector<std::string> ae) { for (auto& s : ae) app_env.insert(s); }
    Value evaluate(Value v) {
        root_val = collect_meta(std::move(v));
        root_val = resolve_merges(std::move(root_val), 0);
        root_val = resolve_env(std::move(root_val));
        Value clone = root_val;
        root_val = resolve_interp(std::move(root_val), clone, 0);
        return resolve_coerce(std::move(root_val));
    }
    Value collect_meta(Value v) {
        if (v.isObject()) {
            Object& o = v.asObject();
            if (o.count("_anchors") && o["_anchors"].isArray()) {
                Array ans = std::move(o["_anchors"].asArray()); o.erase("_anchors");
                for (auto& av : ans) {
                    if (av.isObject()) {
                        Object& a = av.asObject();
                        if (a.count("name") && a["name"].isString()) {
                            std::string n = a["name"].asString();
                            Value val = a.count("value") ? std::move(a["value"]) : Value(nullptr);
                            if (val.isNull()) val = v;
                            anchors[n] = collect_meta(std::move(val));
                        }
                    }
                }
            }
            if (o.count("_env") && o["_env"].isObject()) {
                Value& e = o["_env"];
                if (e.asObject().count("allowed") && e.asObject()["allowed"].isArray()) {
                    for (auto& x : e.asObject()["allowed"].asArray()) if (x.isString()) doc_env.insert(x.asString());
                }
            }
            for (auto& [k, val] : o) val = collect_meta(std::move(val));
        } else if (v.isArray()) {
            for (auto& x : v.asArray()) x = collect_meta(std::move(x));
        }
        return v;
    }
    Value resolve_merges(Value v, int depth) {
        if (!v.isObject() || depth > 20) return v;
        Object& o = v.asObject();
        if (o.count("<<")) {
            Value m_val = std::move(o["<<"]); o.erase("<<");
            Array ma = m_val.isArray() ? std::move(m_val.asArray()) : Array{std::move(m_val)};
            for (auto& m : ma) {
                Value rm = std::move(m);
                if (rm.isString() && !rm.asString().empty() && rm.asString()[0] == '*') {
                    std::string an = rm.asString().substr(1);
                    if (anchors.count(an)) rm = anchors[an];
                }
                rm = resolve_merges(std::move(rm), depth + 1);
                if (rm.isObject()) {
                    for (auto& [mk, mv] : rm.asObject()) {
                        if (mk.find('_') != 0 && !o.count(mk)) o[mk] = mv;
                    }
                }
            }
        }
        for (auto& [k, val] : o) val = resolve_merges(std::move(val), depth);
        return v;
    }
    Value resolve_env(Value v) {
        if (v.isObject()) {
            Object& o = v.asObject();
            if (o.size() == 1 && o.count("env") && o["env"].isString()) {
                std::string var = o["env"].asString();
                if ((app_env.empty() && doc_env.empty()) || app_env.count(var) || doc_env.count(var)) {
                    char* val = getenv(var.c_str());
                    return val ? std::string(val) : "";
                }
            }
            for (auto& [k, val] : o) val = resolve_env(std::move(val));
        } else if (v.isArray()) {
            for (auto& x : v.asArray()) x = resolve_env(std::move(x));
        }
        return v;
    }
    Value resolve_interp(Value v, const Value& root, int depth) {
        if (v.isString() && v.asString().find("${") != std::string::npos && depth < 50) {
            std::string s = v.asString(), res; size_t i = 0;
            while (i < s.size()) {
                if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '{') {
                    size_t end = s.find('}', i + 2);
                    if (end == std::string::npos) break;
                    std::string path = s.substr(i + 2, end - i - 2);
                    size_t ps = 0, pe; const Value* curr = &root;
                    bool found = true;
                    while (true) {
                        pe = path.find('.', ps);
                        std::string p = path.substr(ps, pe - ps);
                        if (!curr->isObject() || !curr->asObject().count(p)) { found = false; break; }
                        curr = &curr->asObject().at(p);
                        if (pe == std::string::npos) break;
                        ps = pe + 1;
                    }
                    if (found) {
                        std::string vs = curr->isString() ? curr->asString() : curr->dump(2, 0, true);
                        if (vs.find("${") != std::string::npos) vs = resolve_interp(vs, root, depth + 1).asString();
                        res += vs;
                    }
                    i = end + 1;
                } else res += s[i++];
            }
            return res;
        }
        if (v.isObject()) for (auto& [k, val] : v.asObject()) val = resolve_interp(std::move(val), root, depth);
        else if (v.isArray()) for (auto& x : v.asArray()) x = resolve_interp(std::move(x), root, depth);
        return v;
    }
    Value resolve_coerce(Value v) {
        if (v.isObject()) {
            Object& o = v.asObject();
            if (o.count("_coerce") && o["_coerce"].isObject()) {
                Object c = std::move(o["_coerce"].asObject()); o.erase("_coerce");
                if (c.count("type") && c["type"].isString() && c.count("value")) {
                    std::string t = c["type"].asString();
                    Value val = resolve_coerce(std::move(c["value"]));
                    std::string s = val.isString() ? val.asString() : val.dump(2, 0, false);
                    try {
                        if (t == "int") return (int64_t)std::stoll(s);
                        if (t == "float") return std::stod(s);
                        if (t == "bool") return s == "true" || s == "TRUE";
                    } catch(...) {}
                    return s;
                }
            }
            for (auto& [k, val] : o) val = resolve_coerce(std::move(val));
        } else if (v.isArray()) {
            for (auto& x : v.asArray()) x = resolve_coerce(std::move(x));
        }
        return v;
    }
};

struct Parser : public NOL::Parser {
    bool nole;
    Parser(std::string t, bool n) : NOL::Parser(std::move(t)), nole(n) {}
    Value parse_value() override {
        skip_ws();
        if (nole && peek() == '*') { advance(); std::string b; while (isalnum((unsigned char)peek())) b += advance(); return std::string("*" + b); }
        return NOL::Parser::parse_value();
    }
    void parse_pair(Object& o) {
        skip_ws();
        if (nole && peek() == '&') {
            advance(); std::string n = read_key(); skip_ws(); Object ai; ai["name"] = n;
            if (peek() == ':') { advance(); ai["value"] = parse_value(); } else ai["value"] = nullptr;
            if (!o.count("_anchors")) o["_anchors"] = Array{};
            o["_anchors"].asArray().push_back(std::move(ai)); return;
        }
        bool is_m = false; if (nole && peek() == '<') { advance(); if (peek() == '<') { advance(); is_m = true; } else pos--; }
        std::string k = is_m ? "<<" : read_key(); if (k.empty()) return;
        skip_ws(); if (peek() == ':') advance();
        Value v = parse_value();
        if (is_m) { if (!o.count("<<")) o["<<"] = Array{}; o["<<"].asArray().push_back(std::move(v)); }
        else o[k] = std::move(v);
    }
    Object parse() override {
        Object root;
        while (pos < text.size()) {
            skip_ws(); char c = peek(); if (!c) break;
            if (c == '[') {
                advance(); bool is_a = false; if (peek() == '*') { advance(); is_a = true; }
                std::string path; while (peek() && peek() != ']') path += advance(); if (peek() == ']') advance();
                size_t s = 0, e; Object* curr = &root;
                while (true) {
                    e = path.find('.', s); std::string p = path.substr(s, e - s); bool last = (e == std::string::npos);
                    if (last && is_a) {
                        if (curr->find(p) == curr->end() || !(*curr)[p].isArray()) (*curr)[p] = Array{};
                        Array& a = (*curr)[p].asArray(); a.push_back(Object{});
                        parse_into(a.back().asObject()); break;
                    }
                    if (curr->find(p) == curr->end() || !(*curr)[p].isObject()) (*curr)[p] = Object{};
                    if (last) { parse_into((*curr)[p].asObject()); break; }
                    curr = &(*curr)[p].asObject(); s = e + 1;
                }
            } else parse_pair(root);
        }
        return root;
    }
    void parse_into(Object& o) override { while (pos < text.size()) { skip_ws(); char c = peek(); if (c == '[' || !c) break; parse_pair(o); } }
};

inline Document parse(std::string text, std::vector<std::string> ae = {}) { return Document(Evaluator(ae).evaluate(Parser(std::move(text), true).parse())); }
}
#endif
