# PavicCompiler
Markos Compiler built in C++ 17

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Run

```bash
./build/pavicc <source-file>
./build/pavicc -q <source-file>   # quiet mode (no verbose lexer trace)
```

Verbose lexer trace is enabled by default.

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

