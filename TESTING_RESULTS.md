# Testing Results 

This is a quick write-up of what I tested during development and what I observed. Lots of tests are important! You can never have too many!(Maybe)

## Test Coverage Used

I ran tests from multiple folders so I could cover lexer/parser behavior, semantic checks, and backend/codegen behavior:

- `testcases/new_tests`: 141 files
- `testcases/marko_suite`: 22 files
- `testcases/marko test final`: 11 files
- `testcases/generated_full_suite`: 76 files
- `testcases/*.markos` (root): 4 files

## Typical Command Patterns

- Single test:

```bash
./build/pavicc --fix "<path-to-test>.markos"
```

- Quiet test (good for pass/fail sweeps):

```bash
./build/pavicc -q --fix "<path-to-test>.markos"
```

- Bulk run for generated suite:

```bash
for f in testcases/generated_full_suite/*.markos; do
  echo "=== $f ==="
  ./build/pavicc --fix "$f"
  echo
done
```

## Representative Results

### Lex stage behavior

- `lex_error_02_unknown_char_expr.markos` correctly fails in lex with unexpected character (`^`).
- When lex errors are present, parsing is not entered (unless `--fix` can repair and restart first).

### Parse stage behavior

- `parse_error_06_decl_with_assignment.markos` correctly fails in parse.
- CST is not printed when parse ends with errors.

### Semantic stage behavior

- `semantic_error_04_int_assigned_string.markos` correctly reports type mismatch (`int` assigned `string`).
- Semantic warnings (example: assigned-but-never-read) still allow progression when there are no semantic errors.

### Codegen behavior

- `11_world_cup_winner.markos` compiles successfully (warning-only output in quiet mode).

### Selected tests from `marko test final` and purpose

- `01_lex_invalid_char.markos`:
  - Purpose: verify lexer rejects invalid characters and reports location/hint clearly.
- `04_while_count.markos`:
  - Purpose: verify integer initialization, `while` loop condition checks, repeated arithmetic updates, and print sequencing.
- `06_boolean_assign.markos`:
  - Purpose: verify boolean values produced from comparisons and used in control flow.
- `08_string_reassign_compare.markos`:
  - Purpose: verify string assignment, reassignment, and equality checks after updates.
- `10_mixed_features.markos`:
  - Purpose: integrated pipeline check for declarations, assignment, loops, booleans, and print behavior in one program.
- `11_world_cup_winner.markos`:
  - Purpose: showcase string reassignment path (`"england"` -> `"croatia"`) and string comparison/print flow.

## Auto-Fix (`--fix`) Observations

`--fix` is useful for conservative repair of common source issues:

- missing quote/brace/`$`
- missing parentheses around `if` / `while` conditions
- common token mistakes (`!` -> `!=`, etc.)

Important note: one command run with `--fix` may internally restart lex/parse several times until no more suggested rewrites remain (bounded attempts).

## Phase-Gating Checks

Current behavior matches intended rules:

- If lex has errors, do not proceed to parse.
- If parse has errors, do not proceed to semantic.
- If semantic has errors, codegen should not run.

I also validated the semantic/codegen gate behavior after updating the driver logic and rebuilding the binary used for execution.

## Practical Notes

- Keep binary/build flow consistent:
  - If running `./build/pavicc`, rebuild with `cmake --build build`.
  - If running `./pavicc`, rebuild with `make`.
- Mismatching the binary and build system can make it look like a fix did not apply.

