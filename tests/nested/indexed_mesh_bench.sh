#!/usr/bin/env bash
# A/B benchmark of the expanded vs indexed window-layer mesh draw in disposable nested Hyprlands.
#
#   tests/nested/indexed_mesh_bench.sh [--rounds N] [--style STYLE] [--seconds S] [--out DIR]
#       [--modes "off on"] [--parent-display NAME] [--expect-renderer TEXT]
#       [--expect-node /dev/dri/renderDN] [--expect-gpu INDEX]
#
# Each round starts one nested compositor per mode (off = expanded triangle list, on = unique
# vertices + indices, compare = indexed plus a per-frame pixel diff against the expanded draw),
# alternating the order between rounds when there are exactly two modes. In each it runs `hyprctl
# hyprimate:profile`, drives repeated open / move / close animations for S seconds, and saves the
# profiler report. Reports are raw text in DIR (default build/indexed-mesh-bench/<timestamp>);
# nothing is aggregated here, so the numbers can be read directly from the files.
#
# Which GPU renders is decided by the parent compositor the nested session attaches to (it renders
# on the parent's advertised DMA-BUF render node). --parent-display NAME attaches to a private
# parent's Wayland socket (e.g. a `kwin_wayland --virtual` started with KWIN_RENDER_NODES set)
# instead of the current session; it also disables run.sh's quiet-window rule, so nothing touches
# the live compositor. --expect-renderer TEXT aborts a session whose compositor log does not name a
# renderer containing TEXT (e.g. "5070 Ti"), so a run can never silently land on another card.
# Every session records its renderer/device lines, monitor geometry and workload counters beside
# the report so runs are only compared when those match.
set -euo pipefail

rounds=3 style=cloth seconds=20 out="" modes="off on" parent="" expect="" node="" gpu_index=""
expect_output=""
while (($#)); do
    case "$1" in
        --rounds) rounds=$2; shift 2 ;;
        --style) style=$2; shift 2 ;;
        --seconds) seconds=$2; shift 2 ;;
        --out) out=$2; shift 2 ;;
        --modes) modes=$2; shift 2 ;;
        --parent-display) parent=$2; shift 2 ;;
        --expect-renderer) expect=$2; shift 2 ;;
        --expect-node) node=$2; shift 2 ;;
        --expect-gpu) gpu_index=$2; shift 2 ;;
        --expect-output) expect_output=$2; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
read -r -a mode_list <<<"$modes"
for mode in "${mode_list[@]}"; do
    [[ $mode == off || $mode == on || $mode == compare ]] || { echo "bad mode: $mode" >&2; exit 2; }
done

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=${out:-$root/build/indexed-mesh-bench/$(date +%Y%m%d-%H%M%S)}
mkdir -p "$out"

# The private route must not mutate the live compositor: run.sh only adds its host window rule when
# HYPRIMATE_NESTED_FOCUS is not 1.
child_env=()
if [[ -n $parent ]]; then
    [[ -S ${XDG_RUNTIME_DIR:?XDG_RUNTIME_DIR is required}/$parent ]] || {
        echo "parent display socket not found: $XDG_RUNTIME_DIR/$parent" >&2; exit 1; }
    child_env=(WAYLAND_DISPLAY="$parent" HYPRIMATE_NESTED_FOCUS=1)
fi

run_mode() {
    local mode=$1 round=$2 tag=$1-$2
    local runner_log=$out/$tag.runner.log nested_log=$out/$tag.nested.log
    local env_value=$mode
    [[ $mode == off ]] && env_value=0
    env "${child_env[@]}" HYPRIMATE_INDEXED_MESH=$env_value HYPRIMATE_NESTED_LOG=$nested_log \
        "$root/tests/nested/run.sh" --style "$style" --no-build --no-window >"$runner_log" 2>&1 &
    local runner=$!
    local instance=""
    for _ in $(seq 100); do
        kill -0 "$runner" 2>/dev/null || { cat "$runner_log" >&2; return 1; }
        instance=$(awk '/^  instance=/ {sub(/^  instance=/, ""); print; exit}' "$runner_log")
        [[ -n $instance ]] && break
        sleep .1
    done
    [[ -n $instance ]] || { echo "$tag: nested compositor did not start" >&2; return 1; }
    hc() { hyprctl -i "$instance" "$@"; }

    # Provenance: which device rendered, and what it rendered to. Taken before any workload.
    local compositor_log=${XDG_RUNTIME_DIR:-/tmp}/hypr/$instance/hyprland.log
    {
        echo "parent_display=${parent:-${WAYLAND_DISPLAY:-}}"
        grep -a "Got node\|eglDeviceFromDRMFD\|Renderer:" "$compositor_log" || true
    } >"$out/$tag.device.txt"
    # Startup admission: never time a session whose output has not appeared yet. Bounded wait for
    # an active output (and the expected geometry when given); an empty monitor list is invalid.
    local ready=0
    for _ in $(seq 150); do
        hc monitors -j >"$out/$tag.monitors.json" 2>/dev/null || true
        if jq -e --arg want "$expect_output" 'any(.[]; .width > 0 and .height > 0 and
            .name != "FALLBACK" and ($want == "" or
            "\(.width)x\(.height)x\(.scale)" == $want))' "$out/$tag.monitors.json" \
            >/dev/null 2>&1; then
            ready=1
            break
        fi
        sleep .1
    done
    if ((!ready)); then
        echo "output not ready (wanted '${expect_output:-any}')" >"$out/$tag.INVALID"
        kill "$runner" 2>/dev/null || true
        wait "$runner" 2>/dev/null || true
        return 3
    fi
    if [[ -n $expect ]] && ! grep -aq "Renderer:.*$expect" "$out/$tag.device.txt"; then
        echo "$tag: compositor renderer does not match '$expect':" >&2
        cat "$out/$tag.device.txt" >&2
        kill "$runner" 2>/dev/null || true
        wait "$runner" 2>/dev/null || true
        return 1
    fi
    if [[ -n $node ]] && ! grep -aq "Got node $node" "$out/$tag.device.txt"; then
        echo "$tag: DMA-BUF node does not match '$node':" >&2
        cat "$out/$tag.device.txt" >&2
        kill "$runner" 2>/dev/null || true
        wait "$runner" 2>/dev/null || true
        return 1
    fi

    hc dispatch 'hl.dsp.exec_cmd("kitty")' >/dev/null
    sleep 3
    hc dispatch 'hl.dsp.window.float({ action = "toggle" })' >/dev/null
    sleep 1
    hc "hyprimate:profile" start >/dev/null
    local end=$((SECONDS + seconds)) n=0 opens=0
    while ((SECONDS < end)); do
        # Move out, back, then open and close a second window: exercises move, open and close.
        hc dispatch 'hl.dsp.window.move({ x = 160, y = 90, relative = true })' >/dev/null
        sleep 1.2
        hc dispatch 'hl.dsp.window.move({ x = -160, y = -90, relative = true })' >/dev/null
        sleep 1.2
        if ((n % 2 == 0)); then
            hc dispatch 'hl.dsp.exec_cmd("kitty")' >/dev/null
            sleep 2
            hc dispatch 'hl.dsp.window.close()' >/dev/null
            sleep 1.5
            opens=$((opens + 1))
        fi
        n=$((n + 1))
    done
    echo "loop_iterations=$n open_close_cycles=$opens seconds=$seconds style=$style" \
        >"$out/$tag.workload.txt"
    hc "hyprimate:profile" stop >"$out/$tag.profile.txt"
    # Actual GPU contexts: which GPU index nvidia-smi attributes the nested compositor's pid to.
    # A device or node that merely appears in the log (or an extra open render-node fd) is not
    # proof, so a session whose pid is not on the expected GPU is rejected after the fact.
    local nested_pid
    nested_pid=$(hyprctl instances -j | jq -r --arg i "$instance" '.[] | select(.instance == $i) | .pid')
    {
        echo "nested_pid=$nested_pid"
        nvidia-smi pmon -c 1 2>&1 | awk -v pid="$nested_pid" 'NR <= 2 || $2 == pid'
    } >"$out/$tag.gpu-contexts.txt"
    if [[ -n $gpu_index ]] && ! awk -v pid="$nested_pid" -v idx="$gpu_index" \
        '$2 == pid && $1 == idx {found = 1} END {exit !found}' "$out/$tag.gpu-contexts.txt"; then
        echo "$tag: nested pid $nested_pid not on GPU $gpu_index:" >&2
        cat "$out/$tag.gpu-contexts.txt" >&2
        kill "$runner" 2>/dev/null || true
        wait "$runner" 2>/dev/null || true
        return 1
    fi
    kill "$runner" 2>/dev/null || true
    wait "$runner" 2>/dev/null || true
    # The plugin prints its draw counters and GPU stopwatch results when it unloads.
    grep -a "indexed-mesh\|gpu-stopwatch" "$nested_log" >"$out/$tag.counters.txt" || true
    # Result gate: nothing is counted unless the hardware scopes carry positive, lossless data.
    # The GPU stopwatch line is written by the plugin at unload, so this runs after teardown.
    local reasons=() stopwatch
    jq -e '.gpu_status == "available"' "$out/$tag.profile.txt" >/dev/null ||
        reasons+=("profile gpu_status is not available")
    jq -e --arg s "$style" '[.styles[] | select(.style == $s and .operation == "shared") |
        .gpu.samples] | add // 0 | . > 0' "$out/$tag.profile.txt" >/dev/null ||
        reasons+=("no GPU samples for the $style style scope")
    jq -e '[.styles[] | select(.style == "sheet" and .operation == "window-layer") |
        .cpu.draw.thread_cpu.samples] | add // 0 | . > 0' "$out/$tag.profile.txt" >/dev/null ||
        reasons+=("no window-layer CPU samples")
    stopwatch=$(grep -a "gpu-stopwatch window-layer" "$out/$tag.counters.txt" || true)
    if [[ $stopwatch =~ n=([0-9]+)\ .*dropped=([0-9]+) ]]; then
        ((BASH_REMATCH[1] > 0)) || reasons+=("GPU stopwatch has no samples")
        ((BASH_REMATCH[2] == 0)) || reasons+=("GPU stopwatch dropped ${BASH_REMATCH[2]} samples")
    else
        reasons+=("no GPU stopwatch result")
    fi
    if ((${#reasons[@]})); then
        printf '%s\n' "${reasons[@]}" >"$out/$tag.INVALID"
        return 3
    fi
    : >"$out/$tag.VALID"
    echo "$tag: report $out/$tag.profile.txt"
}

# A session is timed only if it passed the startup (output ready) and result gates; run_mode
# returns 3 for such a session so the run continues, and 1 for a device mismatch, which aborts.
for round in $(seq "$rounds"); do
    order=("${mode_list[@]}")
    # Alternate the order of an off/on pair between rounds so drift does not favour one mode.
    if ((${#mode_list[@]} == 2 && round % 2 == 0)); then order=("${mode_list[1]}" "${mode_list[0]}"); fi
    for mode in "${order[@]}"; do
        rc=0
        run_mode "$mode" "$round" || rc=$?
        if ((rc == 3)); then
            echo "$mode-$round: INVALID, not counted (see $out/$mode-$round.INVALID)" >&2
        elif ((rc != 0)); then
            exit "$rc"
        fi
        sleep 3
    done
done
echo "valid sessions: $(find "$out" -name '*.VALID' | wc -l) of $((rounds * ${#mode_list[@]}))"
for mode in "${mode_list[@]}"; do
    echo "  $mode: $(find "$out" -name "$mode-*.VALID" | wc -l) valid"
done
echo "reports in $out"
