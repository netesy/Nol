<?php
namespace Nole;
use Nol\Document as NolDocument;
use Nol\Builder as NolBuilder;

class Document extends NolDocument {}
class Builder extends NolBuilder {}

class Parser {
    private $text, $pos = 0, $start, $depth = 0, $nole;
    public function __construct($text, $nole = false) { $this->text = $text; $this->start = microtime(true); $this->nole = $nole; }
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
                $this->skipWs(); $this->parsePair($o);
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
        } elseif ($this->nole && ($c === '*' || $c === '<')) {
            $t = $this->advance();
            if ($t === '<') {
                $b = ""; while ($this->peek() && $this->peek() !== '>') $b .= $this->advance();
                if ($this->peek() === '>') $this->advance();
                $res = ["_coerce" => ["type" => $b, "value" => $this->parseValue()]];
            } else {
                $b = ""; while ($this->peek() && (ctype_alnum($this->peek()) || in_array($this->peek(), ['_', '-']))) $b .= $this->advance();
                $res = "*" . $b;
            }
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
    private function parsePair(&$o) {
        $this->skipWs();
        if ($this->nole && $this->peek() === '&') {
            $this->advance(); $n = $this->readKey(); $this->skipWs();
            $val = ($this->peek() === ':' && ($this->advance())) ? $this->parseValue() : null;
            if (!isset($o["_anchors"])) $o["_anchors"] = [];
            $o["_anchors"][] = ["name" => $n, "value" => $val]; return;
        }
        $is_m = false; if ($this->nole && $this->peek() === '<') { $this->advance(); if ($this->peek() === '<') { $this->advance(); $is_m = true; } else $this->pos--; }
        $k = $is_m ? "<<" : $this->readKey();
        $this->skipWs(); if ($this->peek() === ':') $this->advance();
        $v = $this->parseValue();
        if ($is_m) { if (!isset($o["<<"])) $o["<<"] = []; $o["<<"][] = $v; }
        else { if (array_key_exists($k, $o)) throw new \Exception("Dup: $k"); $o[$k] = $v; }
    }
    public function parse() {
        $root = [];
        while ($this->pos < strlen($this->text)) {
            $this->skipWs(); if (!$this->peek()) break;
            if ($this->peek() === '[') {
                $this->advance(); $is_a = false; if ($this->peek() === '*') { $this->advance(); $is_a = true; }
                $path = ""; while ($this->peek() && $this->peek() !== ']') $path .= $this->advance();
                if ($this->peek() === ']') $this->advance();
                $parts = explode('.', $path); $curr = &$root;
                foreach (array_slice($parts, 0, -1) as $p) {
                    if (!isset($curr[$p]) || !is_array($curr[$p])) $curr[$p] = [];
                    $curr = &$curr[$p];
                }
                $last = end($parts);
                if ($is_a) {
                    if (!isset($curr[$last]) || !is_array($curr[$last])) $curr[$last] = [];
                    $entry = []; $curr[$last][] = &$entry; $this->parseInto($entry);
                } else {
                    if (!isset($curr[$last]) || !is_array($curr[$last])) $curr[$last] = [];
                    $this->parseInto($curr[$last]);
                }
            } else $this->parsePair($root);
        }
        return $root;
    }
    private function parseInto(&$o) {
        while ($this->pos < strlen($this->text)) {
            $this->skipWs(); if ($this->peek() === '[' || !$this->peek()) break;
            $this->parsePair($o);
        }
    }
}

class Evaluator {
    private $anchors = [], $app_env, $doc_env = [];
    public function __construct($app_env) { $this->app_env = array_flip($app_env); }
    public function evaluate($root) {
        $root = $this->collectMeta($root);
        $root = $this->resolveMerges($root);
        $root = $this->resolveEnv($root);
        $root = $this->resolveInterp($root, $root);
        return new Document($this->resolveCoerce($root));
    }
    private function dump($v) {
        if (is_null($v)) return "null"; if (is_bool($v)) return $v ? "true" : "false";
        if (is_numeric($v)) { if (is_int($v)) return strval($v); $s = sprintf("%.5f", $v); while (str_ends_with($s, '0')) $s = substr($s, 0, -1); if (str_ends_with($s, '.')) $s .= '0'; return $s; }
        if (is_string($v)) return $v;
        if (array_is_list($v)) return "[" . implode(", ", array_map([$this, 'dump'], $v)) . "]";
        $items = []; foreach ($v as $k => $val) if ($k[0] !== '_') $items[] = "$k: " . $this->dump($val);
        return "{" . implode(", ", $items) . "}";
    }
    private function collectMeta($v) {
        if (is_array($v) && !array_is_list($v)) {
            if (isset($v["_anchors"])) { foreach ($v["_anchors"] as $a) { $this->anchors[$a["name"]] = $this->collectMeta($a["value"] === null ? $v : $a["value"]); } unset($v["_anchors"]); }
            if (isset($v["_env"]) && isset($v["_env"]["allowed"])) { foreach ($v["_env"]["allowed"] as $x) $this->doc_env[$x] = true; }
            $res = []; foreach ($v as $k => $val) $res[$k] = $this->collectMeta($val); return $res;
        }
        if (is_array($v)) return array_map([$this, 'collectMeta'], $v);
        return $v;
    }
    private function resolveMerges($v, $depth = 0) {
        if ($depth > 20) throw new \Exception("Merge depth");
        if (is_array($v) && !array_is_list($v)) {
            if (isset($v["<<"])) {
                $merges = is_array($v["<<"]) && array_is_list($v["<<"]) ? $v["<<"] : [$v["<<"]]; unset($v["<<"]);
                foreach ($merges as $m) {
                    $rm = $m; if (is_string($rm) && $rm[0] === '*') $rm = $this->anchors[substr($rm, 1)] ?? null;
                    $rm = $this->resolveMerges($rm, $depth + 1);
                    if (is_array($rm)) { foreach ($rm as $mk => $mv) if ($mk[0] !== '_' && !array_key_exists($mk, $v)) $v[$mk] = $mv; }
                }
            }
            $res = []; foreach ($v as $k => $val) $res[$k] = $this->resolveMerges($val, $depth); return $res;
        }
        if (is_array($v)) return array_map(function($x) use ($depth) { return $this->resolveMerges($x, $depth); }, $v);
        return $v;
    }
    private function resolveEnv($v) {
        if (is_array($v) && !array_is_list($v)) {
            if (count($v) === 1 && isset($v["env"]) && is_string($v["env"])) {
                $var = $v["env"]; if (empty($this->app_env) && empty($this->doc_env) || isset($this->app_env[$var]) || isset($this->doc_env[$var])) return getenv($var) ?: "";
            }
            $res = []; foreach ($v as $k => $val) $res[$k] = $this->resolveEnv($val); return $res;
        }
        if (is_array($v)) return array_map([$this, 'resolveEnv'], $v);
        return $v;
    }
    private function resolveInterp($v, $root, $depth = 0) {
        if ($depth > 50) throw new \Exception("Interp depth");
        if (is_string($v) && strpos($v, '${') !== false) {
            $res = ""; $i = 0;
            while ($i < strlen($v)) {
                if (substr($v, $i, 2) === '${') {
                    $end = strpos($v, '}', $i + 2); if ($end === false) break;
                    $path = substr($v, $i + 2, $end - $i - 2);
                    $curr = $root; foreach (explode('.', $path) as $p) { if (!is_array($curr) || !array_key_exists($p, $curr)) throw new \Exception("Undef: $p"); $curr = $curr[$p]; }
                    $vs = is_string($curr) ? $curr : $this->dump($curr);
                    if (strpos($vs, '${') !== false) $vs = $this->resolveInterp($vs, $root, $depth + 1);
                    $res .= $vs; $i = $end + 1;
                } else { $res .= $v[$i]; $i++; }
            }
            return $res;
        }
        if (is_array($v) && !array_is_list($v)) { $res = []; foreach ($v as $k => $val) $res[$k] = $this->resolveInterp($val, $root, $depth); return $res; }
        if (is_array($v)) return array_map(function($x) use ($root, $depth) { return $this->resolveInterp($x, $root, $depth); }, $v);
        return $v;
    }
    private function resolveCoerce($v) {
        if (is_array($v) && !array_is_list($v)) {
            if (isset($v["_coerce"])) {
                $c = $v["_coerce"]; $t = $c["type"]; $val = $this->resolveCoerce($c["value"]);
                $s = is_string($val) ? $val : $this->dump($val);
                if ($t === "int") return intval($s); if ($t === "float") return floatval($s); if ($t === "bool") return strtolower($s) === "true"; return $s;
            }
            $res = []; foreach ($v as $k => $val) $res[$k] = $this->resolveCoerce($val); return $res;
        }
        if (is_array($v)) return array_map([$this, 'resolveCoerce'], $v);
        return $v;
    }
}

function parse($text, $app_env = []) { return (new Evaluator($app_env))->evaluate((new Parser($text, true))->parse()); }
