# Contributing

Thank you for your interest in Lync! This guide explains how to contribute to the language and compiler.

## Contribution Model

Lync is **managed by the core team**. While the source code is available, all changes go through design review and approval before implementation.

### What We Accept

**Bug Reports** - Help us find and fix issues  
**Feature Requests** - Suggest new language features  
**Documentation Improvements** - Fix typos, clarify explanations  
**Example Programs** - Share interesting Lync code  
**Code Contributions** - Must be approved by core team first

## Reporting Bugs

Found a bug? We want to know!

### Before Reporting

1. **Check existing issues** - Your bug may already be reported
2. **Update to latest version** - Bug might be fixed already
3. **Create minimal example** - Simplify to smallest reproducing code

### Bug Report Template

Open an issue on [GitHub](https://github.com/TheHeliPilot/lync/issues) with:

**Title:** Brief description (e.g., "Compiler crashes on nested match statements")

**Description:**
```markdown
## Bug Description
Clear description of the issue

## Steps to Reproduce
1. Create file with this code:
   ```lync
   // Minimal example here
   ```
2. Run: `lync file.lync`
3. Observe: [what happens]

## Expected Behavior
What should happen

## Actual Behavior
What actually happens

## Environment
- Lync version: 0.3.2
- OS: Windows 11 / Ubuntu 22.04 / macOS 14
- C Compiler: GCC 13.2 / Clang 16.0 / MSVC 2022

## Additional Context
Stack trace, error messages, etc.
```

### Example Bug Report

**Title:** Use-after-free not detected for array elements

**Description:**
```markdown
## Bug Description
The compiler doesn't detect use-after-free when accessing freed array elements.

## Steps to Reproduce
```lync
def main(): int {
    arr: own [3] own int;
    arr[0] = alloc 10;
    free arr[0];
    print(arr[0]);  // Should error, but doesn't
    return 0;
}
```

## Expected Behavior
Compiler error: "use of freed variable 'arr[0]'"

## Actual Behavior
Compiles successfully, runtime crash

## Environment
- Lync version: 0.3.2
- OS: Windows 11
- C Compiler: GCC 13.2
```

## Suggesting Features

Have an idea for Lync? We'd love to hear it!

### Feature Request Template

Open an issue with the "Feature Request" label:

```markdown
## Feature Description
Clear description of what you want

## Use Case
Why is this feature needed? What problem does it solve?

## Proposed Syntax
```lync
// Example of how it would work
```

## Alternatives Considered
Other ways to solve the same problem

## Impact
- Breaking change? Yes/No
- Affects: [parser/analyzer/codegen/etc.]
```

### Example Feature Request

**Title:** Add bitwise operators

**Description:**
```markdown
## Feature Description
Add bitwise AND, OR, XOR, and shift operators.

## Use Case
Low-level programming, bit manipulation, flag handling.

Example:
```lync
flags: int = FLAG_A | FLAG_B;
if (flags & FLAG_A) {
    print("Flag A is set");
}
```

## Proposed Syntax
- `&` - Bitwise AND
- `|` - Bitwise OR
- `^` - Bitwise XOR
- `<<` - Left shift
- `>>` - Right shift

## Alternatives Considered
- Using functions: `bitwise_and(a, b)` - Too verbose
- External C functions via `extern` - Workaround exists but not ergonomic

## Impact
- Breaking change: No
- Affects: Lexer (new tokens), Parser (operator precedence), Analyzer (type checking), Codegen (C translation)
```

## Documentation Improvements

Found a typo or unclear explanation? Submit a fix!

### What to Improve

- Fix typos and grammar
- Clarify confusing explanations
- Add missing examples
- Improve code samples
- Update outdated information

### How to Contribute Docs

1. **For Wiki Pages:** Open an issue describing the improvement
2. **For README:** Submit a pull request with changes
3. **For Comments:** Suggest inline in issues

## Example Programs

Share interesting Lync programs with the community!

### What We're Looking For

- Algorithm implementations
- Data structure examples
- Design pattern demonstrations
- Real-world use cases
- Educational examples

### How to Share

1. Create a Gist or paste code in an issue
2. Add description of what it demonstrates
3. Include comments explaining key parts

### Example Submission

**Title:** Binary Search Tree Implementation

**Code:**
```lync
// Binary search tree node
Node: struct {
    value: int;
    left: own? Node;
    right: own? Node;
}

def insert(root: ref? Node, value: int): void {
    // Implementation...
}

// Demonstrates:
// - Recursive data structures
// - Nullable pointers
// - Pattern matching
// - Memory management
```

## Code Contributions

Want to contribute code? Here's the process:

### Step 1: Discuss First

**Before writing code:**

1. Open an issue describing what you want to implement
2. Wait for core team approval
3. Discuss design approach
4. Get assigned to the issue

❌ **Do NOT submit PRs without prior approval**

### Step 2: Development

Once approved:

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Follow coding style (see below)
4. Write tests if applicable
5. Update documentation

### Step 3: Submission

1. Push to your fork
2. Open a Pull Request
3. Reference the original issue
4. Respond to code review feedback

### Coding Style

Follow existing code conventions:

**C Code Style:**
- 4 spaces for indentation (no tabs)
- K&R brace style
- Descriptive variable names
- Comments for complex logic

**Example:**
```c
// Good
void analyze_expression(Expr* expr) {
    if (expr->type == VAR_E) {
        Symbol* sym = lookup(scope, expr->as.var.name);
        if (sym == NULL) {
            add_error(ec, STAGE_ANALYZER, expr->loc,
                "undefined variable '%s'", expr->as.var.name);
        }
    }
}

// Avoid
void ae(Expr*e){Symbol*s=lookup(scope,e->as.var.name);if(!s){/* error */}}
```

**Lync Code Style:**
- Clear variable names
- Type annotations on all declarations
- Comments for non-obvious logic
- Consistent formatting

## Building from Source

### Prerequisites

- CMake 3.29+
- C Compiler (GCC 13+, Clang 16+, or MSVC 2022)
- Git

### Clone and Build

```bash
git clone https://github.com/TheHeliPilot/lync.git
cd lync

# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build

# Test
./build/lync --version
```

### Development Builds

```bash
# Debug build with symbols
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Enable trace mode
./build/lync -trace test.lync

# Keep generated C code
./build/lync --emit-c test.lync
cat test.c
```

## Testing

### Manual Testing

```bash
# Create test file
echo 'def main(): int { return 0; }' > test.lync

# Compile
./build/lync test.lync

# Run
./test
```

### Test Cases to Cover

When implementing features:

- Happy path (normal usage)
- Edge cases (empty, max size, etc.)
- Error cases (invalid input)
- Integration (works with other features)

### Example Test Scenarios

**For new operator:**
```lync
// Happy path
x: int = 5 & 3;  // Should work

// Type checking
x: int = 5 & true;  // Should error

// With variables
a: int = 5;
b: int = 3;
c: int = a & b;  // Should work

// In expressions
if ((flags & MASK) == 0) { }  // Should work
```

## Communication

### Where to Discuss

- **GitHub Issues** - Bug reports, feature requests
- **Pull Requests** - Code review discussions
- **GitHub Discussions** - General questions (if enabled)

### Response Times

- Bug reports: Usually within 1-2 days
- Feature requests: May take longer for design review
- Pull requests: Depends on complexity

## What We Don't Accept

**Unsolicited PRs** - Discuss in an issue first  
**Style changes only** - Must have functional improvement  
**Duplicate features** - Check existing issues first  
**Breaking changes** - Without strong justification  

## Recognition

Contributors will be:
- Mentioned in release notes (for significant contributions)
- Listed in CONTRIBUTORS.md (if we create one)
- Thanked in relevant issues/PRs

## Legal

By contributing, you agree that:
- Your contributions will be licensed under MIT
- You have the right to submit the contribution
- You understand the code may be modified by maintainers

## Questions?

Not sure about something? Ask!

- Open an issue with the "question" label
- Reach out to [@TheHeliPilot](https://github.com/TheHeliPilot)

## Related Pages

- [[Installation]] - Building from source
- [[Compiler Reference]] - Understanding the codebase
- [[Architecture]] - High-level design
- [[Roadmap]] - Planned features
- [[Diagnostics]] - Error message format

---

Thank you for helping make Lync better!
