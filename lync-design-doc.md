# Lync Language Design Document

---

## Overview

**Lync** is a systems programming language with manual memory management, safe nullable pointers, and pattern matching. It compiles to C via a compiler written in C.

### Philosophy
- C-like simplicity
- Explicit memory control
- Zero hidden allocations
- Compile-time memory safety
- No null pointer crashes
- Pattern matching for correctness
- Predictable performance

---

# Core Language Features

---

## 1. Variables

### Declaration Syntax
```rust
x: int = 5;
name: str = "hello";
alive: bool = true;
```

### Mutability
- Variables are **mutable by default**
- Use `const` to make immutable

```rust
const y: int = 10;
```

### Type Requirements
- Types are **always explicit** in Version 1
- No type inference in V1

---

## 2. Primitive Types

| Type | Description | C Equivalent |
|--------|-------------|----------------|
| int | 64-bit signed integer | int64_t |
| float | 64-bit floating point | double |
| bool | Boolean value | int (0/1) |
| char | Single character | char |
| str | String pointer | const char* |
| void | No return value | void |

---

## 3. Memory Management (Hybrid Ownership Model)

Lync combines manual `free()` calls with compile-time ownership validation.

---

### 3.1 Ownership Modifiers

#### `own`
- Owns heap memory
- Responsible for calling `free`

```rust
e: own Entity = alloc Entity;
free e;
```

#### `ref`
- Borrowed reference
- Cannot free memory

```rust
r: ref Entity = e;
```

---

### 3.2 Allocation

```rust
e: own Entity = alloc Entity;
```

Equivalent C:
```c
Entity* e = malloc(sizeof(Entity));
```

---

### 3.3 Freeing Memory

```rust
free e;
```

Rules:
- Only `own` variables can be freed
- Must be freed before leaving scope

---

### 3.4 Analyzer Safety Guarantees

#### No Memory Leaks
Every `own` variable must be freed before scope exit.

#### No Double Free
Calling `free` twice produces a compile error.

#### No Borrow Free
Freeing a `ref` produces a compile error.

#### Tainted References
Using a `ref` after its `own` source is freed triggers a warning.

---

## 4. Auto Dereferencing

Field access always uses `.` regardless of pointer type.

```rust
e.health = 100;
r.health = 50;
```

Compiler automatically converts to C `->` when needed.

---

## 5. Nullable Types

Types are **non-null by default**.

Use `?` to allow null values.

```rust
maybe: own? Entity = find_entity(10);
```

---

### Nullable Safety via Match

```rust
match maybe {
    some(e): {
        e.health = 100;
    }

    null: {
        print("not found");
    }
}
```

Rules:
- Nullable values cannot be used without `match`
- `some()` unwraps safely
- Unwrapped values become `ref`

---

## 6. Pattern Matching

General match syntax:

```rust
match value {
    pattern : { }
    _ : { }
}
```

### Example

```rust
match x {
    0 : print("zero");
    _ : print("other");
}
```

---

## 7. Control Flow

### If Statement
```rust
if x == 5 {
    print("five");
}
```

### If / Else
```rust
if x > 0 {
    print("positive");
} else {
    print("negative");
}
```

---

### While Loop
```rust
while x > 0 {
    x = x - 1;
}
```

---

### For Loop (Planned)
```rust
for i in 0..10 {
    print(i);
}
```

---

## 8. Functions

### Declaration
```rust
fn add(a: int, b: int) -> int {
    return a + b;
}
```

### Void Function
```rust
fn log(msg: str) -> void {
    print(msg);
}
```

---

## 9. Structs

### Definition
```rust
struct Entity {
    health: int;
    name: str;
}
```

### Usage
```rust
e: own Entity = alloc Entity;
e.health = 100;
```

---

## 10. Strings

Strings are immutable pointer types.

```rust
msg: str = "hello world";
```

Equivalent to:

```c
const char*
```

---

## 11. Constants

```rust
const MAX_HEALTH: int = 100;
```

- Must be initialized at declaration
- Cannot be modified

---

## 12. Scope Rules

- Variables exist only inside their block
- `own` variables must be freed before leaving scope

```rust
{
    e: own Entity = alloc Entity;
    free e;
}
```

---

## 13. Built-in Functions

### Memory
```rust
alloc Type
free variable
```

### Output
```rust
print(value);
```

---

## 14. Compiler Architecture

Pipeline:

```
Source (.lync)
    ↓
Lexer
    ↓
Tokens
    ↓
Parser
    ↓
AST
    ↓
Analyzer
    ↓
Codegen
    ↓
C Output (.c)
```

---

## 15. Analyzer Internal Representation

```c
typedef enum {
    OWNERSHIP_NONE,
    OWNERSHIP_OWN,
    OWNERSHIP_REF,
} Ownership;

typedef struct {
    char* name;
    char* type_name;
    Ownership ownership;
    int is_nullable;
    int is_freed;
    int line_declared;
    int col_declared;
} Symbol;
```

---

## 16. Code Generation Principles

### Zero-Cost Safety
All safety checks exist only at compile time.

### Match Translation Example

#### Lync Source
```rust
match x {
    0 : print("zero");
    _ : print("other");
}
```

#### Generated C
```c
if (x == 0) {
    printf("zero\n");
} else {
    printf("other\n");
}
```

---

## 17. Error Types

| Error | Trigger |
|-----------|-------------|
| Memory leak | `own` not freed |
| Double free | Free called twice |
| Ref free | Free called on `ref` |
| Nullable misuse | Using nullable without match |

---

## 18. VS Code Integration (Planned)

- Line/column diagnostics
- tasks.json integration
- problemMatcher support
- Syntax highlighting
- Build automation

---

## 19. V1 Feature Set

### Included
- Explicit typing
- Ownership model
- Borrowing
