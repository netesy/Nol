import time

class Document:
    def __init__(self, root): self.root = root
    def get(self, path):
        curr = self.root
        for p in path.split('.'):
            if not isinstance(curr, dict) or p not in curr: return None
            curr = curr[p]
        return curr
    def exists(self, path): return self.get(path) is not None
    def to_dict(self): return self.root

class Builder:
    def __init__(self): self.root = {}
    def set(self, path, value):
        parts = path.split('.')
        curr = self.root
        for p in parts[:-1]:
            if p not in curr or not isinstance(curr[p], dict): curr[p] = {}
            curr = curr[p]
        curr[parts[-1]] = value
        return self
    def build(self): return Document(self.root)

class Parser:
    def __init__(self, text): self.text, self.pos, self.start, self.depth = text, 0, time.time(), 0
    def peek(self): return self.text[self.pos] if self.pos < len(self.text) else None
    def advance(self):
        if self.pos >= len(self.text): return None
        c = self.text[self.pos]; self.pos += 1; return c
    def skip_ws(self):
        while self.pos < len(self.text):
            if self.text[self.pos].isspace(): self.advance()
            elif self.text[self.pos] == '#':
                self.advance()
                if self.peek() == '#':
                    self.advance()
                    while self.pos < len(self.text) - 1 and self.text[self.pos:self.pos+2] != '##': self.advance()
                    if self.pos < len(self.text): self.advance(); self.advance()
                else:
                    while self.pos < len(self.text) and self.text[self.pos] != '\n': self.advance()
            else: break
    def read_str(self, q):
        s = ""
        while self.pos < len(self.text):
            c = self.advance()
            if c == q: return s
            if c == '\\':
                e = self.advance()
                if e == 'u': s += chr(int(self.text[self.pos:self.pos+4], 16)); self.pos += 4
                elif e == 'U': s += chr(int(self.text[self.pos:self.pos+8], 16)); self.pos += 8
                else: s += {'n':'\n', 'r':'\r', 't':'\t'}.get(e, e)
            else: s += c
        return s
    def read_key(self):
        self.skip_ws(); c = self.peek()
        if c in ('"', "'"): return self.read_str(self.advance())
        k = ""
        while self.pos < len(self.text) and (self.text[self.pos].isalnum() or self.text[self.pos] in '_-'): k += self.advance()
        return k
    def parse_value(self):
        self.skip_ws()
        if time.time() - self.start > 1: raise Exception("Timeout")
        self.depth += 1
        if self.depth > 100: raise Exception("Depth")
        c = self.peek()
        if c == '{':
            self.advance(); o = {}
            while self.peek() and self.peek() != '}':
                self.skip_ws(); k, v = self.parse_pair(); o[k] = v
                self.skip_ws()
                if self.peek() == ',': self.advance()
            if self.peek() == '}': self.advance()
            res = o
        elif c == '[':
            self.advance(); a = []
            while self.peek() and self.peek() != ']':
                self.skip_ws(); a.append(self.parse_value())
                self.skip_ws()
                if self.peek() == ',': self.advance()
            if self.peek() == ']': self.advance()
            res = a
        elif c in ('"', "'"): res = self.read_str(self.advance())
        elif c and (c.isdigit() or c == '-'):
            s = ""
            while self.pos < len(self.text) and (self.text[self.pos].isdigit() or self.text[self.pos] in '.-eE+'): s += self.advance()
            res = float(s) if '.' in s or 'e' in s.lower() else int(s)
        else:
            s = ""
            while self.pos < len(self.text) and self.text[self.pos].isalpha(): s += self.advance()
            if s == "true": res = True
            elif s == "false": res = False
            elif s == "null": res = None
            else: raise Exception(f"Invalid value: {s}")
        self.depth -= 1; return res
    def parse_pair(self):
        k = self.read_key()
        if k in ("_env", "_interpolate", "_meta"): raise Exception(f"Reserved: {k}")
        self.skip_ws()
        if self.peek() == ':': self.advance()
        return k, self.parse_value()
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
                for p in parts[:-1]:
                    if p not in curr or not isinstance(curr[p], dict): curr[p] = {}
                    curr = curr[p]
                last = parts[-1]
                if last not in curr or not isinstance(curr[last], dict): curr[last] = {}
                self.parse_into(curr[last])
            else:
                k, v = self.parse_pair()
                if k in root: raise Exception(f"Dup: {k}")
                root[k] = v
        return Document(root)
    def parse_into(self, o):
        while self.pos < len(self.text):
            self.skip_ws()
            if self.peek() == '[' or not self.peek(): break
            k, v = self.parse_pair()
            if k in o: raise Exception(f"Dup: {k}")
            o[k] = v

def parse(text): return Parser(text).parse()
