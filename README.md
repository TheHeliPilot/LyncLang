# Lync Programming Language

Lync is a systems programming language with manual memory management, compile-time ownership tracking, and pattern matching. It transpiles to C for maximum compatibility while providing modern safety features.

---

## Key Features

* **Manual Memory Control**: Explicit `alloc` and `free` for heap management.
* **Ownership System**: Zero-cost, compile-time tracking of resource ownership through `own` and `ref` qualifiers.
* **Safety Guarantees**: Prevents memory leaks, double frees, and use-after-free errors.
* **Nullable Types**: Safe handling with `?` and exhaustive pattern matching to prevent null pointer dereferences.
* **C Interoperability**: Transpiles to standard C23 for wide compatibility and allows calling C functions via `extern` blocks.

---

## Example Programs

### Hello World
```c
def main(): int {
    print("Hello, world!");
    return 0;
}
```

## Ownership in Action

```c
def main(): int {
    // Allocation on the heap
    ptr: own int = alloc 42;
    print("Value:", ptr);
    
    // Explicit deallocation
    free ptr;
    
    // The analyzer prevents use of 'ptr' after this point
    return 0;
}
```

## Pattern Matching with Nullable Types

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

    free num; // Owned nullables must be freed regardless of state
    return 0;
}
```

## Installation
Lync requires a C23-compatible compiler (GCC 13+, Clang 16+, or MSVC 2022+) and CMake 3.29+.

```bash
# Clone the repository
git clone [https://github.com/TheHeliPilot/lync.git](https://github.com/TheHeliPilot/lync.git)
cd lync

# Configure and build
cmake -B build
cmake --build build
```

## Compiler Tooling

* CLI: Provides a unified interface for compilation (lync file.lync) and execution (lync run file.lync).
* Diagnostics: Clear, colorized error messages identifying the compiler stage (lexer, parser, analyzer, or codegen).
* VS Code Extension: Official support for syntax highlighting, snippets, and real-time error checking.

## Resources
* [GitHub Repository](https://github.com/TheHeliPilot/lync)
* [Documentation wiki](https://www.google.com/search?q=https://github.com/TheHeliPilot/lync/wiki)
* [Issue tracker](https://github.com/TheHeliPilot/lync/issues)
