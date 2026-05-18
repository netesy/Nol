import sys, os, time, re, unicodedata

class NolError(Exception):
    def __init__(self, msg, line=None, col=None):
        super().__init__(f"{msg} at {line}:{col}" if line else msg)
        self.line, self.col = line, col

class NolParser:
    def __init__(self, text):
        self.text = text; self.pos = 0; self.line = 1; self.col = 1
        self.start_time = time.time(); self.depth = 0
    def peek(self, n=0): return self.text[self.pos + n] if self.pos + n < len(self.text) else None
    def advance(self):
        c = self.peek(); self.pos += 1
        if c == '\n': self.line += 1; self.col = 1
        else: self.col += 1
        return c
    def skip_ws(self):
        while self.pos < len(self.text):
            if self.text[self.pos].isspace(): self.advance()
            elif self.text[self.pos] == '#':
                self.advance()
                if self.peek() == '#':
                    self.advance()
                    while self.pos < len(self.text) - 1 and not (self.text[self.pos] == '#' and self.text[self.pos+1] == '#'): self.advance()
                    if self.pos < len(self.text): self.advance(); self.advance()
                else:
                    while self.pos < len(self.text) and self.text[self.pos] != '\n': self.advance()
            else: break
    def read_str(self, q):
        s = ""
        while self.pos < len(self.text):
            c = self.peek()
            if c == q: self.advance(); break
            if c == '\\':
                self.advance(); e = self.advance()
                if e == 'u':
                    h = "".join([self.advance() for _ in range(4)])
                    s += chr(int(h, 16))
                elif e == 'U':
                    h = "".join([self.advance() for _ in range(8)])
                    s += chr(int(h, 16))
                else: s += {'n': '\n', 'r': '\r', 't': '\t'}.get(e, e)
            else: s += self.advance()
        return s
    def read_key(self):
        self.skip_ws(); c = self.peek()
        if c in ('"', "'"): return self.read_str(self.advance())
        k = ""
        while self.pos < len(self.text) and (self.text[self.pos].isalnum() or self.text[self.pos] in ('_', '-')): k += self.advance()
        return k
    def parse_value(self):
        self.skip_ws()
        if time.time() - self.start_time > 1: raise NolError("Timeout")
        self.depth += 1
        if self.depth > 100: raise NolError("Max depth exceeded")
        c = self.peek()
        if c == '{':
            self.advance(); o = {}
            while self.pos < len(self.text):
                self.skip_ws()
                if self.peek() == '}': self.advance(); break
                self.parse_pair(o); self.skip_ws()
                if self.peek() == ',': self.advance()
            res = o
        elif c == '[':
            self.advance(); a = []
            while self.pos < len(self.text):
                self.skip_ws()
                if self.peek() == ']': self.advance(); break
                a.append(self.parse_value()); self.skip_ws()
                if self.peek() == ',': self.advance()
            res = a
        elif c in ('"', "'"): res = self.read_str(self.advance())
        elif c is not None and (c.isdigit() or c == '-'):
            b = ""
            while self.peek() and (self.peek().isdigit() or self.peek() in ('.', '-', 'e', 'E', '+')): b += self.advance()
            res = float(b) if '.' in b or 'e' in b.lower() else int(b)
        else:
            b = ""
            while self.peek() and self.peek().isalpha(): b += self.advance()
            if b == "true": res = True
            elif b == "false": res = False
            elif b == "null": res = None
            else: raise NolError(f"Invalid value: {b}")
        self.depth -= 1; return res
    def parse_pair(self, obj):
        self.skip_ws(); key = unicodedata.normalize('NFC', self.read_key())
        if key in ("_env", "_interpolate", "_meta"): raise NolError(f"Reserved key in NOL: {key}")
        self.skip_ws()
        if self.advance() != ':': raise NolError(f"Expected : for key {key}")
        val = self.parse_value()
        if key in obj: raise NolError(f"Duplicate key: {key}")
        obj[key] = val
    def parse_into(self, obj):
        while self.pos < len(self.text):
            self.skip_ws(); c = self.peek()
            if not c or c == '[': break
            self.parse_pair(obj)
    def parse(self):
        root = {}
        while self.pos < len(self.text):
            self.skip_ws()
            if not self.peek(): break
            if self.peek() == '[':
                self.advance(); path = ""
                while self.peek() and self.peek() != ']': path += self.advance()
                if self.peek() == ']': self.advance()
                parts = path.split('.'); curr = root
                for i, p in enumerate(parts):
                    last = (i == len(parts) - 1)
                    if p not in curr: curr[p] = {}
                    if last: self.parse_into(curr[p])
                    else: curr = curr[p]
            else: self.parse_pair(root)
        return root

def parse(text): return NolParser(text).parse()
def dump(v, indent=2, level=0, root=False):
    if v is None: return "null"
    if isinstance(v, bool): return "true" if v else "false"
    if isinstance(v, (int, float)):
        s = f"{v:.5f}" if isinstance(v, float) else str(v)
        if '.' in s: s = s.rstrip('0').rstrip('.')
        if '.' in s and s.endswith('.'): s += '0'
        return s
    if isinstance(v, str): return v if root else f'"{v}"'
    if isinstance(v, list): return "[" + ", ".join(dump(x, indent, level+1) for x in v) + "]"
    if isinstance(v, dict):
        if not root:
            if not v: return "{}"
            pad = " " * (level * indent); next_pad = " " * ((level + 1) * indent)
            items = [f"\n{next_pad}{k}: {dump(v[k], indent, level+1)}" for k in sorted(v.keys())]
            return "{" + ",".join(items) + "\n" + pad + "}"
        else:
            return "\n".join(f"{k}: {dump(v[k], indent, level+1)}" for k in sorted(v.keys()))
