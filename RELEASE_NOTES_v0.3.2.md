# Release 0.3.2

**Date:** 2026-02-14

## New Features

### Floating Point Support
- Added `float` (32-bit) and `double` (64-bit) data types.
- Support for floating-point literals (e.g., `3.14`, `1.5f`, `1.2e-3`).
- Automatic type promotion in arithmetic operations (`int` -> `float` -> `double`).
- Updated `print` builtin to support floating-point values.

### External Function Interface
- Added support for `extern` blocks to declare C functions.
- Syntax: `extern <header.h> { def func(...); }`
- Enables integration with standard C libraries (like `<math.h>`).

### VS Code Plugin (v0.3.2)
- Added syntax highlighting for `float`, `double`, and `extern`.
- Fixed minimap and syntax highlighting issues with `extern <...>` blocks.
- Added new snippets: `vard` (double declaration), `extern`, `externmath`.
- Updated diagnostics and completion items for new types.

## Bug Fixes
- Fixed parsing of `extern` blocks causing issues with subsequent code.
- Fixed `print` function handling of mixed types.

## Installation

1.  Download `lync.exe` and `lync.bat`.
2.  Run `install.bat` as Administrator.
3.  (Optional) Install `lync-language-0.3.2.vsix` in VS Code.
