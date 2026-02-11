# Lync

**A systems programming language with manual memory management, compile-time ownership tracking, and pattern matching — transpiles to C.**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![Language: C](https://img.shields.io/badge/Language-C-gray.svg)
![Standard: C23](https://img.shields.io/badge/Standard-C23-green.svg)

File extension: `.lync`

---

## Overview

Lync sits between C and Rust. You get manual memory management like C — you `alloc` and `free` by hand — but the compiler statically tracks ownership and catches memory bugs at compile time, like Rust. There's no garbage collector and no runtime overhead from safety checks. The output is readable, standard C.

- **Explicit memory control** — you decide when to allocate and free
- **Compile-time safety** — memory leaks, double free, and use-after-free are caught before your code runs
- **Zero-cost** — all ownership checks happen at compile time; the generated C has no overhead
- **C-like simplicity** — minimal syntax, no hidden magic
- **Transpiles to C** — the output is readable C you can inspect, debug, and compile with any C compiler

---

## Quick Start

### Prerequisites

- A C compiler (GCC, Clang, or MSVC)
- CMake 3.29+

### Build

```bash
cmake -B build
cmake --build build
```

### Write a program

```c
// example.lync
def add(a: int, b: int): int {
    return a + b;
}

def main(): int {
    result: int = add(3, 4);

    ptr: own int = alloc 42;
    free ptr;

    return 0;
}
```

### Compile and run

```bash
./lync example.lync -o example.c
cc example.c -o example
./example
```

The compiler produces a `.c` file which you then compile with any C compiler.

> **Note:** `print` is not yet implemented. See the [Roadmap](#roadmap) for upcoming features.

---

## CLI Reference

```
lync [options] [input_file]
```

| Flag | Description |
|------|-------------|
| `-o <file>` | Output C file (default: `test.c`) |
| `-trace` | Enable debug trace output (stderr) |
| `-no-color` | Disable ANSI-colored diagnostics |
| `-h`, `--help` | Show help message |

If no input file is specified, defaults to `test.lync`.

---

## Language Reference

### Types

| Lync Type | Description | C Equivalent |
|----------|-------------|--------------|
| `int` | 64-bit signed integer | `int64_t` |
| `bool` | Boolean (`true` / `false`) | `bool` |
| `void` | No return value (functions only) | `void` |

### Variables

```c
// Declaration
x: int = 5;
alive: bool = true;

// Assignment
x = 10;
```

All types must be explicitly annotated. Variables are mutable by default.

### Functions

```c
def add(a: int, b: int): int {
    return a + b;
}

def main(): int {
    result: int = add(3, 4);
    return 0;
}
```

- Syntax: `def name(params): return_type { body }`
- `main` must return `int` and take no parameters
- Recursive calls are supported
- Forward declarations are generated automatically

### Control Flow

**if / else**
```c
if (x > 10) {
    x = x - 5;
} else {
    x = x + 5;
}
```
Condition must be `bool`. The `else` branch is optional.

**while**
```c
while (x > 0) {
    x = x - 1;
}
```

**do-while**
```c
do {
    x = x - 1;
} (x > 0);
```
> Note: Lync uses `do { } (cond);` — there is no `while` keyword in the do-while syntax.

**for (range)**
```c
for (i: 0 to 10) {
    // i goes from 0 to 10 inclusive
}
```
- The loop variable is automatically declared as `int` in a new scope
- The upper bound is **inclusive** (`0 to 10` iterates 11 times)

### Match

Lync supports both match **expressions** (return a value) and match **statements** (execute code blocks).

**Match expression:**
```c
result: int = match x {
    1: 10;
    2: 20;
    _: 0;
};
```

**Match statement:**
```c
match x {
    1: {
        y = 10;
    }
    _: {
        y = 0;
    }
};
```

- Wildcard `_` (default branch) is **required**
- All branches in a match expression must return the same type
- Pattern types must match the target expression type
- Compiles to if/else chains in C

### Operators

| Category | Operators | Operand Types | Result Type |
|----------|-----------|---------------|-------------|
| Arithmetic | `+`  `-`  `*`  `/` | `int`, `int` | `int` |
| Comparison | `<`  `>`  `<=`  `>=` | `int`, `int` | `bool` |
| Equality | `==`  `!=` | same type | `bool` |
| Logical | `&&`  `\|\|` | `bool`, `bool` | `bool` |
| Unary | `-` | `int` | `int` |
| Unary | `!` | `bool` | `bool` |

### Comments

```c
// Single-line comment

/* Multi-line
   comment */
```

---

## Ownership System

The ownership system is the core feature of Lync. It tracks heap memory at compile time, ensuring every allocation is properly freed — with zero runtime overhead.

### Qualifiers

**`own` — heap ownership**
```c
ptr: own int = alloc 42;
// use ptr...
free ptr;
```
- Must be initialized with `alloc`
- Must be freed with `free` before scope exit
- Exactly one owner at a time

**`ref` — borrowed reference**
```c
r: ref int = some_own_var;
// use r...
// no need to free — r is just borrowing
```
- Borrows a reference to an `own` variable
- Cannot be freed (compile error if you try)

**No qualifier — stack value**
```c
x: int = 5;
```
- Plain value on the stack
- No ownership tracking needed

### Ownership States

Each `own` variable is tracked in one of three states:

| State | Meaning |
|-------|---------|
| **ALIVE** | Allocated and usable |
| **FREED** | Explicitly freed — cannot use or free again |
| **MOVED** | Ownership transferred to a function — cannot use, free, or move again |

### Compile-Time Checks

The compiler catches these errors **before your code runs**:

**Memory leak** — `own` not freed before function end
```c
def main(): int {
    ptr: own int = alloc 42;
    return 0;  // error: memory leak: 'own' variable 'ptr' was not freed or moved
}
```

**Double free**
```c
ptr: own int = alloc 10;
free ptr;
free ptr;  // error: double free: variable 'ptr' has already been freed
```

**Use after free**
```c
ptr: own int = alloc 10;
free ptr;
x: int = ptr;  // error: use after free
```

**Freeing a ref**
```c
def takes_ref(r: ref int): void {
    free r;  // error: cannot free 'ref' variable 'r'
}
```

**Ownership transfer (move semantics)**
```c
def consume(p: own int): int {
    free p;
    return 0;
}

def main(): int {
    ptr: own int = alloc 5;
    consume(ptr);  // ownership moves to consume()
    // ptr is now MOVED — cannot use or free
    return 0;
}
```

### How It Compiles

All ownership checks are **compile-time only**. The generated C has no safety overhead:

**Lync source:**
```c
ptr: own int = alloc 42;
free ptr;
```

**Generated C:**
```c
int64_t* ptr = malloc(sizeof(int64_t));
*ptr = 42;
free(ptr);
```

---

## Compiler Diagnostics

### Error Format

```
[filename:line:col] stage:severity: message
```

Where `stage` is one of: `lexer`, `parser`, `analyzer`, `codegen`.

### Severities

| Severity | Color | Behavior |
|----------|-------|----------|
| **error** | Red | Compilation fails |
| **warning** | Yellow | Compilation continues |
| **note** | Cyan | Additional context for a preceding error/warning |

### Example Output

```
[test.lync:4:5] analyzer:error: 'own' variables must be initialized with 'alloc'
[test.lync:10:5] analyzer:error: double free: variable 'ptr2' has already been freed

2 errors generated.
```

### Behavior

- **Parser errors** exit immediately on the first syntax error
- **Analyzer errors** are collected and displayed together (max 20 before truncation)
- ANSI colors are auto-detected for terminal output (Windows 10+ supported)
- Use `-no-color` to disable colors
- Use `-trace` for full pipeline debug output (tokens, AST, scopes) — all trace output goes to stderr

---

## Compiler Architecture

```
Source (.lync)
     |
     v
   Lexer         tokenization
     |
     v
   Parser        AST construction
     |
     v
  Analyzer       type checking, ownership validation
     |
     v
  Codegen        C source generation
     |
     v
Output (.c)
```

Written in **C (C23 standard)** with zero external dependencies.

| File | Purpose |
|------|---------|
| `lexer.c/h` | Tokenization |
| `parser.c/h` | Recursive descent parser, AST construction |
| `analyzer.c/h` | Type checking, scope management, ownership tracking |
| `codegen.c/h` | C code generation |
| `error.c/h` | Error collection and ANSI-colored reporting |
| `common.h` | Shared types, macros, diagnostic helpers |
| `main.c` | CLI entry point, pipeline orchestration |

---

## Planned Features

- **Borrow tracking** — track which `ref` borrows from which `own`, prevent dangling references
- **Print built-in** — `print` keyword for output (already reserved in the lexer)
- **Float and char types** — `float` (64-bit) and `char` primitives
- **Nullable types** — `int?`, `own?` with match-based null safety
- **Match expansion** — range patterns, exhaustiveness checking
- **Structs** — user-defined data types with auto-dereferencing
- **Arrays** — fixed-size stack arrays

---

## Roadmap

### Phase 1: Ref Expansion

Upgrade the borrow system with lifetime-lite tracking — no explicit annotations like Rust, but the compiler will know which `ref` borrows from which `own`.

- Track borrow relationships between `ref` and `own` variables
- Prevent dangling refs (using a `ref` after its source `own` is freed)
- Prevent freeing an `own` while `ref`s to it still exist
- No explicit lifetime annotations — simpler than Rust's approach

### Phase 2: Print + Float/Char

Add output capability and expand the type system with new primitives.

- Implement the `print` keyword (already reserved in the lexer)
- Add `float` type (maps to `double` in C)
- Add `char` type

```c
// Preview syntax
x: float = 3.14;
c: char = 'A';
print(x);
```

### Phase 3: Nullable Types

Any type can be nullable with the `?` suffix. Acts like an `Option` type — you must explicitly unwrap before use.

- Nullable types: `int?`, `bool?`, `own?`, `ref?`
- Must unwrap via `match` with `some` / `null` branches
- Using a nullable value without unwrapping is a compile error

```c
// Preview syntax
maybe: own? int = alloc 42;

match maybe {
    some(val): {
        // val is safely unwrapped here
    }
    null: {
        // handle null case
    }
};
```

### Phase 4: Match Expansion

Make pattern matching more powerful and safer.

- Range patterns (e.g., `1 to 10:`)
- Boolean patterns
- Exhaustiveness checking — warn when not all cases are covered
- Unreachable branch warnings — warn on patterns after a `_` wildcard

### Phase 5: Structs

User-defined composite types with compile-time field checking.

- Struct definitions: `struct Name { field: type; }`
- Plain fields only in v1 (`int`, `bool` — no `own`/`ref` fields yet)
- Auto-dereferencing with `.` (no `->` syntax needed)
- "Did you mean" suggestions for field name typos

```c
// Preview syntax
struct Entity {
    health: int;
    alive: bool;
}

e: own Entity = alloc Entity;
e.health = 100;
free e;
```

### Phase 6: Arrays

Fixed-size stack-allocated arrays integrated with the type system.

- Stack arrays: `arr: int[10]`
- Type checking for element access

```c
// Preview syntax
arr: int[5] = {1, 2, 3, 4, 5};
```

### Future

Long-term goals beyond the core language:

- **Dynamic arrays / lists** — as a standard library, not a built-in
- **Templates** — C++ style generics for containers and algorithms
- **`str` type** — a string type built as stdlib *in Lync itself* (not a primitive), using `char` + `own` + length tracking
- **Owned struct fields** — `own` and `ref` qualifiers inside struct definitions
- **`const` keyword** — immutable variable declarations
- **VS Code integration** — syntax highlighting, inline diagnostics, build tasks

---

## License

MIT License — see [LICENSE](LICENSE).

Built by [TheHeliPilot](https://github.com/TheHeliPilot).
