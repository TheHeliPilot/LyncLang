# Lync Programming Language

A systems programming language with manual memory management, compile-time ownership tracking, and pattern matching. Transpiles to C.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![Language: C](https://img.shields.io/badge/Language-C-gray.svg)
![Standard: C23](https://img.shields.io/badge/Standard-C23-green.svg)
![Version: 0.3.1](https://img.shields.io/badge/Version-0.3.1-orange.svg)

File extension: `.lync`

---

## Table of Contents

- [Overview](#overview)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [Language Reference](#language-reference)
  - [Types](#types)
  - [Variables](#variables)
  - [Functions](#functions)
  - [Control Flow](#control-flow)
  - [Pattern Matching](#pattern-matching)
  - [Operators](#operators)
  - [Module System](#module-system)
- [Ownership System](#ownership-system)
  - [Ownership Qualifiers](#ownership-qualifiers)
  - [Compile-Time Checks](#compile-time-checks)
  - [Immutability](#immutability)
- [Compiler Reference](#compiler-reference)
  - [CLI Usage](#cli-usage)
  - [Diagnostics](#diagnostics)
  - [Architecture](#architecture)
- [Development](#development)
  - [Changelog](#changelog)
  - [Roadmap](#roadmap)
- [License](#license)

---

## Overview

Lync is a systems programming language that provides manual memory management with compile-time safety guarantees. It prevents memory leaks, double frees, and use-after-free errors through a strict ownership system, without runtime garbage collection.

**Key Features:**
- **Manual Memory Control**: `alloc` and `free` for explicit heap management.
- **Ownership System**: Zero-cost, compile-time tracking of resource ownership.
- **Safety**: Nullable types (`?`) and exhaustive pattern matching.
- **Interoperability**: Transpiles to standard C23 for wide compatibility.


## Installation

### Windows (Pre-built)

1. Download the latest release.
2. Run `install.bat` as Administrator.
3. (Optional) Install `lync-language-0.3.1.vsix` in VS Code for syntax highlighting.

### Build from Source

Requirements: CMake 3.29+, a C compiler (GCC/Clang/MSVC).

```bash
cmake -B build
cmake --build build
```


## Quick Start

### Hello World

**hello.lync**
```c
def main(): int {
    print("Hello, world!");
    return 0;
}
```

### Compile and Run

```bash
# Compile to executable
lync hello.lync
./hello

# Or compile and run in one step
lync run hello.lync
```

### Ownership Example

```c
def main(): int {
    ptr: own int = alloc 42;
    print("Value:", ptr);
    free ptr;
    return 0;
}
```

### Nullable Types with I/O

```c
include std.io.read_int;

def main(): int {
    print("Enter a number:");
    num: own? int = read_int();

    match num {
        some(n): {
            print("You entered:", n);
        }
        null: {
            print("Invalid input");
        }
    };

    free num;
    return 0;
}
```

---

## Language Reference

### Types

| Type | Description | C Equivalent |
|------|-------------|--------------|
| `int` | Signed integer | `int` |
| `bool` | Boolean (`true` / `false`) | `bool` |
| `string` | String literal (read-only) | `const char*` |
| `char` | Single character | `char` |
| `void` | No return value | `void` |

**Nullable Types:**

Add `?` to pointer types to make them nullable:
- `own? int` - nullable owned pointer
- `ref? int` - nullable reference

Nullable types must be unwrapped via `match` before use.

### Variables

**Declaration:**
```c
x: int = 5;
alive: bool = true;
c: char = 'A';
```

**Assignment:**
```c
x = 10;
```

**Immutable Variables:**
```c
const pi: int = 314;
pi = 400;  // Error: cannot assign to 'pi', variable is immutable
```

All types must be explicitly annotated. Variables are mutable by default unless declared with `const`.

### Arrays

Lync uses a prefix `[size]` syntax for array declarations. Combined with `own`, this creates 4 distinct array types. For reference, `own type` on its own is a simple owned pointer (not an array).

| Lync Syntax | How to Read |
|---|---|
| `arr: [5] int` | 5 ints |
| `arr: own [5] int` | pointer to 5 ints (heap array) |
| `arr: [5] own int` | 5 pointers to ints (stack array of owns) |
| `arr: own [5] own int` | pointer to 5 pointers to ints (heap array of pointers) |

**1. Stack Array — `[N] type`**
```
arr: [5] int = {1, 2, 3, 4, 5};
arr[0] = 10;  // Element access and assignment
x: int = arr[2];
```

**2. Owned Pointer (Scalar) — `own type`**
```
ptr: own int = alloc 42;
free ptr;
```

**3. Heap Array — `own [N] type`**
```
arr: own [5] int = alloc 0;
arr[0] = 42;
free arr;
```

**4. Stack Array of Owned Pointers — `[N] own type`**
```
ptrs: [5] own int;
// Each element is an owned pointer (int*)
// Generates: int* ptrs[5]
```

**5. Heap Array of Owned Pointers — `own [N] own type`**
```
ptrs: own [5] own int;
// Double pointer: int** ptrs = malloc(5 * sizeof(int*))
// On free, automatically frees all elements first:
free ptrs;  // → for each element: free(ptrs[i]); then free(ptrs);
```

**Variable-Length Arrays (VLA):**
```
size: int = 5;
arr: [size] int;  // Uninitialized — must assign elements manually
for (i: 0 to size - 1) {
    arr[i] = i + 1;
}
```

**Built-in Functions:**
```
arr: [5] int = {1, 2, 3, 4, 5};
len: int = length(arr);  // Compile-time constant for stack arrays
```

**Rules:**
- Array types must match their declared type
- Stack arrays with **constant size** can be initialized with array literals
- Stack arrays with **variable size** (VLAs) cannot be initialized (C limitation)
- Heap arrays require `alloc` and must be freed
- Direct array assignment is blocked (prevents accidental copies)
- Element-wise assignment via `arr[i] = value` is allowed
- `free` on arrays of owned pointers automatically frees all elements first (cascading free)
- **Compile-time bounds checking**: Constant indices on constant-sized arrays are validated at compile time

**Compile-Time Safety:**
```
arr: [3] int = {1, 2, 3};

arr[0];   // OK
arr[2];   // OK (last element)
arr[3];   // Error: index 3 out of bounds (valid: 0 to 2)
arr[-1];  // Error: index -1 is negative

i: int = 5;
arr[i];   // No compile-time check (runtime bounds checking needed)
```


### Functions

**Syntax:**
```c
def add(a: int, b: int): int {
    return a + b;
}

def main(): int {
    result: int = add(3, 4);
    return 0;
}
```

**Features:**
- Explicit return types required
- `main` must return `int` and take no parameters
- Recursive calls supported
- Forward declarations generated automatically
- Function overloading by parameter types

**Immutable Parameters:**
```c
def process(const x: int): void {
    x = 10;  // Error: cannot assign to const parameter
}
```

### Control Flow

**If / Else:**
```c
if (x > 10) {
    print("Large");
} else {
    print("Small");
}
```

**While:**
```c
while (x > 0) {
    x = x - 1;
}
```

**Do-While:**
```c
do {
    x = x - 1;
} while (x > 0);
```

**For (Range):**
```c
for (i: 0 to 10) {
    print(i);  // 0, 1, 2, ..., 10 (inclusive)
}
```

**Nullable Checks:**
```c
maybe: own? int = read_int();

if (some(maybe)) {
    // maybe is proven non-null here
    x: int = maybe;
}
```

### Pattern Matching

**Expression Match:**
```c
result: int = match x {
    1: 10;
    2: 20;
    _: 0;
};
```

**Statement Match:**
```c
match x {
    1: {
        print("One");
    }
    _: {
        print("Other");
    }
};
```

**Nullable Unwrapping:**
```c
maybe: own? int = read_int();

match maybe {
    some(val): {
        print("Got:", val);
    }
    null: {
        print("No value");
    }
};

free maybe;
```

**Pattern Types:**
- `some(binding)` - unwraps non-null value
- `null` - matches null
- `_` - wildcard (default case)
- Value patterns - equality matching

### Operators

| Category | Operators | Types | Result |
|----------|-----------|-------|--------|
| Arithmetic | `+` `-` `*` `/` | `int`, `int` | `int` |
| Comparison | `<` `>` `<=` `>=` | `int`, `int` | `bool` |
| Equality | `==` `!=` | same type | `bool` |
| Logical | `&&` `\|\|` | `bool`, `bool` | `bool` |
| Unary | `-` | `int` | `int` |
| Unary | `!` | `bool` | `bool` |

### Module System

**Import Syntax:**
```c
include std.io.*;             // Import all from module
include std.io.read_int;      // Import specific function
```

**Rules:**
- `include` statements must appear at the top of the file
- Wildcard (`*`) and specific imports can be mixed
- Using unimported functions triggers compile error
- Only imported functions generate C helper code

**Available Modules:**

#### std.io

| Function | Return Type | Description |
|----------|-------------|-------------|
| `read_int()` | `own? int` | Read integer from stdin |
| `read_str()` | `own? string` | Read string from stdin |
| `read_bool()` | `own? bool` | Read boolean from stdin |
| `read_char()` | `own? char` | Read character from stdin |
| `read_key()` | `own? char` | Read keypress without echo |

**Example:**
```c
include std.io.read_int;

def main(): int {
    print("Enter number:");
    num: own? int = read_int();

    match num {
        some(n): {
            print("Got:", n);
        }
        null: {
            print("Invalid");
        }
    };

    free num;
    return 0;
}
```

### Print Statement

```c
print(42);                       // Single value
print(true, false);              // Multiple values
print("Result:", x + 5);         // Mix literals and expressions
```

Supports `int`, `bool`, `string`, and `char` types. Type-checked at compile time.

### Arrays

**Stack Arrays:**
```c
arr: int[3] = {10, 20, 30};
arr[1] = 99;
print("Element:", arr[1]);
```

**Heap Arrays:**
```c
arr: own int[2] = alloc int[2];
arr[0] = 5;
arr[1] = 10;
free arr;
```

**Length Function:**
```c
arr: int[5] = {1, 2, 3, 4, 5};
len: int = length(arr);  // Compile-time constant: 5
```

**Features:**
- Fixed-size arrays with compile-time size tracking
- Stack arrays (`int[5]`) require array literal initialization
- Heap arrays (`own int[5]`) allocated with `alloc`
- Element access: `arr[index]`
- Element assignment: `arr[index] = value`
- `length(arr)` built-in for stack arrays (compile-time constant)
- Direct array assignment blocked (prevents accidental copies)
- Reallocation after free allowed: `free arr; arr = alloc 99;`

### String Literals

```c
print("Hello, world!");
print("Escape: \n\t\"quoted\"");
```

Supported escapes: `\n`, `\t`, `\r`, `\\`, `\"`

### Comments

```c
// Single-line comment

/* Multi-line
   comment */
```

---

## Ownership System

The ownership system tracks heap memory at compile time, ensuring every allocation is properly freed with zero runtime overhead.

### Ownership Qualifiers

**`own` - Heap Ownership:**
```c
ptr: own int = alloc 42;
free ptr;
```
- Must be initialized with `alloc` or a function returning `own`
- Must be freed before scope exit
- Exactly one owner at a time

**`ref` - Borrowed Reference:**
```c
r: ref int = some_own_var;
// No need to free - r is borrowing
```
- Borrows from an `own` variable
- Cannot be freed
- Compiler tracks owner lifetime
- Using `ref` after owner goes out of scope is an error

**No Qualifier - Stack Value:**
```c
x: int = 5;
```
- Plain value on the stack
- No ownership tracking

### Compile-Time Checks

**Memory Leak Detection:**
```c
def main(): int {
    ptr: own int = alloc 42;
    return 0;  // Error: 'own' variable 'ptr' was not freed or moved
}
```

**Double Free Prevention:**
```c
ptr: own int = alloc 10;
free ptr;
free ptr;  // Error: double free
```

**Use After Free Prevention:**
```c
ptr: own int = alloc 10;
free ptr;
x: int = ptr;  // Error: use after free
```

**Reallocation After Free:**
```c
ptr: own int = alloc 10;
free ptr;
ptr = alloc 20;  // OK - new allocation resurrects the variable
free ptr;
```

Freed variables can be reassigned with `alloc`, transitioning state from `FREED` back to `ALIVE`. This works for both scalars and arrays.

**Ref Cannot Be Freed:**
```c
def takes_ref(r: ref int): void {
    free r;  // Error: cannot free 'ref' variable
}
```

**Ownership Transfer (Move Semantics):**
```c
def consume(p: own int): int {
    free p;
    return 0;
}

def main(): int {
    ptr: own int = alloc 5;
    consume(ptr);  // Ownership moves
    // ptr is now MOVED - cannot use or free
    return 0;
}
```

**Ownership Return (Factory Functions):**
```c
def create_number(): own int {
    x: own int = alloc 42;
    return x;  // Ownership transfers to caller
}

def main(): int {
    num: own int = create_number();  // Receives ownership
    print("Value:", num);
    free num;
    return 0;
}
```

Functions returning `own` transfer ownership to the caller. The caller must free the returned value.

**Dangling Reference Prevention:**
```c
def main(): int {
    r: ref int;
    {
        ptr: own int = alloc 42;
        r = ptr;
    }  // ptr goes out of scope
    x: int = r;  // Error: owner no longer in scope
    return 0;
}
```

**Nullable Ownership Relaxation:**
```c
maybe: own? int = read_int();  // OK - nullable can remain unfreed

if (some(maybe)) {
    free maybe;  // Required if proven non-null
}
```

### Immutability

**Const Variables:**
```c
const x: int = 10;
x = 20;  // Error: cannot assign to 'x', variable is immutable
```

**Const Parameters:**
```c
def readonly(const val: int): void {
    print(val);  // OK
    val = 42;    // Error: const parameter is immutable
}
```

**Automatic Const Propagation:**

References inherit const-ness from their owner:

```c
const x: own int = alloc 5;
r: ref int = x;  // r is automatically const
r = 10;          // Error: r is implicitly const
```

This prevents modifying const data through borrowed references.

**Generated Code:**

All checks are compile-time only. No runtime overhead:

```c
// Lync source
ptr: own int = alloc 42;
free ptr;

// Generated C
int* ptr = malloc(sizeof(int));
*ptr = 42;
free(ptr);
```

---

## Compiler Reference

### CLI Usage

```
lync [options] [input_file]
lync run [options] [input_file]
```

**Options:**

| Flag | Description |
|------|-------------|
| `run` | Compile and execute in one step |
| `-o <file>` | Output executable name |
| `-S` | Emit assembly instead of executable |
| `--emit-c` | Keep intermediate .c file |
| `-trace` | Enable debug trace output |
| `-no-color` | Disable ANSI colors |
| `-O0` | No optimization (default) |
| `-O1` | Basic optimizations |
| `-O2` | More optimizations |
| `-O3` | All optimizations |
| `-Os` | Optimize for size |
| `-h`, `--help` | Show help |

**Examples:**

```bash
lync example.lync              # Compile to ./example
lync -o myapp example.lync     # Compile to ./myapp
lync run example.lync          # Compile and execute
lync --emit-c example.lync     # Keep example.c file
lync -O2 example.lync          # Compile with optimizations
```

### Diagnostics

**Error Format:**
```
[filename:line:col] stage:severity: message
```

**Severities:**

| Severity | Behavior |
|----------|----------|
| `error` | Compilation fails |
| `warning` | Compilation continues |
| `note` | Additional context |

**Example:**
```
[test.lync:4:5] analyzer:error: 'own' variables must be initialized with 'alloc' or a function returning 'own'
[test.lync:10:5] analyzer:error: double free: variable 'ptr2' has already been freed

2 errors generated.
```

**Behavior:**
- Parser exits on first syntax error
- Analyzer collects up to 20 errors before truncation
- ANSI colors auto-detected (Windows 10+ supported)
- All trace/debug output goes to stderr

### Architecture

```
Source (.lync)
     |
     v
   Lexer         Tokenization
     |
     v
   Parser        AST construction, import parsing
     |
     v
  Analyzer       Type checking, ownership validation
     |
     v
 Optimizer       Constant folding, dead code elimination
     |
     v
  Codegen        C code generation
     |
     v
Output (.c)
```

**Implementation:**

Written in C (C23 standard) with zero external dependencies.

| File | Purpose |
|------|---------|
| `lexer.c/h` | Tokenization |
| `parser.c/h` | AST construction, import parsing |
| `analyzer.c/h` | Type checking, ownership tracking |
| `optimizer.c/h` | AST optimization passes |
| `codegen.c/h` | C code generation |
| `error.c/h` | Error collection and reporting |
| `common.h` | Shared types and macros |
| `main.c` | CLI entry point |

---

## Development

### Changelog

#### [0.2.3] - 2026-02-13

**Added:**
- Compile-time bounds checking for constant-sized arrays with constant indices
  - Detects out-of-bounds access: `arr[6]` when array size is 5
  - Detects negative indices: `arr[-1]`
  - Provides helpful error messages with valid index ranges
  - Zero runtime overhead (compile-time only)
- Optional array initialization for variable-sized stack arrays (VLAs)
  - VLAs can now be declared without initialization: `arr: int[size];`
  - Non-array variables still require initialization
- Compile-time validation preventing VLA initialization (enforces C language limitation)
  - Stack arrays with variable size cannot use array literal initialization
  - Clear error messages direct users to use constant sizes or heap allocation

**Fixed:**
- Critical segfault when declaring non-array variables (NULL dereference in arraySize analyzer)
- Array allocation generating garbage values instead of proper size expressions
- Format string bugs in array size mismatch error messages
- Stack arrays now properly emit variable-sized array expressions
- Heap arrays now properly emit size expressions in malloc calls
- `length()` function incorrectly rejecting constant-sized arrays (type comparison bug)
- Array size extraction checking wrong type (analyzed type vs expression type)

**Changed:**
- Integer type now maps to C `int` instead of `int64_t` for simplicity
- Array sizes can now be runtime variables (e.g., `arr: int[x]` where x is computed)
- Improved codegen for array declarations to handle both constant and variable sizes
- Parser now allows optional initialization for arrays (required for non-arrays)
- Analyzer properly handles uninitialized VLAs using VOID_E placeholder

**Examples:**
```c
// Constant size with initialization ✅
arr1: int[5] = {1, 2, 3, 4, 5};

// Variable size without initialization ✅
size: int = 5;
arr2: int[size];
for (i: 0 to size - 1) {
    arr2[i] = i + 1;
}

// Compile-time bounds checking ✅
arr3: int[3] = {1, 2, 3};
arr3[5];  // Error: index 5 out of bounds (valid: 0 to 2)

// Variable size with initialization ❌
arr4: int[size] = {1, 2, 3};  // Error: C limitation
```

#### [0.2.2] - 2026-02-13

**Added:**
- Array support with stack and heap allocation
  - Stack arrays: `arr: int[5] = {1, 2, 3, 4, 5};`
  - Heap arrays: `arr: own int[5] = alloc int[5];`
  - Element access: `arr[index]`
  - Element assignment: `arr[index] = value;`
  - `length(arr)` built-in for stack arrays (compile-time constant)
  - Direct array assignment blocked to prevent accidental copies
- Reallocation after free for `own` variables and arrays
  - Freed variables can be reassigned with `alloc`
  - State transitions from `FREED` → `ALIVE` automatically
  - Example: `free ptr; ptr = alloc 20;`

**Fixed:**
- Critical buffer overflow in `declare()` (scope reallocation happened after write)
- Format string crashes (using %s with enum integers)
- NULL pointer dereference in ref variable handling
- Missing ALLOC_E codegen case
- Missing expression cases in AST printer

#### [0.2.1] - 2026-02-13

**Added:**
- `const` keyword for immutable variables
  - Syntax: `const x: int = 10;`
  - Assignment to const variables triggers compile error
### File Includes (Modules)

You can split your code into multiple files and include them.

The `include` statement works with:
1. **Standard Library**: `include std.io.*;`
2. **Local Files**: `include path.to.file.*;`

**Rules:**
- Paths are relative to the current file.
- Dots (`.`) usage represent directory separators.
- The `.lync` extension is appended automatically.
- Use `*` to import all functions, or specify a function name.

**Example:**
Refactoring helper functions into `utils/math.lync`:

**File: `utils/math.lync`**
```
def square(x: int): int {
    return x * x;
}
```

**File: `main.lync`**
```
include std.io.*;
include utils.math.*;  // Imports utils/math.lync

def main(): int {
    x: int = square(5);
    print(x);
    return 0;
}
```

### Functions
Lync requires function signatures to be explicit.
- `const` parameters in function signatures
  - Syntax: `def foo(const x: int): void`
  - Attempting to assign to const parameters triggers compile error
- Automatic const propagation for references
  - References inherit const-ness from their owner
  - Prevents modifying const data through borrowed references
  - Example: `const x: own int = alloc 5; r: ref int = x;` makes `r` implicitly const

**Fixed:**
- Parser function parameter validation logic (incorrect use of OR instead of AND)

#### [0.2.0] - 2026-02-13

**Added:**
- Module system with `include` statement
  - Wildcard imports: `include std.io.*;`
  - Specific imports: `include std.io.read_int;`
  - Compile-time import validation
- `std.io` module with five I/O functions
  - `read_int()`, `read_str()`, `read_bool()`, `read_char()`, `read_key()`
  - All return nullable types (`own? T`)
- `char` primitive type
- Conditional code generation (only emit imported functions)

**Fixed:**
- Critical use-after-free crash in codegen (FuncTable freed too early)
- Critical uninitialized Symbol fields causing crashes
- Function name mangling mismatch between declarations and calls

#### [0.1.0] - 2026-02-11

**Added:**
- Nullable types with pattern matching (`own? int`, `ref? int`)
- Reference ownership tracking (prevent dangling refs)
- Print statement with string literals
- Enhanced diagnostics with ANSI color output
- Error collector system (batch up to 20 errors)

#### [0.0.1] - 2026-02-09

**Initial Release:**
- Core language features (variables, functions, control flow)
- Ownership system (`own`, `ref`, `alloc`, `free`)
- Compile-time ownership state tracking
- Memory leak, double-free, use-after-free detection
- Transpiles to C with automatic compiler detection

See [CHANGELOG.md](CHANGELOG.md) for complete version history.

### Roadmap

#### Completed Features

- Ownership tracking with lifetime validation
- Nullable types with safe pattern matching
- Module system with selective imports
- Immutability with `const` keyword
- Reference ownership tracking
- Flow-sensitive nullable unwrapping
- Function overloading by parameter types
- Cross-platform support (Windows, macOS, Linux)
- Fixed-size arrays (stack and heap allocated)
- Variable-size arrays (VLAs on stack, dynamic on heap)
- Compile-time bounds checking for constant array indices
- Reallocation after free for owned variables

#### Planned Features

**Phase 5: Match Expansion**

Enhance pattern matching with more powerful features.

- Range patterns: `1 to 10:`
- Boolean patterns: `true:` and `false:` branches
- Exhaustiveness checking (warn when not all cases covered)
- Unreachable branch warnings (patterns after wildcard)

**Phase 6: Structs**

User-defined composite types with field validation.

```c
Entity: struct {
    health: int;
    alive: bool;
    name: string;
}

e: own Entity = alloc Entity;
e.health = 100;
free e;
```

Features:
- Struct definitions with plain fields (int, bool, char, string)
- Auto-dereferencing with `.` operator (no `->` needed)
- "Did you mean" suggestions for field name typos
- Compile-time field access validation


**Future Goals**

Long-term features beyond core language:

- **Dynamic arrays/lists** - Standard library implementation in Lync
- **Templates** - C++ style generics for containers and algorithms
- **Full string type** - Built as stdlib using `char`, `own`, and length tracking
- **Owned struct fields** - `own` and `ref` fields in structs with auto-cleanup
- **User-defined modules** - `.lync` files as importable modules
- **Float type** - IEEE 754 floating-point (`double` in C)
- **VS Code extension** - Syntax highlighting, diagnostics, code completion
- **Language Server Protocol** - LSP implementation for editor integration

**Known Limitations**

Issues to address in future releases:

- No standalone `{}` blocks (only function bodies)
- String variables not yet supported (only literals)
- No format specifiers in print
- No multi-file compilation (single input file only)
- No incremental compilation
- Variable-sized stack arrays (VLAs) cannot be initialized with array literals (C language limitation)
- Variable-sized stack arrays (VLAs) cannot be initialized (C language limitation)
  - Use constant size or heap allocation for initialized arrays

See [TODO.md](TODO.md) for detailed task tracking.

---

## License

MIT License - see [LICENSE](LICENSE).

Built by [TheHeliPilot](https://github.com/TheHeliPilot).