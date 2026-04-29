
# NOL

**NOL** (Notation Object Language) is a strict, human-readable configuration language built for modern systems.

It combines the readability of YAML, the safety of TOML, and the clarity of structured data formats—without indentation traps, implicit booleans, or dangerous executable directives.

> Human readable. Machine reliable.

---

## Why NOL?

Existing formats force tradeoffs:

- **YAML** → flexible but error-prone
- **JSON** → universal but painful for humans
- **TOML** → clean but limited at scale

NOL aims to solve these issues with a strict and practical design.

---

## Core Features

- No indentation sensitivity
- Strict booleans (`true`, `false`)
- Duplicate keys are errors
- Sections with nesting
- Arrays of objects
- Four string types
- Environment variable support
- String interpolation
- UTF-8 only
- Safe by default

---

## Example

```nol
[server]
host: "0.0.0.0"
port: 8080
debug: false

[database]
host: "localhost"
port: 5432
password: $DB_PASS ?? "local"

url: "http://${server.host}:${server.port}"
````

---

## Planned Tools

```bash
nolfmt config.nol
nollint config.nol
nolcheck config.nol
```

---

## Project Structure

* `README.md` → overview
* `SPEC.md` → official language specification
* `docs/` → tutorials, guides, examples

---

## Use Cases

NOL is ideal for:

* application configs
* build tools
* game engines
* infrastructure
* AI pipelines
* enterprise systems

---

## Philosophy

NOL chooses:

* explicit over magical
* strict over permissive
* readable over cryptic
* safe over clever

