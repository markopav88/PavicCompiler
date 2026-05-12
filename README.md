# PavicCompiler
Markos Compiler built in C++ 17!!!

## Overview

PavicCompiler is a course compiler project for the Markos language. The compiler pipeline includes:

- **Frontend**: lexer, parser, CST/AST construction, and semantic analysis (scope, type, and usage checks).
- **IR Optimization**: AST-level optimizations (constant folding, propagation, dead code elimination, and bounded loop unrolling).
- **Backend**: 6502 code generation for the class VM/OS model.

The backend is intentionally constrained to the **Alan 6502 instruction subset** used in the project spec (for example `LDA`, `STA`, `ADC`, `LDX`, `LDY`, `CPX`, `BNE`, `INC`, `SYS`, `BRK`) and targets a compact 256-byte style memory image.

## Language Grammar

```text
Program ::== Block $

Block ::== { StatementList }

StatementList ::== Statement StatementList
               ::== ε

Statement ::== PrintStatement
          ::== AssignmentStatement
          ::== VarDecl
          ::== WhileStatement
          ::== IfStatement
          ::== Block

PrintStatement ::== print ( Expr )

AssignmentStatement ::== Id = Expr

VarDecl ::== type Id

WhileStatement ::== while BooleanExpr Block

IfStatement ::== if BooleanExpr Block

Expr ::== IntExpr
     ::== StringExpr
     ::== BooleanExpr
     ::== Id

IntExpr ::== digit intop Expr
        ::== digit

StringExpr ::== " CharList "

BooleanExpr ::== ( Expr boolop Expr )
           ::== boolval

Id ::== char

CharList ::== char CharList
        ::== space CharList
        ::== ε
```

- Curly braces denote scope.
- `=` is assignment.
- `type ::== int | string | boolean`
- `char ::== a | b | c ... z`
- `space ::== the space character`
- `digit ::== 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9`
- `boolop ::== == | !=`
- `boolval ::== false | true`
- `intop ::== +`
- `==` is test for equality.
- Comments are bounded by `/*` and `*/` and ignored by the lexer.

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

## Showcase CLI Tests

Use these two tests to quickly demonstrate the full compiler flow on meaningful programs.

### 1) Mixed Features Pipeline Test

File:

```text
testcases/marko test final/10_mixed_features.markos
```

Run:

```bash
./build/pavicc "testcases/marko test final/10_mixed_features.markos"
```

What it shows:
- integer declarations/assignments
- `while` loop with arithmetic updates
- boolean assignment from comparison
- conditional branch on boolean value
- string variable printing after control flow

### 2) String Reassignment Demo (World Cup Winner)

File:

```text
testcases/marko test final/11_world_cup_winner.markos
```

Run:

```bash
./build/pavicc "testcases/marko test final/11_world_cup_winner.markos"
```

What it shows:
- string declaration and assignment
- string reassignment (`"england"` -> `"croatia"`)
- string print before/after reassignment
- string equality check against `"croatia"`

## Optimization and Auto-Fix Stages

### Lexer + Parser: advanced error detection with fix-it rewrites

The frontend supports conservative source rewrites so we do more than report an error:
- detect common syntax issues early
- attach `SUGGESTED FIX` rewrites in diagnostics
- when `--fix` is enabled, automatically apply safe rewrites and re-run lex/parse

Goal:
- recover from common mistakes without changing intended program meaning

Run with automatic fix attempts:

```bash
./build/pavicc --fix <source-file>
```

Run without auto-fix (report-only mode):

```bash
./build/pavicc <source-file>
```

### Semantic analysis: AST optimization phase

After type checking, the compiler rewrites AST into a more efficient equivalent form using:
- constant folding
- constant propagation
- dead code elimination
- bounded loop unrolling

This optimization phase runs in normal compilation; use the same command:

```bash
./build/pavicc <source-file>
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

