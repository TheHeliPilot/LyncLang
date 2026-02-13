# Lync

**A systems programming language with manual memory management, compile-time ownership tracking, pattern matching, and a module system — transpiles to C.**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![Language: C](https://img.shields.io/badge/Language-C-gray.svg)
![Standard: C23](https://img.shields.io/badge/Standard-C23-green.svg)
![Version: 0.2.0](https://img.shields.io/badge/Version-0.2.0-orange.svg)

File extension: `.lync`

---

## Overview

Lync sits between C and Rust. You get manual memory management like C — you `alloc` and `free` by hand — but the compiler statically tracks ownership and catches memory bugs at compile time, like Rust. There's no garbage collector and no runtime overhead from safety checks. The output is readable, standard C.

- **Explicit memory control** — you decide when to allocate and free
- **Compile-time safety** — memory leaks, double free, and use-after-free are caught before your code runs
- **Zero-cost** — all ownership checks happen at compile time; the generated C has no overhead
- **Module system** — selective imports for clean, maintainable code
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

### Install (Optional)

To install Lync system-wide and add it to your PATH:

**Windows (PowerShell as Administrator):**
```powershell
.\install.ps1
```

**Windows (Batch File as Administrator):**
```bash
install.bat
```

After installation, you can use `lync` from any directory. See **[INSTALL.md](INSTALL.md)** for detailed installation instructions, troubleshooting, and manual installation steps.

### Write a program

```c
// example.lync
include std.io.read_int;

def add(a: int, b: int): int {
    return a + b;
}

def main(): int {
    print("Enter a number:");
    num: own? int = read_int();

    match num {
        some(n): {
            result: int = add(n, 10);
            print("Result:", result);
        }
        null: {
            print("Invalid input");
        }
    };

    return 0;
}
```

### Compile and run

**Option 1: Separate steps**
```bash
./lync example.lync          # Produces example executable
./example
```

**Option 2: Run mode**
```bash
./lync run example.lync      # Compile and execute in one step
```

The compiler transpiles to C, automatically detects your C compiler (gcc, clang, or MSVC), compiles the generated C code, and produces a native executable. The intermediate `.c` file is cleaned up automatically unless you specify `--emit-c`.

---

## CLI Reference

```
lync [options] [input_file]
lync run [options] [input_file]
```

| Flag | Description |
|------|-------------|
| `run` | Compile and immediately execute (shorthand mode) |
| `-o <file>` | Output executable name (default: input name without extension) |
| `-S` | Emit assembly instead of executable |
| `--emit-c` | Keep the intermediate .c file after compilation |
| `-trace` | Enable debug trace output (stderr) |
| `-no-color` | Disable ANSI-colored diagnostics |
| `-O0` | No optimization (default) |
| `-O1` | Basic optimizations (constant folding) |
| `-O2` | More optimizations (dead code elimination) |
| `-O3` | All optimizations (including inlining) |
| `-Os` | Optimize for size |
| `-h`, `--help` | Show help message |

If no input file is specified, defaults to `test.lync`.

**Cross-platform support:** The compiler auto-detects available C compilers (gcc, clang, MSVC) and manages intermediate files and executable extensions automatically on Windows, macOS, and Linux.

---

## Language Reference

### Module System (✅ New in 0.2.0)

Lync uses a `include` statement to selectively import functionality from modules, reducing code bloat and improving maintainability.

**Import syntax:**
```c
include std.io.*;             // Import all I/O functions
include std.io.read_int;      // Import only read_int
include std.io.read_str;      // Import multiple specific functions
```

**Rules:**
- `include` statements must appear at the **top of the file** (before any function definitions)
- You can mix wildcard (`*`) and specific imports
- Importing from a non-existent module is a compile error
- Using a function that hasn't been imported is a compile error

**Available modules:**

#### `std.io` — Input/Output Functions

| Function | Return Type | Description |
|----------|-------------|-------------|
| `read_int()` | `own? int` | Read an integer from stdin (nullable) |
| `read_str()` | `own? string` | Read a string from stdin (nullable) |
| `read_bool()` | `own? bool` | Read a boolean from stdin (nullable) |
| `read_char()` | `own? char` | Read a character from stdin (nullable) |
| `read_key()` | `own? char` | Read a single keypress without echo (nullable) |

All I/O functions return nullable types because input can fail (EOF, invalid format, etc.). You must unwrap the result using `match` or `if(some())`.

**Example:**
```c
include std.io.read_int;
include std.io.read_str;

def main(): int {
    print("Enter your age:");
    age: own? int = read_int();

    match age {
        some(a): {
            print("You are", a, "years old");
        }
        null: {
            print("Invalid age");
        }
    };

    return 0;
}
```

**Code generation:**
- Only imported functions generate helper C code
- Unused functions don't add bloat to the output
- All helper functions are conditionally generated based on imports

### Types

| Lync Type | Description | C Equivalent |
|----------|-------------|--------------|
| `int` | 64-bit signed integer | `int64_t` |
| `bool` | Boolean (`true` / `false`) | `bool` |
| `string` | String literal (read-only) | `const char*` |
| `char` | Single character | `char` |
| `void` | No return value (functions only) | `void` |

**Nullable pointers:** Owned and reference types can be made nullable by adding `?`:
- `own? int` — nullable owned pointer (can be `null` or a valid pointer)
- `ref? int` — nullable reference pointer
- Must be unwrapped via `match` with `some`/`null` patterns before use
- Only pointer types (`own`, `ref`) can be nullable, not base types

### Variables

```c
// Declaration
x: int = 5;
alive: bool = true;
c: char = 'A';

// Assignment
x = 10;
```

All types must be explicitly annotated. Variables are mutable by default.

### String Literals

```c
print("Hello, world!");
print("Value:", 42);
print("Escape sequences: \n\t\"quoted\"");
```

String literals support escape sequences: `\n` (newline), `\t` (tab), `\r` (carriage return), `\\` (backslash), `\"` (quote). Strings are currently read-only and can only be used with `print`.

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
- Function overloading supported (same name, different parameter types)

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

**Nullable checks with `some()`:**
```c
maybe: own? int = alloc 42;

if(some(maybe)) {
    // maybe is proven non-null here (flow-sensitive unwrapping)
    x = maybe;  // safe to use without match
}
```
The `some()` function checks if a nullable value is non-null. Inside the `if` block, the variable is automatically unwrapped and can be used directly.

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
} while (x > 0);
```

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

**Pattern matching rules:**
- Wildcard `_` (default branch) is required for value matches
- All branches in a match expression must return the same type
- Pattern types must match the target expression type
- Compiles to if/else chains in C

**Nullable unwrapping:**

Match statements provide safe unwrapping of nullable types:

```c
maybe_ptr: own? int = alloc 42;

// Statement match
match maybe_ptr {
    some(val): {
        // val is 'own int' (non-nullable)
        // safe to use here
        i = val;
    }
    null: {
        // handle null case
    }
};

// Expression match
result: int = match maybe_ptr {
    some(val): val;  // val available in this expression
    null: 0;
};
```

**Pattern types:**
- `some(binding)` — matches non-null, creates binding variable with unwrapped type
- `null` — matches null value
- `_` — wildcard (covers both some and null)
- Value patterns — any expression for equality matching

**Nullable match requirements:**
- Nullable types **must** be matched with both `some` and `null` branches
- Binding variables in `some(name)` are references to the original value
- Bindings are automatically typed as non-nullable
- Using nullable without unwrapping is a compile error

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

### Print Statement

```c
print(42);                    // Print a single value
print(true, false);           // Print multiple values (comma-separated)
print("Result:", x + 5);      // Mix literals and expressions
```

The `print` statement is a built-in for output. It accepts `int`, `bool`, `string`, and `char` values. Multiple values can be printed with comma separation. `print` is reserved and cannot be used as a variable name.

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
- The compiler tracks which `own` variable the `ref` borrows from
- Using a `ref` after its owner goes out of scope is a compile error

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

**Use after owner out of scope**
```c
def main(): int {
    r: ref int;
    {
        ptr: own int = alloc 42;
        r = ptr;  // r borrows from ptr
    }  // ptr goes out of scope here
    x: int = r;  // error: use after owner no longer in scope
    return 0;
}
```
The compiler tracks the lifetime of the owner and prevents using a `ref` after its `own` source is freed or out of scope.

**Nullable ownership:**
```c
// Nullable own variables are allowed to remain unfreed (they might be null)
maybe: own? int = read_int();  // OK - can go out of scope without freeing

// But if you know it's non-null, you still must free
if(some(maybe)) {
    free maybe;  // Required if unwrapped
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

Where `stage` is one of: `lexer`, `parser`, `analyzer`, `optimizer`, `codegen`.

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
   Parser        AST construction, module import parsing
     |
     v
  Analyzer       type checking, ownership validation, import verification
     |
     v
 Optimizer       constant folding, dead code elimination, inlining (optional)
     |
     v
  Codegen        C source generation (conditional based on imports)
     |
     v
Output (.c)
```

Written in **C (C23 standard)** with zero external dependencies.

| File | Purpose |
|------|---------|
| `lexer.c/h` | Tokenization |
| `parser.c/h` | Recursive descent parser, AST construction, import parsing |
| `analyzer.c/h` | Type checking, scope management, ownership tracking, import validation |
| `optimizer.c/h` | AST optimization passes |
| `codegen.c/h` | C code generation with conditional helper emission |
| `error.c/h` | Error collection and ANSI-colored reporting |
| `common.h` | Shared types, macros, diagnostic helpers |
| `main.c` | CLI entry point, pipeline orchestration |

---

## Recent Features

### Module System with `include` Statement (✅ New in v0.2.0)

A Java/C#-style module system for clean, selective imports:

- **Selective imports:** Only generate code for what you use
- **Namespace organization:** Logical grouping of built-in functions
- **Wildcard and specific imports:** `include std.io.*;` or `include std.io.read_int;`
- **Compile-time validation:** Importing from non-existent modules or using unimported functions triggers errors
- **Zero bloat:** Unused functions generate no C code

```c
include std.io.read_int;
include std.io.read_str;

def main(): int {
    print("Enter number:");
    num: own? int = read_int();  // OK - imported

    // str: own? string = read_bool();  // ERROR - not imported

    match num {
        some(n): {
            print("Got:", n);
        }
        null: { print("Invalid"); }
    };

    return 0;
}
```

**Current modules:**
- `std.io` — Input/output functions (read_int, read_str, read_bool, read_char, read_key)

**Future modules planned:**
- `std.math` — Mathematical functions (sqrt, pow, abs, etc.)
- `std.string` — String manipulation
- `std.file` — File I/O operations

### Nullable Types with Pattern Matching (✅ Implemented — v0.1.0)

Full nullable type support with safe unwrapping:
- **Nullable syntax:** Any pointer type can be nullable with `?` suffix (`own? int`, `ref? int`)
- **Pattern matching:** Unwrap with `some(binding)` and `null` patterns in match expressions and statements
- **Flow-sensitive unwrapping:** Variables proven non-null via `if(some(x))` are automatically unwrapped in the block
- **Compile-time safety:** Using nullable without unwrapping is a compile error
- **Binding variables:** `some(val)` creates a non-nullable reference available in the branch
- **Zero-cost:** All checks are compile-time; generated C uses simple null checks

### Reference Ownership Tracking (✅ Implemented — v0.1.0)

The compiler now tracks which `own` variable each `ref` borrows from. This enables compile-time detection of:
- Using a `ref` after its owner has been freed
- Using a `ref` after its owner goes out of scope
- Attempting to assign a non-`own` variable to a `ref`

### Print Built-in (✅ Implemented — v0.1.0)

The `print` statement is now available for basic output:
- Supports `int`, `bool`, `string`, and `char` types
- Multiple values with comma separation: `print("Result:", x, true);`
- Type-checked at compile time (unsupported types trigger errors)
- Reserved keyword (cannot be used as a variable name)
- Warns on empty `print()` calls

### String Literals (✅ Implemented — v0.1.0)

String literals with escape sequence support:
- Basic escapes: `\n`, `\t`, `\r`, `\\`, `\"`
- Compile to `const char*` in C
- Currently limited to use with `print` (more features planned)

---

## Roadmap

### Phase 1: Ref Expansion (**Completed** ✅ — v0.1.0)

~~Basic ownership tracking is complete~~. All ref tracking features implemented:

- ~~Track borrow relationships between `ref` and `own` variables~~ ✅
- ~~Prevent dangling refs (using a `ref` after its source `own` is freed)~~ ✅
- ~~Prevent using a `ref` after owner goes out of scope~~ ✅
- ~~Error when trying to use a `ref` after its `own` source is freed or out of scope~~ ✅

### Phase 2: Print + Char Type (**Completed** ✅ — v0.2.0)

Expand output capabilities and the type system.

- ~~Implement basic `print` statement~~ ✅
- ~~String literal support~~ ✅
- ~~Add `char` type~~ ✅
- **TODO:** Print formatting (format specifiers like `%d`, `%s`)
- **TODO:** String variables (not just literals)
- **TODO:** Add `float` type (maps to `double` in C)

### Phase 3: Nullable Types (**Completed** ✅ — v0.1.0)

Pointer types can be made nullable with the `?` suffix. Acts like an `Option` type — you must explicitly unwrap before use.

- ~~Nullable pointers: `own?`, `ref?`~~ ✅
- ~~Must unwrap via `match` with `some` / `null` branches~~ ✅
- ~~Using a nullable value without unwrapping is a compile error~~ ✅
- ~~Flow-sensitive unwrapping: variables are known to be non-null within `if(some(x))` blocks~~ ✅
- ~~Pattern matching with binding: `some(val)` creates a non-nullable reference~~ ✅
- ~~Supported in both statement and expression matches~~ ✅

### Phase 4: Module System (**Completed** ✅ — v0.2.0)

- ~~`include` statement for imports~~ ✅
- ~~Wildcard imports: `include std.io.*;`~~ ✅
- ~~Specific imports: `include std.io.read_int;`~~ ✅
- ~~Compile-time import validation~~ ✅
- ~~Conditional code generation (only emit used functions)~~ ✅
- ~~Standard library I/O functions (read_int, read_str, read_bool, read_char, read_key)~~ ✅

### Phase 5: Match Expansion

Make pattern matching more powerful and safer.

- Range patterns (e.g., `1 to 10:`)
- Boolean patterns
- Exhaustiveness checking — warn when not all cases are covered
- Unreachable branch warnings — warn on patterns after a `_` wildcard

### Phase 6: Structs

User-defined composite types with compile-time field checking.

- Struct definitions: `struct Name { field: type; }`
- Plain fields only in v1 (`int`, `bool`, `char` — no `own`/`ref` fields yet)
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

### Phase 7: Arrays

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
- **User-defined modules** — `.lync` files as importable modules
- **VS Code integration** — syntax highlighting, inline diagnostics, build tasks

---

## License

MIT License — see [LICENSE](LICENSE).

Built by [TheHeliPilot](https://github.com/TheHeliPilot).
