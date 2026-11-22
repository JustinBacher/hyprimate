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
IFS=$' \t\n' read -r -d '' -a styles <<<"${CHECK_STYLES-liquid paper cloth swarm bubble smoke burn}" || true
(( ${#styles[@]} > 0 )) || { echo "CHECK_STYLES selects no styles" >&2; exit 1; }
workspace_checked=0
for style in "${styles[@]}"; do
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
    compositor_log=${XDG_RUNTIME_DIR:-/tmp}/hypr/$instance/hyprland.log
    [[ -f $nested_log && -r $nested_log && -f $compositor_log && -r $compositor_log ]] || {
        echo "$style: nested compositor logs are missing or unreadable" >&2; exit 1;
    }
    hc() { hyprctl -i "$instance" "$@"; }
    output_ready() { hc monitors -j | jq -e 'any(.[]; .width > 0 and .height > 0 and .name != "FALLBACK")' >/dev/null; }
    for _ in {1..20}; do output_ready && break; sleep .1; done
    if ! output_ready; then
        # A nested backend can start IPC before announcing its first Wayland output.
        # Ask this disposable instance for an output; do not map a client to a 0x0 fallback.
        result=$(hc output create wayland hyprimate-test)
        [[ $result == ok ]] || { echo "$style: could not create nested output: $result" >&2; exit 1; }
        for _ in {1..50}; do output_ready && break; sleep .1; done
        output_ready || { echo "$style: no usable nested Wayland output" >&2; exit 1; }
    fi
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
        # Cross the native capture/renderer seam for each workspace presentation.
        for study in rotation planet_hop cube:solid cube:inside_out; do
            workspace_style=${study%%:*}
            options=''
            if [[ $study == cube:* ]]; then
                options=", options = { cube = { variant = \"${study#*:}\" } }"
            fi
            result=$(hc eval "hl.config({ plugin = { hyprimate = { workspace = { enabled = true, style = \"$workspace_style\" $options } } } })")
            [[ $result == ok ]] || { echo "$workspace_style settings rejected: $result" >&2; exit 1; }
            for workspace in 2 1; do
                result=$(hc dispatch "hl.dsp.focus({ workspace = $workspace })")
                [[ $result == ok ]] || { echo "workspace $workspace switch failed: $result" >&2; exit 1; }
                sleep 1
                [[ $(hc activeworkspace -j | jq -r '.id') == "$workspace" ]] || {
                    echo "nested compositor did not switch to workspace $workspace" >&2; exit 1;
                }
            done
        done
        result=$(hc dispatch "hl.dsp.focus({ window = \"address:$address\" })")
        [[ $result == ok ]] || { echo "could not refocus nested kitty: $result" >&2; exit 1; }
    fi
    hc dispatch 'hl.dsp.window.close()' >/dev/null
    sleep 2
    kill -0 "$runner" 2>/dev/null || { cat "$log" >&2; exit 1; }
    [[ $(hc clients -j | jq --arg a "$address" '[.[] | select(.address == $a)] | length') == 0 ]] || { echo "$style: window did not close" >&2; exit 1; }
    # Exercise teardown while the driver, work-buffer pool and output are still live.
    # A process exit alone cannot distinguish safe cleanup from context destruction.
    plugin=${HYPRIMATE_PLUGIN:-$root/build/hyprimate.so}
    result=$(hc plugin unload "$plugin")
    [[ $result == ok ]] || { echo "$style: plugin unload failed: $result" >&2; exit 1; }
    hc monitors -j | jq -e 'any(.[]; .width > 0 and .height > 0)' >/dev/null
    result=$(hc plugin load "$plugin")
    [[ $result == ok ]] || { echo "$style: plugin reload failed: $result" >&2; exit 1; }
    echo "$style: unloaded and reloaded with a live output"
    # The compositor buffers its file log. Stop it before inspecting presentation
    # evidence or GL errors, so a quiet final frame cannot go unobserved.
    kill "$runner"
    wait "$runner" || true
    runner=''
    [[ -f $nested_log && -r $nested_log && -f $compositor_log && -r $compositor_log ]] || {
        echo "$style: nested compositor logs are missing or unreadable" >&2; exit 1;
    }
    grep_status=0
    grep -Ei 'GL_INVALID_|OpenGL error|GL error|failed to (compile|link).*shader' "$nested_log" "$compositor_log" || grep_status=$?
    if (( grep_status == 0 )); then
        echo "$style: GL error in nested compositor log" >&2
        exit 1
    elif (( grep_status != 1 )); then
        echo "$style: failed to read nested compositor logs" >&2
        exit 1
    fi
    if (( ! workspace_checked )); then
        # The log uses composition identities, not workspace numbers: empty workspaces
        # can be destroyed and recreated. Pair each accepted request with its actual draw.
        awk '
            function field(key, i) { for (i=1; i<=NF; i++) if (index($i, key "=")==1) return substr($i, length(key)+2); return "" }
            /\[hyprimate\] workspace request / && field("accepted")=="true" {
                if (pending) exit 1
                monitor=field("monitor"); src=field("before"); dst=field("after"); pending=1
            }
            /\[hyprimate\] workspace presented / && pending && field("monitor")==monitor &&
                field("source")==src && field("destination")==dst && field("style")==((count<2) ? "rotation" : (count<4) ? "planet_hop" : "cube") {
                pending=0; count++
            }
            END { if (count!=8 || pending) { print "expected eight request-matched workspace presentations; found " count+0 > "/dev/stderr"; exit 1 } }
        ' "$compositor_log"
        echo "rotation, planet_hop and both cube variants: plugin presented 1 -> 2 -> 1"
        workspace_checked=1
    fi
    echo "$style: open, move and close passed"
done
