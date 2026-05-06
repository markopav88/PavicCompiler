# PavicCompiler
Markos Compiler built in C++ 17

## Build

**Makefile (uses `g++ -std=c++17`, as required):**

```bash
make
```

**Direct fallback compile command (without `make`):**

```bash
g++ -std=c++17 -Wall -Wextra -pedantic -Isrc -o pavicc \
  src/main.cpp src/diagnostic.cpp src/source.cpp src/token.cpp src/lexer.cpp \
  src/cst.cpp src/parser.cpp src/ast.cpp src/ast_lower.cpp src/symbol_table.cpp \
  src/semantic_scope.cpp src/semantic_type.cpp src/semantic_usage.cpp \
  src/codegen/memory_layout.cpp src/codegen/code_buffer.cpp src/codegen/codegen.cpp
```

**CMake (optional):**

```bash
cmake -S . -B build
cmake --build build
```

## Run

```bash
./pavicc <source-file>              # after `make`
./pavicc -q <source-file>           # quiet mode (no verbose lexer trace)
```

Or, if built with CMake:

```bash
./build/pavicc <source-file>
./build/pavicc -q <source-file>
```

Verbose lexer trace is enabled by default.

## Quick Run

```bash
# Build (recommended)
make

# Or direct fallback build (no make)
g++ -std=c++17 -Wall -Wextra -pedantic -Isrc -o pavicc \
  src/main.cpp src/diagnostic.cpp src/source.cpp src/token.cpp src/lexer.cpp \
  src/cst.cpp src/parser.cpp src/ast.cpp src/ast_lower.cpp src/symbol_table.cpp \
  src/semantic_scope.cpp src/semantic_type.cpp src/semantic_usage.cpp \
  src/codegen/memory_layout.cpp src/codegen/code_buffer.cpp src/codegen/codegen.cpp

# Run on any source program provided 
./pavicc <source-file>

# Optional modes
./pavicc -q <source-file>
./pavicc --fix <source-file>
./pavicc -q --emulator -o <out.bin> <source-file>
./pavicc -q --intel-hex -o <out.hex> <source-file>
```

## Developer Test Commands

```bash
make test
make phase-tests
```

## Lexer Multi-Program Policy (Step 5)

- The lexer accepts multiple programs in one source file.
- Each `$` token closes the current program.
- After `$`, the next token (if any) is treated as the start of the next program.

Example sequence:

```text
{ print(1) } $ { print(2) } $
```

## Warnings and Errors

- Unterminated block comment (`/*` without closing `*/`) is a fatal lexer error.
- Missing final `$` for the current program at EOF is a warning.
- Warnings do not block parser-readiness; errors do.

## Parse Gate Rule

- If lexer `errorCount > 0`: stop and skip parser.
- If lexer `errorCount == 0`: report "ready for parser next milestone".

