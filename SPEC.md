# NOL Specification v0.1

## A configuration language for structured static data

---

# 1. Overview

NOL is a configuration language for structured static data.

A NOL document represents a single root object.

> **Security is a first-class requirement.** All parsers MUST implement the constraints defined in this specification.

NOL is strictly **data-only**:

* No evaluation
* No interpolation
* No environment variables
* No merging or anchors
* No type coercion

---

# 2. File Extension

```text
.nol
```

---

# 3. Encoding

* All files MUST be UTF-8
* Invalid UTF-8 MUST fail parsing immediately

Parsers MUST reject:

* Overlong UTF-8 encodings
* Null bytes (U+0000), including via escape sequences
* Lone UTF-16 surrogate code points (U+D800–U+DFFF)

---

## 3.1 Unicode Normalization (REQUIRED)

All keys MUST be normalized using Unicode NFC before comparison.

Duplicate key detection MUST occur after normalization.

String values:

* MUST preserve original byte representation
* MAY expose normalized views

---

## 3.2 Unicode Non-Characters (RECOMMENDED)

Parsers SHOULD reject:

* U+FDD0–U+FDEF
* U+FFFE, U+FFFF (and equivalents in all Unicode planes)

---

## 3.3 String Escape Sequences (STRICT)

Valid escape sequences inside quoted strings:

| Sequence     | Meaning                       |
| :----------- | :---------------------------- |
| `\\`         | Backslash                     |
| `\"`         | Double quote                  |
| `\'`         | Single quote                  |
| `\n`         | Line feed                     |
| `\r`         | Carriage return               |
| `\t`         | Horizontal tab                |
| `\uXXXX`     | Unicode scalar (4 hex digits) |
| `\UXXXXXXXX` | Unicode scalar (8 hex digits) |

### Rules

* Unknown escape sequences MUST fail parsing
* `\u` MUST have exactly 4 hex digits
* `\U` MUST have exactly 8 hex digits
* Escaped values MUST be valid Unicode scalar values
* `\u0000` MUST fail parsing
* Surrogate code points MUST fail parsing

---

## 3.4 Raw Strings

```nol
'''raw string'''
```

* No escape processing
* Backslash is literal
* MUST still obey UTF-8, null byte, and surrogate rules

---

# 4. Comments

### Single Line

```nol
# comment
```

### Block

```nol
##
multi line
comment
##
```

* Nested block comments MUST fail parsing

---

# 5. Whitespace

Whitespace outside strings is ignored.

NOL is not indentation-sensitive.

---

# 6. Keys

### Unquoted Keys

```nol
name
server_host
db-port
```

### Quoted Keys

```nol
"my key": 1
```

---

## 6.1 Key Equivalence

Quoted and unquoted keys are equivalent after NFC normalization.

Example:

```nol
server: "a"
"server": "b"  # FAIL (duplicate key)
```

---

## 6.2 Duplicate Keys

Duplicate keys in the same object scope MUST fail parsing.

---

# 7. Assignment

```nol
key: value
```

---

# 8. Types

## 8.1 String

```nol
"a"
'b'
"""multi"""
'''raw'''
```

---

## 8.2 Integer

```nol
10
-5
1_000_000
```

Range:

-2⁶³ to 2⁶³−1

Rules:

* Overflow MUST fail parsing
* No truncation or wrapping allowed

---

## 8.3 Float (STRICT GRAMMAR)

Grammar:

```text
-? DIGITS "." DIGITS ( (e|E) (+|-)? DIGITS )?
```

Valid:

```nol
1.0
-0.5
3.14e10
```

Invalid:

```nol
.5
01.2
NaN
Inf
-Infinity
```

---

## 8.4 Boolean

```nol
true
false
```

---

## 8.5 Null

```nol
null
```

Semantics:

* Null and absent keys are distinct
* Parsers MUST expose this distinction

---

## 8.6 Date (RFC 3339)

Allowed:

```nol
2026-04-24
2026-04-24T10:00:00Z
2026-04-24T10:00:00+02:00
```

Rules:

* MUST follow RFC 3339 format
* Date-only values MUST be interpreted as UTC midnight

---

# 9. Arrays

```nol
ports: [80, 443]
```

Multiline:

```nol
ports: [
80
443
]
```

Trailing commas allowed.

---

## 9.1 Ordering Guarantee

Arrays MUST preserve document order.

---

# 10. Objects

```nol
user: {name:"A", age:22}
```

Expanded:

```nol
user: {
name:"A"
age:22
}
```

Trailing commas allowed.

---

# 11. Sections

```nol
[server]
host: "localhost"
```

Nested:

```nol
[server.tls]
enabled: true
```

---

## 11.1 Section vs Value Collision (REQUIRED)

A key MUST NOT be defined both as a section and as a value.

Example:

```nol
[server]
host: "a"

server: {port: 80}  # FAIL
```

---

# 12. Arrays of Objects via Sections

```nol
[users.*]
name: "Alice"

[users.*]
name: "Bob"
```

Equivalent to:

```json
{
  "users": [
    {"name": "Alice"},
    {"name": "Bob"}
  ]
}
```

---

## 12.1 Rules

* Elements MUST be appended in document order
* Total elements count toward array limits

---

# 13. Security Limits (REQUIRED)

| Limit                 | Value             |
| :-------------------- | :---------------- |
| Maximum document size | 10 MB             |
| Maximum nesting depth | 100               |
| Maximum string length | 1 MB              |
| Maximum array size    | 100,000 elements  |
| Maximum object keys   | 10,000 per object |
| Parse timeout         | 1,000 ms          |

---

## 13.1 Memory Enforcement

Parsers MUST enforce limits on their own allocations.

Applications are responsible for global process memory limits.

---

# 14. Error Handling

All violations MUST:

1. Fail parsing immediately
2. Return a descriptive error message
3. Not continue parsing

---

## 14.1 Error Message Safety

Error messages MUST NOT reveal:

* Internal parser state
* Memory addresses
* Implementation-specific details

Error messages MAY include:

* Line and column number
* Type of error (e.g. duplicate key, invalid escape)

---

# 15. Prohibited Features

The following are NEVER allowed:

```text
❌ Environment variables
❌ Interpolation
❌ Anchors or merge
❌ Type coercion
❌ File includes or imports
❌ Command execution
❌ Network access
❌ Cross-document references
```

---

# 16. Canonical Form (RECOMMENDED)

Canonical serialization SHOULD:

* Normalize keys using NFC
* Sort object keys lexicographically
* Preserve array order
* Use minimal float representation
* Use double-quoted strings with standard escapes

---

# 17. Parser Requirements (MANDATORY)

All parsers MUST implement:

* UTF-8 validation
* Rejection of overlong encodings
* Rejection of null bytes (raw and escaped)
* Rejection of surrogate code points
* Escape sequence validation
* Unicode NFC normalization for keys
* Duplicate key detection
* Section vs value collision detection
* Integer overflow detection
* Recursion depth limits
* Parse timeout enforcement

---

# 18. Philosophy

> **Static. Deterministic. Safe by default.**

* No hidden behavior
* No execution
* No implicit coercion
* No side effects
* Explicit structure only

---

# End of NOL Specification v0.1
