#!/usr/bin/env bash
# Runs the widget benchmark across backends, modes, counts and renderers and
# writes a Markdown table plus the raw JSON lines.
#   scripts/bench-matrix.sh [build-dir] [out-dir]
# Set BACKENDS (default "x11 wayland"), RENDERERS (default "default ngl"),
# COUNTS (default "1000 10000"). On a real desktop session use BACKENDS=native.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
build=${1:-$here/../spike/build}
out=${2:-$here/../spike/out/bench}
mkdir -p "$out"
: >"$out/results.jsonl"

for be in ${BACKENDS:-x11 wayland}; do
  for renderer in ${RENDERERS:-default ngl}; do
    for mode in rnview fixed; do
      for n in ${COUNTS:-1000 10000}; do
        for u in 1 0.01; do
          env_args=()
          [[ "$renderer" != default ]] && env_args=(GSK_RENDERER="$renderer")
          cmd=(env "${env_args[@]}" "$build/widget-benchmark" --count "$n"
               --frames "${FRAMES:-120}" --mode "$mode" --update "$u")
          if [[ "$be" == native ]]; then
            "${cmd[@]}" 2>/dev/null | tail -1 >>"$out/results.jsonl"
          else
            "$here/run-headless.sh" "$be" -- "${cmd[@]}" 2>/dev/null |
              tail -1 >>"$out/results.jsonl"
          fi
          echo "done: $be $renderer $mode $n update=$u" >&2
        done
      done
    done
  done
done

python3 - "$out/results.jsonl" >"$out/results.md" <<'PY'
import json, sys
rows = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
print("| backend | renderer | mode | views | moved/frame | mount ms | mount→paint ms | frame p50 ms | frame p95 ms | layout ms | paint ms | RSS MB |")
print("|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
for d in rows:
    p = d["phase_mean_ms"]
    print(f"| {d['backend']} | {d['renderer'].replace('Gsk','').replace('Renderer','')} | {d['mode']} | {d['count']} "
          f"| {d['update']*100:g}% | {d['mount_ms']:.0f} | {d['mount_to_paint_ms']:.0f} "
          f"| {d['frame_work_ms']['p50']:.1f} | {d['frame_work_ms']['p95']:.1f} "
          f"| {p['layout']:.1f} | {p['paint']:.1f} | {d['rss_mounted_delta_kb']/1024:.0f} |")
PY
cat "$out/results.md"
