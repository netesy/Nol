use std::collections::{HashMap, HashSet};
#[derive(Debug, Clone, PartialEq)]
pub enum Value { Null, Bool(bool), Int(i64), Float(f64), String(String), Array(Vec<Value>), Object(HashMap<String, Value>) }
#[derive(Debug)]
pub struct NolError(pub String);
pub struct Parser { input: Vec<char>, pos: usize, _is_nole: bool, _app_env: HashSet<String>, _doc_env: HashSet<String>, anchors: HashMap<String, Value> }
impl Parser {
    pub fn new(input: &str, is_nole: bool, env_allow: Vec<String>) -> Result<Self, NolError> {
        if input.len() > 10 * 1024 * 1024 { return Err(NolError("Size".into())); }
        if input.contains('\0') { return Err(NolError("Null".into())); }
        Ok(Self { input: input.chars().collect(), pos: 0, _is_nole: is_nole, _app_env: env_allow.into_iter().collect(), _doc_env: HashSet::new(), anchors: HashMap::new() })
    }
    fn peek(&self, n: usize) -> char { *self.input.get(self.pos + n).unwrap_or(&'\0') }
    fn adv(&mut self) -> char { let c = self.peek(0); if c != '\0' { self.pos += 1; } c }
    fn skip_ws(&mut self) { while self.peek(0).is_whitespace() || self.peek(0) == '#' { if self.peek(0) == '#' { while self.peek(0) != '\n' && self.peek(0) != '\0' { self.adv(); } } else { self.adv(); } } }
    pub fn parse(&mut self) -> Result<Value, NolError> {
        let mut root = HashMap::new();
        while self.peek(0) != '\0' { self.skip_ws(); if self.peek(0) == '\0' { break; } if self.peek(0) == '[' { self.parse_section(&mut root)?; } else { self.parse_pair(&mut root)?; } }
        Ok(Value::Object(root))
    }
    fn parse_section(&mut self, root: &mut HashMap<String, Value>) -> Result<(), NolError> {
        self.adv(); while self.peek(0) != ']' && self.peek(0) != '\0' { self.adv(); }
        self.adv(); self.skip_ws();
        while self.peek(0) != '[' && self.peek(0) != '\0' { self.parse_pair(root)?; self.skip_ws(); }
        Ok(())
    }
    fn parse_pair(&mut self, obj: &mut HashMap<String, Value>) -> Result<(), NolError> {
        let mut key = String::new();
        if self.peek(0) == '<' && self.peek(1) == '<' { self.adv(); self.adv(); key = "<<".into(); }
        else if self.peek(0) == '"' { self.adv(); while self.peek(0) != '"' && self.peek(0) != '\0' { key.push(self.adv()); } self.adv(); }
        else { while self.peek(0).is_alphanumeric() || "_-".contains(self.peek(0)) { key.push(self.adv()); } }
        self.skip_ws(); if self.adv() != ':' { return Err(NolError("Expected :".into())); }
        self.skip_ws(); let val = self.parse_value()?; obj.insert(key, val); Ok(())
    }
    fn parse_value(&mut self) -> Result<Value, NolError> {
        self.skip_ws(); let c = self.peek(0);
        match c {
            '"' => { self.adv(); let mut s = String::new(); while self.peek(0) != '"' && self.peek(0) != '\0' { s.push(self.adv()); } self.adv(); Ok(Value::String(s)) }
            '[' => { self.adv(); let mut a = Vec::new(); while self.peek(0) != ']' && self.peek(0) != '\0' { self.skip_ws(); a.push(self.parse_value()?); self.skip_ws(); if self.peek(0) == ',' { self.adv(); } } self.adv(); Ok(Value::Array(a)) }
            '{' => { self.adv(); let mut o = HashMap::new(); while self.peek(0) != '}' && self.peek(0) != '\0' { self.skip_ws(); self.parse_pair(&mut o)?; self.skip_ws(); if self.peek(0) == ',' { self.adv(); } } self.adv(); Ok(Value::Object(o)) }
            '&' => { self.adv(); let mut n = String::new(); while self.peek(0).is_alphanumeric() { n.push(self.adv()); } let v = self.parse_value()?; self.anchors.insert(n, v.clone()); Ok(v) }
            '*' => { self.adv(); let mut n = String::new(); while self.peek(0).is_alphanumeric() { n.push(self.adv()); } Ok(Value::String(format!("*{}", n))) }
            '<' => { self.adv(); while self.peek(0) != '>' { self.adv(); } self.adv(); self.parse_value() }
            _ if c.is_digit(10) || c == '-' => { let mut s = String::new(); while self.peek(0).is_digit(10) || ".-".contains(self.peek(0)) { s.push(self.adv()); } if s.contains('.') { Ok(Value::Float(s.parse().map_err(|_| NolError("F".into()))?)) } else { Ok(Value::Int(s.parse().map_err(|_| NolError("I".into()))?)) } }
            't' => { for _ in 0..4 { self.adv(); } Ok(Value::Bool(true)) }
            'f' => { for _ in 0..5 { self.adv(); } Ok(Value::Bool(false)) }
            'n' => { for _ in 0..4 { self.adv(); } Ok(Value::Null) }
            _ => Err(NolError(format!("C: {}", c)))
        }
    }
}
pub struct Document { pub root: Value }
impl Document {
    pub fn parse(s: &str, n: bool, e: Vec<String>) -> Result<Self, NolError> { Ok(Self { root: Parser::new(s, n, e)?.parse()? }) }
    pub fn get(&self, p: &str) -> Option<&Value> {
        let mut curr = &self.root;
        for part in p.split('.') {
            if let Value::Object(o) = curr { curr = o.get(part)?; } else { return None; }
        }
        Some(curr)
    }
}
pub fn parse(input: &str) -> Result<Value, NolError> { Parser::new(input, false, vec![])?.parse() }
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn test_basic() {
        let input = "server: { host: \"localhost\", port: 8080 }";
        let doc = Document::parse(input, false, vec![]).unwrap();
        assert_eq!(doc.get("server.port"), Some(&Value::Int(8080)));
    }
}
