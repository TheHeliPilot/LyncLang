# Changelog

All notable changes to the Lync compiler will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.2.0] - 2026-02-13

### Added

#### Module System with `using` Statement
- **Import syntax:** Java/C#-style `using` statements for selective imports
  - Wildcard imports: `using std.io.*;` imports all functions from a module
  - Specific imports: `using std.io.read_int;` imports only named functions
  - Multiple imports supported per file
- **Import validation:** Compile-time errors for:
  - Importing from non-existent modules
  - Using functions that haven't been imported
  - Importing non-existent functions from valid modules
- **`std.io` module** with five I/O functions:
  - `read_int()` → `own? int` — Read integer from stdin
  - `read_str()` → `own? string` — Read string from stdin
  - `read_bool()` → `own? bool` — Read boolean from stdin
  - `read_char()` → `own? char` — Read character from stdin
  - `read_key()` → `own? char` — Read single keypress without echo (platform-specific)
- **Conditional code generation:** Only imported functions emit C helper code (reduces output bloat)
- **Import registry:** Analyzer tracks imported functions and validates usage

#### char Type
- New `char` primitive type for single characters
- Maps to C `char` in generated code
- Supported in function signatures, variables, and `print` statements
- Added `CHAR_KEYWORD_T` token type and keyword recognition

#### Language Improvements
- **Nullable I/O returns:** All `std.io` functions return nullable types (can fail on invalid input/EOF)
- **Nullable ownership relaxation:** Nullable `own` variables (`own? int`) are now allowed to go out of scope without being freed (they might be null)
- **Parser enhancements:**
  - Added `UsingStmt`, `ImportList`, and `Program` AST structures
  - Added `is_nullable` field to `Expr` for tracking nullable function returns
  - `parseProgram()` now returns `Program*` containing both imports and functions
- **Analyzer enhancements:**
  - Import registry tracks imported functions across compilation
  - Marks I/O function returns as nullable
  - Validates import usage in function calls
  - Updated ownership checks to handle nullable owned variables

### Changed
- **Version:** Bumped from 0.1.0 to 0.2.0 (minor version for new feature)
- **`parseProgram` signature:** Changed from returning `Func**` to returning `Program*`
- **`analyze_program` signature:** Changed to accept `Program*` instead of separate function array and count
- **`generate_code` signature:** Changed to accept `Program*` for conditional helper generation
- **FuncTable lifecycle:** Now persists until after codegen (previously freed at end of analyzer)
  - Fixes use-after-free bug where `resolved_sign` pointers were dangling
  - Added TODO comment for proper memory management in future versions

### Fixed
- **Critical: Use-after-free crash in codegen**
  - Root cause: `analyze_program()` was freeing the FuncTable, but `resolved_sign` pointers in function call expressions pointed into the freed memory
  - When codegen tried to access `resolved_sign->name`, it crashed with segmentation fault
  - Debug output showed freed memory marked with `0xDDDDDDDD` (MSVC debug heap marker)
  - Solution: Commented out FuncTable cleanup until after codegen completes
- **Parser:** DOT_T token now properly recognized for module path parsing (`std.io.read_int`)
- **Deep copy of FuncSign:** Parameters array now properly deep-copied when storing function signatures
  - Prevents issues with pointer invalidation during array reallocation
- **MSVC compatibility:** Added `nullptr` macro definition for older C standards and MSVC

### Internal Changes
- Added extensive trace logging throughout codegen for debugging nested function calls
- Pre-allocate FuncTable capacity based on function count to prevent realloc pointer invalidation
- Added `stage_trace`, `stage_trace_enter`, and `stage_trace_exit` macros for hierarchical debug output
- Token type name function updated with new tokens (USING_T, CHAR_KEYWORD_T, DOT_T)

---

## [0.1.0] - 2026-02-11

### Added

#### Nullable Types with Pattern Matching
- **Nullable syntax:** `own? int`, `ref? int` for optional pointer types
- **Pattern matching unwrapping:**
  - `some(binding)` pattern extracts non-null value with type safety
  - `null` pattern handles null case
  - Match expressions and statements support nullable unwrapping
- **Flow-sensitive unwrapping:** `if(some(x))` proves x is non-null within the block
- **Compile-time safety:**
  - Using nullable without unwrapping is an error
  - Missing `null` branch in nullable match is an error
  - Assigning nullable to non-nullable variable is an error
- **Binding variables:** `some(val)` creates non-nullable reference in match branch
- **Ownership integration:** Nullable owned variables tracked correctly through match branches

#### Reference Ownership Tracking
- Compiler tracks which `own` variable each `ref` borrows from
- Compile-time errors for:
  - Using `ref` after owner is freed
  - Using `ref` after owner goes out of scope
  - Freeing a `ref` variable (only `own` can be freed)
  - Assigning non-owned variable to `ref`
- **Lifetime validation:** `ref` variables marked as dangling when owner is freed or out of scope

#### Print Statement
- Built-in `print` keyword for output
- Supports `int`, `bool`, and `string` types (variadic arguments)
- Type-checked at compile time
- Reserved keyword (cannot be used as variable name)
- Warns on empty `print()` calls

#### String Literals
- String literal support with double quotes
- Escape sequences: `\n`, `\t`, `\r`, `\\`, `\"`
- Compiles to `const char*` in C
- Currently limited to `print` usage

#### Enhanced Diagnostics
- **Error collector system:** Batches multiple analyzer errors before failing
- **ANSI color output:**
  - Red for errors
  - Yellow for warnings
  - Cyan for notes
  - Bold for file locations
- **Windows terminal support:** Auto-enables ANSI colors on Windows 10+
- **Error limit:** Maximum 20 errors before truncation
- **Severity levels:** MSG_ERROR, MSG_WARNING, MSG_NOTE with proper formatting
- **Source locations:** Line and column tracking for all errors

### Changed
- **Main function requirement:** `main` must return `int` and take no parameters
- **Error handling:** Parser exits immediately on syntax error; analyzer collects semantic errors
- **Trace output:** All debug/trace output goes to stderr (only success message to stdout)

### Fixed
- Scope handling: Function body block creates child scope correctly
- `check_function_cleanup` called on correct scope (block scope, not function scope)
- Pattern matching type validation for match expressions vs statements

### Internal
- Added `SourceLocation` struct for tracking error positions
- Implemented `ErrorCollector` with message batching
- Added `stage_error`, `stage_fatal`, `stage_warning`, `stage_note` macros
- Print AST and tokens only when `-trace` flag is enabled

---

## [0.0.1] - 2026-02-09 (Initial Implementation)

### Added
- **Core language features:**
  - Variables with explicit type annotations
  - Functions with forward declarations
  - `int`, `bool`, `void` primitive types
  - Arithmetic, comparison, logical, and unary operators
  - Control flow: `if`/`else`, `while`, `do-while`, `for` (range)
  - Pattern matching: `match` expressions and statements
  - Comments: single-line (`//`) and multi-line (`/* */`)

- **Ownership system:**
  - `own` qualifier for heap-allocated values
  - `ref` qualifier for borrowed references
  - `alloc` keyword for heap allocation
  - `free` keyword for explicit deallocation
  - Compile-time ownership state tracking (ALIVE, FREED, MOVED)
  - Memory leak detection
  - Double-free detection
  - Use-after-free detection
  - Move semantics for `own` function parameters

- **Compiler pipeline:**
  - Lexer with tokenization
  - Recursive descent parser with AST construction
  - Semantic analyzer with type checking and ownership validation
  - Code generator transpiling to readable C
  - Automatic C compiler detection (gcc, clang, MSVC)
  - Cross-platform support (Windows, macOS, Linux)

- **CLI features:**
  - `run` mode for compile-and-execute
  - `-o` flag for output name
  - `--emit-c` flag to keep intermediate C file
  - `-trace` flag for debug output
  - `-no-color` flag to disable ANSI colors

- **Error handling:**
  - File location tracking (line, column)
  - Descriptive error messages
  - Stage-based error reporting (lexer, parser, analyzer, codegen)

---

## Legend

- **Added** — New features
- **Changed** — Changes to existing functionality
- **Deprecated** — Soon-to-be removed features
- **Removed** — Removed features
- **Fixed** — Bug fixes
- **Security** — Vulnerability fixes
- **Internal** — Implementation details not visible to users
