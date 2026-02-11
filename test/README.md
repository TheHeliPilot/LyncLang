# CMinusMinus Compiler Test Suite

This directory contains test files for the new error collection and reporting system.

## Test Files

### 1. `test_syntax_error.cmm`
**Purpose:** Test syntax error handling (parser)
**Expected behavior:** Should exit immediately on line 3 with error about missing semicolon
**Command:** `./CMinusMinus ../test/test_syntax_error.cmm`
**Expected output:**
```
[../test/test_syntax_error.cmm:4:5] parser:error: expected ; but found return
```

### 2. `test_multiple_semantic.cmm`
**Purpose:** Test multiple semantic error collection (analyzer)
**Expected behavior:** Should collect all 4 errors before exiting
**Command:** `./CMinusMinus ../test/test_multiple_semantic.cmm`
**Expected output:** All 4 errors with proper line:column locations

### 3. `test_ownership_errors.cmm`
**Purpose:** Test ownership system error detection
**Expected behavior:** Catch 'own' without alloc, use after free, double free
**Command:** `./CMinusMinus ../test/test_ownership_errors.cmm`
**Expected output:** 3-4 ownership-related errors with locations

### 4. `test_memory_leak.cmm`
**Purpose:** Test memory leak detection at function end
**Expected behavior:** Detect unfreed 'own' variable in main()
**Command:** `./CMinusMinus ../test/test_memory_leak.cmm`
**Expected output:** Memory leak error for 'ptr' in main
**Note:** Currently only checks function-level scope, not nested block scopes (known limitation)

### 5. `test_type_errors.cmm`
**Purpose:** Test various type checking errors
**Expected behavior:** Collect multiple type mismatch errors
**Command:** `./CMinusMinus ../test/test_type_errors.cmm`
**Expected output:** 4-5 type errors with proper locations

### 6. `test_lexer_errors.cmm`
**Purpose:** Test lexer error recovery with panic mode
**Expected behavior:** Should suggest && and || for single & and |
**Command:** `./CMinusMinus ../test/test_lexer_errors.cmm`
**Expected output:** Lexer errors with helpful suggestions

### 7. `test_match_errors.cmm`
**Purpose:** Test match expression error handling
**Expected behavior:** Detect missing default, type mismatches in branches
**Command:** `./CMinusMinus ../test/test_match_errors.cmm`
**Expected output:** 3 match-related errors

### 8. `test_successful.cmm`
**Purpose:** Test successful compilation with no errors
**Expected behavior:** Should compile successfully with no errors/warnings
**Command:** `./CMinusMinus ../test/test_successful.cmm -o ../test/test_successful.c`
**Expected output:**
```
Compilation successful.
```

### 9. `test_trace_mode.cmm`
**Purpose:** Test trace/debug mode output
**Expected behavior:** Should show detailed trace output during compilation
**Command:** `./CMinusMinus ../test/test_trace_mode.cmm -trace`
**Expected output:** Verbose trace output from lexer, parser, and analyzer

## Running Tests

From the build directory:
```bash
# Test syntax error
./CMinusMinus ../test/test_syntax_error.cmm

# Test multiple errors
./CMinusMinus ../test/test_multiple_semantic.cmm

# Test with trace mode
./CMinusMinus ../test/test_trace_mode.cmm -trace

# Test successful compilation with output
./CMinusMinus ../test/test_successful.cmm -o ../test/output.c
```

## Expected Error Format

All errors should be formatted as:
```
[filename:line:col] stage:severity: message
```

Example:
```
[../test/test_type_errors.cmm:7:19] analyzer:error: left side of '+' must be int, got bool

3 errors generated.
```

## Notes

- Syntax errors (parser) exit immediately after first error
- Semantic errors (analyzer) are collected and shown all at once
- Line and column numbers should be accurate
- The `-trace` flag enables runtime debug output without recompilation
