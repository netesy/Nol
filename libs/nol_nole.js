const fs = require('fs');
class NolError extends Error {
    constructor(m, l, c) { super(`${m}${l ? ` at ${l}:${c}` : ''}`); this.name = 'NolError'; this.line = l; this.col = c; }
}
const Tok = {
    EOF: 'EOF', Identifier: 'Identifier', String: 'String', Number: 'Number', Date: 'Date',
    True: 'True', False: 'False', Null: 'Null', Colon: 'Colon', Comma: 'Comma',
    LBracket: 'LBracket', RBracket: 'RBracket', LBrace: 'LBrace', RBrace: 'RBrace', Newline: 'Newline',
    Dot: 'Dot', Star: 'Star', LShift: 'LShift', Ampersand: 'Ampersand', LArrow: 'LArrow', RArrow: 'RArrow'
};
class Lexer {
    constructor(input) {
        if (input.length > 10 * 1024 * 1024) throw new NolError("Document size limit exceeded");
        if (input.includes('\0')) throw new NolError("Null bytes are prohibited");
        for (let i = 0; i < input.length; i++) { const cp = input.charCodeAt(i); if (cp >= 0xD800 && cp <= 0xDFFF) throw new NolError("Surrogate code points are prohibited"); }
        this.input = input; this.pos = 0; this.line = 1; this.col = 1;
    }
    peek(n = 0) { return this.pos + n < this.input.length ? this.input[this.pos + n] : '\0'; }
    advance() { const c = this.peek(); this.pos++; if (c === '\n') { this.line++; this.col = 1; } else this.col++; return c; }
    nextToken() {
        while (true) {
            const c = this.peek(); if (c === '\0') return { type: Tok.EOF, text: '', line: this.line, col: this.col };
            if (/\s/.test(c)) { if (c === '\n') { const t = { type: Tok.Newline, text: '\n', line: this.line, col: this.col }; this.advance(); return t; } this.advance(); continue; }
            if (c === '#') { this.advance(); if (this.peek() === '#') { this.advance(); while (this.peek() !== '\0') { if (this.peek() === '#' && this.peek(1) === '#') { this.advance(); this.advance(); break; } this.advance(); } } else { while (this.peek() !== '\0' && this.peek() !== '\n') this.advance(); } continue; }
            if (c === '"' || c === "'") return this.scanString();
            if (/\d/.test(c) || (c === '-' && /\d/.test(this.peek(1)))) return this.scanNumberOrDate();
            if (/[a-zA-Z_]/.test(c)) return this.scanIdentifier();
            const s = { ':': Tok.Colon, ',': Tok.Comma, '[': Tok.LBracket, ']': Tok.RBracket, '{': Tok.LBrace, '}': Tok.RBrace, '.': Tok.Dot, '*': Tok.Star, '&': Tok.Ampersand, '>': Tok.RArrow }[c];
            if (s) { const t = { type: s, text: c, line: this.line, col: this.col }; this.advance(); return t; }
            if (c === '<') { const l = this.line, col = this.col; this.advance(); if (this.peek() === '<') { this.advance(); return { type: Tok.LShift, text: '<<', line: l, col }; } return { type: Tok.LArrow, text: '<', line: l, col }; }
            throw new NolError(`Unexpected character: ${c}`, this.line, this.col);
        }
    }
    scanString() {
        const q = this.advance(); const l = this.line, col = this.col; const triple = this.peek() === q && this.peek(1) === q; if (triple) { this.advance(); this.advance(); }
        let res = '';
        while (true) {
            const c = this.peek(); if (c === '\0') throw new NolError("Unterminated string", l, col);
            if (triple) { if (c === q && this.peek(1) === q && this.peek(2) === q) { this.advance(); this.advance(); this.advance(); break; } }
            else { if (c === q) { this.advance(); break; } if (c === '\n') throw new NolError("Newline in string", l, col); }
            if (c === '\\' && q === '"') {
                this.advance(); const e = this.advance();
                switch (e) {
                    case '"': res += '"'; break; case "'": res += "'"; break; case '\\': res += '\\'; break;
                    case 'n': res += '\n'; break; case 'r': res += '\r'; break; case 't': res += '\t'; break;
                    case 'u': case 'U': { const n = e === 'u' ? 4 : 8; let u = ''; for (let i = 0; i < n; i++) u += this.advance(); const cp = parseInt(u, 16); if (cp === 0) throw new NolError("Null prohibited", l, col); if (cp >= 0xD800 && cp <= 0xDFFF) throw new NolError("Surrogate prohibited", l, col); res += String.fromCodePoint(cp); break; }
                    default: throw new NolError(`Invalid escape: \\${e}`, l, col);
                }
            } else res += this.advance();
            if (res.length > 1024 * 1024) throw new NolError("String length limit exceeded");
        }
        return { type: Tok.String, text: res, line: l, col };
    }
    scanNumberOrDate() {
        const l = this.line, c = this.col; let s = ''; while (/[a-zA-Z0-9\-\._:T\+]/.test(this.peek())) s += this.advance();
        if (/^\d{4}-\d{2}-\d{2}(T\d{2}:\d{2}:\d{2}(Z|[+-]\d{2}:\d{2})?)?$/.test(s)) return { type: Tok.Date, text: s, line: l, col: c };
        return { type: Tok.Number, text: s, line: l, col: c };
    }
    scanIdentifier() { const l = this.line, c = this.col; let s = ''; while (/[a-zA-Z0-9_-]/.test(this.peek())) s += this.advance(); const kw = { 'true': Tok.True, 'false': Tok.False, 'null': Tok.Null }; return { type: kw[s] || Tok.Identifier, text: s, line: l, col: c }; }
}
class Parser {
    constructor(i, n = false, e = []) { this.lexer = new Lexer(i); this.cur = this.lexer.nextToken(); this.isNole = n; this.appEnvAllowlist = new Set(e); this.docEnvAllowlist = new Set(); this.anchors = new Map(); this.root = {}; this.depth = 0; this.startTime = Date.now(); }
    error(m) { throw new NolError(m, this.cur.line, this.cur.col); }
    next() { this.cur = this.lexer.nextToken(); }
    skipNewlines() { while (this.cur.type === Tok.Newline) this.next(); }
    parse() {
        while (this.cur.type !== Tok.EOF) { this.skipNewlines(); if (this.cur.type === Tok.EOF) break; if (this.cur.type === Tok.LBracket) this.parseSection(); else if (this.isKeyToken()) this.parsePair(this.root); else this.error(`Unexpected token: ${this.cur.type}`); if (Date.now() - this.startTime > 1000) this.error("Parse timeout"); }
        if (this.isNole) { this.collectEnvAllowlist(this.root); this.root = this.resolveMerges(this.root); this.root = this.resolveEnv(this.root); this.root = this.resolveInterpolations(this.root); this.root = this.resolveCoercions(this.root); }
        return this.root;
    }
    collectEnvAllowlist(o) { if (o && typeof o === 'object') { if (o._env && o._env.allowed && Array.isArray(o._env.allowed)) o._env.allowed.forEach(a => this.docEnvAllowlist.add(String(a))); Object.values(o).forEach(v => this.collectEnvAllowlist(v)); } }
    isKeyToken() { return [Tok.Identifier, Tok.String, Tok.LShift].includes(this.cur.type); }
    parseSection() {
        this.next(); let path = [], isArray = false; while (true) { if (this.cur.type === Tok.Star) { isArray = true; this.next(); } else if ([Tok.Identifier, Tok.String].includes(this.cur.type)) { path.push(this.cur.text.normalize('NFC')); this.next(); } else this.error("Expected section name"); if (this.cur.type === Tok.Dot) { this.next(); continue; } if (this.cur.type === Tok.RBracket) { this.next(); break; } this.error("Expected . or ]"); }
        let target = this.root;
        for (let i = 0; i < path.length; i++) { const part = path[i]; if (part in target) { if (typeof target[part] !== 'object' && !(isArray && i === path.length - 1 && Array.isArray(target[part]))) this.error(`Collision at ${part}`); } else target[part] = (isArray && i === path.length - 1) ? [] : {}; if (!(isArray && i === path.length - 1)) target = target[part]; }
        if (isArray) { const arr = target[path[path.length - 1]], newObj = {}; arr.push(newObj); target = newObj; }
        this.skipNewlines(); while (this.isKeyToken()) { this.parsePair(target); this.skipNewlines(); }
    }
    parsePair(obj) { const k = this.cur.type === Tok.LShift ? '<<' : this.cur.text.normalize('NFC'); if (['_env', '_interpolate', '_meta'].includes(k) && !this.isNole) this.error(`Reserved key ${k}`); if (k in obj && k !== '<<') this.error(`Duplicate key: ${k}`); this.next(); if (this.cur.type !== Tok.Colon) this.error("Expected :"); this.next(); const val = this.parseValue(); if (k === '<<') { if (!obj['<<']) obj['<<'] = []; obj['<<'].push(val); } else obj[k] = val; }
    parseValue() {
        this.depth++; if (this.depth > 100) this.error("Nesting limit exceeded");
        let res;
        switch (this.cur.type) {
            case Tok.Ampersand: this.next(); const n = this.cur.text; this.next(); res = this.parseValue(); this.anchors.set(n, res); break;
            case Tok.Star: this.next(); res = '*' + this.cur.text; this.next(); break;
            case Tok.LArrow: this.next(); const t = this.cur.text; this.next(); if (this.cur.type !== Tok.RArrow) this.error("Expected >"); this.next(); res = { _coerce: { type: t, value: this.parseValue() } }; break;
            case Tok.String: res = this.cur.text; this.next(); break;
            case Tok.Number: const s = this.cur.text; this.next(); res = (s.includes('.') || s.toLowerCase().includes('e')) ? parseFloat(s) : parseInt(s, 10); break;
            case Tok.True: res = true; this.next(); break; case Tok.False: res = false; this.next(); break; case Tok.Null: res = null; this.next(); break;
            case Tok.Date: res = this.cur.text; this.next(); break;
            case Tok.LBracket: res = this.parseArray(); break; case Tok.LBrace: res = this.parseObject(); break;
            default: this.error(`Invalid value: ${this.cur.type}`);
        }
        this.depth--; return res;
    }
    parseArray() { this.next(); const res = []; while (this.cur.type !== Tok.RBracket) { this.skipNewlines(); if (this.cur.type === Tok.RBracket) break; res.push(this.parseValue()); this.skipNewlines(); if (this.cur.type === Tok.Comma) this.next(); } this.next(); if (res.length > 100000) this.error("Array size limit"); return res; }
    parseObject() { this.next(); const res = {}; while (this.cur.type !== Tok.RBrace) { this.skipNewlines(); if (this.cur.type === Tok.RBrace) break; this.parsePair(res); this.skipNewlines(); if (this.cur.type === Tok.Comma) this.next(); } this.next(); if (Object.keys(res).length > 10000) this.error("Object keys limit"); return res; }
    resolveMerges(val, visited = new Set()) {
        if (visited.has(val)) throw new NolError("Cycle detected");
        if (val && typeof val === 'object' && !Array.isArray(val)) {
            visited.add(val); if (val['<<']) { const merges = val['<<']; delete val['<<']; for (let m of merges) { let rm = this.resolveMerges(m, visited); if (typeof rm === 'string' && rm.startsWith('*')) rm = this.resolveMerges(this.anchors.get(rm.substring(1)), visited); if (!rm || typeof rm !== 'object' || Array.isArray(rm)) throw new NolError("Can only merge objects"); for (let [k, v] of Object.entries(rm)) if (!(k in val)) val[k] = v; } }
            for (let k of Object.keys(val)) val[k] = this.resolveMerges(val[k], visited); visited.delete(val);
        } else if (Array.isArray(val)) { visited.add(val); for (let i = 0; i < val.length; i++) val[i] = this.resolveMerges(val[i], visited); visited.delete(val); }
        else if (typeof val === 'string' && val.startsWith('*')) { const a = val.substring(1); if (!this.anchors.has(a)) throw new NolError(`Undefined anchor: ${a}`); return this.resolveMerges(this.anchors.get(a), visited); }
        return val;
    }
    resolveEnv(v) {
        if (v && typeof v === 'object' && !Array.isArray(v)) { if (v.env && Object.keys(v).length === 1 && typeof v.env === 'string') { if (this.appEnvAllowlist.has(v.env) && this.docEnvAllowlist.has(v.env)) return process.env[v.env] || ''; throw new NolError(`Env denied: ${v.env}`); } for (let k of Object.keys(v)) v[k] = this.resolveEnv(v[k]); }
        else if (Array.isArray(v)) { for (let i = 0; i < v.length; i++) v[i] = this.resolveEnv(v[i]); } return v;
    }
    resolveInterpolations(v) { if (v && typeof v === 'object' && !Array.isArray(v)) { for (let k of Object.keys(v)) v[k] = this.resolveInterpolations(v[k]); } else if (Array.isArray(v)) { for (let i = 0; i < v.length; i++) v[i] = this.resolveInterpolations(v[i]); } else if (typeof v === 'string') return this.resolveInterpolation(v); return v; }
    resolveInterpolation(s, depth = 0) {
        if (depth > 50) throw new NolError("Interpolation depth limit");
        const res = s.replace(/\$\{([^}]+)\}/g, (_, path) => { const parts = path.split('.'); let curr = this.root; for (let p of parts) { if (!curr || typeof curr !== 'object' || !(p in curr)) throw new NolError(`Undefined: ${path}`); curr = curr[p]; } if (!['string', 'number', 'boolean'].includes(typeof curr) && curr !== null) throw new NolError("Not scalar"); const vs = String(curr); return vs.includes('${') ? this.resolveInterpolation(vs, depth + 1) : vs; });
        if (res.length > 10 * 1024 * 1024) throw new NolError("Interpolation size limit"); return res;
    }
    resolveCoercions(v) { if (v && typeof v === 'object' && !Array.isArray(v)) { if (v._coerce && Object.keys(v).length === 1) { const { type: t, value: rv } = v._coerce; const val = this.resolveCoercions(rv); switch (t) { case 'int': return parseInt(val, 10); case 'float': return parseFloat(val); case 'bool': return typeof val === 'boolean' ? val : (String(val).toLowerCase() === 'true'); case 'string': return String(val); default: throw new NolError(`Unknown coercion: ${t}`); } } for (let k of Object.keys(v)) v[k] = this.resolveCoercions(v[k]); } else if (Array.isArray(v)) { for (let i = 0; i < v.length; i++) v[i] = this.resolveCoercions(v[i]); } return v; }
}
class Document {
    constructor(r) { this.root = r; }
    get(p) { if (!p) return this.root; let curr = this.root; for (let part of p.split('.')) { if (!curr || typeof curr !== 'object' || !(part in curr)) return undefined; curr = curr[part]; } return curr; }
    static parse(s, n = false, e = []) { return new Document(new Parser(s, n, e).parse()); }
}
function parse(i, n = false, e = []) { return new Parser(i, n, e).parse(); }
module.exports = { parse, NolError, Document };
