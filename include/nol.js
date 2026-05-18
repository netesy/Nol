class Document {
    constructor(root) { this.root = root; }
    get(path) {
        let curr = this.root;
        for (const p of path.split('.')) {
            if (curr === null || typeof curr !== 'object' || !(p in curr)) return null;
            curr = curr[p];
        }
        return curr;
    }
    exists(path) { return this.get(path) !== null; }
    toObject() { return this.root; }
}

class Builder {
    constructor() { this.root = {}; }
    set(path, value) {
        const parts = path.split('.');
        let curr = this.root;
        for (let i = 0; i < parts.length - 1; i++) {
            const p = parts[i];
            if (!(p in curr) || typeof curr[p] !== 'object') curr[p] = {};
            curr = curr[p];
        }
        curr[parts[parts.length - 1]] = value;
        return this;
    }
    build() { return new Document(this.root); }
}

class Parser {
    constructor(text) { this.text = text; this.pos = 0; this.start = Date.now(); this.depth = 0; }
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
                this.skipWs(); const [k, v] = this.parsePair(); o[k] = v;
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
    parsePair() {
        const k = this.readKey();
        if (["_env", "_interpolate", "_meta"].includes(k)) throw new Error("Reserved: " + k);
        this.skipWs(); if (this.peek() === ':') this.advance();
        return [k, this.parseValue()];
    }
    parse() {
        const root = {};
        while (this.pos < this.text.length) {
            this.skipWs(); if (!this.peek()) break;
            if (this.peek() === '[') {
                this.advance(); let path = "";
                while (this.peek() && this.peek() !== ']') path += this.advance();
                if (this.peek() === ']') this.advance();
                const parts = path.split('.'); let curr = root;
                for (let i = 0; i < parts.length - 1; i++) {
                    const p = parts[i];
                    if (!(p in curr) || typeof curr[p] !== 'object') curr[p] = {};
                    curr = curr[p];
                }
                const last = parts[parts.length - 1];
                if (!(last in curr) || typeof curr[last] !== 'object') curr[last] = {};
                this.parseInto(curr[last]);
            } else {
                const [k, v] = this.parsePair();
                if (k in root) throw new Error("Dup: " + k);
                root[k] = v;
            }
        }
        return new Document(root);
    }
    parseInto(o) {
        while (this.pos < this.text.length) {
            this.skipWs(); if (this.peek() === '[' || !this.peek()) break;
            const [k, v] = this.parsePair();
            if (k in o) throw new Error("Dup: " + k);
            o[k] = v;
        }
    }
}

function parse(text) { return new Parser(text).parse(); }
module.exports = { parse, Document, Builder };
