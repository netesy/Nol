<?php
namespace NOL;
class NolError extends \Exception {
    public $nolLine, $nolCol;
    public function __construct($msg, $line = 0, $col = 0) {
        parent::__construct("$msg at $line:$col");
        $this->nolLine = $line; $this->nolCol = $col;
    }
}
class NolParser {
    private $text, $pos = 0, $line = 1, $col = 1, $startTime, $depth = 0;
    public function __construct($text) { $this->text = $text; $this->startTime = microtime(true); }
    private function peek($n = 0) { return ($this->pos + $n < strlen($this->text)) ? $this->text[$this->pos + $n] : null; }
    private function advance() { $c = $this->peek(); $this->pos++; if ($c === "\n") { $this->line++; $this->col = 1; } else $this->col++; return $c; }
    private function skipWs() {
        while ($this->pos < strlen($this->text)) {
            $c = $this->text[$this->pos];
            if (ctype_space($c)) $this->advance();
            else if ($c === '#') {
                $this->advance();
                if ($this->peek() === '#') { $this->advance(); while ($this->pos < strlen($this->text) - 1 && !($this->text[$this->pos] === '#' && $this->text[$this->pos+1] === '#')) $this->advance(); if ($this->pos < strlen($this->text)) { $this->advance(); $this->advance(); } }
                else { while ($this->pos < strlen($this->text) && $this->text[$this->pos] !== "\n") $this->advance(); }
            } else break;
        }
    }
    private function readStr($q) {
        $s = "";
        while ($this->pos < strlen($this->text)) {
            $c = $this->peek(); if ($c === $q) { $this->advance(); break; }
            if ($c === '\\') { $this->advance(); $e = $this->advance(); if ($e === 'u') { $h = substr($this->text, $this->pos, 4); $this->pos += 4; $s .= mb_chr(hexdec($h)); } else if ($e === 'U') { $h = substr($this->text, $this->pos, 8); $this->pos += 8; $s .= mb_chr(hexdec($h)); } else $s .= ['n'=>"\n", 'r'=>"\r", 't'=>"\t"][$e] ?? $e; }
            else $s .= $this->advance();
        }
        return $s;
    }
    private function readKey() { $this->skipWs(); $c = $this->peek(); if ($c === '"' || $c === "'") return $this->readStr($this->advance()); $k = ""; while ($this->pos < strlen($this->text) && preg_match('/[a-zA-Z0-9_-]/', $this->text[$this->pos])) $k .= $this->advance(); return $k; }
    public function parseValue() {
        $this->skipWs(); if (microtime(true) - $this->startTime > 1) throw new NolError("Timeout", $this->line, $this->col); if (++$this->depth > 100) throw new NolError("Max depth", $this->line, $this->col);
        $c = $this->peek(); $res = null;
        if ($c === '{') { $this->advance(); $res = []; while ($this->pos < strlen($this->text)) { $this->skipWs(); if ($this->peek() === '}') { $this->advance(); break; } $this->parsePair($res); $this->skipWs(); if ($this->peek() === ',') $this->advance(); } }
        else if ($c === '[') { $this->advance(); $res = []; while ($this->pos < strlen($this->text)) { $this->skipWs(); if ($this->peek() === ']') { $this->advance(); break; } $res[] = $this->parseValue(); $this->skipWs(); if ($this->peek() === ',') $this->advance(); } }
        else if ($c === '"' || $c === "'") $res = $this->readStr($this->advance());
        else if (preg_match('/[0-9-]/', $c)) { $b = ""; while ($this->peek() && preg_match('/[0-9.eE+-]/', $this->peek())) $b .= $this->advance(); $res = (strpos($b, '.') !== false || stripos($b, 'e') !== false) ? (float)$b : (int)$b; }
        else { $b = ""; while ($this->peek() && ctype_alpha($this->peek())) $b .= $this->advance(); if ($b === "true") $res = true; else if ($b === "false") $res = false; else if ($b === "null") $res = null; else throw new NolError("Invalid: $b", $this->line, $this->col); }
        $this->depth--; return $res;
    }
    public function parsePair(&$obj) { $this->skipWs(); $key = $this->readKey(); if (class_exists('Normalizer')) $key = \Normalizer::normalize($key, \Normalizer::FORM_C); if (in_array($key, ["_env", "_interpolate", "_meta"])) throw new NolError("Reserved: $key", $this->line, $this->col); $this->skipWs(); if ($this->advance() !== ':') throw new NolError("Expected :", $this->line, $this->col); $val = $this->parseValue(); if (isset($obj[$key])) throw new NolError("Duplicate: $key", $this->line, $this->col); $obj[$key] = $val; }
    private function parseInto(&$obj) { while ($this->pos < strlen($this->text)) { $this->skipWs(); if (!$this->peek() || $this->peek() === '[') break; $this->parsePair($obj); } }
    public function parse() { $root = []; while ($this->pos < strlen($this->text)) { $this->skipWs(); if (!$this->peek()) break; if ($this->peek() === '[') { $this->advance(); $path = ""; while ($this->peek() && $this->peek() !== ']') $path .= $this->advance(); if ($this->peek() === ']') $this->advance(); $parts = explode('.', $path); $curr = &$root; foreach ($parts as $i => $p) { if (!isset($curr[$p])) $curr[$p] = []; $curr = &$curr[$p]; } $this->parseInto($curr); } else $this->parsePair($root); } return $root; }
}
function parse($text) { return (new NolParser($text))->parse(); }
function dump($v, $indent = 2, $level = 0, $root = false) {
    if ($v === null) return "null"; if (is_bool($v)) return $v ? "true" : "false";
    if (is_numeric($v)) { $s = sprintf("%.5f", $v); while (str_contains($s, '.') && str_ends_with($s, '0')) $s = substr($s, 0, -1); if (str_ends_with($s, '.')) $s .= '0'; return $s; }
    if (is_string($v)) return $root ? $v : "\"$v\"";
    if (array_is_list($v)) return "[" . implode(", ", array_map(fn($x) => dump($x, $indent, $level + 1), $v)) . "]";
    ksort($v); $pad = str_repeat(" ", $level * $indent); $nextPad = str_repeat(" ", ($level + 1) * $indent);
    $items = []; foreach ($v as $k => $val) { if (str_starts_with($k, "_") && !$root) continue; $items[] = $root ? "$k: " . dump($val, $indent, $level + 1) : "\n$nextPad$k: " . dump($val, $indent, $level + 1); }
    if ($root) return implode("\n", $items); if (empty($items)) return "{}"; return "{" . implode(",", $items) . "\n$pad}";
}
