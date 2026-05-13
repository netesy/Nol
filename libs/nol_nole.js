const fs = require('fs');
const path = require('path');

class NolError extends Error {
    constructor(message, line, col) {
        super(`${message}${line ? ` at ${line}:${col}` : ''}`);
        this.name = 'NolError';
        this.line = line;
        this.col = col;
    }
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
        this.input = input;
        this.pos = 0;
        this.line = 1;
        this.col = 1;
    }

    peek(n = 0) {
        return this.pos + n < this.input.length ? this.input[this.pos + n] : '\0';
    }

    advance() {
        const c = this.peek();
        this.pos++;
        if (c === '\n') {
            this.line++;
            this.col = 1;
        } else {
            this.col++;
        }
        return c;
    }

    nextToken() {
        while (true) {
            const c = this.peek();
            if (c === '\0') return { type: Tok.EOF, text: '', line: this.line, col: this.col };
            if (/\s/.test(c)) {
                if (c === '\n') {
                    const t = { type: Tok.Newline, text: '\n', line: this.line, col: this.col };
                    this.advance();
                    return t;
                }
                this.advance();
                continue;
            }
            if (c === '#') {
                this.advance();
                if (this.peek() === '#') {
                    this.advance();
                    while (this.peek() !== '\0') {
                        if (this.peek() === '#' && this.peek(1) === '#') {
                            this.advance(); this.advance();
                            break;
                        }
                        this.advance();
                    }
                } else {
                    while (this.peek() !== '\0' && this.peek() !== '\n') this.advance();
                }
                continue;
            }
            if (c === '"' || c === "'") return this.scanString();
            if (/\d/.test(c) || (c === '-' && /\d/.test(this.peek(1)))) return this.scanNumberOrDate();
            if (/[a-zA-Z_]/.test(c)) return this.scanIdentifier();

            const single = {
                ':': Tok.Colon, ',': Tok.Comma, '[': Tok.LBracket, ']': Tok.RBracket,
                '{': Tok.LBrace, '}': Tok.RBrace, '.': Tok.Dot, '*': Tok.Star, '&': Tok.Ampersand, '>': Tok.RArrow
            }[c];
            if (single) {
                const t = { type: single, text: c, line: this.line, col: this.col };
                this.advance();
                return t;
            }
            if (c === '<') {
                const line = this.line, col = this.col;
                this.advance();
                if (this.peek() === '<') {
                    this.advance();
                    return { type: Tok.LShift, text: '<<', line, col };
                }
                return { type: Tok.LArrow, text: '<', line, col };
            }
            throw new NolError(`Unexpected character: ${c}`, this.line, this.col);
        }
    }

    scanString() {
        const quote = this.advance();
        const line = this.line, col = this.col;
        const isTriple = this.peek() === quote && this.peek(1) === quote;
        if (isTriple) { this.advance(); this.advance(); }
        let res = '';
        while (true) {
            const c = this.peek();
            if (c === '\0') throw new NolError("Unterminated string", line, col);
            if (isTriple) {
                if (c === quote && this.peek(1) === quote && this.peek(2) === quote) {
                    this.advance(); this.advance(); this.advance();
                    break;
                }
            } else {
                if (c === quote) { this.advance(); break; }
                if (c === '\n') throw new NolError("Newline in single-quoted string", line, col);
            }
            if (c === '\\' && quote === '"') {
                this.advance();
                const e = this.advance();
                switch (e) {
                    case '"': res += '"'; break;
                    case "'": res += "'"; break;
                    case '\\': res += '\\'; break;
                    case 'n': res += '\n'; break;
                    case 'r': res += '\r'; break;
                    case 't': res += '\t'; break;
                    case 'u': case 'U': {
                        const n = e === 'u' ? 4 : 8;
                        let u = '';
                        for (let i = 0; i < n; i++) u += this.advance();
                        res += String.fromCodePoint(parseInt(u, 16));
                        break;
                    }
                    default: throw new NolError(`Invalid escape: \\${e}`, this.line, this.col);
                }
            } else {
                res += this.advance();
            }
            if (res.length > 1024 * 1024) throw new NolError("String length limit exceeded");
        }
        return { type: Tok.String, text: res, line, col };
    }

    scanNumberOrDate() {
        const line = this.line, col = this.col;
        let s = '';
        while (/[a-zA-Z0-9\-\._:T\+]/.test(this.peek())) s += this.advance();
        if (/^\d{4}-\d{2}-\d{2}(T\d{2}:\d{2}:\d{2}(Z|[+-]\d{2}:\d{2})?)?$/.test(s))
            return { type: Tok.Date, text: s, line, col };
        return { type: Tok.Number, text: s, line, col };
    }

    scanIdentifier() {
        const line = this.line, col = this.col;
        let s = '';
        while (/[a-zA-Z0-9_-]/.test(this.peek())) s += this.advance();
        const kw = { 'true': Tok.True, 'false': Tok.False, 'null': Tok.Null };
        return { type: kw[s] || Tok.Identifier, text: s, line, col };
    }
}

class Parser {
    constructor(input, isNole = false, envAllowlist = []) {
        this.lexer = new Lexer(input);
        this.cur = this.lexer.nextToken();
        this.isNole = isNole;
        this.appEnvAllowlist = new Set(envAllowlist);
        this.docEnvAllowlist = new Set();
        this.anchors = new Map();
        this.root = {};
        this.depth = 0;
        this.startTime = Date.now();
    }

    error(msg) { throw new NolError(msg, this.cur.line, this.cur.col); }
    next() { this.cur = this.lexer.nextToken(); }
    skipNewlines() { while (this.cur.type === Tok.Newline) this.next(); }

    parse() {
        while (this.cur.type !== Tok.EOF) {
            this.skipNewlines();
            if (this.cur.type === Tok.EOF) break;
            if (this.cur.type === Tok.LBracket) this.parseSection();
            else if (this.isKeyToken()) this.parsePair(this.root);
            else this.error(`Unexpected token: ${this.cur.type}`);
            if (Date.now() - this.startTime > 1000) this.error("Parse timeout");
        }
        if (this.isNole) {
            if (this.root._env && typeof this.root._env === 'object' && Array.isArray(this.root._env.allowed)) {
                this.root._env.allowed.forEach(a => this.docEnvAllowlist.add(a));
            }
            this.root = this.resolveMerges(this.root);
            this.root = this.resolveEnv(this.root);
            this.root = this.resolveInterpolations(this.root);
            this.root = this.resolveCoercions(this.root);
        }
        return this.root;
    }

    isKeyToken() { return [Tok.Identifier, Tok.String, Tok.LShift].includes(this.cur.type); }

    parseSection() {
        this.next();
        let path = [], isArray = false;
        while (true) {
            if (this.cur.type === Tok.Star) { isArray = true; this.next(); }
            else if ([Tok.Identifier, Tok.String].includes(this.cur.type)) {
                path.push(this.cur.text.normalize('NFC'));
                this.next();
            } else this.error("Expected section name");
            if (this.cur.type === Tok.Dot) { this.next(); continue; }
            if (this.cur.type === Tok.RBracket) { this.next(); break; }
            this.error("Expected . or ]");
        }
        let target = this.root;
        for (let i = 0; i < path.length; i++) {
            const part = path[i];
            if (part in target) {
                if (typeof target[part] !== 'object' && !(isArray && i === path.length - 1 && Array.isArray(target[part])))
                    this.error(`Collision at ${part}`);
            } else target[part] = (isArray && i === path.length - 1) ? [] : {};
            if (!(isArray && i === path.length - 1)) target = target[part];
        }
        if (isArray) {
            const arr = target[path[path.length - 1]];
            const newObj = {};
            arr.push(newObj);
            target = newObj;
        }
        this.skipNewlines();
        while (this.isKeyToken()) { this.parsePair(target); this.skipNewlines(); }
    }

    parsePair(obj) {
        const key = this.cur.type === Tok.LShift ? '<<' : this.cur.text.normalize('NFC');
        if (['_env', '_interpolate', '_meta'].includes(key) && !this.isNole) this.error(`Reserved key ${key}`);
        if (key in obj && key !== '<<') this.error(`Duplicate key: ${key}`);
        this.next();
        if (this.cur.type !== Tok.Colon) this.error("Expected :");
        this.next();
        const val = this.parseValue();
        if (key === '<<') {
            if (!obj['<<']) obj['<<'] = [];
            obj['<<'].push(val);
        } else obj[key] = val;
    }

    parseValue() {
        this.depth++;
        if (this.depth > 100) this.error("Nesting limit exceeded");
        let res;
        switch (this.cur.type) {
            case Tok.Ampersand:
                this.next(); const name = this.cur.text; this.next();
                res = this.parseValue(); this.anchors.set(name, res); break;
            case Tok.Star:
                this.next(); res = '*' + this.cur.text; this.next(); break;
            case Tok.LArrow:
                this.next(); const t = this.cur.text; this.next();
                if (this.cur.type !== Tok.RArrow) this.error("Expected >");
                this.next(); res = { _coerce: { type: t, value: this.parseValue() } }; break;
            case Tok.String: res = this.cur.text; this.next(); break;
            case Tok.Number:
                const s = this.cur.text; this.next();
                res = (s.includes('.') || s.toLowerCase().includes('e')) ? parseFloat(s) : parseInt(s, 10);
                break;
            case Tok.True: res = true; this.next(); break;
            case Tok.False: res = false; this.next(); break;
            case Tok.Null: res = null; this.next(); break;
            case Tok.Date: res = this.cur.text; this.next(); break;
            case Tok.LBracket: res = this.parseArray(); break;
            case Tok.LBrace: res = this.parseObject(); break;
            default: this.error(`Invalid value: ${this.cur.type}`);
        }
        this.depth--;
        return res;
    }

    parseArray() {
        this.next();
        const res = [];
        while (this.cur.type !== Tok.RBracket) {
            this.skipNewlines();
            if (this.cur.type === Tok.RBracket) break;
            res.push(this.parseValue());
            this.skipNewlines();
            if (this.cur.type === Tok.Comma) this.next();
        }
        this.next();
        if (res.length > 100000) this.error("Array size limit");
        return res;
    }

    parseObject() {
        this.next();
        const res = {};
        while (this.cur.type !== Tok.RBrace) {
            this.skipNewlines();
            if (this.cur.type === Tok.RBrace) break;
            this.parsePair(res);
            this.skipNewlines();
            if (this.cur.type === Tok.Comma) this.next();
        }
        this.next();
        if (Object.keys(res).length > 10000) this.error("Object keys limit");
        return res;
    }

    resolveMerges(val, visited = new Set()) {
        if (visited.has(val)) throw new NolError("Cycle detected");
        if (val && typeof val === 'object' && !Array.isArray(val)) {
            visited.add(val);
            if (val['<<']) {
                const merges = val['<<'];
                delete val['<<'];
                for (let m of merges) {
                    let resolvedM = this.resolveMerges(m, visited);
                    if (typeof resolvedM === 'string' && resolvedM.startsWith('*'))
                        resolvedM = this.resolveMerges(this.anchors.get(resolvedM.substring(1)), visited);
                    if (!resolvedM || typeof resolvedM !== 'object' || Array.isArray(resolvedM))
                        throw new NolError("Can only merge objects");
                    for (let [k, v] of Object.entries(resolvedM)) if (!(k in val)) val[k] = v;
                }
            }
            for (let k of Object.keys(val)) val[k] = this.resolveMerges(val[k], visited);
            visited.delete(val);
        } else if (Array.isArray(val)) {
            visited.add(val);
            for (let i = 0; i < val.length; i++) val[i] = this.resolveMerges(val[i], visited);
            visited.delete(val);
        } else if (typeof val === 'string' && val.startsWith('*')) {
            const a = val.substring(1);
            if (!this.anchors.has(a)) throw new NolError(`Undefined anchor: ${a}`);
            return this.resolveMerges(this.anchors.get(a), visited);
        }
        return val;
    }

    resolveEnv(val) {
        if (val && typeof val === 'object' && !Array.isArray(val)) {
            if (val.env && Object.keys(val).length === 1 && typeof val.env === 'string') {
                if (this.appEnvAllowlist.has(val.env) && this.docEnvAllowlist.has(val.env))
                    return process.env[val.env] || '';
                throw new NolError(`Env access denied: ${val.env}`);
            }
            for (let k of Object.keys(val)) val[k] = this.resolveEnv(val[k]);
        } else if (Array.isArray(val)) {
            for (let i = 0; i < val.length; i++) val[i] = this.resolveEnv(val[i]);
        }
        return val;
    }

    resolveInterpolations(val) {
        if (val && typeof val === 'object' && !Array.isArray(val)) {
            for (let k of Object.keys(val)) val[k] = this.resolveInterpolations(val[k]);
        } else if (Array.isArray(val)) {
            for (let i = 0; i < val.length; i++) val[i] = this.resolveInterpolations(val[i]);
        } else if (typeof val === 'string') return this.resolveInterpolation(val);
        return val;
    }

    resolveInterpolation(s, depth = 0) {
        if (depth > 50) throw new NolError("Interpolation depth limit exceeded");
        const res = s.replace(/\$\{([^}]+)\}/g, (_, path) => {
            const parts = path.split('.');
            let curr = this.root;
            for (let p of parts) {
                if (!curr || typeof curr !== 'object' || !(p in curr)) throw new NolError(`Undefined: ${path}`);
                curr = curr[p];
            }
            if (!['string', 'number', 'boolean'].includes(typeof curr) && curr !== null)
                throw new NolError("Interpolation must resolve to scalar");
            const valStr = String(curr);
            return valStr.includes('${') ? this.resolveInterpolation(valStr, depth + 1) : valStr;
        });
        if (res.length > 10 * 1024 * 1024) throw new NolError("Interpolation output size limit exceeded");
        return res;
    }

    resolveCoercions(val) {
        if (val && typeof val === 'object' && !Array.isArray(val)) {
            if (val._coerce && Object.keys(val).length === 1) {
                const { type: t, value: rawV } = val._coerce;
                const v = this.resolveCoercions(rawV);
                switch (t) {
                    case 'int': return parseInt(v, 10);
                    case 'float': return parseFloat(v);
                    case 'bool': return typeof v === 'boolean' ? v : (String(v).toLowerCase() === 'true');
                    case 'string': return String(v);
                    default: throw new NolError(`Unknown coercion: ${t}`);
                }
            }
            for (let k of Object.keys(val)) val[k] = this.resolveCoercions(val[k]);
        } else if (Array.isArray(val)) {
            for (let i = 0; i < val.length; i++) val[i] = this.resolveCoercions(val[i]);
        }
        return val;
    }
}

function parse(input, isNole = false, envAllowlist = []) {
    return new Parser(input, isNole, envAllowlist).parse();
}

function parseFile(filename, envAllowlist = []) {
    const content = fs.readFileSync(filename, 'utf-8');
    return parse(content, filename.endsWith('.nole'), envAllowlist);
}

module.exports = { parse, parseFile, NolError };
