# NOL User Guide

## Getting Started

A NOL file ends in:

```text
.nol
````

Example:

```nol
app: "Takadah"

[server]
host: "localhost"
port: 8080
```

---

## Strings

### Escaped

```nol
name: "hello\nworld"
```

### Raw

```nol
path: 'C:\folder\file.txt'
```

### Multiline

```nol
text: """
Hello
World
"""
```

---

## Arrays

```nol
ports: [80, 443, 8080]
```

---

## Objects

```nol
user: {
name:"Emmanuel"
age:22
}
```

---

## Sections

```nol
[database]
host: "localhost"
port: 5432
```

Nested:

```nol
[database.pool]
size: 20
```

---

## Arrays of Objects

```nol
[users.*]
name: "Alice"

[users.*]
name: "Bob"
```

---

## Environment Variables

```nol
password: $DB_PASS
```

Fallback:

```nol
password: $DB_PASS ?? "local"
```

---

## Interpolation

```nol
host: "localhost"
port: 8080

url: "http://${host}:${port}"
```

---

## Best Practices

### Use Clear Sections

```nol
[server]
[database]
[cache]
```

### Keep Secrets in Env Vars

```nol
password: $DB_PASS
```

### Use Formatter

```bash
nolfmt config.nol
```

### Avoid Giant Files

Split by domain:

* server.nol
* database.nol
* auth.nol

---

## Common Errors

### Invalid Boolean

```nol
yes
```

Use:

```nol
true
```

### Duplicate Key

```nol
port: 80
port: 90
```

Invalid.

### Missing Env Var

```nol
password: $DB_PASS
```

If unset → error.

---

## Example Production Config

```nol
[server]
host: "0.0.0.0"
port: 8080

[database]
host: "localhost"
port: 5432
password: $DB_PASS ?? "devpass"

[users.*]
name: "admin"
role: "owner"

url: "http://${server.host}:${server.port}"
```

---

## Final Advice

Use NOL when you want configs that are:

* strict
* readable
* maintainable
* production-safe


---

## Programming API

All NOL libraries provide a clean API using `Document` and `Builder` classes.

### Reading (Document)

Use dot-notation to access nested values easily.

```python
doc = nol.parse(input_str)
port = doc.get("server.port") # Returns 8080
exists = doc.exists("database") # Returns true
```

### Writing (Builder)

Programmatically create NOL documents.

```python
builder = nol.Builder()
builder.set("app.name", "MyApp").set("server.port", 80)
doc = builder.build()
print(doc.dump())
```

### Language Specifics

| Language | Get Value | Set Value |
|----------|-----------|-----------|
| C++      | `doc.get("a.b")->asInt()` | `builder.set("a.b", 1)` |
| Python   | `doc.get("a.b")` | `builder.set("a.b", 1)` |
| JS       | `doc.get("a.b")` | `builder.set("a.b", 1)` |
| PHP      | `$doc->get("a.b")` | `$builder->set("a.b", 1)` |
| Rust     | `doc.get("a.b")` | `builder.set("a.b", val)` |
| C        | `nol_doc_get(d, "a.b")` | `nol_builder_set(b, "a.b", v)` |
