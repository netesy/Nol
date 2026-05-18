const { Document: NolDocument, Builder: NolBuilder } = require('./nol.js');

class Document extends NolDocument {}
class Builder extends NolBuilder {}

class Parser {
    constructor(text, nole = false) { this.text = text; this.pos = 0; this.start = Date.now(); this.depth = 0; this.nole = nole; }
    peek() { return this.text[this.pos] || null; }
    advance() { return this.text[this.pos++] || null; }
    skipWs() {
        while (this.pos < this.text.length) {
            if (/\s/.test(this.text[this.pos])) this.advance();
            else if (this.text[this.pos] === '#') {
                this.advance();
                if (this.peek() === '#') {
                    this.advance();
                    while (this.pos < this.text.length - 1 && this.text.slice(this.pos, this.pos + 2) !== '##') this.advance();
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
            const c = this.advance();
            if (c === q) return s;
            if (c === '\\') {
                const e = this.advance();
                if (e === 'u') { s += String.fromCharCode(parseInt(this.text.slice(this.pos, this.pos + 4), 16)); this.pos += 4; }
                else if (e === 'U') { s += String.fromCodePoint(parseInt(this.text.slice(this.pos, this.pos + 8), 16)); this.pos += 8; }
                else { s += { 'n': '\n', 'r': '\r', 't': '\t' }[e] || e; }
            } else s += c;
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
        if (Date.now() - this.start > 1000) throw new Error("Timeout");
        if (++this.depth > 100) throw new Error("Depth");
        const c = this.peek(); let res;
        if (c === '{') {
            this.advance(); const o = {};
            while (this.peek() && this.peek() !== '}') {
                this.skipWs(); this.parsePair(o);
                this.skipWs(); if (this.peek() === ',') this.advance();
            }
            if (this.peek() === '}') this.advance();
            res = o;
        } else if (c === '[') {
            this.advance(); const a = [];
            while (this.peek() && this.peek() !== ']') {
                this.skipWs(); a.push(this.parseValue());
                this.skipWs(); if (this.peek() === ',') this.advance();
            }
            if (this.peek() === ']') this.advance();
            res = a;
        } else if (this.nole && (c === '*' || c === '<')) {
            const t = this.advance();
            if (t === '<') {
                let b = ""; while (this.peek() && this.peek() !== '>') b += this.advance();
                if (this.peek() === '>') this.advance();
                res = { "_coerce": { type: b, value: this.parseValue() } };
            } else {
                let b = ""; while (this.peek() && /[a-zA-Z0-9_-]/.test(this.peek())) b += this.advance();
                res = "*" + b;
            }
        } else if (c === '"' || c === "'") res = this.readStr(this.advance());
        else if (c && (/[0-9]/.test(c) || c === '-')) {
            let s = "";
            while (this.pos < this.text.length && /[0-9.-eE+]/.test(this.text[this.pos])) s += this.advance();
            res = s.includes('.') || /e/i.test(s) ? parseFloat(s) : parseInt(s, 10);
        } else {
            let s = "";
            while (this.pos < this.text.length && /[a-zA-Z]/.test(this.text[this.pos])) s += this.advance();
            if (s === "true") res = true;
            else if (s === "false") res = false;
            else if (s === "null") res = null;
            else throw new Error("Invalid value: " + s);
        }
        this.depth--; return res;
    }
    parsePair(o) {
        this.skipWs();
        if (this.nole && this.peek() === '&') {
            this.advance(); const n = this.readKey(); this.skipWs();
            const val = (this.peek() === ':' && (this.advance())) ? this.parseValue() : null;
            if (!o._anchors) o._anchors = [];
            o._anchors.push({ name: n, value: val }); return;
        }
        let isM = false; if (this.nole && this.peek() === '<') { this.advance(); if (this.peek() === '<') { this.advance(); isM = true; } else this.pos--; }
        const k = isM ? "<<" : this.readKey();
        this.skipWs(); if (this.peek() === ':') this.advance();
        const v = this.parseValue();
        if (isM) { if (!o["<<"]) o["<<"] = []; o["<<"].push(v); }
        else { if (k in o) throw new Error("Dup: " + k); o[k] = v; }
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
                for (let i = 0; i < parts.length - 1; i++) {
                    const p = parts[i];
                    if (!(p in curr) || typeof curr[p] !== 'object') curr[p] = {};
                    curr = curr[p];
                }
                const last = parts[parts.length - 1];
                if (isA) { if (!Array.isArray(curr[last])) curr[last] = []; const entry = {}; curr[last].push(entry); this.parseInto(entry); }
                else { if (!(last in curr) || typeof curr[last] !== 'object') curr[last] = {}; this.parseInto(curr[last]); }
            } else this.parsePair(root);
        }
        return root;
    }
    parseInto(o) {
        while (this.pos < this.text.length) {
            this.skipWs(); if (this.peek() === '[' || !this.peek()) break;
            this.parsePair(o);
        }
    }
}

class Evaluator {
    constructor(appEnv) { this.anchors = {}; this.appEnv = new Set(appEnv); this.docEnv = new Set(); }
    evaluate(root) {
        root = this.collectMeta(root);
        root = this.resolveMerges(root);
        root = this.resolveEnv(root);
        root = this.resolveInterp(root, root);
        return new Document(this.resolveCoerce(root));
    }
    dump(v) {
        if (v === null) return "null"; if (typeof v === 'boolean') return String(v);
        if (typeof v === "number") { if (Number.isInteger(v)) return String(v); let s = v.toFixed(5); while (s.endsWith('0')) s = s.slice(0, -1); if (s.endsWith('.')) s += '0'; return s; }
        if (typeof v === 'string') return v;
        if (Array.isArray(v)) return "[" + v.map(x => this.dump(x)).join(", ") + "]";
        const items = Object.entries(v).filter(([k]) => !k.startsWith('_')).map(([k, val]) => `${k}: ${this.dump(val)}`);
        return "{" + items.join(", ") + "}";
    }
    collectMeta(v) {
        if (v !== null && typeof v === 'object' && !Array.isArray(v)) {
            if (v._anchors) { for (const a of v._anchors) { this.anchors[a.name] = this.collectMeta(a.value === null ? v : a.value); } delete v._anchors; }
            if (v._env && v._env.allowed) { for (const x of v._env.allowed) this.docEnv.add(x); }
            const res = {}; for (const [k, val] of Object.entries(v)) res[k] = this.collectMeta(val); return res;
        }
        if (Array.isArray(v)) return v.map(x => this.collectMeta(x));
        return v;
    }
    resolveMerges(v, depth = 0) {
        if (depth > 20) throw new Error("Merge depth");
        if (v !== null && typeof v === 'object' && !Array.isArray(v)) {
            if (v["<<"]) {
                const merges = Array.isArray(v["<<"]) ? v["<<"] : [v["<<"]]; delete v["<<"];
                for (const m of merges) {
                    let rm = m; if (typeof rm === 'string' && rm.startsWith('*')) rm = this.anchors[rm.slice(1)];
                    rm = this.resolveMerges(rm, depth + 1);
                    if (rm && typeof rm === 'object') { for (const [mk, mv] of Object.entries(rm)) if (!mk.startsWith('_') && !(mk in v)) v[mk] = mv; }
                }
            }
            const res = {}; for (const [k, val] of Object.entries(v)) res[k] = this.resolveMerges(val, depth); return res;
        }
        if (Array.isArray(v)) return v.map(x => this.resolveMerges(x, depth));
        return v;
    }
    resolveEnv(v) {
        if (v !== null && typeof v === 'object' && !Array.isArray(v)) {
            const keys = Object.keys(v);
            if (keys.length === 1 && keys[0] === "env" && typeof v.env === 'string') {
                if (this.appEnv.size === 0 && this.docEnv.size === 0 || this.appEnv.has(v.env) || this.docEnv.has(v.env)) return process.env[v.env] || "";
            }
            const res = {}; for (const [k, val] of Object.entries(v)) res[k] = this.resolveEnv(val); return res;
        }
        if (Array.isArray(v)) return v.map(x => this.resolveEnv(x));
        return v;
    }
    resolveInterp(v, root, depth = 0) {
        if (depth > 50) throw new Error("Interp depth");
        if (typeof v === 'string' && v.includes("${")) {
            let res = ""; let i = 0;
            while (i < v.length) {
                if (v.slice(i, i + 2) === "${") {
                    const end = v.indexOf("}", i + 2); if (end === -1) break;
                    const path = v.slice(i + 2, end);
                    let curr = root; for (const p of path.split('.')) { if (curr === null || typeof curr !== 'object' || !(p in curr)) throw new Error("Undef: " + p); curr = curr[p]; }
                    let vs = typeof curr === 'string' ? curr : this.dump(curr);
                    if (vs.includes("${")) vs = this.resolveInterp(vs, root, depth + 1);
                    res += vs; i = end + 1;
                } else { res += v[i]; i++; }
            }
            return res;
        }
        if (v !== null && typeof v === 'object' && !Array.isArray(v)) { const res = {}; for (const [k, val] of Object.entries(v)) res[k] = this.resolveInterp(val, root, depth); return res; }
        if (Array.isArray(v)) return v.map(x => this.resolveInterp(x, root, depth));
        return v;
    }
    resolveCoerce(v) {
        if (v !== null && typeof v === 'object' && !Array.isArray(v)) {
            if (v._coerce) {
                const { type: t, value: val } = v._coerce; const rv = this.resolveCoerce(val);
                const s = typeof rv === 'string' ? rv : this.dump(rv);
                if (t === "int") return parseInt(s, 10); if (t === "float") return parseFloat(s); if (t === "bool") return s.toLowerCase() === "true"; return s;
            }
            const res = {}; for (const [k, val] of Object.entries(v)) res[k] = this.resolveCoerce(val); return res;
        }
        if (Array.isArray(v)) return v.map(x => this.resolveCoerce(x));
        return v;
    }
}

function parse(text, appEnv = []) { return new Evaluator(appEnv).evaluate(new Parser(text, true).parse()); }
module.exports = { parse, Document, Builder };
