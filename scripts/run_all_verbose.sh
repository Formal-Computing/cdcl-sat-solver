#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
echo '=== Build ==='
cmake -S . -B build -G Ninja
cmake --build build
echo
echo '=== All unit tests (full verbosity) ==='
ctest --test-dir build --verbose --output-on-failure
echo
echo '=== Every example: explanatory CDCL-MOMS trace ==='
shopt -s nullglob
for cnf in examples/*.cnf; do
  echo
  echo "----------------- $cnf -----------------"
  code=0
  ./build/sat_solver --verbose "$cnf" || code=$?
  if [[ "$code" != 10 && "$code" != 20 ]]; then
    echo "ERROR: $cnf exited $code"
    exit "$code"
  fi
  echo "Verified solver exit code: $code"
done
echo 'All examples completed.'
