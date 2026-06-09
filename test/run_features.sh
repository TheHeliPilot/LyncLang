#!/usr/bin/env bash
# Golden tests for the language features landed in the
# defer / break-continue / labeled-loops / else-if / compound-assign /
# bitwise / methods / operator-overload / pipe work.
#
# Each fixture under features/ is compiled, run, and its stdout diffed
# against the expected string (newlines shown as '|'). Exits non-zero on
# any mismatch so it can gate CI.
#
# Usage:  LYNC=build_win/lync.exe  bash test/run_features.sh
set -u
LYNC="${LYNC:-build_win/lync.exe}"
DIR="$(cd "$(dirname "$0")" && pwd)"
FEAT="$DIR/features"
pass=0; fail=0

run_case() {
  local name="$1"; local expected="$2"
  local src="$FEAT/$name.lync"
  local exe="$FEAT/$name.exe"
  if ! "$LYNC" "$src" -no-color >/dev/null 2>&1; then
    echo "FAIL $name (compile error)"; fail=$((fail+1)); return
  fi
  local got
  got=$("$exe" 2>&1 | tr -d '\r' | tr '\n' '|')
  rm -f "$exe" "$FEAT/$name.c"
  if [ "$got" = "$expected" ]; then
    echo "PASS $name"; pass=$((pass+1))
  else
    echo "FAIL $name"; echo "  expected: $expected"; echo "  got:      $got"; fail=$((fail+1))
  fi
}

run_case defer               "a|b|deferred-2|deferred-1|"
run_case defer_free          "allocated and will free at exit|"
run_case defer_call          "main body|cleanup 42|"
run_case defer_early_return  "start|returning early|cleanup|---|start|normal path|cleanup|"
run_case break_continue      "i 0|i 1|i 3|i 4|"
run_case labeled_loop        "ij 0 0|ij 1 0|ij 2 0|ij 3 0|"
run_case else_if             "-5 negative|0 zero|3 small|100 big|"
run_case compound_assign     "x 6|"
run_case bitwise             "and 2|or 7|xor 5|shl 12|shr 3|"
run_case bitwise_precedence  "shift_vs_add 6|and_then_eq true|mask 15|shl_or 5|"
run_case methods             "count 2|"
run_case operators           "c 4 6|"
run_case operators_unary     "neg -5 -7|"
run_case pipe                "r 11|"

echo ""
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
