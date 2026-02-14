# Lync Project Roadmap

## Current Status: v0.3.1 (File Includes)

**Completed Features:**
- [x] **File Include System** (`include path.to.file.*`)
- [x] **Basic Arrays** (Stack & Heap, Initialization, Access)
- [x] **VS Code Plugin** (Autocompletion, Highlighting)
- [x] **Ownership System** (Core, Move Semantics)
- [x] **Nullable Types** (`own?`, `some`, `null`)
- [x] **Pattern Matching** (Values, Nullables, Wildcards)

---

## SOON (High Priority)

### 1. Structs (Composite Types)
**Next Immediate Task**
- [ ] **Definition**: `Point: struct { x: int; y: int; }`
- [ ] **Allocation**: `p: own Point = alloc Point;`
- [ ] **Field Access**: `p.x = 10;` (Auto-dereference).
- [ ] **Pass-by-Value/Ref**: Stack structs vs `ref` structs.

### 2. Templates & Macros (Replacing Generics)
*Adopt C++ style metaprogramming for compile-time power.*
- [ ] **Templates**: `def max<T>(a: T, b: T): T` (Compile-time Monomorphization).
- [ ] **Macros**: `#define` style constants and code replacement.
- [ ] **Conditional Compilation**: `#if DEBUG`, `#ifdef WINDOWS`.

### 3. Missing Control Flow & Operators
- [ ] **Break/Continue**: Loop control (currently missing).
- [ ] **Else If**: Syntactic sugar.
- [ ] **Bitwise Ops**: `&`, `|`, `^`, `<<`, `>>`.
- [ ] **Compound Assignments**: `+=`, `-=`, `*=`, `/=`.

---

## MEDIUM TERM (Refinement)

### 1. Compiler Polish & Error Recovery
*(Currently the compiler exits on the first error. expected behavior is to report multiple errors)*
- [ ] **Error Recovery**: Synchronize token stream to find multiple errors per run.
- [ ] **Suggestions**: "Did you mean..?" for typos.
- [ ] **Warnings**: Unused variables, unreachable code.

### 2. Standard Library
- [x] **Strings**: Basic string library (`concat`, `clone`, `substring`, etc.).
- [x] **Extern C**: `extern <header> { ... }` blocks for C interop.
- [ ] **File I/O**: Wrapper around C `stdio`.
- [ ] **Math**: Common math functions.

---

## FAR (Future Goals)

### 1. Advanced Type System
- [ ] **Traits/Interfaces**: Shared behavior definition.
- [ ] **Enums**: Tagged unions with payloads.
- [ ] **Methods**: `type.function` syntax.

### 2. Output Targets
- [ ] **WebAssembly**: Compile to Wasm.
- [ ] **Optimized IR**: LLVM or custom IR.

---

## Technical Debt (Internal Fixes)

### 1. Parser Refactoring
- [ ] **Split Monolithic Functions**: Break `parseExpr` and `parseStmt` into smaller, manageable functions.

### 2. Memory Management
- [ ] **Arena Allocator**: Usage of a region-based allocator to fix current memory leaks in the compiler.

### 3. Testing
- [ ] **Regression Suite**: Automated tests for all features.
