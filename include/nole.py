import time, os

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
    def __init__(self, text, nole=False): self.text, self.pos, self.start, self.depth, self.nole = text, 0, time.time(), 0, nole
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
                self.skip_ws(); self.parse_pair(o)
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
        elif self.nole and (c == '*' or c == '<'):
            t = self.advance()
            if t == '<':
                b = "";
                while self.peek() and self.peek() != '>': b += self.advance()
                if self.peek() == '>': self.advance()
                res = {"_coerce": {"type": b, "value": self.parse_value()}}
            else:
                b = ""
                while self.peek() and (self.peek().isalnum() or self.peek() in '_-'): b += self.advance()
                res = "*" + b
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
    def parse_pair(self, o):
        self.skip_ws()
        if self.nole and self.peek() == '&':
            self.advance(); n = self.read_key(); self.skip_ws()
            val = self.parse_value() if self.peek() == ':' and self.advance() else None
            if "_anchors" not in o: o["_anchors"] = []
            o["_anchors"].append({"name": n, "value": val}); return
        is_m = False
        if self.nole and self.peek() == '<':
            self.advance()
            if self.peek() == '<': self.advance(); is_m = True
            else: self.pos -= 1
        k = "<<" if is_m else self.read_key()
        self.skip_ws()
        if self.peek() == ':': self.advance()
        v = self.parse_value()
        if is_m:
            if "<<" not in o: o["<<"] = []
            o["<<"].append(v)
        else:
            if k in o: raise Exception(f"Dup: {k}")
            o[k] = v
    def parse(self):
        root = {}
        while self.pos < len(self.text):
            self.skip_ws()
            if not self.peek(): break
            if self.peek() == '[':
                self.advance(); is_a = False
                if self.peek() == '*': self.advance(); is_a = True
                path = ""
                while self.peek() and self.peek() != ']': path += self.advance()
                if self.peek() == ']': self.advance()
                parts = path.split('.'); curr = root
                for p in parts[:-1]:
                    if p not in curr or not isinstance(curr[p], dict): curr[p] = {}
                    curr = curr[p]
                last = parts[-1]
                if is_a:
                    if last not in curr or not isinstance(curr[last], list): curr[last] = []
                    entry = {}; curr[last].append(entry); self.parse_into(entry)
                else:
                    if last not in curr or not isinstance(curr[last], dict): curr[last] = {}
                    self.parse_into(curr[last])
            else: self.parse_pair(root)
        return root
    def parse_into(self, o):
        while self.pos < len(self.text):
            self.skip_ws()
            if self.peek() == '[' or not self.peek(): break
            self.parse_pair(o)

class Evaluator:
    def __init__(self, app_env): self.anchors, self.app_env, self.doc_env = {}, set(app_env), set()
    def evaluate(self, root):
        root = self.collect_meta(root)
        root = self.resolve_merges(root)
        root = self.resolve_env(root)
        root = self.resolve_interp(root, root)
        return Document(self.resolve_coerce(root))
    def dump(self, v):
        if v is None: return "null"
        if isinstance(v, bool): return str(v).lower()
        if isinstance(v, (int, float)):
            s = f"{v:.5f}"
            while s.endswith('0'): s = s[:-1]
            if s.endswith('.'): s += '0'
            return s if isinstance(v, float) else str(v)
        if isinstance(v, str): return v
        if isinstance(v, list): return "[" + ", ".join(self.dump(x) for x in v) + "]"
        if isinstance(v, dict):
            items = [f"{k}: {self.dump(val)}" for k, val in v.items() if not k.startswith('_')]
            return "{" + ", ".join(items) + "}"
        return str(v)
    def collect_meta(self, v):
        if isinstance(v, dict):
            if "_anchors" in v:
                for a in v.pop("_anchors"):
                    name = a["name"]
                    val = self.collect_meta(v if a["value"] is None else a["value"])
                    self.anchors[name] = val
            if "_env" in v:
                for x in v["_env"].get("allowed", []): self.doc_env.add(x)
            return {k: self.collect_meta(val) for k, val in v.items()}
        if isinstance(v, list): return [self.collect_meta(x) for x in v]
        return v
    def resolve_merges(self, v, depth=0):
        if depth > 20: raise Exception("Merge depth")
        if isinstance(v, dict):
            if "<<" in v:
                merges = v.pop("<<")
                if not isinstance(merges, list): merges = [merges]
                for m in merges:
                    rm = m
                    if isinstance(rm, str) and rm.startswith('*'): rm = self.anchors.get(rm[1:])
                    rm = self.resolve_merges(rm, depth + 1)
                    if isinstance(rm, dict):
                        for mk, mv in rm.items():
                            if not mk.startswith('_') and mk not in v: v[mk] = mv
            return {k: self.resolve_merges(val, depth) for k, val in v.items()}
        if isinstance(v, list): return [self.resolve_merges(x, depth) for x in v]
        return v
    def resolve_env(self, v):
        if isinstance(v, dict):
            if len(v) == 1 and "env" in v and isinstance(v["env"], str):
                var = v["env"]
                if not self.app_env and not self.doc_env or var in self.app_env or var in self.doc_env:
                    return os.environ.get(var, "")
            return {k: self.resolve_env(val) for k, val in v.items()}
        if isinstance(v, list): return [self.resolve_env(x) for x in v]
        return v
    def resolve_interp(self, v, root, depth=0):
        if depth > 50: raise Exception("Interp depth")
        if isinstance(v, str) and "${" in v:
            res = ""
            i = 0
            while i < len(v):
                if v[i:i+2] == "${":
                    end = v.find("}", i+2)
                    if end == -1: break
                    path = v[i+2:end]
                    curr = root
                    for p in path.split('.'):
                        if not isinstance(curr, dict) or p not in curr: raise Exception(f"Undef: {p}")
                        curr = curr[p]
                    vs = curr if isinstance(curr, str) else self.dump(curr)
                    if "${" in vs: vs = self.resolve_interp(vs, root, depth + 1)
                    res += vs; i = end + 1
                else: res += v[i]; i += 1
            return res
        if isinstance(v, dict): return {k: self.resolve_interp(val, root, depth) for k, val in v.items()}
        if isinstance(v, list): return [self.resolve_interp(x, root, depth) for x in v]
        return v
    def resolve_coerce(self, v):
        if isinstance(v, dict):
            if "_coerce" in v:
                c = v.pop("_coerce")
                t, val = c["type"], self.resolve_coerce(c["value"])
                s = val if isinstance(val, str) else self.dump(val)
                if t == "int": return int(s)
                if t == "float": return float(s)
                if t == "bool": return s.lower() == "true"
                return s
            return {k: self.resolve_coerce(val) for k, val in v.items()}
        if isinstance(v, list): return [self.resolve_coerce(x) for x in v]
        return v

def parse(text, app_env=[]):
    p = Parser(text, True)
    return Evaluator(app_env).evaluate(p.parse())
