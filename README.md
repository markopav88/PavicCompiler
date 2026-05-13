# PavicCompiler
Markos Compiler built in C++ 17!!!

## Overview

PavicCompiler is a compiler project written around the grammar Professor Labouseur created for the course. The compiler pipeline includes:

- **Frontend**: lexer, parser, CST/AST construction, and semantic analysis (scope, type, and usage checks).
- **IR Optimization**: AST-level optimizations (constant folding, propagation, dead code elimination, and bounded loop unrolling).
- **Backend**: targets 6502a machine code that executes on SvegOS.

The backend is intentionally constrained to the **Alan 6502 instruction subset** used in the project spec (for example `LDA`, `STA`, `ADC`, `LDX`, `LDY`, `CPX`, `BNE`, `INC`, `SYS`, `BRK`) and targets a compact 256-byte style memory image.

## Executable Image Layout (256 Bytes)

Behind the scenes, each compiled program is emitted as a single executable image with exactly 256 bytes of code and data.

- **Code (text section)**:
  - stores machine opcodes
  - execution begins at `0x00`
  - grows upward toward `0xFF`
- **Static area (stack-like region)**:
  - stores static variables and temporary slots (`int`, `boolean`, string pointers)
  - begins immediately after emitted code
  - grows upward toward `0xFF`
- **Heap (dynamic/reference region)**:
  - stores dynamic string bytes that static pointers refer to
  - begins at `0xFF`
  - grows downward toward `0x00`

Code generation enforces collision checks between the upward-growing static region and downward-growing heap region so programs fail safely when memory does not fit.

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
  src/cst.cpp src/parser.cpp src/ast.cpp src/ast_lower.cpp src/optimizer.cpp src/symbol_table.cpp \
  src/semantic_scope.cpp src/semantic_type.cpp src/semantic_usage.cpp \
  src/codegen/memory_layout.cpp src/codegen/code_buffer.cpp src/codegen/codegen.cpp src/multi_backend.cpp
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
./pavicc --target=6502 <source-file>
./pavicc --target=llvm-ir -o out.ll <source-file>
./pavicc --target=java -o GeneratedProgram.java <source-file>
./pavicc --target=typescript -o generated.ts <source-file>
```

Or, if built with CMake:

```bash
./build/pavicc <source-file>
./build/pavicc -q <source-file>
```

Verbose compiler traces (lexer, parser, semantic, and codegen) are enabled by default.

## Multi-Backend Targets

In addition to the default 6502 backend, the compiler can now emit:

- LLVM IR text (`--target=llvm-ir`)
- Java source (`--target=java`)
- TypeScript source (`--target=typescript`)

`6502` remains the default target when `--target` is omitted.

### What was added

- Added a backend selector in the CLI (`--target=...`) while keeping original 6502 behavior as the default path.
- Added a new multi-backend module:
  - `src/multi_backend.hpp`
  - `src/multi_backend.cpp`
- Added source generation backends that consume the same optimized AST:
  - LLVM IR text emitter
  - Java source emitter
  - TypeScript source emitter
- Added batch test automation for the final showcase suite:
  - `tests/run_marko_final_multibackend.sh`

### Compatibility and safety

- Existing 6502 flow is unchanged when `--target` is omitted.
- New backend output is opt-in only via `--target`.
- Semantic/type/scope checks still gate backend generation.

### LLVM -> JVM note

This repository now emits LLVM IR text, but direct LLVM-to-JVM bytecode conversion requires an external bridge toolchain (not bundled here). Java source and TypeScript source generation are fully integrated and runnable with `javac/java` and `tsc/node`.

Example (single test):

```bash
./build/pavicc --target=llvm-ir -o /tmp/program.ll "testcases/marko test final/11_world_cup_winner.markos"
./build/pavicc --target=java -o /tmp/GeneratedProgram.java "testcases/marko test final/11_world_cup_winner.markos"
./build/pavicc --target=typescript -o /tmp/generated.ts "testcases/marko test final/11_world_cup_winner.markos"
```

Run generated Java + TypeScript:

```bash
javac /tmp/GeneratedProgram.java
java -cp /tmp GeneratedProgram

tsc --target ES2020 --module commonjs /tmp/generated.ts --outDir /tmp
node /tmp/generated.js
```

Batch script for `marko test final` (generates LLVM IR + Java + TypeScript, and runs Java/TypeScript when toolchains exist):

```bash
./tests/run_marko_final_multibackend.sh
```

### Optional toolchain support (simple)

A **toolchain** is the external compiler/runtime pair used for a backend target.

- Java toolchain: `javac` + `java`
- TypeScript toolchain: `tsc` + `node`

How to use this option:

1. Generate target source from Markos:

```bash
./build/pavicc --target=java -o /tmp/GeneratedProgram.java "testcases/marko test final/11_world_cup_winner.markos"
./build/pavicc --target=typescript -o /tmp/generated.ts "testcases/marko test final/11_world_cup_winner.markos"
```

2. Compile/run with the matching toolchain:

```bash
javac /tmp/GeneratedProgram.java
java -cp /tmp GeneratedProgram

tsc --target ES2020 --module commonjs /tmp/generated.ts --outDir /tmp
node /tmp/generated.js
```

If a toolchain is not installed, 6502 still works normally and remains the default target.

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

Auto-fix coverage (current):
- unterminated block comments (`/* ...` -> add `*/`)
- unterminated or line-broken strings (insert missing `"`)
- glued type declarations (`inta` -> `int a`, `stringz` -> `string z`)
- case-normalized reserved words (`Print` -> `print`, etc.)
- malformed operators (`!` -> `!=`, stray `/` -> start `/*`)
- missing wrappers/tokens (missing `(` / `)` around `if`/`while`, missing `}` and trailing `$`)

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

## AI Assistance Reflection

AI tools were a major help throughout this project, especially while learning C++ and working through a steep compiler-development learning curve.

The biggest value areas were:
- **Code formatting and cleanup**: keeping code readable and consistent while iterating quickly.
- **Code explanation and learning support**: helping break down unfamiliar C++ and compiler concepts into understandable steps.
- **System design guidance**: helping reason about architecture decisions across lexer, parser, semantic analysis, optimization, and 6502 codegen.
- **Code Generation**: Cursor was also extremely helpful for building out stages such as Lex, Parse(majority done by myself), Semantic Analysis, and Code Gen. Cursor was also extremely helpful in explaining concepts and learning about how to work with LLVM and IR's. Lastly, it was good for building out the portability features for multiple backends such as Typescript and Java.

AI support also improved productivity during difficult debugging sessions (for example around codegen behavior and edge-case failures). Overall, the experience was strongly positive, and LLM-based tooling was genuinely helpful in delivering this project.

