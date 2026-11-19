#!/usr/bin/env bash
# Exercise the production shader path in a disposable nested compositor.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
for tool in hyprctl jq kitty; do
    command -v "$tool" >/dev/null || { echo "missing required tool: $tool" >&2; exit 1; }
done
log=$(mktemp)
nested_log=${HYPRIMATE_NESTED_LOG:-$(mktemp)}
runner=''
cleanup() {
    [[ -z $runner ]] || { kill "$runner" 2>/dev/null || true; wait "$runner" 2>/dev/null || true; }
    rm -f "$log"
    [[ -n ${HYPRIMATE_NESTED_LOG:-} ]] || rm -f "$nested_log"
}
trap cleanup EXIT
workspace_checked=0
for style in ${CHECK_STYLES:-liquid paper cloth swarm bubble smoke}; do
    : >"$log"
    HYPRIMATE_NESTED_LOG="$nested_log" "$root/tests/nested/run.sh" --style "$style" --no-build --no-window >"$log" 2>&1 &
    runner=$!
    instance=''
    for _ in $(seq 100); do
        kill -0 "$runner" 2>/dev/null || { cat "$log" >&2; exit 1; }
        instance=$(awk '/^  instance=/ {sub(/^  instance=/, ""); print; exit}' "$log")
        [[ -n $instance ]] && break
        sleep .1
    done
    [[ -n $instance ]] || { echo "$style: nested compositor did not become ready" >&2; cat "$log" >&2; exit 1; }
    hc() { hyprctl -i "$instance" "$@"; }
    hc dispatch 'hl.dsp.exec_cmd("kitty")' >/dev/null
    address=''
    for _ in $(seq 100); do
        kill -0 "$runner" 2>/dev/null || { cat "$log" >&2; exit 1; }
        address=$(hc clients -j | jq -r 'first(.[] | select(.class == "kitty" and .mapped) | .address) // empty')
        [[ -n $address ]] && break
        sleep .1
    done
    if [[ -z $address ]]; then
        echo "$style: kitty did not map within 10 seconds (instance $instance)" >&2
        hc clients -j >&2 || true
        cat "$log" >&2
        tail -n 80 "$nested_log" >&2 || true
        exit 1
    fi
    result=$(hc dispatch "hl.dsp.focus({ window = \"address:$address\" })")
    [[ $result == ok ]] || { echo "$style: could not focus kitty: $result" >&2; exit 1; }
    sleep 2
    hc dispatch 'hl.dsp.window.float({ action = "toggle" })' >/dev/null
    before_pos=$(hc clients -j | jq -r --arg a "$address" '.[] | select(.address == $a) | .at | @tsv')
    hc dispatch 'hl.dsp.window.move({ x = 120, y = 80, relative = true })' >/dev/null
    sleep 2
    after_pos=$(hc clients -j | jq -r --arg a "$address" '.[] | select(.address == $a) | .at | @tsv')
    [[ -n $before_pos && -n $after_pos && $before_pos != "$after_pos" ]] || { echo "$style: window did not move" >&2; exit 1; }
    if (( ! workspace_checked )); then
        # Exercise both production workspace renderers with a mapped window. Unlike
        # the offscreen material tests, this crosses the native capture and CShader seam.
        for workspace_style in rotation planet_hop; do
            result=$(hc eval "hl.config({ plugin = { hyprimate = { workspace = { enabled = true, style = \"$workspace_style\" } } } })")
            [[ $result == ok ]] || { echo "$workspace_style settings rejected: $result" >&2; exit 1; }
            for workspace in 2 1; do
                result=$(hc dispatch "hl.dsp.focus({ workspace = $workspace })")
                [[ $result == ok ]] || { echo "workspace $workspace switch failed: $result" >&2; exit 1; }
                sleep 1
                [[ $(hc activeworkspace -j | jq -r '.id') == "$workspace" ]] || {
                    echo "nested compositor did not switch to workspace $workspace" >&2; exit 1;
                }
            done
            echo "workspace $workspace_style: 1 -> 2 -> 1 passed"
        done
        workspace_checked=1
        result=$(hc dispatch "hl.dsp.focus({ window = \"address:$address\" })")
        [[ $result == ok ]] || { echo "could not refocus nested kitty: $result" >&2; exit 1; }
    fi
    hc dispatch 'hl.dsp.window.close()' >/dev/null
    sleep 2
    kill -0 "$runner" 2>/dev/null || { cat "$log" >&2; exit 1; }
    [[ $(hc clients -j | jq --arg a "$address" '[.[] | select(.address == $a)] | length') == 0 ]] || { echo "$style: window did not close" >&2; exit 1; }
    compositor_log=${XDG_RUNTIME_DIR:-/tmp}/hypr/$instance/hyprland.log
    [[ -f $compositor_log ]] || { echo "$style: missing nested compositor log: $compositor_log" >&2; exit 1; }
    if grep -Ei 'GL_INVALID_|OpenGL error|GL error|failed to (compile|link).*shader' "$nested_log" "$compositor_log"; then
        echo "$style: GL error in nested compositor log" >&2
        exit 1
    fi
    kill "$runner" 2>/dev/null || true
    wait "$runner" 2>/dev/null || true
    runner=''
    echo "$style: open, move and close passed"
done
