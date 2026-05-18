use std::collections::{BTreeMap};
use std::time::{Instant, Duration};

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
                for (k, v) in o { if k.starts_with('_') { continue; } let val = v.dump(indent, level + 1, false); if is_root { lines.push(format!("{}: {}", k, val)); } else { lines.push(format!("\n{}{}: {}", next_pad, k, val)); } }
                if is_root { return lines.join("\n"); } if o.is_empty() { return "{}".to_string(); }
                format!("{{{}\n{}}}", lines.join(","), pad)
            }
        }
    }
}

pub struct Document {
    pub root: BTreeMap<String, NolValue>,
}

impl Document {
    pub fn get(&self, path: &str) -> Option<&NolValue> {
        let parts: Vec<&str> = path.split('.').collect();
        let mut curr = &self.root;
        for (i, p) in parts.iter().enumerate() {
            if i == parts.len() - 1 {
                return curr.get(*p);
            } else {
                match curr.get(*p) {
                    Some(NolValue::Object(o)) => curr = o,
                    _ => return None,
                }
            }
        }
        None
    }
    pub fn exists(&self, path: &str) -> bool { self.get(path).is_some() }
}

pub struct Builder {
    pub root: BTreeMap<String, NolValue>,
}

impl Builder {
    pub fn new() -> Self { Self { root: BTreeMap::new() } }
    pub fn set(mut self, path: &str, value: NolValue) -> Self {
        let parts: Vec<String> = path.split('.').map(|s| s.to_string()).collect();
        let mut curr = &mut self.root;
        for p in parts.iter().take(parts.len() - 1) {
            if !curr.contains_key(p) || !matches!(curr.get(p), Some(NolValue::Object(_))) {
                curr.insert(p.clone(), NolValue::Object(BTreeMap::new()));
            }
            curr = match curr.get_mut(p).unwrap() {
                NolValue::Object(o) => o,
                _ => unreachable!(),
            };
        }
        curr.insert(parts.last().unwrap().clone(), value);
        self
    }
    pub fn build(self) -> Document { Document { root: self.root } }
}

pub struct NolParser<'a> { text: &'a str, pos: usize, start: Instant, depth: usize }
impl<'a> NolParser<'a> {
    pub fn new(text: &'a str) -> Self { Self { text, pos: 0, start: Instant::now(), depth: 0 } }
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
            '{' => { self.advance(); let mut o = BTreeMap::new(); while let Some(_) = self.peek() { self.skip_ws(); if self.peek() == Some('}') { self.advance(); break; } self.parse_pair(&mut o); self.skip_ws() ; if self.peek() == Some(',') { self.advance(); } } NolValue::Object(o) }
            '[' => { self.advance(); let mut a = Vec::new(); while let Some(_) = self.peek() { self.skip_ws(); if self.peek() == Some(']') { self.advance(); break; } a.push(self.parse_value()); self.skip_ws(); if self.peek() == Some(',') { self.advance(); } } NolValue::Array(a) }
            '"' | '\'' => { let q = self.advance().unwrap(); NolValue::String(self.read_str(q)) }
            '0'..='9' | '-' => { let mut s = String::new(); while let Some(c) = self.peek() { if c.is_ascii_digit() || ".-eE+".contains(c) { s.push(self.advance().unwrap()); } else { break; } } if s.contains('.') || s.to_lowercase().contains('e') { NolValue::Float(s.parse().unwrap()) } else { NolValue::Int(s.parse().unwrap()) } }
            _ => { let mut s = String::new(); while let Some(c) = self.peek() { if c.is_alphabetic() { s.push(self.advance().unwrap()); } else { break; } } match s.as_str() { "true" => NolValue::Bool(true), "false" => NolValue::Bool(false), "null" => NolValue::Null, _ => panic!("Invalid: {}", s) } }
        }; self.depth -= 1; res
    }
    fn parse_pair(&mut self, o: &mut BTreeMap<String, NolValue>) {
        self.skip_ws(); let key = self.read_key(); if ["_env", "_interpolate", "_meta"].contains(&key.as_str()) { panic!("Reserved: {}", key); }
        self.skip_ws(); if self.advance() != Some(':') { panic!("Expected : for {}", key); }
        let val = self.parse_value(); if o.contains_key(&key) { panic!("Duplicate: {}", key); } o.insert(key, val);
    }
    pub fn parse(&mut self) -> Document {
        let mut root = BTreeMap::new();
        while self.pos < self.text.len() {
            self.skip_ws(); if self.peek().is_none() { break; }
            if self.peek() == Some('[') {
                self.advance(); let mut path_s = String::new(); while let Some(c) = self.peek() { if c == ']' { self.advance(); break; } path_s.push(self.advance().unwrap()); }
                let parts: Vec<String> = path_s.split('.').map(|s| s.to_string()).collect();
                let mut curr = &mut root;
                for p in parts.iter().take(parts.len() - 1) {
                    if !curr.contains_key(p) || !matches!(curr.get(p), Some(NolValue::Object(_))) {
                        curr.insert(p.clone(), NolValue::Object(BTreeMap::new()));
                    }
                    curr = match curr.get_mut(p).unwrap() {
                        NolValue::Object(o) => o,
                        _ => unreachable!(),
                    };
                }
                let last_p = parts.last().unwrap();
                if !curr.contains_key(last_p) || !matches!(curr.get(last_p), Some(NolValue::Object(_))) {
                    curr.insert(last_p.clone(), NolValue::Object(BTreeMap::new()));
                }
                if let NolValue::Object(ref mut o) = curr.get_mut(last_p).unwrap() {
                    self.parse_into(o);
                }
            } else { self.parse_pair(&mut root); }
        } Document { root }
    }
    fn parse_into(&mut self, o: &mut BTreeMap<String, NolValue>) { while self.pos < self.text.len() { self.skip_ws(); match self.peek() { Some('[') | None => break, _ => self.parse_pair(o) } } }
}
pub fn parse(text: &str) -> Document { let mut p = NolParser::new(text); p.parse() }
