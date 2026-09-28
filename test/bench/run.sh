#!/bin/bash
# Re-run the benchmarks documented in doc/md/50-benchmarks.md and
# print the results table. Best of twenty runs per cell (RUNS=… to change);
# gforth's run times vary widely from run to run, and fewer can miss its
# fastest.
#
# Requires: bash 5 (for its microsecond clock), ffsh built somewhere
# (set FFSH=… or pass --ffsh PATH), gforth, gforth-itc, gforth-fast on
# PATH. lua5.4 and python3 are optional; if present, "lua" and "py"
# columns are appended to the output.

set -euo pipefail

cd "$(dirname "$0")"

RUNS="${RUNS:-20}"
if [ -z "${EPOCHREALTIME:-}" ]; then
    echo "run.sh needs bash 5 or newer (\$EPOCHREALTIME)." >&2
    exit 1
fi

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

# Print the best wall-clock time of $RUNS runs, in milliseconds. The
# clock is $EPOCHREALTIME with its decimal separator (which follows the
# locale) dropped, i.e. microseconds; /usr/bin/time's 10 ms steps are
# too coarse for runs that take 100 ms.
bench() {
    local label="$1" file="$2" cmd="$3"
    local best=999999999999 start end i
    for ((i = 0; i < RUNS; i++)); do
        start=${EPOCHREALTIME/[.,]/}
        "$cmd" < "$file" > /dev/null 2>&1
        end=${EPOCHREALTIME/[.,]/}
        if (( end - start < best )); then best=$(( end - start )); fi
    done
    printf '%4d ' $(( (best + 500) / 1000 ))
}

HAS_LUA=0
HAS_PY=0
if command -v lua5.4 >/dev/null 2>&1; then HAS_LUA=1; fi
if command -v python3 >/dev/null 2>&1; then HAS_PY=1;  fi

# Header. Optional columns appended in order: lua, py.
header_fmt='%-18s | %4s | %4s | %4s | %4s'
header_args=("Workload (ms)" "ffsh" "g-itc" "g" "g-f")
sep='-------------------+------+------+------+------'
if [ "$HAS_LUA" = 1 ]; then
    header_fmt+=' | %4s'; header_args+=("lua"); sep+='+------'
fi
if [ "$HAS_PY" = 1 ]; then
    header_fmt+=' | %4s'; header_args+=("py");  sep+='+------'
fi
header_fmt+='\n'

# shellcheck disable=SC2059  # printf format intentionally built dynamically
printf "$header_fmt" "${header_args[@]}"
printf -- '%s\n' "$sep"

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
    if [ "$HAS_PY" = 1 ]; then
        printf '| '; bench "python3" "b$n.py" "/usr/bin/python3"
    fi
    echo
done
