
# NOLE Specification v0.1

## A configuration language for structured static + evaluated data

---

# 1. Overview

NOLE is an extension of NOL that supports controlled evaluation features including:

* Environment variables (dual allowlist)
* Interpolation (local references only)
* Anchors and merge
* Optional type coercion

A NOLE document represents a single root object.

> **Security remains a first-class requirement.** All parsers MUST implement the constraints defined in this specification.

---

# 2. File Extension

```text
.nole
```

### 2.1 Dialect Separation Rule

```text
.nole files MUST be parsed using the NOLE specification.

.nol parsers MUST reject NOLE syntax.
```

---

# 3. Encoding

* All files MUST be UTF-8
* Invalid UTF-8 MUST fail parsing

Parsers MUST reject:

* Overlong UTF-8 encodings
* Null bytes (U+0000), including via escape
* Lone UTF-16 surrogate code points (U+D800–U+DFFF)

### 3.1 Unicode Normalization (REQUIRED)

```text
All keys MUST be normalized to Unicode NFC before comparison.

Duplicate key detection MUST occur after normalization.
```

String values:

* MUST preserve original byte form
* MAY expose normalized views

### 3.2 Unicode Non-Characters (RECOMMENDED)

Parsers SHOULD reject:

```text
U+FDD0–U+FDEF
U+FFFE, U+FFFF (and equivalents in all planes)
```

---

# 4. Strings

## 4.1 Types

```nole
"a"          # processed
'b'          # processed
"""multi"""  # processed
'''raw'''    # no escapes
```

## 4.2 Escape Sequences (STRICT)

Allowed:

```text
\\ \" \' \n \r \t
\uXXXX
\UXXXXXXXX
```

Rules:

* Unknown escape sequences MUST fail
* `\u` = exactly 4 hex digits
* `\U` = exactly 8 hex digits
* Result MUST be valid Unicode scalar
* `\u0000` MUST fail
* Surrogates MUST fail

Raw strings:

* No escape processing
* Still MUST obey UTF-8 + null/surrogate rules

---

# 5. Comments

```nole
# single line

##
block
##
```

* Nested block comments MUST fail

---

# 6. Whitespace

* Ignored outside strings
* Not indentation-sensitive

---

# 7. Keys

## 7.1 Allowed

```nole
name
server_host
db-port
```

Quoted:

```nole
"my key": 1
```

## 7.2 Key Equivalence

```text
Quoted and unquoted keys are equivalent after NFC normalization.
```

Duplicate keys MUST fail.

---

# 8. Reserved Names

These names are **globally reserved**:

```text
_env
_interpolate
_meta
```

Usage MUST fail anywhere in document except for their defined roles.

---

# 9. Assignment

```nole
key: value
```

---

# 10. Types

## 10.1 Integer

```nole
10
-5
1_000
```

Range:

```text
-2^63 to 2^63-1
```

Overflow MUST fail.

---

## 10.2 Float (STRICT GRAMMAR)

```text
-? DIGITS "." DIGITS ( (e|E) (+|-)? DIGITS )?
```

Valid:

```nole
0.1
-0.5
3.14e10
```

Invalid:

```nole
.5
01.2
NaN
Inf
```

---

## 10.3 Boolean

```nole
true
false
```

---

## 10.4 Null

```nole
null
```

Null ≠ absent.

---

## 10.5 Date (RFC 3339)

Allowed:

```nole
2026-04-24
2026-04-24T10:00:00Z
2026-04-24T10:00:00+02:00
```

Rules:

```text
Date-only values MUST be interpreted as UTC midnight.
```

---

# 11. Arrays

```nole
ports: [80, 443]
```

Multiline allowed.

### 11.1 Ordering Guarantee

```text
Arrays MUST preserve document order.
```

---

# 12. Objects

```nole
user: {name:"A"}
```

Expanded allowed.

---

# 13. Sections

```nole
[server]
host: "localhost"
```

Nested:

```nole
[server.tls]
enabled: true
```

---

## 13.1 Section vs Value Collision (REQUIRED)

```text
A key MUST NOT be defined both as a section and as a value.
```

Violation MUST fail.

---

# 14. Arrays via Sections

```nole
[users.*]
name: "Alice"
```

Rules:

* Appends in document order
* Counts toward array limits

---

# 15. Evaluation Model (CRITICAL)

Evaluation order MUST be:

```text
1. Parse
2. Merge resolution
3. Interpolation
4. Type coercion
```

---

# 16. Environment Variables

## 16.1 Dual Allowlist (REQUIRED)

Access requires:

1. Application-level allowlist
2. Document `_env` allowlist

Intersection enforced.

If application allowlist missing → treated as empty.

---

## 16.2 Syntax

```nole
[_env]
allowed = ["DB_PASS"]

db: { env = "DB_PASS" }
```

---

## 16.3 Rules

* No wildcards
* Defaults MUST be literals
* Values are strings
* Max size: 64KB
* Errors MUST NOT reveal values

---

## 16.4 Dangerous Variables

Examples:

```text
LD_PRELOAD, PATH, NODE_PATH
```

Require explicit override:

```text
allow_dangerous = true
```

---

# 17. Interpolation

## 17.1 Syntax

```nole
url: "http://${server.host}:${server.port}"
```

---

## 17.2 Rules

* Local references only
* No env/file/exec access

---

## 17.3 Type Constraints (REQUIRED)

Interpolation MUST resolve to:

```text
string, int, float, bool
```

Objects/arrays → FAIL

Missing references → FAIL

---

## 17.4 Limits

| Limit       | Value |
| ----------- | ----- |
| Depth       | 50    |
| Output size | 10MB  |
| Time        | 100ms |

---

## 17.5 Cycles

MUST be detected and rejected.

---

# 18. Anchors & Merge

## 18.1 Syntax

```nole
base: &b {host:"localhost"}

server: {
<<: *b
}
```

---

## 18.2 Rules

* Max depth: 20
* Cycles MUST fail
* Explicit keys override merged keys

---

# 19. Type Coercion

```nole
threads: <int>"8"
```

## 19.1 Order

```text
Interpolation → Coercion
```

---

## 19.2 Rules

| Type  | Allowed        |
| ----- | -------------- |
| int   | digits only    |
| float | decimal        |
| bool  | "true"/"false" |

Invalid → FAIL

No truncation.

---

# 20. Security Limits

| Limit                | Value  |
| -------------------- | ------ |
| File size            | 10MB   |
| Depth                | 100    |
| String length        | 1MB    |
| Array size           | 100k   |
| Object keys          | 10k    |
| Merge depth          | 20     |
| Interpolation depth  | 50     |
| Env size             | 64KB   |
| Total timeout        | 1000ms |
| Interpolation budget | 100ms  |

---

## 20.1 Memory Clarification

```text
Parsers MUST enforce limits on internal allocations.
Applications enforce global memory limits.
```

---

# 21. Error Handling

* Fail immediately
* No recovery

## 21.1 Safety

MUST NOT reveal:

* Env values
* App-level denied variable names

---

## 21.2 Constant-Time Recommendation

```text
Parsers SHOULD ensure constant-time failure for env resolution.
```

---

# 22. Canonical Form (RECOMMENDED)

Canonical serialization MUST:

```text
- Normalize keys (NFC)
- Sort keys lexicographically
- Preserve array order
- Use minimal float form
- Use double-quoted strings
```

---

# 23. Prohibited Features

```text
❌ File includes
❌ Command execution
❌ Network access
❌ Wildcard env access
❌ Cross-document references
```

---

# 24. Compatibility with NOL

```text
All valid .nol files MUST be valid .nole files.

.nole files using extended features are NOT valid .nol.
```

---

# 25. Philosophy

> **Explicit power. No hidden privilege. Secure by construction.**

* Core is safe
* Extended is controlled
* Application is authority
* No document self-privilege

---

# End of NOLE v0.1
