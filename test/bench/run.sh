#!/bin/bash
# Re-run the benchmarks documented in doc/md/50-benchmarks.md and
# print the results table. Best of five runs per cell.
#
# Requires: ffsh built somewhere (set FFSH=… or pass --ffsh PATH),
# gforth, gforth-itc, gforth-fast on PATH. lua5.4 is optional; if
# present, a "lua" column is appended to the output.

set -euo pipefail

cd "$(dirname "$0")"

# Locate ffsh: --ffsh override → FFSH env var → built example dir.
FFSH="${FFSH:-}"
while [ "${1:-}" != "" ]; do
    case "$1" in
        --ffsh) FFSH="$2"; shift 2 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done
if [ -z "$FFSH" ]; then
    for cand in ../../examples/ffsh/build/ffsh /tmp/ffsh-build/ffsh ./ffsh; do
        if [ -x "$cand" ]; then FFSH="$cand"; break; fi
    done
fi
if [ -z "$FFSH" ] || [ ! -x "$FFSH" ]; then
    echo "ffsh not found. Build it (cd examples/ffsh && cmake -B build && cmake --build build) and pass --ffsh path/to/ffsh." >&2
    exit 1
fi

bench() {
    local label="$1" file="$2" cmd="$3"
    local best=99999999
    for _ in 1 2 3 4 5; do
        local t
        t=$( { /usr/bin/time -f '%e' "$cmd" < "$file" >/dev/null; } 2>&1 )
        local ms
        ms=$(awk -v t="$t" 'BEGIN { printf "%d", t * 1000 }')
        if [ "$ms" -lt "$best" ]; then best=$ms; fi
    done
    printf '%4d ' "$best"
}

HAS_LUA=0
if command -v lua5.4 >/dev/null 2>&1; then HAS_LUA=1; fi

if [ "$HAS_LUA" = 1 ]; then
    printf '%-18s | %4s | %4s | %4s | %4s | %4s\n' \
           "Workload (ms)" "ffsh" "g-itc" "g" "g-f" "lua"
    printf -- '-------------------+------+------+------+------+------\n'
else
    printf '%-18s | %4s | %4s | %4s | %4s\n' \
           "Workload (ms)" "ffsh" "g-itc" "g" "g-f"
    printf -- '-------------------+------+------+------+------\n'
fi

declare -A names=(
    [b1]="empty loop"
    [b2]="sum"
    [b3]="fib(36)"
    [b4]="var r/m/w"
    [b5]="nested loops"
)

for n in 1 2 3 4 5; do
    printf '%-18s | ' "${names[b$n]}"
    bench "ffsh"        "b$n.ff" "$FFSH"             ; printf '| '
    bench "gforth-itc"  "b$n.gf" "/usr/bin/gforth-itc" ; printf '| '
    bench "gforth"      "b$n.gf" "/usr/bin/gforth"     ; printf '| '
    bench "gforth-fast" "b$n.gf" "/usr/bin/gforth-fast"
    if [ "$HAS_LUA" = 1 ]; then
        printf '| '; bench "lua" "b$n.lua" "/usr/bin/lua5.4"
    fi
    echo
done
