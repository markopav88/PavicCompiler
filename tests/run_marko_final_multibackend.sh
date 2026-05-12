#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${ROOT_DIR}/build/pavicc"
TEST_DIR="${ROOT_DIR}/testcases/marko test final"
OUT_DIR="${ROOT_DIR}/build/multibackend-out"

mkdir -p "${OUT_DIR}"

if [[ ! -x "${BIN}" ]]; then
  echo "Missing ${BIN}. Build first: cmake --build build"
  exit 1
fi

echo "Running multi-backend generation for tests in: ${TEST_DIR}"
echo

for f in "${TEST_DIR}"/*.markos; do
  base="$(basename "${f}" .markos)"
  ll="${OUT_DIR}/${base}.ll"
  java_src="${OUT_DIR}/${base}.java"
  ts_src="${OUT_DIR}/${base}.ts"

  echo "=== ${base} ==="
  "${BIN}" -q --fix "${f}" >/dev/null
  "${BIN}" -q --target=llvm-ir -o "${ll}" "${f}" >/dev/null
  "${BIN}" -q --target=java -o "${java_src}" "${f}" >/dev/null
  "${BIN}" -q --target=typescript -o "${ts_src}" "${f}" >/dev/null
  echo "generated: ${ll}"
  echo "generated: ${java_src}"
  echo "generated: ${ts_src}"

  if command -v javac >/dev/null 2>&1 && command -v java >/dev/null 2>&1; then
    cp "${java_src}" "${OUT_DIR}/GeneratedProgram.java"
    javac "${OUT_DIR}/GeneratedProgram.java"
    java -cp "${OUT_DIR}" GeneratedProgram > "${OUT_DIR}/${base}.java.out"
    echo "java output: ${OUT_DIR}/${base}.java.out"
  else
    echo "java toolchain not found; skipping Java run"
  fi

  if command -v tsc >/dev/null 2>&1 && command -v node >/dev/null 2>&1; then
    tsc --target ES2020 --module commonjs "${ts_src}" --outDir "${OUT_DIR}" >/dev/null
    node "${OUT_DIR}/${base}.js" > "${OUT_DIR}/${base}.ts.out"
    echo "ts output: ${OUT_DIR}/${base}.ts.out"
  else
    echo "ts/node toolchain not found; skipping TypeScript run"
  fi

  if command -v lljvm-cc >/dev/null 2>&1; then
    echo "lljvm-cc detected. You can add JVM-bytecode-from-LLVM execution here."
  fi
  echo
done

echo "Done. Outputs in ${OUT_DIR}"

