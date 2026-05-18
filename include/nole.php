<?php
namespace NOLE;

class NolError extends \Exception {
    public $nolLine, $nolCol;
    public function __construct($msg, $line = 0, $col = 0) {
        parent::__construct("$msg at $line:$col");
        $this->nolLine = $line; $this->nolCol = $col;
    }
}

class NolParser {
    private $text, $pos = 0, $line = 1, $col = 1, $startTime, $depth = 0, $nole;
    public function __construct($text, $nole = false) {
        $this->text = $text; $this->startTime = microtime(true); $this->nole = $nole;
    }
    private function peek($n = 0) { return ($this->pos + $n < strlen($this->text)) ? $this->text[$this->pos + $n] : null; }
    private function advance() {
        $c = $this->peek(); $this->pos++;
        if ($c === "\n") { $this->line++; $this->col = 1; }
        else $this->col++;
        return $c;
    }
    private function skipWs() {
        while ($this->pos < strlen($this->text)) {
            $c = $this->text[$this->pos];
            if (ctype_space($c)) $this->advance();
            else if ($c === '#') {
                $this->advance();
                if ($this->peek() === '#') {
                    $this->advance();
                    while ($this->pos < strlen($this->text) - 1 && !($this->text[$this->pos] === '#' && $this->text[$this->pos+1] === '#')) $this->advance();
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
            $c = $this->peek();
            if ($c === $q) { $this->advance(); break; }
            if ($c === '\\') {
                $this->advance(); $e = $this->advance();
                if ($e === 'u') {
                    $h = substr($this->text, $this->pos, 4); $this->pos += 4;
                    $s .= mb_chr(hexdec($h));
                } else if ($e === 'U') {
                    $h = substr($this->text, $this->pos, 8); $this->pos += 8;
                    $s .= mb_chr(hexdec($h));
                } else $s .= ['n'=>"\n", 'r'=>"\r", 't'=>"\t"][$e] ?? $e;
            } else $s .= $this->advance();
        }
        return $s;
    }
    private function readKey() {
        $this->skipWs(); $c = $this->peek();
        if ($c === '"' || $c === "'") return $this->readStr($this->advance());
        $k = "";
        while ($this->pos < strlen($this->text) && preg_match('/[a-zA-Z0-9_-]/', $this->text[$this->pos])) $k .= $this->advance();
        return $k;
    }
    public function parseValue() {
        $this->skipWs();
        if (microtime(true) - $this->startTime > 1) throw new NolError("Timeout", $this->line, $this->col);
        if (++$this->depth > 100) throw new NolError("Max depth", $this->line, $this->col);
        $c = $this->peek(); $res = null;
        if ($c === '{') {
            $this->advance(); $res = [];
            while ($this->pos < strlen($this->text)) {
                $this->skipWs(); if ($this->peek() === '}') { $this->advance(); break; }
                $this->parsePair($res); $this->skipWs(); if ($this->peek() === ',') $this->advance();
            }
        } else if ($c === '[') {
            $this->advance(); $res = [];
            while ($this->pos < strlen($this->text)) {
                $this->skipWs(); if ($this->peek() === ']') { $this->advance(); break; }
                $res[] = $this->parseValue(); $this->skipWs(); if ($this->peek() === ',') $this->advance();
            }
        } else if ($this->nole && ($c === '*' || $c === '<')) {
            $t = $this->advance();
            if ($t === '<') {
                $b = ""; while ($this->peek() && $this->peek() !== '>') $b .= $this->advance();
                if ($this->peek() === '>') $this->advance();
                $res = ["_coerce" => ["type" => $b, "value" => $this->parseValue()]];
            } else {
                $b = ""; while ($this->peek() && preg_match('/[a-zA-Z0-9_-]/', $this->peek())) $b .= $this->advance();
                $res = "*" . $b;
            }
        } else if ($c === '"' || $c === "'") $res = $this->readStr($this->advance());
        else if (preg_match('/[0-9-]/', $c)) {
            $b = ""; while ($this->peek() && preg_match('/[0-9.eE+-]/', $this->peek())) $b .= $this->advance();
            $res = (strpos($b, '.') !== false || stripos($b, 'e') !== false) ? (float)$b : (int)$b;
        } else {
            $b = ""; while ($this->peek() && ctype_alpha($this->peek())) $b .= $this->advance();
            if ($b === "true") $res = true; else if ($b === "false") $res = false; else if ($b === "null") $res = null;
            else throw new NolError("Invalid value: $b", $this->line, $this->col);
        }
        $this->depth--; return $res;
    }
    public function parsePair(&$obj) {
        $this->skipWs();
        if ($this->nole && $this->peek() === '&') {
            $this->advance(); $n = $this->readKey(); $this->skipWs();
            $val = ($this->peek() === ':' && ($this->advance() || true)) ? $this->parseValue() : null;
            if (!isset($obj["_anchors"])) $obj["_anchors"] = [];
            $obj["_anchors"][] = ["name" => $n, "value" => $val]; return;
        }
        $isM = false; if ($this->nole && $this->peek() === '<' && $this->peek(1) === '<') { $this->advance(); $this->advance(); $isM = true; }
        $key = $isM ? "<<" : $this->readKey();
        if (class_exists('Normalizer')) $key = \Normalizer::normalize($key, \Normalizer::FORM_C);
        $this->skipWs(); if ($this->advance() !== ':') throw new NolError("Expected :", $this->line, $this->col);
        $val = $this->parseValue();
        if ($isM) { if (!isset($obj["<<"])) $obj["<<"] = []; $obj["<<"][] = $val; }
        else {
            if (isset($obj[$key])) throw new NolError("Duplicate key: $key", $this->line, $this->col);
            $obj[$key] = $val;
        }
    }
    private function parseInto(&$obj) {
        while ($this->pos < strlen($this->text)) {
            $this->skipWs(); if (!$this->peek() || $this->peek() === '[') break;
            $this->parsePair($obj);
        }
    }
    public function parse() {
        $root = [];
        while ($this->pos < strlen($this->text)) {
            $this->skipWs(); if (!$this->peek()) break;
            if ($this->peek() === '[') {
                $this->advance(); $isA = false; if ($this->peek() === '*') { $this->advance(); $isA = true; }
                $path = ""; while ($this->peek() && $this->peek() !== ']') $path .= $this->advance();
                if ($this->peek() === ']') $this->advance();
                $parts = explode('.', $path); $curr = &$root;
                foreach ($parts as $i => $p) {
                    $last = $i === count($parts) - 1;
                    if (!isset($curr[$p])) $curr[$p] = ($last && $isA) ? [] : [];
                    if ($last) {
                        if ($isA) { $entry = []; $curr[$p][] = &$entry; $this->parseInto($entry); }
                        else $this->parseInto($curr[$p]);
                    } else $curr = &$curr[$p];
                }
            } else $this->parsePair($root);
        }
        return $root;
    }
}

class Evaluator {
    private $anchors = [], $appEnv, $docEnv, $rootVal;
    public function __construct($appEnv = []) { $this->appEnv = array_flip($appEnv); $this->docEnv = []; }
    public function evaluate($root) {
        $this->rootVal = $this->collectMeta($root);
        $this->rootVal = $this->resolveMerges($this->rootVal);
        $this->rootVal = $this->resolveEnv($this->rootVal);
        $clone = $this->rootVal;
        $this->rootVal = $this->resolveInterp($this->rootVal, $clone);
        return $this->resolveCoerce($this->rootVal);
    }
    private function collectMeta($v) {
        if (is_array($v)) {
            if (isset($v["_anchors"])) {
                $ans = $v["_anchors"]; unset($v["_anchors"]);
                foreach ($ans as $a) $this->anchors[$a["name"]] = $this->collectMeta($a["value"] === null ? $v : $a["value"]);
            }
            if (isset($v["_env"]) && is_array($v["_env"]) && isset($v["_env"]["allowed"])) {
                foreach ($v["_env"]["allowed"] as $x) $this->docEnv[$x] = true;
            }
            foreach ($v as $k => &$val) $val = $this->collectMeta($val);
        }
        return $v;
    }
    private function resolveMerges($v, $depth = 0) {
        if ($depth > 20) throw new \Exception("Max merge depth");
        if (is_array($v)) {
            if (isset($v["<<"])) {
                $merges = $v["<<"]; unset($v["<<"]); if (!isset($merges[0])) $merges = [$merges];
                foreach ($merges as $m) {
                    $rm = $m; if (is_string($rm) && str_starts_with($rm, "*")) $rm = $this->anchors[substr($rm, 1)] ?? null;
                    $rm = $this->resolveMerges($rm, $depth + 1);
                    if (is_array($rm)) {
                        foreach ($rm as $mk => $mv) if (!str_starts_with($mk, "_") && !isset($v[$mk])) $v[$mk] = $mv;
                    }
                }
            }
            foreach ($v as $k => &$val) $val = $this->resolveMerges($val, $depth);
        }
        return $v;
    }
    private function resolveEnv($v) {
        if (is_array($v)) {
            if (count($v) === 1 && isset($v["env"]) && is_string($v["env"])) {
                $var = $v["env"]; if (empty($this->appEnv) && empty($this->docEnv) || isset($this->appEnv[$var]) || isset($this->docEnv[$var])) return getenv($var) ?: "";
            }
            foreach ($v as $k => &$val) $val = $this->resolveEnv($val);
        }
        return $v;
    }
    private function resolveInterp($v, $root, $depth = 0) {
        if ($depth > 50) throw new \Exception("Max interp depth");
        if (is_string($v) && str_contains($v, "\${")) {
            return preg_replace_callback('/\$\{([^}]+)\}/', function($m) use ($root, $depth) {
                $curr = $root; foreach (explode('.', $m[1]) as $p) $curr = $curr[$p];
                $vs = is_string($curr) ? $curr : dump($curr, 2, 0, true);
                if (str_contains($vs, "\${")) $vs = $this->resolveInterp($vs, $root, $depth + 1);
                return $vs;
            }, $v);
        }
        if (is_array($v)) foreach ($v as $k => &$val) $val = $this->resolveInterp($val, $root, $depth);
        return $v;
    }
    private function resolveCoerce($v) {
        if (is_array($v)) {
            if (isset($v["_coerce"])) {
                $c = $v["_coerce"]; unset($v["_coerce"]);
                $val = $this->resolveCoerce($c["value"]);
                $s = is_string($val) ? $val : dump($val, 2);
                if ($c["type"] === "int") return (int)$s; if ($c["type"] === "float") return (float)$s;
                if ($c["type"] === "bool") return strtolower($s) === "true"; return $s;
            }
            foreach ($v as $k => &$val) $val = $this->resolveCoerce($val);
        }
        return $v;
    }
}

function parse($text, $appEnv = []) {
    $p = new NolParser($text, true); return (new Evaluator($appEnv))->evaluate($p->parse());
}

function dump($v, $indent = 2, $level = 0, $root = false) {
    if ($v === null) return "null"; if (is_bool($v)) return $v ? "true" : "false";
    if (is_numeric($v)) { $s = sprintf("%.5f", $v); while (str_contains($s, '.') && str_ends_with($s, '0')) $s = substr($s, 0, -1); if (str_ends_with($s, '.')) $s .= '0'; return $s; }
    if (is_string($v)) return $root ? $v : "\"$v\"";
    if (array_is_list($v)) return "[" . implode(", ", array_map(fn($x) => dump($x, $indent, $level + 1), $v)) . "]";
    ksort($v); $pad = str_repeat(" ", $level * $indent); $nextPad = str_repeat(" ", ($level + 1) * $indent);
    $items = []; foreach ($v as $k => $val) { if (str_starts_with($k, "_") && !$root) continue; $items[] = $root ? "$k: " . dump($val, $indent, $level + 1) : "\n$nextPad$k: " . dump($val, $indent, $level + 1); }
    if ($root) return implode("\n", $items); if (empty($items)) return "{}";
    return "{" . implode(",", $items) . "\n$pad}";
}
