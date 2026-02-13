# Lync Compiler TODO

This document tracks planned features and improvements for the Lync programming language. Features are organized by priority and development phase.

---

## In Progress

Currently no features in active development.

---

## Phase 5: Match Expansion

Enhance pattern matching with more powerful features and better safety guarantees.

### Range Patterns
- [ ] Add range pattern syntax: `1 to 10:`
- [ ] Support inclusive and exclusive ranges
- [ ] Type checking for range bounds
- [ ] Optimize range checks in codegen

**Example:**
```c
match age {
    0 to 12: print("Child");
    13 to 19: print("Teen");
    20 to 64: print("Adult");
    _: print("Senior");
};
```

### Boolean Patterns
- [ ] Allow boolean patterns in match statements
- [ ] Support `true:` and `false:` branches
- [ ] Type checking for boolean match targets

**Example:**
```c
match is_valid {
    true: print("Valid");
    false: print("Invalid");
};
```

### Exhaustiveness Checking
- [ ] Analyze match statements for completeness
- [ ] Warn when not all cases are covered
- [ ] Suggest missing patterns
- [ ] Special handling for boolean matches (must cover both true/false)

**Example Warning:**
```
[test.lync:10:5] analyzer:warning: match statement is not exhaustive
[test.lync:10:5] analyzer:note: missing pattern: false
```

### Unreachable Branch Detection
- [ ] Detect patterns that can never be reached
- [ ] Warn on branches after wildcard `_` pattern
- [ ] Track which patterns have been covered

**Example Warning:**
```
[test.lync:15:9] analyzer:warning: unreachable pattern
[test.lync:13:9] analyzer:note: wildcard pattern `_` covers all remaining cases
```

---

## Phase 6: Structs

User-defined composite types with compile-time field validation.

### Basic Struct Support
- [ ] Struct definition syntax: `Name: struct { field: type; }`
- [ ] Plain fields only (int, bool, char, string)
- [ ] Struct allocation with `alloc`
- [ ] Field access with `.` operator
- [ ] Auto-dereferencing (no `->` needed)
- [ ] Struct assignment and copying
- [ ] Type checking for field access

**Example:**
```c
Entity: struct {
    health: int;
    alive: bool;
    name: string;
}

e: own Entity = alloc Entity;
e.health = 100;
e.alive = true;
free e;
```

### Field Name Validation
- [ ] Detect typos in field names
- [ ] "Did you mean" suggestions using string similarity
- [ ] Calculate Levenshtein distance for suggestions
- [ ] Show note messages with suggested field names

**Example:**
```
[test.lync:10:5] analyzer:error: struct 'Entity' has no field 'helth'
[test.lync:10:5] analyzer:note: did you mean 'health'?
```

### Struct Type System
- [ ] Add `STRUCT_T` token type
- [ ] Struct type registry in analyzer
- [ ] Struct definitions in AST
- [ ] Type checking for struct expressions
- [ ] Nested struct access (if time permits)

### Limitations (v1)
- No `own` or `ref` fields in structs (future phase)
- No methods or associated functions
- No struct literals (must allocate then assign)
- No nested struct definitions

---

## Phase 7: Arrays

Fixed-size stack-allocated arrays with type checking.

### Basic Array Support
- [ ] Array declaration syntax: `arr: int[10]`
- [ ] Array initialization with literals: `{1, 2, 3}`
- [ ] Array indexing: `arr[i]`
- [ ] Bounds checking (compile-time where possible)
- [ ] Array size tracking in type system
- [ ] Multi-dimensional arrays (if time permits)

**Example:**
```c
arr: int[5] = {1, 2, 3, 4, 5};
arr[0] = 10;
print(arr[2]);  // 3
```

### Array Type System
- [ ] Add array type representation in AST
- [ ] Size as part of type (int[5] != int[10])
- [ ] Type checking for array access
- [ ] Type checking for array initialization
- [ ] Prevent array decay to pointer (keep size information)

### Array Safety
- [ ] Compile-time bounds checking for constant indices
- [ ] Runtime bounds checking for dynamic indices (optional, with flag)
- [ ] Warn on potential out-of-bounds access
- [ ] Array length tracking

---

## Future Features

Long-term goals beyond the core language phases.

### Dynamic Arrays / Lists
- [ ] Design dynamic array API
- [ ] Implement as standard library (not built-in)
- [ ] Written in Lync itself (uses `own`, templates)
- [ ] Push, pop, resize operations
- [ ] Bounds checking

**Example (syntax TBD):**
```c
list: own List<int> = List.new();
list.push(10);
list.push(20);
free list;
```

### Templates (Generics)
- [ ] C++ style template syntax
- [ ] Type parameters for functions and structs
- [ ] Monomorphization (generate code for each instantiation)
- [ ] Type constraints (if needed)

**Example (syntax TBD):**
```c
def max<T>(a: T, b: T): T {
    if (a > b) { return a; }
    return b;
}

Container<T>: struct {
    value: T;
}
```

### Full String Type
- [ ] Design `str` type as standard library
- [ ] Written in Lync using `char`, `own`, and length tracking
- [ ] String concatenation
- [ ] String slicing
- [ ] UTF-8 support (stretch goal)

**Example (syntax TBD):**
```c
s: own str = str.from("Hello");
s.append(" world");
print(s.len());
free s;
```

### Owned Struct Fields
- [ ] Allow `own` fields in structs
- [ ] Track ownership of struct fields
- [ ] Automatic cleanup when struct is freed
- [ ] Borrow checking for struct field refs

**Example (syntax TBD):**
```c
Node: struct {
    value: int;
    next: own? Node;
}
```

### User-Defined Modules
- [ ] Allow `.lync` files as modules
- [ ] Public/private visibility modifiers
- [ ] Module resolution and search paths
- [ ] Module dependency tracking
- [ ] Prevent circular dependencies

**Example (syntax TBD):**
```c
// math.lync
pub def square(x: int): int {
    return x * x;
}

// main.lync
include math.square;
```

### Float Type
- [ ] Add `float` primitive type
- [ ] Map to C `double`
- [ ] Float literals: `3.14`, `1.0e-5`
- [ ] Arithmetic operations on floats
- [ ] Type promotion rules (int to float)
- [ ] Float printing support

### Additional Optimizations
- [ ] Function inlining (beyond -O3)
- [ ] Loop unrolling
- [ ] Tail call optimization
- [ ] Common subexpression elimination
- [ ] Strength reduction

### Developer Tools
- [ ] VS Code extension
  - Syntax highlighting
  - Error diagnostics
  - Code completion
  - Go to definition
- [ ] Language Server Protocol (LSP) implementation
- [ ] Debugger integration (map back to .lync source)
- [ ] Standard library documentation generator

### Error Recovery
- [ ] Better parser error recovery
- [ ] Continue parsing after syntax errors
- [ ] Suggest fixes for common mistakes
- [ ] More detailed error messages

---

## Bugs and Technical Debt

Issues to address in future releases.

### Known Limitations
- [ ] No support for standalone `{}` blocks
- [ ] String variables not yet supported (only literals)
- [ ] No format specifiers in print (e.g., `%d`, `%s`)
- [ ] No multi-file compilation (single input file only)
- [ ] No incremental compilation

### Memory Management
- [ ] Proper FuncTable cleanup (currently deferred until after codegen)
- [ ] Review all malloc/free pairs for leaks
- [ ] Add arena allocator for compiler (reduce allocation overhead)

### Code Quality
- [ ] Refactor parser (some functions are very long)
- [ ] Add more internal documentation
- [ ] Improve test coverage
- [ ] Add fuzzing tests

---

## Completed

Features that have been implemented.

- [x] Ownership tracking (ref/own with lifetime validation)
- [x] Nullable types with pattern matching
- [x] Module system with `include` statement
- [x] Print statement with string literals
- [x] `char` primitive type
- [x] Immutability with `const` keyword
- [x] Automatic const propagation for refs
- [x] Reference ownership tracking (prevent dangling refs)
- [x] Flow-sensitive nullable unwrapping
- [x] Function overloading by parameter types
- [x] Compile-time error collection and reporting
- [x] ANSI color diagnostics
- [x] Cross-platform support (Windows, macOS, Linux)
- [x] Automatic C compiler detection
- [x] Optimization passes (constant folding, dead code elimination)

---

## Contributing

To suggest a feature or report an issue, please open an issue on GitHub or contact the maintainer.

Features are prioritized based on:
1. Impact on language safety and usability
2. Implementation complexity
3. User demand
4. Alignment with language design philosophy
