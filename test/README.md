# Lync Compiler Test Suite

This directory contains test files for the new error collection and reporting system.

## Test Files

### 1. `test_syntax_error.lync`
**Purpose:** Test syntax error handling (parser)
**Expected behavior:** Should exit immediately on line 3 with error about missing semicolon
**Command:** `./lync ../test/test_syntax_error.lync`
**Expected output:**
```
[../test/test_syntax_error.lync:4:5] parser:error: expected ; but found return
```

### 2. `test_multiple_semantic.lync`
**Purpose:** Test multiple semantic error collection (analyzer)
**Expected behavior:** Should collect all 4 errors before exiting
**Command:** `./lync ../test/test_multiple_semantic.lync`
**Expected output:** All 4 errors with proper line:column locations

### 3. `test_ownership_errors.lync`
**Purpose:** Test ownership system error detection
**Expected behavior:** Catch 'own' without alloc, use after free, double free
**Command:** `./lync ../test/test_ownership_errors.lync`
**Expected output:** 3-4 ownership-related errors with locations

### 4. `test_memory_leak.lync`
**Purpose:** Test memory leak detection at function end
**Expected behavior:** Detect unfreed 'own' variable in main()
**Command:** `./lync ../test/test_memory_leak.lync`
**Expected output:** Memory leak error for 'ptr' in main
**Note:** Currently only checks function-level scope, not nested block scopes (known limitation)

### 5. `test_type_errors.lync`
**Purpose:** Test various type checking errors
**Expected behavior:** Collect multiple type mismatch errors
**Command:** `./lync ../test/test_type_errors.lync`
**Expected output:** 4-5 type errors with proper locations

### 6. `test_lexer_errors.lync`
**Purpose:** Test lexer error recovery with panic mode
**Expected behavior:** Should suggest && and || for single & and |
**Command:** `./lync ../test/test_lexer_errors.lync`
**Expected output:** Lexer errors with helpful suggestions

### 7. `test_match_errors.lync`
**Purpose:** Test match expression error handling
**Expected behavior:** Detect missing default, type mismatches in branches
**Command:** `./lync ../test/test_match_errors.lync`
**Expected output:** 3 match-related errors

### 8. `test_successful.lync`
**Purpose:** Test successful compilation with no errors
**Expected behavior:** Should compile successfully with no errors/warnings
**Command:** `./lync ../test/test_successful.lync -o ../test/test_successful.c`
**Expected output:**
```
Compilation successful.
```

### 9. `test_trace_mode.lync`
**Purpose:** Test trace/debug mode output
**Expected behavior:** Should show detailed trace output during compilation
**Command:** `./lync ../test/test_trace_mode.lync -trace`
**Expected output:** Verbose trace output from lexer, parser, and analyzer

## Running Tests

From the build directory:
```bash
# Test syntax error
./lync ../test/test_syntax_error.lync

# Test multiple errors
./lync ../test/test_multiple_semantic.lync

# Test with trace mode
./lync ../test/test_trace_mode.lync -trace

# Test successful compilation with output
./lync ../test/test_successful.lync -o ../test/output.c
```

## Expected Error Format

All errors should be formatted as:
```
[filename:line:col] stage:severity: message
```

Example:
```
[../test/test_type_errors.lync:7:19] analyzer:error: left side of '+' must be int, got bool

3 errors generated.
```

## Notes

- Syntax errors (parser) exit immediately after first error
- Semantic errors (analyzer) are collected and shown all at once
- Line and column numbers should be accurate
- The `-trace` flag enables runtime debug output without recompilation
