import os, re, unicodedata, time
from enum import Enum, auto
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Set, Union

class NolError(Exception): pass

class Tok(Enum):
    EOF = auto(); Identifier = auto(); String = auto(); Number = auto(); Date = auto()
    True_ = auto(); False_ = auto(); Null = auto(); Colon = auto(); Comma = auto()
    LBracket = auto(); RBracket = auto(); LBrace = auto(); RBrace = auto(); Newline = auto()
    Dot = auto(); Star = auto(); LShift = auto(); Ampersand = auto(); LArrow = auto(); RArrow = auto()

@dataclass
class Token:
    type: Tok; text: str; line: int; col: int

class Lexer:
    def __init__(self, input_str: str):
        self.input = input_str; self.pos = 0; self.line = 1; self.col = 1
        if len(input_str) > 10 * 1024 * 1024: raise NolError("Document size limit exceeded")
        if '\0' in input_str: raise NolError("Null bytes are prohibited")
        for c in input_str:
            if 0xD800 <= ord(c) <= 0xDFFF: raise NolError("Surrogate code points are prohibited")

    def peek(self, n=0): return self.input[self.pos + n] if self.pos + n < len(self.input) else '\0'
    def advance(self):
        c = self.peek(); self.pos += 1
        if c == '\n': self.line += 1; self.col = 1
        else: self.col += 1
        return c
    def next_token(self) -> Token:
        while True:
            c = self.peek()
            if c == '\0': return Token(Tok.EOF, "", self.line, self.col)
            if c.isspace():
                if c == '\n': t = Token(Tok.Newline, "\n", self.line, self.col); self.advance(); return t
                self.advance(); continue
            if c == '#':
                self.advance()
                if self.peek() == '#':
                    self.advance()
                    while self.peek() != '\0':
                        if self.peek() == '#' and self.peek(1) == '#': self.advance(); self.advance(); break
                        self.advance()
                else:
                    while self.peek() != '\0' and self.peek() != '\n': self.advance()
                continue
            if c == '"' or c == "'": return self.scan_string()
            if c.isdigit() or (c == '-' and self.peek(1).isdigit()): return self.scan_number_or_date()
            if c.isalpha() or c == '_': return self.scan_identifier()
            if c == ':': self.advance(); return Token(Tok.Colon, ":", self.line, self.col-1)
            if c == ',': self.advance(); return Token(Tok.Comma, ",", self.line, self.col-1)
            if c == '[': self.advance(); return Token(Tok.LBracket, "[", self.line, self.col-1)
            if c == ']': self.advance(); return Token(Tok.RBracket, "]", self.line, self.col-1)
            if c == '{': self.advance(); return Token(Tok.LBrace, "{", self.line, self.col-1)
            if c == '}': self.advance(); return Token(Tok.RBrace, "}", self.line, self.col-1)
            if c == '.': self.advance(); return Token(Tok.Dot, ".", self.line, self.col-1)
            if c == '*': self.advance(); return Token(Tok.Star, "*", self.line, self.col-1)
            if c == '&': self.advance(); return Token(Tok.Ampersand, "&", self.line, self.col-1)
            if c == '<':
                self.advance()
                if self.peek() == '<': self.advance(); return Token(Tok.LShift, "<<", self.line, self.col-2)
                return Token(Tok.LArrow, "<", self.line, self.col-1)
            if c == '>': self.advance(); return Token(Tok.RArrow, ">", self.line, self.col-1)
            raise NolError(f"Unexpected character: {c} at {self.line}:{self.col}")
    def scan_string(self) -> Token:
        quote = self.advance(); is_triple = (self.peek() == quote and self.peek(1) == quote)
        if is_triple: self.advance(); self.advance()
        res = []
        while True:
            c = self.peek()
            if c == '\0': raise NolError("Unterminated string")
            if is_triple:
                if c == quote and self.peek(1) == quote and self.peek(2) == quote: self.advance(); self.advance(); self.advance(); break
            else:
                if c == quote: self.advance(); break
                if c == '\n': raise NolError("Newline in string")
            if c == '\\' and quote == '"':
                self.advance(); e = self.advance()
                if e == '"': res.append('"')
                elif e == "'": res.append("'")
                elif e == '\\': res.append('\\')
                elif e == 'n': res.append('\n')
                elif e == 'r': res.append('\r')
                elif e == 't': res.append('\t')
                elif e in ('u', 'U'):
                    n = 4 if e == 'u' else 8; u = "".join(self.advance() for _ in range(n))
                    cp = int(u, 16)
                    if cp == 0: raise NolError("Null prohibited")
                    if 0xD800 <= cp <= 0xDFFF: raise NolError("Surrogate prohibited")
                    res.append(chr(cp))
                else: raise NolError(f"Invalid escape: \\{e}")
            else: res.append(self.advance())
            if len(res) > 1024 * 1024: raise NolError("String length limit exceeded")
        return Token(Tok.String, "".join(res), self.line, self.col)
    def scan_number_or_date(self) -> Token:
        res = []
        while self.peek().isalnum() or self.peek() in "-._:T+": res.append(self.advance())
        s = "".join(res)
        if re.match(r'^\d{4}-\d{2}-\d{2}(T\d{2}:\d{2}:\d{2}(Z|[+-]\d{2}:\d{2})?)?$', s): return Token(Tok.Date, s, self.line, self.col)
        return Token(Tok.Number, s, self.line, self.col)
    def scan_identifier(self) -> Token:
        res = []
        while self.peek().isalnum() or self.peek() in "_-": res.append(self.advance())
        s = "".join(res)
        kw = {"true": Tok.True_, "false": Tok.False_, "null": Tok.Null}
        return Token(kw.get(s, Tok.Identifier), s, self.line, self.col)

class Parser:
    def __init__(self, input_str: str, is_nole: bool = False, env_allowlist: List[str] = None):
        self.lexer = Lexer(input_str); self.cur = self.lexer.next_token(); self.is_nole = is_nole
        self.app_env_allowlist = set(env_allowlist or []); self.doc_env_allowlist = set()
        self.anchors = {}; self.root = {}; self.depth = 0; self.start_time = time.time()
    def error(self, msg): raise NolError(f"{msg} at {self.cur.line}:{self.cur.col}")
    def next(self): self.cur = self.lexer.next_token()
    def skip_newlines(self):
        while self.cur.type == Tok.Newline: self.next()
    def parse(self) -> Dict[str, Any]:
        while self.cur.type != Tok.EOF:
            self.skip_newlines()
            if self.cur.type == Tok.EOF: break
            if self.cur.type == Tok.LBracket: self.parse_section()
            elif self.is_key_token(): self.parse_pair(self.root)
            else: self.error(f"Unexpected token: {self.cur.type}")
            if time.time() - self.start_time > 1.0: raise NolError("Parse timeout")
        if self.is_nole:
            self.collect_env_allowlist(self.root)
            self.root = self.resolve_merges(self.root)
            self.root = self.resolve_env(self.root)
            self.root = self.resolve_interpolations(self.root)
            self.root = self.resolve_coercions(self.root)
        return self.root
    def collect_env_allowlist(self, obj):
        if isinstance(obj, dict):
            if "_env" in obj and isinstance(obj["_env"], dict):
                allowed = obj["_env"].get("allowed")
                if isinstance(allowed, list):
                    for a in allowed: self.doc_env_allowlist.add(str(a))
            for v in obj.values(): self.collect_env_allowlist(v)
        elif isinstance(obj, list):
            for v in obj: self.collect_env_allowlist(v)
    def is_key_token(self): return self.cur.type in (Tok.Identifier, Tok.String, Tok.LShift)
    def parse_section(self):
        self.next()
        path = []; is_array = False
        while True:
            if self.cur.type == Tok.Star: is_array = True; self.next()
            elif self.cur.type in (Tok.Identifier, Tok.String): path.append(unicodedata.normalize('NFC', self.cur.text)); self.next()
            else: self.error("Expected section name")
            if self.cur.type == Tok.Dot: self.next(); continue
            if self.cur.type == Tok.RBracket: self.next(); break
            self.error("Expected . or ]")
        target = self.root
        for i, part in enumerate(path):
            if part in target:
                if not isinstance(target[part], dict) and not (is_array and i == len(path)-1 and isinstance(target[part], list)): self.error(f"Collision at {part}")
            else: target[part] = [] if (is_array and i == len(path)-1) else {}
            if not (is_array and i == len(path)-1): target = target[part]
        if is_array:
            arr = target[path[-1]]; new_obj = {}; arr.append(new_obj); target = new_obj
        self.skip_newlines()
        while self.is_key_token(): self.parse_pair(target); self.skip_newlines()
    def parse_pair(self, obj):
        key = "<<" if self.cur.type == Tok.LShift else unicodedata.normalize('NFC', self.cur.text)
        if key in ("_env", "_interpolate", "_meta") and not self.is_nole: self.error(f"Reserved key {key}")
        if key in obj and key != "<<": self.error(f"Duplicate key: {key}")
        self.next()
        if self.cur.type != Tok.Colon: self.error("Expected :")
        self.next(); val = self.parse_value()
        if key == "<<":
            if "<<" not in obj: obj["<<"] = []
            obj["<<"].append(val)
        else: obj[key] = val
    def parse_value(self) -> Any:
        self.depth += 1
        if self.depth > 100: self.error("Nesting limit exceeded")
        res = None
        if self.cur.type == Tok.Ampersand:
            self.next(); name = self.cur.text; self.next(); res = self.parse_value(); self.anchors[name] = res
        elif self.cur.type == Tok.Star:
            self.next(); res = "*" + self.cur.text; self.next()
        elif self.cur.type == Tok.LArrow:
            self.next(); t = self.cur.text; self.next(); self.next(); res = {"_coerce": {"type": t, "value": self.parse_value()}}
        elif self.cur.type == Tok.String: res = self.cur.text; self.next()
        elif self.cur.type == Tok.Number:
            s = self.cur.text; self.next()
            try:
                if '.' in s or 'e' in s.lower(): res = float(s)
                else: res = int(s)
            except ValueError: self.error(f"Invalid number: {s}")
        elif self.cur.type in (Tok.Date, Tok.True_, Tok.False_, Tok.Null):
            m = {Tok.Date: lambda x: x, Tok.True_: lambda x: True, Tok.False_: lambda x: False, Tok.Null: lambda x: None}
            res = m[self.cur.type](self.cur.text); self.next()
        elif self.cur.type == Tok.LBracket: res = self.parse_array()
        elif self.cur.type == Tok.LBrace: res = self.parse_object()
        else: self.error(f"Invalid value: {self.cur.type}")
        self.depth -= 1; return res
    def parse_array(self) -> List[Any]:
        self.next(); res = []
        while self.cur.type != Tok.RBracket:
            self.skip_newlines()
            if self.cur.type == Tok.RBracket: break
            res.append(self.parse_value()); self.skip_newlines()
            if self.cur.type == Tok.Comma: self.next()
        self.next()
        if len(res) > 100000: self.error("Array size limit")
        return res
    def parse_object(self) -> Dict[str, Any]:
        self.next(); res = {}
        while self.cur.type != Tok.RBrace:
            self.skip_newlines()
            if self.cur.type == Tok.RBrace: break
            self.parse_pair(res); self.skip_newlines()
            if self.cur.type == Tok.Comma: self.next()
        self.next()
        if len(res) > 10000: self.error("Object keys limit")
        return res
    def resolve_merges(self, val, visited=None):
        if visited is None: visited = []
        if id(val) in visited: raise NolError("Cycle detected")
        if isinstance(val, dict):
            visited.append(id(val))
            if "<<" in val:
                merges = val.pop("<<")
                for m in merges:
                    resolved_m = self.resolve_merges(m, visited)
                    if isinstance(resolved_m, str) and resolved_m.startswith("*"):
                        resolved_m = self.resolve_merges(self.anchors.get(resolved_m[1:]), visited)
                    if not isinstance(resolved_m, dict): raise NolError("Can only merge objects")
                    for k, v in resolved_m.items():
                        if k not in val: val[k] = v
            for k in list(val.keys()): val[k] = self.resolve_merges(val[k], visited)
            visited.pop()
        elif isinstance(val, list):
            visited.append(id(val))
            for i in range(len(val)): val[i] = self.resolve_merges(val[i], visited)
            visited.pop()
        elif isinstance(val, str) and val.startswith("*"):
            a = val[1:]
            if a not in self.anchors: raise NolError(f"Undefined anchor: {a}")
            return self.resolve_merges(self.anchors[a], visited)
        return val
    def resolve_env(self, val):
        if isinstance(val, dict):
            if "env" in val and len(val) == 1 and isinstance(val["env"], str):
                var = val["env"]
                if var in self.app_env_allowlist and var in self.doc_env_allowlist: return os.environ.get(var, "")
                raise NolError(f"Env access denied: {var}")
            for k in list(val.keys()): val[k] = self.resolve_env(val[k])
        elif isinstance(val, list):
            for i in range(len(val)): val[i] = self.resolve_env(val[i])
        return val
    def resolve_interpolations(self, val):
        if isinstance(val, dict):
            for k in list(val.keys()): val[k] = self.resolve_interpolations(val[k])
        elif isinstance(val, list):
            for i in range(len(val)): val[i] = self.resolve_interpolations(val[i])
        elif isinstance(val, str): return self.resolve_interpolation(val)
        return val
    def resolve_interpolation(self, s, depth=0):
        if depth > 50: raise NolError("Interpolation depth")
        def repl(match):
            parts = match.group(1).split('.'); curr = self.root
            for p in parts:
                if not isinstance(curr, dict) or p not in curr: raise NolError(f"Undefined: {match.group(1)}")
                curr = curr[p]
            if not isinstance(curr, (str, int, float, bool)): raise NolError("Not scalar")
            res = str(curr)
            return self.resolve_interpolation(res, depth+1) if "${" in res else res
        res = re.sub(r'\$\{([^}]+)\}', repl, s)
        if len(res) > 10 * 1024 * 1024: raise NolError("Interpolation size")
        return res
    def resolve_coercions(self, val):
        if isinstance(val, dict):
            if "_coerce" in val and len(val) == 1:
                c = val["_coerce"]; v = self.resolve_coercions(c["value"]); t = c["type"]
                if t == "int": return int(v)
                if t == "float": return float(v)
                if t == "bool":
                    if isinstance(v, bool): return v
                    if str(v).lower() == "true": return True
                    if str(v).lower() == "false": return False
                    raise NolError(f"Invalid bool coercion: {v}")
                if t == "string": return str(v)
                raise NolError(f"Unknown coercion: {t}")
            for k in list(val.keys()): val[k] = self.resolve_coercions(val[k])
        elif isinstance(val, list):
            for i in range(len(val)): val[i] = self.resolve_coercions(val[i])
        return val

class Document:
    def __init__(self, root): self.root = root
    def get(self, path=None):
        if path is None: return self.root
        curr = self.root
        for p in path.split('.'):
            if not isinstance(curr, dict) or p not in curr: return None
            curr = curr[p]
        return curr
    @staticmethod
    def parse(s, n=False, e=None): return Document(Parser(s, n, e).parse())

def parse(s, n=False, e=None): return Parser(s, n, e).parse()
