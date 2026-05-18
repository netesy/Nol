class NolError extends Error {
    constructor(msg, line, col) {
        super(`${msg} at ${line}:${col}`);
        this.line = line; this.col = col;
    }
}
class NolParser {
    constructor(text) {
        this.text = text; this.pos = 0; this.line = 1; this.col = 1;
        this.startTime = Date.now(); this.depth = 0;
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
        this.skipWs(); const key = this.readKey().normalize('NFC');
        if (["_env", "_interpolate", "_meta"].includes(key)) throw new Error("Reserved key in NOL: " + key);
        this.skipWs(); if (this.advance() !== ':') throw new NolError("Expected :", this.line, this.col);
        const val = this.parseValue();
        if (Object.prototype.hasOwnProperty.call(obj, key)) throw new Error("Duplicate key: " + key);
        obj[key] = val;
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
                this.advance(); let path = ""; while (this.peek() && this.peek() !== ']') path += this.advance();
                if (this.peek() === ']') this.advance();
                const parts = path.split('.'); let curr = root;
                for (let i = 0; i < parts.length; i++) {
                    const p = parts[i], last = i === parts.length - 1;
                    if (!curr[p]) curr[p] = {};
                    if (last) this.parseInto(curr[p]);
                    else curr = curr[p];
                }
            } else this.parsePair(root);
        }
        return root;
    }
}
function parse(text) { return new NolParser(text).parse(); }
function dump(v, indent = 2, level = 0, root = false) {
    if (v === null) return "null"; if (typeof v === 'boolean') return v ? "true" : "false";
    if (typeof v === 'number') { let s = v.toFixed(5); while (s.endsWith('0')) s = s.slice(0, -1); if (s.endsWith('.')) s += '0'; return s; }
    if (typeof v === 'string') return root ? v : `"${v}"`;
    if (Array.isArray(v)) return "[" + v.map(x => dump(x, indent, level + 1)).join(", ") + "]";
    const pad = " ".repeat(level * indent), nextPad = " ".repeat((level + 1) * indent);
    const keys = Object.keys(v).sort();
    if (root) return keys.map(k => `${k}: ${dump(v[k], indent, level + 1)}`).join("\n");
    if (keys.length === 0) return "{}";
    return "{" + keys.map(k => `\n${nextPad}${k}: ${dump(v[k], indent, level + 1)}`).join(",") + `\n${pad}}`;
}
module.exports = { parse, dump };
