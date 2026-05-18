use std::collections::{BTreeMap, HashSet};
use std::time::{Instant, Duration};
use std::env;

#[derive(Debug, Clone, PartialEq)]
pub enum NolValue {
    Null, Bool(bool), Int(i64), Float(f64), String(String), Array(Vec<NolValue>), Object(BTreeMap<String, NolValue>),
}

impl NolValue {
    pub fn dump(&self, indent: usize, level: usize, is_root: bool) -> String {
        match self {
            NolValue::Null => "null".to_string(),
            NolValue::Bool(b) => b.to_string(),
            NolValue::Int(i) => i.to_string(),
            NolValue::Float(f) => { let mut s = format!("{:.5}", f); while s.ends_with('0') { s.pop(); } if s.ends_with('.') { s.push('0'); } s }
            NolValue::String(s) => if is_root { s.clone() } else { format!("{:?}", s) },
            NolValue::Array(a) => format!("[{}]", a.iter().map(|v| v.dump(indent, level + 1, false)).collect::<Vec<_>>().join(", ")),
            NolValue::Object(o) => {
                let pad = " ".repeat(level * indent); let next_pad = " ".repeat((level + 1) * indent);
                let mut lines = Vec::new();
                for (k, v) in o { if k.starts_with('_') && !["_env", "_interpolate", "_meta"].contains(&k.as_str()) { continue; } let val = v.dump(indent, level + 1, false); if is_root { lines.push(format!("{}: {}", k, val)); } else { lines.push(format!("\n{}{}: {}", next_pad, k, val)); } }
                if is_root { return lines.join("\n"); } if o.is_empty() { return "{}".to_string(); }
                format!("{{{}\n{}}}", lines.join(","), pad)
            }
        }
    }
}

pub struct NolParser<'a> { text: &'a str, pos: usize, start: Instant, depth: usize, nole: bool }
impl<'a> NolParser<'a> {
    pub fn new(text: &'a str, nole: bool) -> Self { Self { text, pos: 0, start: Instant::now(), depth: 0, nole } }
    fn peek(&self) -> Option<char> { self.text[self.pos..].chars().next() }
    fn advance(&mut self) -> Option<char> { let c = self.peek()?; self.pos += c.len_utf8(); Some(c) }
    fn skip_ws(&mut self) { while let Some(c) = self.peek() { if c.is_whitespace() { self.advance(); } else if c == '#' { self.advance(); if self.peek() == Some('#') { self.advance(); while self.pos < self.text.len() - 1 && &self.text[self.pos..self.pos+2] != "##" { self.advance(); } if self.pos < self.text.len() { self.advance(); self.advance(); } } else { while let Some(c) = self.peek() { if c == '\n' { break; } self.advance(); } } } else { break; } } }
    fn parse_hex(&mut self, n: usize) -> char { let mut h = String::new(); for _ in 0..n { h.push(self.advance().expect("Hex")); } std::char::from_u32(u32::from_str_radix(&h, 16).expect("HexVal")).expect("Char") }
    fn read_str(&mut self, q: char) -> String { let mut s = String::new(); while let Some(c) = self.peek() { if c == q { self.advance(); return s; } if c == '\\' { self.advance(); let e = self.advance().expect("Esc"); if e == 'u' { s.push(self.parse_hex(4)); } else if e == 'U' { s.push(self.parse_hex(8)); } else { s.push(match e { 'n'=>'\n', 'r'=>'\r', 't'=>'\t', x=>x }); } } else { s.push(self.advance().unwrap()); } } s }
    fn read_key(&mut self) -> String { self.skip_ws(); let c = self.peek().expect("Key"); if c == '"' || c == '\'' { let q = self.advance().unwrap(); self.read_str(q) } else { let mut s = String::new(); while let Some(c) = self.peek() { if c.is_alphanumeric() || c == '_' || c == '-' { s.push(self.advance().unwrap()); } else { break; } } s } }
    pub fn parse_value(&mut self) -> NolValue {
        self.skip_ws(); if self.start.elapsed() > Duration::from_secs(1) { panic!("Timeout"); } self.depth += 1; if self.depth > 100 { panic!("Depth"); }
        let c = self.peek().expect("EOF");
        let res = match c {
            '{' => { self.advance(); let mut o = BTreeMap::new(); while let Some(_) = self.peek() { self.skip_ws(); if self.peek() == Some('}') { self.advance(); break; } self.parse_pair(&mut o); self.skip_ws(); if self.peek() == Some(',') { self.advance(); } } NolValue::Object(o) }
            '[' => { self.advance(); let mut a = Vec::new(); while let Some(_) = self.peek() { self.skip_ws(); if self.peek() == Some(']') { self.advance(); break; } a.push(self.parse_value()); self.skip_ws(); if self.peek() == Some(',') { self.advance(); } } NolValue::Array(a) }
            '"' | '\'' => { let q = self.advance().unwrap(); NolValue::String(self.read_str(q)) }
            '&' | '*' | '<' if self.nole => {
                let op = self.advance().unwrap();
                if op == '<' { if self.peek() == Some('<') { self.pos -= 1; self.depth -= 1; return NolValue::Null; } let mut t = String::new(); while let Some(c) = self.peek() { if c == '>' { break; } t.push(self.advance().unwrap()); } if self.peek() == Some('>') { self.advance(); } let val = self.parse_value(); let mut ci = BTreeMap::new(); ci.insert("type".to_string(), NolValue::String(t)); ci.insert("value".to_string(), val); let mut co = BTreeMap::new(); co.insert("_coerce".to_string(), NolValue::Object(ci)); NolValue::Object(co) }
                else if op == '*' { let mut n = String::new(); while let Some(c) = self.peek() { if c.is_alphanumeric() || c == '_' || c == '-' { n.push(self.advance().unwrap()); } else { break; } } NolValue::String(format!("*{}", n)) }
                else { let mut n = String::new(); while let Some(c) = self.peek() { if c.is_alphanumeric() || c == '_' || c == '-' { n.push(self.advance().unwrap()); } else { break; } } let val = self.parse_value(); let mut ai = BTreeMap::new(); ai.insert("name".to_string(), NolValue::String(n)); ai.insert("value".to_string(), val); let mut an = BTreeMap::new(); an.insert("_anchor".to_string(), NolValue::Object(ai)); NolValue::Object(an) }
            }
            '0'..='9' | '-' => { let mut s = String::new(); while let Some(c) = self.peek() { if c.is_ascii_digit() || ".-eE+".contains(c) { s.push(self.advance().unwrap()); } else { break; } } if s.contains('.') || s.to_lowercase().contains('e') { NolValue::Float(s.parse().unwrap()) } else { NolValue::Int(s.parse().unwrap()) } }
            _ => { let mut s = String::new(); while let Some(c) = self.peek() { if c.is_alphabetic() { s.push(self.advance().unwrap()); } else { break; } } match s.as_str() { "true" => NolValue::Bool(true), "false" => NolValue::Bool(false), "null" => NolValue::Null, _ => panic!("Invalid: {}", s) } }
        }; self.depth -= 1; res
    }
    fn parse_pair(&mut self, o: &mut BTreeMap<String, NolValue>) {
        self.skip_ws(); if self.nole && self.peek() == Some('&') {
            self.advance(); let n = self.read_key(); self.skip_ws(); let mut ai = BTreeMap::new(); ai.insert("name".to_string(), NolValue::String(n));
            if self.peek() == Some(':') { self.advance(); ai.insert("value".to_string(), self.parse_value()); } else { ai.insert("value".to_string(), NolValue::Null); }
            if !o.contains_key("_anchors") { o.insert("_anchors".to_string(), NolValue::Array(Vec::new())); }
            if let Some(NolValue::Array(a)) = o.get_mut("_anchors") { a.push(NolValue::Object(ai)); } return;
        }
        let mut is_m = false; if self.nole && self.peek() == Some('<') { self.advance(); if self.peek() == Some('<') { self.advance(); is_m = true; } else { self.pos -= 1; } }
        let key = if is_m { "<<".to_string() } else { self.read_key() };
        if !self.nole && ["_env", "_interpolate", "_meta"].contains(&key.as_str()) { panic!("Reserved: {}", key); }
        self.skip_ws(); if self.advance() != Some(':') { panic!("Expected : for {}", key); }
        let val = self.parse_value(); if is_m { if !o.contains_key("<<") { o.insert("<<".to_string(), NolValue::Array(Vec::new())); } if let Some(NolValue::Array(a)) = o.get_mut("<<") { a.push(val); } } else { if o.contains_key(&key) { panic!("Duplicate: {}", key); } o.insert(key, val); }
    }
    pub fn parse(&mut self) -> BTreeMap<String, NolValue> {
        let mut root = BTreeMap::new();
        while self.pos < self.text.len() {
            self.skip_ws(); if self.peek().is_none() { break; }
            if self.peek() == Some('[') {
                self.advance(); let mut is_a = false; if self.peek() == Some('*') { self.advance(); is_a = true; }
                let mut path = String::new(); while let Some(c) = self.peek() { if c == ']' { self.advance(); break; } path.push(self.advance().unwrap()); }
                let parts: Vec<String> = path.split('.').map(|s| s.to_string()).collect(); let mut curr = &mut root;
                for (i, p) in parts.iter().enumerate() {
                    let last = i == parts.len() - 1; if !curr.contains_key(p) { curr.insert(p.clone(), if last && is_a { NolValue::Array(Vec::new()) } else { NolValue::Object(BTreeMap::new()) }); }
                    if last {
                        if is_a { if let Some(NolValue::Array(a)) = curr.get_mut(p) { a.push(NolValue::Object(BTreeMap::new())); let idx = a.len() - 1; if let Some(NolValue::Object(o)) = a.get_mut(idx) { self.parse_into(o); } } }
                        else if let Some(NolValue::Object(o)) = curr.get_mut(p) { self.parse_into(o); }
                    } else if let Some(NolValue::Object(o)) = curr.get_mut(p) { curr = o; } else { panic!("Collision"); }
                }
            } else { self.parse_pair(&mut root); }
        } root
    }
    fn parse_into(&mut self, o: &mut BTreeMap<String, NolValue>) { while self.pos < self.text.len() { self.skip_ws(); match self.peek() { Some('[') | None => break, _ => self.parse_pair(o) } } }
}

pub struct Evaluator { anchors: BTreeMap<String, NolValue>, app_env: HashSet<String>, doc_env: HashSet<String> }
impl Evaluator {
    pub fn new(app_env: Vec<String>) -> Self { Self { anchors: BTreeMap::new(), app_env: app_env.into_iter().collect(), doc_env: HashSet::new() } }
    pub fn evaluate(&mut self, mut root: NolValue) -> NolValue { root = self.collect_meta(root); root = self.resolve_merges(root, 0); root = self.resolve_env(root); let root_clone = root.clone(); root = self.resolve_interp(root, &root_clone, 0); self.resolve_coerce(root) }
    fn collect_meta(&mut self, v: NolValue) -> NolValue {
        match v {
            NolValue::Object(mut o) => {
                if let Some(NolValue::Array(ans)) = o.remove("_anchors") { for av in ans { if let NolValue::Object(mut a) = av { if let Some(NolValue::String(n)) = a.remove("name") { let val_raw = a.remove("value").unwrap(); let val = if val_raw == NolValue::Null { NolValue::Object(o.clone()) } else { val_raw }; let val = self.collect_meta(val); self.anchors.insert(n, val); } } } }
                if let Some(NolValue::Object(mut a)) = o.remove("_anchor") { let n = if let Some(NolValue::String(n)) = a.remove("name") { n } else { panic!() }; let val_raw = a.remove("value").unwrap(); let val = if val_raw == NolValue::Null { NolValue::Object(o.clone()) } else { val_raw }; let val = self.collect_meta(val); self.anchors.insert(n, val.clone()); return val; }
                if let Some(NolValue::Object(e)) = o.get("_env") { if let Some(NolValue::Array(al)) = e.get("allowed") { for x in al { if let NolValue::String(s) = x { self.doc_env.insert(s.clone()); } } } }
                NolValue::Object(o.into_iter().map(|(k, val)| (k, self.collect_meta(val))).collect())
            }
            NolValue::Array(a) => NolValue::Array(a.into_iter().map(|x| self.collect_meta(x)).collect()), _ => v
        }
    }
    fn resolve_merges(&mut self, v: NolValue, depth: usize) -> NolValue {
        if depth > 20 { panic!("Merge depth"); }
        match v {
            NolValue::Object(mut o) => {
                if let Some(merges) = o.remove("<<") { let ma = match merges { NolValue::Array(a) => a, _ => vec![merges] }; for m in ma { let mut rm = m; if let NolValue::String(ref s) = rm { if s.starts_with('*') { let an = &s[1..]; rm = self.anchors.get(an).expect("Undef anchor").clone(); } } rm = self.resolve_merges(rm, depth + 1); if let NolValue::Object(mo) = rm { for (mk, mv) in mo { if mk.starts_with('_') { continue; } if !o.contains_key(&mk) { o.insert(mk, mv); } } } } }
                NolValue::Object(o.into_iter().map(|(k, val)| (k, self.resolve_merges(val, depth))).collect())
            }
            NolValue::Array(a) => NolValue::Array(a.into_iter().map(|x| self.resolve_merges(x, depth)).collect()), _ => v
        }
    }
    fn resolve_env(&mut self, v: NolValue) -> NolValue {
        match v {
            NolValue::Object(o) => { if o.len() == 1 && o.contains_key("env") { if let Some(NolValue::String(var)) = o.get("env") { if self.app_env.is_empty() && self.doc_env.is_empty() || self.app_env.contains(var) || self.doc_env.contains(var) { let val = env::var(var).unwrap_or_default(); if val.len() > 65536 { panic!("Env size limit"); } return NolValue::String(val); } panic!("Env denied: {}", var); } } NolValue::Object(o.into_iter().map(|(k, val)| (k, self.resolve_env(val))).collect()) }
            NolValue::Array(a) => NolValue::Array(a.into_iter().map(|x| self.resolve_env(x)).collect()), _ => v
        }
    }
    fn resolve_interp(&mut self, v: NolValue, root: &NolValue, depth: usize) -> NolValue {
        if depth > 50 { panic!("Interp depth"); }
        match v {
            NolValue::String(s) if s.contains("${") => {
                let mut res = String::new(); let mut i = 0; let b = s.as_bytes();
                while i < b.len() { if b[i] == b'$' && i + 1 < b.len() && b[i+1] == b'{' { let start = i + 2; let mut end = start; while end < b.len() && b[end] != b'}' { end += 1; } if end == b.len() { panic!("Unclosed"); } let path = &s[start..end]; let mut curr = root; for p in path.split('.') { if let NolValue::Object(o) = curr { curr = o.get(p).expect("Undef interp"); } else { panic!("Undef interp"); } } let mut vs = match curr { NolValue::String(ref s) => s.clone(), _ => curr.dump(2, 0, true) }; if vs.contains("${") { vs = match self.resolve_interp(NolValue::String(vs), root, depth + 1) { NolValue::String(s) => s, _ => panic!() }; } res.push_str(&vs); i = end + 1; } else { res.push(b[i] as char); i += 1; } }
                if res.len() > 10*1024*1024 { panic!("Interp size limit"); }
                NolValue::String(res)
            }
            NolValue::Object(o) => NolValue::Object(o.into_iter().map(|(k, val)| (k, self.resolve_interp(val, root, depth))).collect()),
            NolValue::Array(a) => NolValue::Array(a.into_iter().map(|x| self.resolve_interp(x, root, depth)).collect()), _ => v
        }
    }
    pub fn resolve_coerce(&mut self, v: NolValue) -> NolValue {
        match v {
            NolValue::Object(mut o) => { if let Some(NolValue::Object(mut c)) = o.remove("_coerce") { if let Some(NolValue::String(t)) = c.remove("type") { let val_raw = c.remove("value").unwrap(); let val = self.resolve_coerce(val_raw); let s = match val { NolValue::String(ref s) => s.clone(), _ => val.dump(2, 0, false) }; return match t.as_str() { "int" => NolValue::Int(s.parse().unwrap()), "float" => NolValue::Float(s.parse().unwrap()), "bool" => NolValue::Bool(s.to_lowercase() == "true"), _ => NolValue::String(s) }; } } NolValue::Object(o.into_iter().map(|(k, val)| (k, self.resolve_coerce(val))).collect()) }
            NolValue::Array(a) => NolValue::Array(a.into_iter().map(|x| self.resolve_coerce(x)).collect()), _ => v
        }
    }
}
pub fn parse(text: &str, app_env: Vec<String>) -> NolValue { let mut p = NolParser::new(text, true); let root = NolValue::Object(p.parse()); Evaluator::new(app_env).evaluate(root) }
