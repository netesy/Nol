<?php
namespace Nol;

class Document {
    public $root;
    public function __construct($root) { $this->root = $root; }
    public function get($path) {
        $curr = $this->root;
        foreach (explode('.', $path) as $p) {
            if (!is_array($curr) || !array_key_exists($p, $curr)) return null;
            $curr = $curr[$p];
        }
        return $curr;
    }
    public function exists($path) { return $this->get($path) !== null; }
    public function toArray() { return $this->root; }
}

class Builder {
    public $root = [];
    public function set($path, $value) {
        $parts = explode('.', $path);
        $curr = &$this->root;
        foreach (array_slice($parts, 0, -1) as $p) {
            if (!isset($curr[$p]) || !is_array($curr[$p])) $curr[$p] = [];
            $curr = &$curr[$p];
        }
        $curr[end($parts)] = $value;
        return $this;
    }
    public function build() { return new Document($this->root); }
}

class Parser {
    private $text, $pos = 0, $start, $depth = 0;
    public function __construct($text) { $this->text = $text; $this->start = microtime(true); }
    private function peek() { return $this->pos < strlen($this->text) ? $this->text[$this->pos] : null; }
    private function advance() { return $this->pos < strlen($this->text) ? $this->text[$this->pos++] : null; }
    private function skipWs() {
        while ($this->pos < strlen($this->text)) {
            if (ctype_space($this->text[$this->pos])) $this->advance();
            elseif ($this->text[$this->pos] === '#') {
                $this->advance();
                if ($this->peek() === '#') {
                    $this->advance();
                    while ($this->pos < strlen($this->text) - 1 && substr($this->text, $this->pos, 2) !== '##') $this->advance();
                    if ($this->pos < strlen($this->text)) { $this->advance(); $this->advance(); }
                } else {
                    while ($this->pos < strlen($this->text) && $this->text[$this->pos] !== "\n") $this->advance();
                }
            } else break;
        }
    }
    private function readStr($q) {
        $s = "";
        while ($this->pos < strlen($this->text)) {
            $c = $this->advance();
            if ($c === $q) return $s;
            if ($c === '\\') {
                $e = $this->advance();
                if ($e === 'u') { $s .= mb_chr(hexdec(substr($this->text, $this->pos, 4))); $this->pos += 4; }
                elseif ($e === 'U') { $s .= mb_chr(hexdec(substr($this->text, $this->pos, 8))); $this->pos += 8; }
                else { $s .= ['n'=>"\n", 'r'=>"\r", 't'=>"\t"][$e] ?? $e; }
            } else $s .= $c;
        }
        return $s;
    }
    private function readKey() {
        $this->skipWs(); $c = $this->peek();
        if ($c === '"' || $c === "'") return $this->readStr($this->advance());
        $k = "";
        while ($this->pos < strlen($this->text) && (ctype_alnum($this->text[$this->pos]) || in_array($this->text[$this->pos], ['_', '-']))) $k .= $this->advance();
        return $k;
    }
    private function parseValue() {
        $this->skipWs();
        if (microtime(true) - $this->start > 1) throw new \Exception("Timeout");
        if (++$this->depth > 100) throw new \Exception("Depth");
        $c = $this->peek();
        if ($c === '{') {
            $this->advance(); $o = [];
            while ($this->peek() && $this->peek() !== '}') {
                $this->skipWs(); [$k, $v] = $this->parsePair(); $o[$k] = $v;
                $this->skipWs(); if ($this->peek() === ',') $this->advance();
            }
            if ($this->peek() === '}') $this->advance();
            $res = $o;
        } elseif ($c === '[') {
            $this->advance(); $a = [];
            while ($this->peek() && $this->peek() !== ']') {
                $this->skipWs(); $a[] = $this->parseValue();
                $this->skipWs(); if ($this->peek() === ',') $this->advance();
            }
            if ($this->peek() === ']') $this->advance();
            $res = $a;
        } elseif ($c === '"' || $c === "'") $res = $this->readStr($this->advance());
        elseif ($c && (ctype_digit($c) || $c === '-')) {
            $s = "";
            while ($this->pos < strlen($this->text) && (ctype_digit($this->text[$this->pos]) || strpos(".-eE+", $this->text[$this->pos]) !== false)) $s .= $this->advance();
            $res = (strpos($s, '.') !== false || stripos($s, 'e') !== false) ? floatval($s) : intval($s);
        } else {
            $s = "";
            while ($this->pos < strlen($this->text) && ctype_alpha($this->text[$this->pos])) $s .= $this->advance();
            if ($s === "true") $res = true;
            elseif ($s === "false") $res = false;
            elseif ($s === "null") $res = null;
            else throw new \Exception("Invalid value: $s");
        }
        $this->depth--; return $res;
    }
    private function parsePair() {
        $k = $this->readKey();
        if (in_array($k, ["_env", "_interpolate", "_meta"])) throw new \Exception("Reserved: $k");
        $this->skipWs(); if ($this->peek() === ':') $this->advance();
        return [$k, $this->parseValue()];
    }
    public function parse() {
        $root = [];
        while ($this->pos < strlen($this->text)) {
            $this->skipWs(); if (!$this->peek()) break;
            if ($this->peek() === '[') {
                $this->advance(); $path = "";
                while ($this->peek() && $this->peek() !== ']') $path .= $this->advance();
                if ($this->peek() === ']') $this->advance();
                $parts = explode('.', $path); $curr = &$root;
                foreach (array_slice($parts, 0, -1) as $p) {
                    if (!isset($curr[$p]) || !is_array($curr[$p])) $curr[$p] = [];
                    $curr = &$curr[$p];
                }
                $last = end($parts);
                if (!isset($curr[$last]) || !is_array($curr[$last])) $curr[$last] = [];
                $this->parseInto($curr[$last]);
            } else {
                [$k, $v] = $this->parsePair();
                if (array_key_exists($k, $root)) throw new \Exception("Dup: $k");
                $root[$k] = $v;
            }
        }
        return new Document($root);
    }
    private function parseInto(&$o) {
        while ($this->pos < strlen($this->text)) {
            $this->skipWs(); if ($this->peek() === '[' || !$this->peek()) break;
            [$k, $v] = $this->parsePair();
            if (array_key_exists($k, $o)) throw new \Exception("Dup: $k");
            $o[$k] = $v;
        }
    }
}

function parse($text) { return (new Parser($text))->parse(); }
