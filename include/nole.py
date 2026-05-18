import sys, os, time, re, unicodedata

class NolError(Exception):
    def __init__(self, msg, line=None, col=None):
        super().__init__(f"{msg} at {line}:{col}" if line else msg)
        self.line, self.col = line, col

class NolParser:
    def __init__(self, text, nole=False):
        self.text = text; self.pos = 0; self.line = 1; self.col = 1
        self.start_time = time.time(); self.depth = 0; self.nole = nole
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
        elif self.nole and (c == '*' or c == '<'):
            t = self.advance()
            if t == '<':
                b = ""
                while self.peek() and self.peek() != '>': b += self.advance()
                if self.peek() == '>': self.advance()
                res = {"_coerce": {"type": b, "value": self.parse_value()}}
            else:
                b = ""
                while self.peek() and (self.peek().isalnum() or self.peek() in ('_', '-')): b += self.advance()
                res = "*" + b
        elif c in ('"', "'"): res = self.read_str(self.advance())
        elif c.isdigit() or c == '-':
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
        self.skip_ws()
        if self.nole and self.peek() == '&':
            self.advance(); n = self.read_key(); self.skip_ws()
            val = self.parse_value() if self.peek() == ':' and (self.advance() or True) else None
            if "_anchors" not in obj: obj["_anchors"] = []
            obj["_anchors"].append({"name": n, "value": val}); return
        is_m = False
        if self.nole and self.peek() == '<' and self.peek(1) == '<': self.advance(); self.advance(); is_m = True
        key = unicodedata.normalize('NFC', self.read_key()) if not is_m else "<<"
        self.skip_ws()
        if self.advance() != ':': raise NolError(f"Expected : for key {key}")
        val = self.parse_value()
        if is_m:
            if "<<" not in obj: obj["<<" ] = []
            obj["<<"].append(val)
        else:
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
                self.advance(); is_a = False
                if self.peek() == '*': self.advance(); is_a = True
                path = ""
                while self.peek() and self.peek() != ']': path += self.advance()
                if self.peek() == ']': self.advance()
                parts = path.split('.'); curr = root
                for i, p in enumerate(parts):
                    last = (i == len(parts) - 1)
                    if p not in curr: curr[p] = [] if last and is_a else {}
                    if last:
                        if is_a:
                            entry = {}; curr[p].append(entry); self.parse_into(entry)
                        else: self.parse_into(curr[p])
                    else: curr = curr[p]
            else: self.parse_pair(root)
        return root

class Evaluator:
    def __init__(self, app_env=None):
        self.anchors = {}; self.app_env = set(app_env or []); self.doc_env = set(); self.root = None
    def evaluate(self, root):
        self.root = self.collect_meta(root)
        self.root = self.resolve_merges(self.root)
        self.root = self.resolve_env(self.root)
        import copy
        clone = copy.deepcopy(self.root)
        self.root = self.resolve_interp(self.root, clone)
        return self.resolve_coerce(self.root)
    def collect_meta(self, v):
        if isinstance(v, dict):
            if "_anchors" in v:
                ans = v.pop("_anchors")
                for a in ans:
                    name = a["name"]; val = self.collect_meta(a["value"]) if a["value"] is not None else v
                    self.anchors[name] = val
            if "_env" in v and isinstance(v["_env"], dict):
                for x in v["_env"].get("allowed", []): self.doc_env.add(x)
            for k in list(v.keys()): v[k] = self.collect_meta(v[k])
        elif isinstance(v, list): v = [self.collect_meta(x) for x in v]
        return v
    def resolve_merges(self, v, depth=0):
        if depth > 20: raise NolError("Max merge depth exceeded")
        if isinstance(v, dict):
            if "<<" in v:
                merges = v.pop("<<")
                if not isinstance(merges, list): merges = [merges]
                for m in merges:
                    rm = m
                    if isinstance(rm, str) and rm.startswith("*"):
                        an = rm[1:]; rm = self.anchors.get(an)
                    rm = self.resolve_merges(rm, depth + 1)
                    if isinstance(rm, dict):
                        for mk, mv in rm.items():
                            if not mk.startswith("_") and mk not in v: v[mk] = mv
            for k in list(v.keys()): v[k] = self.resolve_merges(v[k], depth)
        elif isinstance(v, list): v = [self.resolve_merges(x, depth) for x in v]
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
        if depth > 50: raise NolError("Max interpolation depth exceeded")
        if isinstance(v, str) and "${" in v:
            def repl(m):
                path = m.group(1); curr = root
                for p in path.split('.'): curr = curr[p]
                vs = curr if isinstance(curr, str) else dump(curr, indent=2, root=True)
                if "${" in vs: vs = self.resolve_interp(vs, root, depth + 1)
                return vs
            return re.sub(r"\$\{([^}]+)\}", repl, v)
        if isinstance(v, dict): return {k: self.resolve_interp(val, root, depth) for k, val in v.items()}
        if isinstance(v, list): return [self.resolve_interp(x, root, depth) for x in v]
        return v
    def resolve_coerce(self, v):
        if isinstance(v, dict):
            if "_coerce" in v:
                c = v.pop("_coerce"); t = c["type"]; val = self.resolve_coerce(c["value"])
                s = val if isinstance(val, str) else dump(val, indent=2)
                if t == "int": return int(s)
                if t == "float": return float(s)
                if t == "bool": return s.lower() == "true"
                return s
            return {k: self.resolve_coerce(val) for k, val in v.items()}
        if isinstance(v, list): return [self.resolve_coerce(x) for x in v]
        return v

def parse(text, app_env=None):
    p = NolParser(text, True); root = p.parse()
    return Evaluator(app_env).evaluate(root)

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
            items = []
            for k in sorted(v.keys()):
                if k.startswith("_"): continue
                items.append(f"\n{next_pad}{k}: {dump(v[k], indent, level+1)}")
            return "{" + ",".join(items) + "\n" + pad + "}"
        else:
            lines = []
            for k in sorted(v.keys()):
                if k.startswith("_"): continue
                lines.append(f"{k}: {dump(v[k], indent, level+1)}")
            return "\n".join(lines)
