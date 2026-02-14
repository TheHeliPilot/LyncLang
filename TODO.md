# Lync Project Roadmap

## Current Status: v0.3.1 (File Includes)

**Recently Completed:**
- [x] File Include System (`include path.to.file.*`)
- [x] VS Code Plugin with file completion
- [x] Basic Nullable Types & Pattern Matching
- [x] Ownership System (Core)

---

## SOON (Next Priority)

### 1. Structs (Composite Types)
The next major feature to enable complex data structures.
- [ ] **Definition**: `Point: struct { x: int; y: int; }`
- [ ] **Allocation**: `p: own Point = alloc Point;`
- [ ] **Field Access**: `p.x = 10;` (Auto-dereference for `own` pointers)
- [ ] **Pass-by-Value**: Structs on stack.
- [ ] **Pass-by-Reference**: `ref Point`.

### 2. Missing Control Flow
Essential control flow statements are currently missing.
- [ ] **Break/Continue**: `break` and `continue` keywords for loops.
- [ ] **Else If**: Syntactic sugar for `else { if ... }`.
- [ ] **Fallthrough**: logic for `match` (optional, or explicit `fallthrough`).

### 3. Missing Operators
Standard operators expected in a systems language.
- [ ] **Bitwise Operators**: `&`, `|`, `^`, `~`, `<<`, `>>`.
- [ ] **Compound Assignment**: `+=`, `-=`, `*=`, `/=`.
- [ ] **Increment/Decrement**: `++`, `--` (Pre/Post).
- [ ] **Modulo**: `%` operator.

### 4. Basic Arrays
Fixed-size arrays.
- [ ] **Stack Arrays**: `arr: [5] int;`
- [ ] **Heap Arrays**: `arr: own [5] int;`
- [ ] **Bounds Checking**: Compile-time (where possible) and Runtime checks.

---

## MEDIUM TERM (Refinement)

### 1. Standard Library Expansion
- [ ] **Strings**: Basic string manipulation (concat, substring, length) in `std.string`.
- [ ] **File I/O**: `fopen`, `fread`, `fwrite` wrappers in `std.io`.
- [ ] **Math**: Common math functions in `std.math`.

### 2. Compiler Polish
- [ ] **Error Recovery**: specific error messages for missing semicolons, etc.
- [ ] **Warnings**: Unused variables, unreachable code.
- [ ] **CLI**: Improved argument parsing and help messages.

---

## FAR (Future Goals)

### 1. Advanced Type System
- [ ] **Generics**: `List<T>`, `Map<K, V>`.
- [ ] **Enums**: Tagged unions with payloads (e.g., `enum Shape { Circle(int), Rect(int, int) }`).
- [ ] **Methods**: Syntax to attach functions to types (e.g., `p.move(1, 1)`).
- [ ] **Interfaces/Traits**: Shared behavior for types.

### 2. Dynamic Collections
- [ ] **Vector**: Growable array `Vector<T>`.
- [ ] **HashMap**: Key-value store.
- [ ] **String Builder**: Mutable string buffer.

### 3. Developer Experience
- [ ] **Language Server (LSP)**: Go-to-definition, Rename, Find References.
- [ ] **Debugger**: Source map support for GDB/LLDB.
- [ ] **Package Manager**: Semantic versioning and dependency resolution.
- [ ] **Self-Hosting**: Rewrite the compiler in Lync.

---

## Known Bugs / Tech Debt
- **Parser**: Large monolithic functions need splitting.
- **Memory**: Compiler currently leaks memory (AST nodes not freed). Use an arena allocator.
- **Testing**: Need automated regression test suite for all language features.
