#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
COMPILER="$ROOT_DIR/pavicc"

if [[ ! -x "$COMPILER" ]]; then
  echo "error: compiler binary not found at $COMPILER"
  echo "hint: run 'make' in repo root first."
  exit 1
fi

pass_count=0
fail_count=0

run_expect_fail() {
  local label="$1"
  local input="$2"
  local expected_substring="$3"

  local output_file
  output_file="$(mktemp)"

  "$COMPILER" -q "$input" >"$output_file" 2>&1
  local exit_code=$?

  if [[ $exit_code -eq 0 ]]; then
    echo "[FAIL] $label"
    echo "  expected non-zero exit code, got 0"
    echo "  output:"
    sed 's/^/    /' "$output_file"
    fail_count=$((fail_count + 1))
    rm -f "$output_file"
    return
  fi

  if ! rg -q --fixed-strings "$expected_substring" "$output_file"; then
    echo "[FAIL] $label"
    echo "  expected output to contain: $expected_substring"
    echo "  output:"
    sed 's/^/    /' "$output_file"
    fail_count=$((fail_count + 1))
    rm -f "$output_file"
    return
  fi

  echo "[PASS] $label"
  pass_count=$((pass_count + 1))
  rm -f "$output_file"
}

run_expect_fail "lexer/01_unterminated_block_comment" \
  "$ROOT_DIR/tests/phases/lexer/01_unterminated_block_comment.markos" \
  "unterminated block comment starting with"

run_expect_fail "lexer/02_newline_in_string" \
  "$ROOT_DIR/tests/phases/lexer/02_newline_in_string.markos" \
  "newline character is not allowed inside a string literal"

run_expect_fail "lexer/03_unexpected_character" \
  "$ROOT_DIR/tests/phases/lexer/03_unexpected_character.markos" \
  "unexpected character in source text"

run_expect_fail "parser/01_missing_right_paren_print" \
  "$ROOT_DIR/tests/phases/parser/01_missing_right_paren_print.markos" \
  "expected RightParen but found RightBrace"

run_expect_fail "parser/02_invalid_boolean_paren" \
  "$ROOT_DIR/tests/phases/parser/02_invalid_boolean_paren.markos" \
  "expected \`==\` or \`!=\` inside BooleanExpr"

run_expect_fail "parser/03_missing_rhs_expression" \
  "$ROOT_DIR/tests/phases/parser/03_missing_rhs_expression.markos" \
  "expected an expression (integer, string, boolean, identifier, or parenthesized comparison)"

run_expect_fail "semantic/01_undeclared_assignment" \
  "$ROOT_DIR/tests/phases/semantic/01_undeclared_assignment.markos" \
  "assignment to undeclared identifier"

run_expect_fail "semantic/02_assignment_type_mismatch" \
  "$ROOT_DIR/tests/phases/semantic/02_assignment_type_mismatch.markos" \
  "type mismatch in assignment to"

run_expect_fail "semantic/03_comparison_type_mismatch" \
  "$ROOT_DIR/tests/phases/semantic/03_comparison_type_mismatch.markos" \
  "operands of \`==\` must have the same type"

run_expect_fail "codegen/01_unsupported_string_storage" \
  "$ROOT_DIR/tests/phases/codegen/01_unsupported_string_storage.markos" \
  "variable type \`string\` is not supported for 6502 output yet"

run_expect_fail "codegen/02_unsupported_boolean_storage" \
  "$ROOT_DIR/tests/phases/codegen/02_unsupported_boolean_storage.markos" \
  "variable type \`boolean\` is not supported for 6502 output yet"

run_expect_fail "codegen/03_int_literal_out_of_range" \
  "$ROOT_DIR/tests/phases/codegen/03_int_literal_out_of_range.markos" \
  "integer literal out of range for 8-bit immediate"

echo
echo "Phase tests: $pass_count passed, $fail_count failed"

if [[ $fail_count -ne 0 ]]; then
  exit 1
fi

exit 0
