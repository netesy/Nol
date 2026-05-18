const fs = require('fs');

class NolError extends Error {
    constructor(msg, line, col) {
        super(`${msg} at ${line}:${col}`);
        this.line = line; this.col = col;
    }
}

class NolParser {
    constructor(text, nole = false) {
        this.text = text; this.pos = 0; this.line = 1; this.col = 1;
        this.startTime = Date.now(); this.depth = 0; this.nole = nole;
    }
    peek(n = 0) { return this.text[this.pos + n] || null; }
    advance() {
        const c = this.peek(); this.pos++;
        if (c === '\n') { this.line++; this.col = 1; }
        else this.col++;
        return c;
    }
    skipWs() {
        while (this.pos < this.text.length) {
            const c = this.text[this.pos];
            if (/\s/.test(c)) this.advance();
            else if (c === '#') {
                this.advance();
                if (this.peek() === '#') {
                    this.advance();
                    while (this.pos < this.text.length - 1 && !(this.text[this.pos] === '#' && this.text[this.pos+1] === '#')) this.advance();
                    if (this.pos < this.text.length) { this.advance(); this.advance(); }
                } else {
                    while (this.pos < this.text.length && this.text[this.pos] !== '\n') this.advance();
                }
            } else break;
        }
    }
    readStr(q) {
        let s = "";
        while (this.pos < this.text.length) {
            const c = this.peek();
            if (c === q) { this.advance(); break; }
            if (c === '\\') {
                this.advance(); const e = this.advance();
                if (e === 'u') {
                    let h = this.text.slice(this.pos, this.pos + 4); this.pos += 4;
                    s += String.fromCharCode(parseInt(h, 16));
                } else if (e === 'U') {
                    let h = this.text.slice(this.pos, this.pos + 8); this.pos += 8;
                    s += String.fromCodePoint(parseInt(h, 16));
                } else s += {'n': '\n', 'r': '\r', 't': '\t'}[e] || e;
            } else s += this.advance();
        }
        return s;
    }
    readKey() {
        this.skipWs(); const c = this.peek();
        if (c === '"' || c === "'") return this.readStr(this.advance());
        let k = "";
        while (this.pos < this.text.length && /[a-zA-Z0-9_-]/.test(this.text[this.pos])) k += this.advance();
        return k;
    }
    parseValue() {
        this.skipWs();
        if (Date.now() - this.startTime > 1000) throw new NolError("Timeout", this.line, this.col);
        if (++this.depth > 100) throw new NolError("Max depth", this.line, this.col);
        const c = this.peek(); let res;
        if (c === '{') {
            this.advance(); const o = {};
            while (this.pos < this.text.length) {
                this.skipWs(); if (this.peek() === '}') { this.advance(); break; }
                this.parsePair(o); this.skipWs(); if (this.peek() === ',') this.advance();
            }
            res = o;
        } else if (c === '[') {
            this.advance(); const a = [];
            while (this.pos < this.text.length) {
                this.skipWs(); if (this.peek() === ']') { this.advance(); break; }
                a.push(this.parseValue()); this.skipWs(); if (this.peek() === ',') this.advance();
            }
            res = a;
        } else if (this.nole && (c === '*' || c === '<')) {
            const t = this.advance();
            if (t === '<') {
                let b = ""; while (this.peek() && this.peek() !== '>') b += this.advance();
                if (this.peek() === '>') this.advance();
                res = {"_coerce": {"type": b, "value": this.parseValue()}};
            } else {
                let b = ""; while (this.peek() && /[a-zA-Z0-9_-]/.test(this.peek())) b += this.advance();
                res = "*" + b;
            }
        } else if (c === '"' || c === "'") res = this.readStr(this.advance());
        else if (/[0-9-]/.test(c)) {
            let b = ""; while (this.peek() && /[0-9.eE+-]/.test(this.peek())) b += this.advance();
            res = b.includes('.') || /[eE]/.test(b) ? parseFloat(b) : parseInt(b);
        } else {
            let b = ""; while (this.peek() && /[a-z]/.test(this.peek())) b += this.advance();
            if (b === "true") res = true; else if (b === "false") res = false; else if (b === "null") res = null;
            else throw new NolError("Invalid value: " + b, this.line, this.col);
        }
        this.depth--; return res;
    }
    parsePair(obj) {
        this.skipWs();
        if (this.nole && this.peek() === '&') {
            this.advance(); const n = this.readKey(); this.skipWs();
            let val = null; if (this.peek() === ':') { this.advance(); val = this.parseValue(); }
            if (!obj._anchors) obj._anchors = [];
            obj._anchors.push({name: n, value: val}); return;
        }
        let isM = false; if (this.nole && this.peek() === '<' && this.peek(1) === '<') { this.advance(); this.advance(); isM = true; }
        const key = isM ? "<<" : this.readKey().normalize('NFC');
        this.skipWs(); if (this.advance() !== ':') throw new NolError("Expected :", this.line, this.col);
        const val = this.parseValue();
        if (isM) { if (!obj["<<"]) obj["<<"] = []; obj["<<"].push(val); }
        else {
            if (Object.prototype.hasOwnProperty.call(obj, key)) throw new Error("Duplicate key: " + key);
            obj[key] = val;
        }
    }
    parseInto(obj) {
        while (this.pos < this.text.length) {
            this.skipWs(); if (!this.peek() || this.peek() === '[') break;
            this.parsePair(obj);
        }
    }
    parse() {
        const root = {};
        while (this.pos < this.text.length) {
            this.skipWs(); if (!this.peek()) break;
            if (this.peek() === '[') {
                this.advance(); let isA = false; if (this.peek() === '*') { this.advance(); isA = true; }
                let path = ""; while (this.peek() && this.peek() !== ']') path += this.advance();
                if (this.peek() === ']') this.advance();
                const parts = path.split('.'); let curr = root;
                for (let i = 0; i < parts.length; i++) {
                    const p = parts[i], last = i === parts.length - 1;
                    if (!curr[p]) curr[p] = (last && isA) ? [] : {};
                    if (last) {
                        if (isA) { const entry = {}; curr[p].push(entry); this.parseInto(entry); }
                        else this.parseInto(curr[p]);
                    } else curr = curr[p];
                }
            } else this.parsePair(root);
        }
        return root;
    }
}

class Evaluator {
    constructor(appEnv = []) { this.anchors = {}; this.appEnv = new Set(appEnv); this.docEnv = new Set(); }
    evaluate(root) {
        this.root = this.collectMeta(root);
        this.root = this.resolveMerges(this.root);
        this.root = this.resolveEnv(this.root);
        const clone = JSON.parse(JSON.stringify(this.root));
        this.root = this.resolveInterp(this.root, clone);
        return this.resolveCoerce(this.root);
    }
    collectMeta(v) {
        if (v && typeof v === 'object' && !Array.isArray(v)) {
            if (v._anchors) {
                const ans = v._anchors; delete v._anchors;
                for (const a of ans) this.anchors[a.name] = this.collectMeta(a.value === null ? v : a.value);
            }
            if (v._env && typeof v._env === 'object' && Array.isArray(v._env.allowed)) {
                for (const x of v._env.allowed) this.docEnv.add(x);
            }
            for (const k in v) v[k] = this.collectMeta(v[k]);
        } else if (Array.isArray(v)) return v.map(x => this.collectMeta(x));
        return v;
    }
    resolveMerges(v, depth = 0) {
        if (depth > 20) throw new Error("Max merge depth");
        if (v && typeof v === 'object' && !Array.isArray(v)) {
            if (v["<<"]) {
                let merges = v["<<"]; delete v["<<"]; if (!Array.isArray(merges)) merges = [merges];
                for (let m of merges) {
                    let rm = m; if (typeof rm === 'string' && rm.startsWith("*")) rm = this.anchors[rm.slice(1)];
                    rm = this.resolveMerges(rm, depth + 1);
                    if (rm && typeof rm === 'object' && !Array.isArray(rm)) {
                        for (const mk in rm) if (!mk.startsWith("_") && !Object.prototype.hasOwnProperty.call(v, mk)) v[mk] = JSON.parse(JSON.stringify(rm[mk]));
                    }
                }
            }
            for (const k in v) v[k] = this.resolveMerges(v[k], depth);
        } else if (Array.isArray(v)) return v.map(x => this.resolveMerges(x, depth));
        return v;
    }
    resolveEnv(v) {
        if (v && typeof v === 'object' && !Array.isArray(v)) {
            const keys = Object.keys(v);
            if (keys.length === 1 && keys[0] === 'env' && typeof v.env === 'string') {
                if (this.appEnv.size === 0 && this.docEnv.size === 0 || this.appEnv.has(v.env) || this.docEnv.has(v.env)) return process.env[v.env] || "";
            }
            for (const k in v) v[k] = this.resolveEnv(v[k]);
        } else if (Array.isArray(v)) return v.map(x => this.resolveEnv(x));
        return v;
    }
    resolveInterp(v, root, depth = 0) {
        if (depth > 50) throw new Error("Max interp depth");
        if (typeof v === 'string' && v.includes("${")) {
            return v.replace(/\$\{([^}]+)\}/g, (_, path) => {
                let curr = root; for (const p of path.split('.')) curr = curr[p];
                let vs = typeof curr === 'string' ? curr : dump(curr, 2, 0, true);
                if (vs.includes("${")) vs = this.resolveInterp(vs, root, depth + 1);
                return vs;
            });
        }
        if (v && typeof v === 'object' && !Array.isArray(v)) { for (const k in v) v[k] = this.resolveInterp(v[k], root, depth); }
        else if (Array.isArray(v)) return v.map(x => this.resolveInterp(x, root, depth));
        return v;
    }
    resolveCoerce(v) {
        if (v && typeof v === 'object' && !Array.isArray(v)) {
            if (v._coerce) {
                const {type, value} = v._coerce; delete v._coerce;
                const val = this.resolveCoerce(value);
                const s = typeof val === 'string' ? val : dump(val, 2);
                if (type === "int") return parseInt(s); if (type === "float") return parseFloat(s);
                if (type === "bool") return s.toLowerCase() === "true"; return s;
            }
            for (const k in v) v[k] = this.resolveCoerce(v[k]);
        } else if (Array.isArray(v)) return v.map(x => this.resolveCoerce(x));
        return v;
    }
}

function parse(text, appEnv = []) {
    const p = new NolParser(text, true);
    return new Evaluator(appEnv).evaluate(p.parse());
}

function dump(v, indent = 2, level = 0, root = false) {
    if (v === null) return "null";
    if (typeof v === 'boolean') return v ? "true" : "false";
    if (typeof v === 'number') {
        let s = v.toFixed(5); while (s.endsWith('0')) s = s.slice(0, -1); if (s.endsWith('.')) s += '0';
        return s;
    }
    if (typeof v === 'string') return root ? v : `"${v}"`;
    if (Array.isArray(v)) return "[" + v.map(x => dump(x, indent, level + 1)).join(", ") + "]";
    const pad = " ".repeat(level * indent), nextPad = " ".repeat((level + 1) * indent);
    const keys = Object.keys(v).filter(k => !k.startsWith("_")).sort();
    if (root) return keys.map(k => `${k}: ${dump(v[k], indent, level + 1)}`).join("\n");
    if (keys.length === 0) return "{}";
    return "{" + keys.map(k => `\n${nextPad}${k}: ${dump(v[k], indent, level + 1)}`).join(",") + `\n${pad}}`;
}

module.exports = { parse, dump };
