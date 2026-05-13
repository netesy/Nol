use std::collections::HashMap;

#[derive(Debug, Clone, PartialEq)]
pub enum Value {
    Null,
    Bool(bool),
    Int(i64),
    Float(f64),
    String(String),
    Array(Vec<Value>),
    Object(HashMap<String, Value>),
}

#[derive(Debug)]
pub struct NolError(pub String);

pub struct Parser {
    input: Vec<char>,
    pos: usize,
}

impl Parser {
    pub fn new(input: &str) -> Self {
        Self { input: input.chars().collect(), pos: 0 }
    }

    fn peek(&self) -> char { *self.input.get(self.pos).unwrap_or(&'\0') }
    fn adv(&mut self) -> char { let c = self.peek(); if c != '\0' { self.pos += 1; } c }

    fn skip_ws(&mut self) {
        while self.peek().is_whitespace() || self.peek() == '#' {
            if self.peek() == '#' { while self.peek() != '\n' && self.peek() != '\0' { self.adv(); } }
            else { self.adv(); }
        }
    }

    pub fn parse(&mut self) -> Result<Value, NolError> {
        let mut root = HashMap::new();
        while self.peek() != '\0' {
            self.skip_ws();
            if self.peek() == '\0' { break; }
            if self.peek() == '[' { self.parse_section(&mut root)?; }
            else { self.parse_pair(&mut root)?; }
        }
        Ok(Value::Object(root))
    }

    fn parse_section(&mut self, root: &mut HashMap<String, Value>) -> Result<(), NolError> {
        self.adv(); // [
        let mut path = Vec::new();
        while self.peek() != ']' && self.peek() != '\0' {
            if self.peek() == '*' { self.adv(); } // Ignore for now
            else if self.peek().is_alphanumeric() || self.peek() == '_' {
                let mut s = String::new();
                while self.peek().is_alphanumeric() || self.peek() == '_' || self.peek() == '-' { s.push(self.adv()); }
                path.push(s);
            }
            if self.peek() == '.' { self.adv(); }
        }
        self.adv(); // ]
        self.skip_ws();
        // Just a simple implementation for now
        while self.peek() != '[' && self.peek() != '\0' {
            self.parse_pair(root)?;
            self.skip_ws();
        }
        Ok(())
    }

    fn parse_pair(&mut self, obj: &mut HashMap<String, Value>) -> Result<(), NolError> {
        let mut key = String::new();
        if self.peek() == '"' {
            self.adv();
            while self.peek() != '"' && self.peek() != '\0' { key.push(self.adv()); }
            self.adv();
        } else {
            while self.peek().is_alphanumeric() || self.peek() == '_' || self.peek() == '-' { key.push(self.adv()); }
        }
        self.skip_ws();
        if self.adv() != ':' { return Err(NolError("Expected :".into())); }
        self.skip_ws();
        let val = self.parse_value()?;
        obj.insert(key, val);
        Ok(())
    }

    fn parse_value(&mut self) -> Result<Value, NolError> {
        self.skip_ws();
        let c = self.peek();
        match c {
            '"' => {
                self.adv();
                let mut s = String::new();
                while self.peek() != '"' && self.peek() != '\0' { s.push(self.adv()); }
                self.adv();
                Ok(Value::String(s))
            }
            '[' => {
                self.adv();
                let mut a = Vec::new();
                while self.peek() != ']' && self.peek() != '\0' {
                    self.skip_ws();
                    a.push(self.parse_value()?);
                    self.skip_ws();
                    if self.peek() == ',' { self.adv(); }
                }
                self.adv();
                Ok(Value::Array(a))
            }
            '{' => {
                self.adv();
                let mut o = HashMap::new();
                while self.peek() != '}' && self.peek() != '\0' {
                    self.skip_ws();
                    self.parse_pair(&mut o)?;
                    self.skip_ws();
                    if self.peek() == ',' { self.adv(); }
                }
                self.adv();
                Ok(Value::Object(o))
            }
            _ if c.is_digit(10) || c == '-' => {
                let mut s = String::new();
                while self.peek().is_digit(10) || self.peek() == '.' || self.peek() == '-' { s.push(self.adv()); }
                if s.contains('.') { Ok(Value::Float(s.parse().map_err(|_| NolError("Invalid float".into()))?)) }
                else { Ok(Value::Int(s.parse().map_err(|_| NolError("Invalid int".into()))?)) }
            }
            't' => { for _ in 0..4 { self.adv(); } Ok(Value::Bool(true)) }
            'f' => { for _ in 0..5 { self.adv(); } Ok(Value::Bool(false)) }
            'n' => { for _ in 0..4 { self.adv(); } Ok(Value::Null) }
            _ => Err(NolError(format!("Unexpected char: {}", c)))
        }
    }
}

pub fn parse(input: &str) -> Result<Value, NolError> {
    Parser::new(input).parse()
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn test_basic() {
        let input = "server: { host: \"localhost\", port: 8080 }";
        let res = parse(input).unwrap();
        if let Value::Object(o) = res {
            if let Some(Value::Object(so)) = o.get("server") {
                assert_eq!(so.get("port"), Some(&Value::Int(8080)));
            } else { panic!("Expected server object"); }
        } else { panic!("Expected root object"); }
    }
}
