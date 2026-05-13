<?php

class NolError extends Exception {
    public function __construct($message, $line = null, $col = null) {
        $fullMessage = $message . ($line !== null ? " at $line:$col" : "");
        parent::__construct($fullMessage);
    }
}

class Tok {
    const EOF = 'EOF'; const Identifier = 'Identifier'; const String = 'String'; const Number = 'Number';
    const Date = 'Date'; const TrueVal = 'True'; const FalseVal = 'False'; const NullVal = 'Null';
    const Colon = 'Colon'; const Comma = 'Comma'; const LBracket = 'LBracket'; const RBracket = 'RBracket';
    const LBrace = 'LBrace'; const RBrace = 'RBrace'; const Newline = 'Newline'; const Dot = 'Dot';
    const Star = 'Star'; const LShift = 'LShift'; const Ampersand = 'Ampersand';
    const LArrow = 'LArrow'; const RArrow = 'RArrow';
}

class Lexer {
    private $input; private $pos = 0; private $line = 1; private $col = 1;

    public function __construct($input) {
        if (strlen($input) > 10 * 1024 * 1024) throw new NolError("Document size limit exceeded");
        $this->input = $input;
    }

    private function peek($n = 0) {
        return ($this->pos + $n < strlen($this->input)) ? $this->input[$this->pos + $n] : "\0";
    }

    private function advance() {
        $c = $this->peek();
        $this->pos++;
        if ($c === "\n") { $this->line++; $this->col = 1; } else { $this->col++; }
        return $c;
    }

    public function nextToken() {
        while (true) {
            $c = $this->peek();
            if ($c === "\0") return ['type' => Tok::EOF, 'text' => '', 'line' => $this->line, 'col' => $this->col];
            if (ctype_space($c)) {
                if ($c === "\n") {
                    $t = ['type' => Tok::Newline, 'text' => "\n", 'line' => $this->line, 'col' => $this->col];
                    $this->advance(); return $t;
                }
                $this->advance(); continue;
            }
            if ($c === '#') {
                $this->advance();
                if ($this->peek() === '#') {
                    $this->advance();
                    while ($this->peek() !== "\0") {
                        if ($this->peek() === '#' && $this->peek(1) === '#') { $this->advance(); $this->advance(); break; }
                        $this->advance();
                    }
                } else {
                    while ($this->peek() !== "\0" && $this->peek() !== "\n") $this->advance();
                }
                continue;
            }
            if ($c === '"' || $c === "'") return $this->scanString();
            if (ctype_digit($c) || ($c === '-' && ctype_digit($this->peek(1)))) return $this->scanNumberOrDate();
            if (ctype_alpha($c) || $c === '_') return $this->scanIdentifier();

            $singles = [
                ':' => Tok::Colon, ',' => Tok::Comma, '[' => Tok::LBracket, ']' => Tok::RBracket,
                '{' => Tok::LBrace, '}' => Tok::RBrace, '.' => Tok::Dot, '*' => Tok::Star,
                '&' => Tok::Ampersand, '>' => Tok::RArrow
            ];
            if (isset($singles[$c])) {
                $t = ['type' => $singles[$c], 'text' => $c, 'line' => $this->line, 'col' => $this->col];
                $this->advance(); return $t;
            }
            if ($c === '<') {
                $line = $this->line; $col = $this->col; $this->advance();
                if ($this->peek() === '<') { $this->advance(); return ['type' => Tok::LShift, 'text' => '<<', 'line' => $line, 'col' => $col]; }
                return ['type' => Tok::LArrow, 'text' => '<', 'line' => $line, 'col' => $col];
            }
            throw new NolError("Unexpected character: $c", $this->line, $this->col);
        }
    }

    private function scanString() {
        $quote = $this->advance(); $line = $this->line; $col = $this->col;
        $isTriple = ($this->peek() === $quote && $this->peek(1) === $quote);
        if ($isTriple) { $this->advance(); $this->advance(); }
        $res = "";
        while (true) {
            $c = $this->peek();
            if ($c === "\0") throw new NolError("Unterminated string", $line, $col);
            if ($isTriple) {
                if ($c === $quote && $this->peek(1) === $quote && $this->peek(2) === $quote) {
                    $this->advance(); $this->advance(); $this->advance(); break;
                }
            } else {
                if ($c === $quote) { $this->advance(); break; }
                if ($c === "\n") throw new NolError("Newline in single-quoted string", $line, $col);
            }
            if ($c === '\\' && $quote === '"') {
                $this->advance(); $e = $this->advance();
                switch ($e) {
                    case '"': $res .= '"'; break; case "'": $res .= "'"; break; case '\\': $res .= '\\'; break;
                    case 'n': $res .= "\n"; break; case 'r': $res .= "\r"; break; case 't': $res .= "\t"; break;
                    case 'u': case 'U':
                        $n = ($e === 'u' ? 4 : 8); $u = ""; for ($i = 0; $i < $n; $i++) $u .= $this->advance();
                        $res .= mb_chr(hexdec($u), 'UTF-8'); break;
                    default: throw new NolError("Invalid escape: \\$e", $this->line, $this->col);
                }
            } else { $res .= $this->advance(); }
            if (strlen($res) > 1024 * 1024) throw new NolError("String length limit exceeded");
        }
        return ['type' => Tok::String, 'text' => $res, 'line' => $line, 'col' => $col];
    }

    private function scanNumberOrDate() {
        $line = $this->line; $col = $this->col; $s = "";
        while (preg_match('/[a-zA-Z0-9\-\._:T\+]/', $this->peek())) $s .= $this->advance();
        if (preg_match('/^\d{4}-\d{2}-\d{2}(T\d{2}:\d{2}:\d{2}(Z|[+-]\d{2}:\d{2})?)?$/', $s))
            return ['type' => Tok::Date, 'text' => $s, 'line' => $line, 'col' => $col];
        return ['type' => Tok::Number, 'text' => $s, 'line' => $line, 'col' => $col];
    }

    private function scanIdentifier() {
        $line = $this->line; $col = $this->col; $s = "";
        while (preg_match('/[a-zA-Z0-9_-]/', $this->peek())) $s .= $this->advance();
        $kw = ['true' => Tok::TrueVal, 'false' => Tok::FalseVal, 'null' => Tok::NullVal];
        return ['type' => $kw[$s] ?? Tok::Identifier, 'text' => $s, 'line' => $line, 'col' => $col];
    }
}

class Parser {
    private $lexer; private $cur; private $isNole; private $appEnvAllowlist; private $docEnvAllowlist;
    private $anchors; private $root; private $depth = 0; private $startTime;

    public function __construct($input, $isNole = false, $envAllowlist = []) {
        $this->lexer = new Lexer($input); $this->cur = $this->lexer->nextToken();
        $this->isNole = $isNole; $this->appEnvAllowlist = array_flip($envAllowlist);
        $this->docEnvAllowlist = []; $this->anchors = []; $this->root = (object)[];
        $this->startTime = microtime(true);
    }

    private function error($msg) { throw new NolError($msg, $this->cur['line'], $this->cur['col']); }
    private function next() { $this->cur = $this->lexer->nextToken(); }
    private function skipNewlines() { while ($this->cur['type'] === Tok::Newline) $this->next(); }

    public function parse() {
        while ($this->cur['type'] !== Tok::EOF) {
            $this->skipNewlines(); if ($this->cur['type'] === Tok::EOF) break;
            if ($this->cur['type'] === Tok::LBracket) $this->parseSection();
            elseif ($this->isKeyToken()) $this->parsePair($this->root);
            else $this->error("Unexpected token: " . $this->cur['type']);
            if (microtime(true) - $this->startTime > 1.0) $this->error("Parse timeout");
        }
        if ($this->isNole) {
            if (isset($this->root->_env) && is_object($this->root->_env) && isset($this->root->_env->allowed) && is_array($this->root->_env->allowed)) {
                $this->docEnvAllowlist = array_flip($this->root->_env->allowed);
            }
            $this->root = $this->resolveMerges($this->root);
            $this->root = $this->resolveEnv($this->root);
            $this->root = $this->resolveInterpolations($this->root);
            $this->root = $this->resolveCoercions($this->root);
        }
        return $this->root;
    }

    private function isKeyToken() { return in_array($this->cur['type'], [Tok::Identifier, Tok::String, Tok::LShift]); }

    private function parseSection() {
        $this->next(); $path = []; $isArray = false;
        while (true) {
            if ($this->cur['type'] === Tok::Star) { $isArray = true; $this->next(); }
            elseif (in_array($this->cur['type'], [Tok::Identifier, Tok::String])) {
                $path[] = Normalizer::normalize($this->cur['text'], Normalizer::FORM_C);
                $this->next();
            } else $this->error("Expected section name");
            if ($this->cur['type'] === Tok::Dot) { $this->next(); continue; }
            if ($this->cur['type'] === Tok::RBracket) { $this->next(); break; }
            $this->error("Expected . or ]");
        }
        $target = $this->root;
        for ($i = 0; $i < count($path); $i++) {
            $part = $path[$i];
            if (isset($target->$part)) {
                if (!is_object($target->$part) && !($isArray && $i === count($path) - 1 && is_array($target->$part)))
                    $this->error("Collision at $part");
            } else {
                $target->$part = ($isArray && $i === count($path) - 1) ? [] : (object)[];
            }
            if (!($isArray && $i === count($path) - 1)) $target = $target->$part;
        }
        if ($isArray) {
            $part = end($path);
            $newObj = (object)[];
            $target->{$part}[] = $newObj;
            $target = $newObj;
        }
        $this->skipNewlines();
        while ($this->isKeyToken()) { $this->parsePair($target); $this->skipNewlines(); }
    }

    private function parsePair($obj) {
        $key = ($this->cur['type'] === Tok::LShift) ? '<<' : Normalizer::normalize($this->cur['text'], Normalizer::FORM_C);
        if (in_array($key, ['_env', '_interpolate', '_meta']) && !$this->isNole) $this->error("Reserved key $key");
        if (isset($obj->$key) && $key !== '<<') $this->error("Duplicate key: $key");
        $this->next(); if ($this->cur['type'] !== Tok::Colon) $this->error("Expected :");
        $this->next(); $val = $this->parseValue();
        if ($key === '<<') {
            if (!isset($obj->{'<<'})) $obj->{'<<'} = [];
            $obj->{'<<'}[] = $val;
        } else $obj->$key = $val;
    }

    private function parseValue() {
        $this->depth++; if ($this->depth > 100) $this->error("Nesting limit exceeded");
        $res = null;
        switch ($this->cur['type']) {
            case Tok::Ampersand:
                $this->next(); $name = $this->cur['text']; $this->next();
                $res = $this->parseValue(); $this->anchors[$name] = $res; break;
            case Tok::Star:
                $this->next(); $res = '*' . $this->cur['text']; $this->next(); break;
            case Tok::LArrow:
                $this->next(); $t = $this->cur['text']; $this->next();
                if ($this->cur['type'] !== Tok::RArrow) $this->error("Expected >");
                $this->next(); $res = (object)['_coerce' => (object)['type' => $t, 'value' => $this->parseValue()]]; break;
            case Tok::String: $res = $this->cur['text']; $this->next(); break;
            case Tok::Number:
                $s = $this->cur['text']; $this->next();
                $res = (strpos($s, '.') !== false || stripos($s, 'e') !== false) ? (float)$s : (int)$s; break;
            case Tok::TrueVal: $res = true; $this->next(); break;
            case Tok::FalseVal: $res = false; $this->next(); break;
            case Tok::NullVal: $res = null; $this->next(); break;
            case Tok::Date: $res = $this->cur['text']; $this->next(); break;
            case Tok::LBracket: $res = $this->parseArray(); break;
            case Tok::LBrace: $res = $this->parseObject(); break;
            default: $this->error("Invalid value: " . $this->cur['type']);
        }
        $this->depth--; return $res;
    }

    private function parseArray() {
        $this->next(); $res = [];
        while ($this->cur['type'] !== Tok::RBracket) {
            $this->skipNewlines(); if ($this->cur['type'] === Tok::RBracket) break;
            $res[] = $this->parseValue(); $this->skipNewlines();
            if ($this->cur['type'] === Tok::Comma) $this->next();
        }
        $this->next(); if (count($res) > 100000) $this->error("Array size limit");
        return $res;
    }

    private function parseObject() {
        $this->next(); $res = (object)[];
        while ($this->cur['type'] !== Tok::RBrace) {
            $this->skipNewlines(); if ($this->cur['type'] === Tok::RBrace) break;
            $this->parsePair($res); $this->skipNewlines();
            if ($this->cur['type'] === Tok::Comma) $this->next();
        }
        $this->next(); if (count(get_object_vars($res)) > 10000) $this->error("Object keys limit");
        return $res;
    }

    private function resolveMerges($val, &$visited = []) {
        if (is_object($val) || is_array($val)) {
            $oid = spl_object_hash((object)$val);
            if (isset($visited[$oid])) throw new NolError("Cycle detected");
            $visited[$oid] = true;
        }
        if (is_object($val)) {
            if (isset($val->{'<<'})) {
                $merges = $val->{'<<'}; unset($val->{'<<'});
                foreach ($merges as $m) {
                    $resolvedM = $this->resolveMerges($m, $visited);
                    if (is_string($resolvedM) && strpos($resolvedM, '*') === 0)
                        $resolvedM = $this->resolveMerges($this->anchors[substr($resolvedM, 1)] ?? null, $visited);
                    if (!is_object($resolvedM)) throw new NolError("Can only merge objects");
                    foreach (get_object_vars($resolvedM) as $k => $v) if (!isset($val->$k)) $val->$k = $v;
                }
            }
            foreach (get_object_vars($val) as $k => $v) $val->$k = $this->resolveMerges($v, $visited);
        } elseif (is_array($val)) {
            foreach ($val as $i => $v) $val[$i] = $this->resolveMerges($v, $visited);
        } elseif (is_string($val) && strpos($val, '*') === 0) {
            $a = substr($val, 1); if (!isset($this->anchors[$a])) throw new NolError("Undefined anchor: $a");
            return $this->resolveMerges($this->anchors[$a], $visited);
        }
        if (is_object($val) || is_array($val)) { $oid = spl_object_hash((object)$val); unset($visited[$oid]); }
        return $val;
    }

    private function resolveEnv($val) {
        if (is_object($val)) {
            $vars = get_object_vars($val);
            if (isset($val->env) && count($vars) === 1 && is_string($val->env)) {
                if (isset($this->appEnvAllowlist[$val->env]) && isset($this->docEnvAllowlist[$val->env]))
                    return getenv($val->env) ?: "";
                throw new NolError("Env access denied: {$val->env}");
            }
            foreach ($vars as $k => $v) $val->$k = $this->resolveEnv($v);
        } elseif (is_array($val)) {
            foreach ($val as $i => $v) $val[$i] = $this->resolveEnv($v);
        }
        return $val;
    }

    private function resolveInterpolations($val) {
        if (is_object($val)) { foreach (get_object_vars($val) as $k => $v) $val->$k = $this->resolveInterpolations($v); }
        elseif (is_array($val)) { foreach ($val as $i => $v) $val[$i] = $this->resolveInterpolations($v); }
        elseif (is_string($val)) return $this->resolveInterpolation($val);
        return $val;
    }

    private function resolveInterpolation($s, $depth = 0) {
        if ($depth > 50) throw new NolError("Interpolation depth limit exceeded");
        $res = preg_replace_callback('/\$\{([^}]+)\}/', function($m) use ($depth) {
            $parts = explode('.', $m[1]); $curr = $this->root;
            foreach ($parts as $p) {
                if (!is_object($curr) || !isset($curr->$p)) throw new NolError("Undefined: " . $m[1]);
                $curr = $curr->$p;
            }
            if (!is_scalar($curr) && !is_null($curr)) throw new NolError("Not scalar");
            $vStr = (string)$curr;
            return (strpos($vStr, '${') !== false) ? $this->resolveInterpolation($vStr, $depth + 1) : $vStr;
        }, $s);
        if (strlen($res) > 10 * 1024 * 1024) throw new NolError("Interpolation size limit exceeded");
        return $res;
    }

    private function resolveCoercions($val) {
        if (is_object($val)) {
            if (isset($val->_coerce) && count(get_object_vars($val)) === 1) {
                $c = $val->_coerce; $v = $this->resolveCoercions($c->value); $t = $c->type;
                switch ($t) {
                    case 'int': return (int)$v; case 'float': return (float)$v;
                    case 'bool': return is_bool($v) ? $v : (strtolower((string)$v) === 'true');
                    case 'string': return (string)$v;
                    default: throw new NolError("Unknown coercion: $t");
                }
            }
            foreach (get_object_vars($val) as $k => $v) $val->$k = $this->resolveCoercions($v);
        } elseif (is_array($val)) {
            foreach ($val as $i => $v) $val[$i] = $this->resolveCoercions($v);
        }
        return $val;
    }
}

function parse($input, $isNole = false, $envAllowlist = []) {
    $parser = new Parser($input, $isNole, $envAllowlist); return $parser->parse();
}
