# Lync v0.2.0 Release Notes

**Release Date:** February 13, 2026

This release introduces a major new feature — the **module system** — along with I/O capabilities, the `char` type, critical bug fixes, and a complete installation system for easy deployment.

---

## 🎉 What's New

### Module System with `include` Statement

Lync now supports a Java/C#-style module system for organizing and importing functionality:

```c
include std.io.*;              // Import all I/O functions
include std.io.read_int;       // Import specific function
```

**Key Features:**
- **Selective imports** — Only generate C code for what you actually use (zero bloat)
- **Compile-time validation** — Importing from non-existent modules or using unimported functions triggers errors
- **Clean code organization** — Logical namespacing of built-in functions
- **Wildcard and specific imports** — Use `*` for everything or name specific functions

**Example:**
```c
include std.io.read_int;

def main(): int {
    print("Enter a number:");
    num: own? int = read_int();

    match num {
        some(n): {
            print("You entered:", n);
            free n;
        }
        null: {
            print("Invalid input");
        }
    };

    return 0;
}
```

### New `std.io` Module - Input/Output Functions

Five new I/O functions for reading user input:

| Function | Return Type | Description |
|----------|-------------|-------------|
| `read_int()` | `own? int` | Read an integer from stdin |
| `read_str()` | `own? string` | Read a string from stdin |
| `read_bool()` | `own? bool` | Read a boolean from stdin |
| `read_char()` | `own? char` | Read a character from stdin |
| `read_key()` | `own? char` | Read a single keypress without echo |

**All I/O functions return nullable types** because input can fail (EOF, invalid format). You must unwrap using `match` or `if(some())`.

### New `char` Type

Added a new primitive type for single characters:
- Maps to C `char` in generated code
- Supported in variables, function parameters, and `print` statements
- Integrated with ownership system (`own char`, `ref char`, `own? char`)

**Note:** Character literals (`'A'`) are not yet implemented — use `read_char()` for now.

### Installation System

**Windows users can now install Lync system-wide!**

The compiler includes professional installers that:
- Copy `lync.exe` to `C:\Program Files\Lync\`
- Add Lync to your system PATH automatically
- Allow you to run `lync` from any directory

**Quick Install:**
```powershell
# Right-click PowerShell, select "Run as Administrator"
.\install.ps1
```

After installation:
```bash
# From anywhere:
lync myprogram.lync
lync run myprogram.lync
```

See [INSTALL.md](INSTALL.md) for full instructions.

---

## 🐛 Critical Bug Fixes

### Fixed: Use-After-Free Crash in Codegen
- **Problem:** FuncTable was freed at the end of analyzer, but codegen tried to access `resolved_sign` pointers into that freed memory
- **Symptom:** Segmentation fault when compiling programs
- **Solution:** FuncTable now persists until after codegen completes

### Fixed: Uninitialized Symbol Fields Causing Crashes
- **Problem:** `declare()` function didn't initialize `owner` and `is_dangling` fields in Symbol struct, leaving garbage memory
- **Symptom:** Random crashes when processing ref variables or complex programs
- **Solution:** All Symbol fields now explicitly initialized during declaration

### Fixed: Function Name Mangling Mismatch
- **Problem:** Function declarations used `get_type_signature()` with trailing underscore, but calls used `get_mangled_name()` without it
- **Symptom:** "Undeclared function" errors for functions with no parameters (e.g., `add(): int` became `add_int_()` in declaration but was called as `add_int()`)
- **Solution:** Use `get_mangled_name()` consistently for both declarations and calls

---

## 🔧 Language Improvements

### Nullable Ownership Relaxation
Nullable `own` variables (`own? int`) are now allowed to go out of scope without being freed:

```c
maybe: own? int = read_int();  // OK - can go out of scope
// No need to free if it might be null

// But if proven non-null, still must free:
if (some(maybe)) {
    free maybe;  // Required
}
```

### Enhanced AST Structure
- Added `IncludeStmt`, `ImportList`, and `Program` AST structures
- Added `is_nullable` field to `Expr` for tracking nullable function returns
- `parseProgram()` now returns `Program*` containing both imports and functions

### Analyzer Enhancements
- Import registry tracks which functions are imported
- Validates import usage in function calls
- Marks I/O function returns as nullable automatically
- Properly tracks ref variable ownership

### Codegen Improvements
- Conditional helper generation (only emit imported functions)
- Fixed nullable pointer assignments
- Extensive trace logging for debugging

---

## 📝 Breaking Changes

### Keyword Change: `using` → `include`

**Old syntax (v0.1.0):**
```c
using std.io.*;
```

**New syntax (v0.2.0):**
```c
include std.io.*;
```

All existing code using `using` must be updated to use `include`.

### Function Signature Changes

Several internal functions now accept `Program*` instead of separate arrays:
- `analyze_program(Program*)` (was `analyze_program(Func**, int)`)
- `generate_code(Program*, FILE*)` (was `generate_code(Func**, int, FILE*)`)

This only affects internal compiler code, not user programs.

---

## 📦 What's Included

### New Files
- `install.ps1` - PowerShell installer for Windows
- `install.bat` - Batch file installer for Windows
- `uninstall.ps1` - PowerShell uninstaller
- `uninstall.bat` - Batch file uninstaller
- `INSTALL.md` - Comprehensive installation guide
- `INSTALLER_README.txt` - Quick installation reference

### Updated Files
- `README.md` - Full rewrite with module system documentation
- `CHANGELOG.md` - Detailed change history for v0.2.0 and v0.1.0
- All test files - Updated to use `include` instead of `using`

---

## 🚀 Getting Started

### Build from Source
```bash
cmake -B build
cmake --build build
```

### Install (Windows)
```powershell
# As Administrator:
.\install.ps1
```

### First Program with Modules
```c
include std.io.read_int;

def main(): int {
    print("Enter two numbers:");

    a: own? int = read_int();
    b: own? int = read_int();

    match a {
        some(x): {
            match b {
                some(y): {
                    print("Sum:", x + y);
                }
                null: { print("Invalid second number"); }
            };
        }
        null: { print("Invalid first number"); }
    };

    free a;
    free b;
    return 0;
}
```

Save as `calculator.lync` and run:
```bash
lync run calculator.lync
```

---

## 🎯 Performance & Quality

### Code Quality
- ✅ Zero warnings in release build
- ✅ All memory leaks fixed
- ✅ Comprehensive error checking
- ✅ MSVC, GCC, and Clang compatible

### Compiler Size
- **Windows:** ~187 KB executable
- **Linux:** ~170 KB executable (estimated)

### Test Coverage
- ✅ Simple programs (arithmetic, functions)
- ✅ Complex programs (refs, ownership, nested calls)
- ✅ Module system (wildcard and specific imports)
- ✅ Nullable types (pattern matching, flow-sensitive unwrapping)
- ✅ I/O functions (all five tested)

---

## 🗺️ Roadmap

### Coming in v0.3.0 (Phase 5: Match Expansion)
- Range patterns (`1 to 10:`)
- Boolean patterns
- Exhaustiveness checking
- Unreachable branch warnings

### Future Versions
- **v0.4.0** - Structs with field access and auto-dereferencing
- **v0.5.0** - Arrays (fixed-size stack arrays)
- **v1.0.0** - Stable language with all core features

See [README.md](README.md) for full roadmap.

---

## 📚 Documentation

- **[README.md](README.md)** - Complete language reference
- **[INSTALL.md](INSTALL.md)** - Installation guide with troubleshooting
- **[CHANGELOG.md](CHANGELOG.md)** - Full version history
- **[INSTALLER_README.txt](INSTALLER_README.txt)** - Quick install reference

---

## 🙏 Acknowledgments

Built with C (C23 standard) and zero external dependencies.

Lync compiles to readable, standard C that works with any C compiler (GCC, Clang, MSVC).

---

## 📄 License

MIT License - See [LICENSE](LICENSE) for details.

---

## 🔗 Links

- **Repository:** [GitHub - TheHeliPilot/Lync](https://github.com/TheHeliPilot/Lync) *(update with actual URL)*
- **Issues:** Report bugs or request features
- **Documentation:** See README.md

---

**Upgrade to v0.2.0 today and start building safer programs with Lync's module system and I/O capabilities!** 🚀
