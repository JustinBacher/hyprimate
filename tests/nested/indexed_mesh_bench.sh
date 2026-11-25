#!/usr/bin/env bash
# A/B benchmark of the expanded vs indexed window-layer mesh draw in disposable nested Hyprlands.
#
#   tests/nested/indexed_mesh_bench.sh [--rounds N] [--style STYLE] [--seconds S] [--windows N]
#       [--modes "off on"] [--max-attempts K] [--out DIR] [--startup-only]
#       [--private-kwin /dev/dri/renderDN | --parent-display NAME]
#       [--expect-renderer TEXT] [--expect-node /dev/dri/renderDN] [--expect-gpu INDEX]
#       [--expect-output WxHxS] [--plugin PATH] [--production]
#
# Each round starts one nested compositor per mode (off = expanded triangle list, on = unique
# vertices + indices, compare = indexed plus a per-frame pixel diff against the expanded draw),
# alternating the order between rounds when there are exactly two modes. In each it runs `hyprctl
# hyprimate:profile`, drives repeated open / move / close animations for S seconds on N concurrent
# windows, and saves the profiler report. Reports are raw text in DIR (default
# build/indexed-mesh-bench/<timestamp>); nothing is aggregated here.
#
# Which GPU renders is decided by the parent compositor the nested session attaches to (it renders
# on the parent's advertised DMA-BUF render node).
#   --private-kwin NODE      start a private `kwin_wayland --virtual` pinned to NODE with its own
#                            runtime dir, HOME and dbus, FRESH FOR EVERY ATTEMPT, and attach the
#                            nested session to it. Nothing touches the live compositor. Replaces the
#                            reused parent: a long-lived parent that stopped configuring new
#                            toplevels was the observed cause of sessions without an output.
#   --parent-display NAME    attach to an existing private parent socket instead.
#   --expect-renderer TEXT   abort when the compositor log does not name a renderer containing TEXT
#   --expect-node NODE       abort when the DMA-BUF node in the log differs
#   --expect-gpu INDEX       abort when nvidia-smi does not attribute the nested pid to this GPU
#   --expect-output WxHxS    require exactly one real output with this geometry and scale
#   --plugin PATH            use this plugin instead of HYPRIMATE_PLUGIN or the default build
#   --production             skip diagnostic counter, CPU scope and stopwatch gates
#
# Every attempt is recorded under its own tag MODE-ROUND-aK (device, monitors, startup, workload,
# environment, cpu, profile, counters, compositor log). A session whose attempt fails a startup or
# result gate is retried with a fresh nested compositor (and parent) up to --max-attempts; failed
# attempts are kept as TAG.INVALID with their reasons and are never counted or replaced silently.
# sessions.txt lists, per MODE-ROUND, the attempts used and whether one was valid.
set -euo pipefail

rounds=3 style=cloth seconds=20 out="" modes="off on" parent="" expect="" node="" gpu_index=""
expect_output="" startup_only=0 windows=1 max_attempts=3 private_node=""
plugin=${HYPRIMATE_PLUGIN:-} production=0
while (($#)); do
    case "$1" in
        --rounds) rounds=$2; shift 2 ;;
        --style) style=$2; shift 2 ;;
        --seconds) seconds=$2; shift 2 ;;
        --windows) windows=$2; shift 2 ;;
        --out) out=$2; shift 2 ;;
        --modes) modes=$2; shift 2 ;;
        --max-attempts) max_attempts=$2; shift 2 ;;
        --private-kwin) private_node=$2; shift 2 ;;
        --parent-display) parent=$2; shift 2 ;;
        --expect-renderer) expect=$2; shift 2 ;;
        --expect-node) node=$2; shift 2 ;;
        --expect-gpu) gpu_index=$2; shift 2 ;;
        --expect-output) expect_output=$2; shift 2 ;;
        --plugin) plugin=$2; shift 2 ;;
        --production) production=1; shift ;;
        --startup-only) startup_only=1; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
read -r -a mode_list <<<"$modes"
for mode in "${mode_list[@]}"; do
    [[ $mode == off || $mode == on || $mode == compare ]] || { echo "bad mode: $mode" >&2; exit 2; }
done
[[ $windows =~ ^[0-9]+$ && $windows -ge 1 ]] || { echo "--windows must be >= 1" >&2; exit 2; }
[[ -z $private_node || -z $parent ]] || { echo "--private-kwin and --parent-display conflict" >&2; exit 2; }

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
plugin=${plugin:-$root/build/hyprimate.so}
[[ $plugin == /* ]] || plugin=$PWD/$plugin
export HYPRIMATE_PLUGIN=$plugin
out=${out:-$root/build/indexed-mesh-bench/$(date +%Y%m%d-%H%M%S)}
mkdir -p "$out"

# The private route must not mutate the live compositor: run.sh only adds its host window rule when
# HYPRIMATE_NESTED_FOCUS is not 1.
child_env=()
parent_pid=""
base=""
if [[ -n $private_node ]]; then
    [[ -c $private_node ]] || { echo "not a render node: $private_node" >&2; exit 1; }
    base=$(mktemp -d /tmp/ixk.XXXXXX)
    mkdir -m700 "$base/rt"
    mkdir -p "$base/home"/{config,cache,data,state}
    # Everything this script and its children do from here on talks to the private runtime only.
    unset DISPLAY WAYLAND_DISPLAY WAYLAND_SOCKET HYPRLAND_INSTANCE_SIGNATURE DBUS_SESSION_BUS_ADDRESS \
        LD_PRELOAD LD_LIBRARY_PATH
    export XDG_RUNTIME_DIR=$base/rt HOME=$base/home XDG_CONFIG_HOME=$base/home/config \
        XDG_CACHE_HOME=$base/home/cache XDG_DATA_HOME=$base/home/data \
        XDG_STATE_HOME=$base/home/state XDG_SESSION_TYPE=wayland NO_AT_BRIDGE=1 \
        HYPRLAND_NO_SD_VARS=1 HYPRLAND_NO_SD_NOTIFY=1
    parent=bench-parent
    child_env=(WAYLAND_DISPLAY="$parent" HYPRIMATE_NESTED_FOCUS=1 AQ_BACKENDS=wayland
        LIBSEAT_BACKEND=seatd SEATD_SOCK="$base/rt/no-seat")
elif [[ -n $parent ]]; then
    [[ -S ${XDG_RUNTIME_DIR:?XDG_RUNTIME_DIR is required}/$parent ]] || {
        echo "parent display socket not found: $XDG_RUNTIME_DIR/$parent" >&2; exit 1; }
    child_env=(WAYLAND_DISPLAY="$parent" HYPRIMATE_NESTED_FOCUS=1)
fi

start_parent() {
    local attempt_tag=$1
    setsid dbus-run-session -- env KWIN_RENDER_NODES="$private_node" kwin_wayland --virtual \
        --width 1280 --height 960 --no-lockscreen --no-global-shortcuts --socket "$parent" \
        >"$out/$attempt_tag.parent.log" 2>&1 &
    parent_pid=$!
    for _ in $(seq 100); do
        [[ -S $XDG_RUNTIME_DIR/$parent ]] && break
        sleep .1
    done
    # A socket file can exist before the compositor accepts connections; require a real connect.
    for _ in $(seq 100); do
        python3 -c 'import socket,sys; s=socket.socket(socket.AF_UNIX); s.settimeout(1); s.connect(sys.argv[1])' \
            "$XDG_RUNTIME_DIR/$parent" 2>/dev/null && return 0
        sleep .1
    done
    echo "$attempt_tag: private parent socket missing" >&2
    return 1
}

stop_parent() {
    if [[ -n $parent_pid ]]; then
        kill -- -"$parent_pid" 2>/dev/null || kill "$parent_pid" 2>/dev/null || true
        wait "$parent_pid" 2>/dev/null || true
        parent_pid=""
        rm -f "$XDG_RUNTIME_DIR/$parent" "$XDG_RUNTIME_DIR/$parent.lock" 2>/dev/null || true
    fi
}

cleanup() {
    stop_parent
    if [[ -n $base ]]; then
        # Evidence that nothing of this run outlives it; a non-empty file lists survivors.
        ps -eo pid,pgid,cmd | awk -v b="$base" 'index($0, b) && !/awk -v/' >"$out/leftover-ps.txt" || true
        rm -rf "$base"
    fi
}
trap cleanup EXIT

# Measurement environment for one attempt (clocks, governor, competing load), so a result is never
# read without knowing what else the machine was doing.
record_environment() {
    local file=$1 label=$2
    {
        echo "== $label $(date +%T.%N)"
        echo "loadavg: $(cut -d' ' -f1-3 /proc/loadavg)"
        echo "cpu_governors: $(sort /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null | uniq -c | tr '\n' ' ')"
        echo "competing_builds: cc1plus=$(pgrep -c cc1plus || true) ninja=$(pgrep -c ninja || true) \
clang=$(pgrep -c clang || true)"
        nvidia-smi --query-gpu=index,name,pstate,clocks.gr,clocks.mem,power.draw,temperature.gpu,utilization.gpu \
            --format=csv,noheader 2>&1
        echo "gpu_processes:"
        nvidia-smi pmon -c 1 2>&1 | awk 'NR > 2 {print "  " $0}'
    } >>"$file"
}

# run_mode MODE ROUND TAG: returns 0 valid, 3 invalid attempt (gate), 1 abort (device mismatch).
run_mode() {
    local mode=$1 tag=$3
    local runner_log=$out/$tag.runner.log nested_log=$out/$tag.nested.log
    local env_value=$mode
    [[ $mode == off ]] && env_value=0
    env "${child_env[@]}" HYPRIMATE_INDEXED_MESH=$env_value HYPRIMATE_NESTED_LOG=$nested_log \
        "$root/tests/nested/run.sh" --style "$style" --no-build --no-window >"$runner_log" 2>&1 &
    local runner=$!
    local instance=""
    stop_nested_early() {
        kill "$runner" 2>/dev/null || true
        wait "$runner" 2>/dev/null || true
    }
    for _ in $(seq 100); do
        kill -0 "$runner" 2>/dev/null || break
        instance=$(awk '/^  instance=/ {sub(/^  instance=/, ""); print; exit}' "$runner_log")
        [[ -n $instance ]] && break
        sleep .1
    done
    if [[ -z $instance ]]; then
        # The nested Hyprland exited or never registered (for example it crashed before IPC).
        echo "nested compositor did not start; runner log tail:" >"$out/$tag.INVALID"
        tail -n 5 "$runner_log" >>"$out/$tag.INVALID" 2>/dev/null || true
        tail -n 5 "$nested_log" >>"$out/$tag.INVALID" 2>/dev/null || true
        stop_nested_early
        return 3
    fi
    hc() { hyprctl -i "$instance" "$@"; }
    local compositor_log=${XDG_RUNTIME_DIR:-/tmp}/hypr/$instance/hyprland.log
    save_compositor_log() { cp "$compositor_log" "$out/$tag.compositor.log" 2>/dev/null || true; }
    stop_nested() {
        kill "$runner" 2>/dev/null || true
        wait "$runner" 2>/dev/null || true
    }

    # Provenance: which device rendered, and what it rendered to. Taken before any workload.
    {
        echo "parent_display=${parent:-${WAYLAND_DISPLAY:-}}"
        grep -a "Got node\|eglDeviceFromDRMFD\|Renderer:" "$compositor_log" || true
    } >"$out/$tag.device.txt"

    # Phase 1: a nested backend can start IPC before announcing its first Wayland output; the only
    # monitor is then the 0x0 FALLBACK. The saved logs of such sessions show the parent never sent
    # the toplevel configure, so the backend never announced the output. A created output is not a
    # recovery (it was tried and never produced a monitor), so there is no create fallback: the
    # attempt is invalidated and retried with a fresh compositor and parent.
    # Topology gate: exactly one real (non-FALLBACK) output at the expected geometry and scale.
    output_ready() {
        hc monitors -j >"$out/$tag.monitors.json" 2>/dev/null || return 1
        jq -e --arg want "$1" '([.[] | select(.name != "FALLBACK")] | length) == 1 and
            any(.[]; .width > 0 and .height > 0 and .name != "FALLBACK" and
            ($want == "" or "\(.width)x\(.height)x\(.scale)" == $want))' \
            "$out/$tag.monitors.json" >/dev/null 2>&1
    }
    local ready=0 waited=0
    for _ in $(seq 150); do
        waited=$((waited + 1))
        output_ready "$expect_output" && { ready=1; break; }
        sleep .1
    done
    echo "ready=$ready admission_wait_ds=$waited" >"$out/$tag.startup.txt"
    if ((!ready)); then
        echo "output not ready after ${waited}00 ms (wanted '${expect_output:-any}')" \
            >"$out/$tag.INVALID"
        save_compositor_log
        stop_nested
        return 3
    fi
    if [[ -n $expect ]] && ! grep -aq "Renderer:.*$expect" "$out/$tag.device.txt"; then
        echo "$tag: compositor renderer does not match '$expect':" >&2
        cat "$out/$tag.device.txt" >&2
        save_compositor_log
        stop_nested
        return 1
    fi
    if [[ -n $node ]] && ! grep -aq "Got node $node" "$out/$tag.device.txt"; then
        echo "$tag: DMA-BUF node does not match '$node':" >&2
        cat "$out/$tag.device.txt" >&2
        save_compositor_log
        stop_nested
        return 1
    fi
    if ((startup_only)); then
        save_compositor_log
        cp "$out/$tag.monitors.json" "$out/$tag.startup-monitors.json"
        stop_nested
        : >"$out/$tag.STARTUP_OK"
        echo "$tag: startup ok ($(tr '\n' ' ' <"$out/$tag.startup.txt"))"
        return 0
    fi

    local nested_pid
    nested_pid=$(hyprctl instances -j | jq -r --arg i "$instance" '.[] | select(.instance == $i) | .pid')
    record_environment "$out/$tag.environment.txt" "before-workload"

    # Windows: the first is opened and floated exactly as in the single-window runs so those stay
    # comparable. Extra windows are floated by a rule, opened and given grid positions.
    if ((windows > 1)); then
        hc eval 'hl.window_rule({ name = "bench-float", match = { class = "^kitty$" }, float = true })' \
            >/dev/null
    fi
    hc dispatch 'hl.dsp.exec_cmd("kitty")' >/dev/null
    sleep 3
    if ((windows == 1)); then
        hc dispatch 'hl.dsp.window.float({ action = "toggle" })' >/dev/null
    else
        local opened=1
        while ((opened < windows)); do
            hc dispatch 'hl.dsp.exec_cmd("kitty")' >/dev/null
            sleep 0.8
            opened=$((opened + 1))
        done
        # Bounded wait for every window to map.
        for _ in $(seq 100); do
            (($(hc clients -j | jq '[.[] | select(.mapped)] | length') >= windows)) && break
            sleep .2
        done
    fi
    sleep 1
    local addresses=()
    mapfile -t addresses < <(hc clients -j | jq -r '.[] | select(.mapped) | .address')
    if ((${#addresses[@]} < windows)); then
        echo "only ${#addresses[@]} of $windows windows mapped" >"$out/$tag.INVALID"
        save_compositor_log
        stop_nested
        return 3
    fi
    # Spread the windows over the output so they animate and composite independently.
    if ((windows > 1)); then
        local i=0 cols=4
        for address in "${addresses[@]}"; do
            hc dispatch "hl.dsp.focus({ window = \"address:$address\" })" >/dev/null
            hc dispatch "hl.dsp.window.move({ x = $((40 + (i % cols) * 300)), y = $((40 + (i / cols) * 170)) })" \
                >/dev/null
            i=$((i + 1))
        done
        sleep 1
    fi

    local ticks utime0 stime0 utime1 stime1
    ticks=$(getconf CLK_TCK)
    read -r utime0 stime0 < <(awk '{print $14, $15}' "/proc/$nested_pid/stat")
    local t0=$SECONDS
    hc "hyprimate:profile" start >/dev/null
    local end=$((SECONDS + seconds)) n=0 opens=0
    while ((SECONDS < end)); do
        # Move every window out and back at once, then open and close one more window: exercises
        # move, open and close with all windows animating concurrently.
        for address in "${addresses[@]}"; do
            ((windows > 1)) && hc dispatch "hl.dsp.focus({ window = \"address:$address\" })" >/dev/null
            hc dispatch 'hl.dsp.window.move({ x = 160, y = 90, relative = true })' >/dev/null
        done
        sleep 1.2
        for address in "${addresses[@]}"; do
            ((windows > 1)) && hc dispatch "hl.dsp.focus({ window = \"address:$address\" })" >/dev/null
            hc dispatch 'hl.dsp.window.move({ x = -160, y = -90, relative = true })' >/dev/null
        done
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
    echo "loop_iterations=$n open_close_cycles=$opens seconds=$seconds style=$style windows=$windows" \
        >"$out/$tag.workload.txt"
    hc "hyprimate:profile" stop >"$out/$tag.profile.txt"
    read -r utime1 stime1 < <(awk '{print $14, $15}' "/proc/$nested_pid/stat")
    echo "nested_pid=$nested_pid clk_tck=$ticks utime_ticks=$((utime1 - utime0)) stime_ticks=$((stime1 - stime0)) wall_s=$((SECONDS - t0))" \
        >"$out/$tag.cpu.txt"
    record_environment "$out/$tag.environment.txt" "after-workload"

    # Actual GPU contexts: which GPU index nvidia-smi attributes the nested compositor's pid to.
    # A device or node that merely appears in the log (or an extra open render-node fd) is not
    # proof, so a session whose pid is not on the expected GPU is rejected after the fact.
    {
        echo "nested_pid=$nested_pid"
        nvidia-smi pmon -c 1 2>&1 | awk -v pid="$nested_pid" 'NR <= 2 || $2 == pid'
    } >"$out/$tag.gpu-contexts.txt"
    if [[ -n $gpu_index ]] && ! awk -v pid="$nested_pid" -v idx="$gpu_index" \
        '$2 == pid && $1 == idx {found = 1} END {exit !found}' "$out/$tag.gpu-contexts.txt"; then
        echo "$tag: nested pid $nested_pid not on GPU $gpu_index:" >&2
        cat "$out/$tag.gpu-contexts.txt" >&2
        save_compositor_log
        stop_nested
        return 1
    fi
    save_compositor_log
    stop_nested
    # The plugin prints its draw counters and GPU stopwatch results when it unloads.
    grep -a "indexed-mesh\|gpu-stopwatch" "$nested_log" >"$out/$tag.counters.txt" || true
    # Production builds omit the diagnostic scopes and counters, but still need valid
    # profiler GPU and pacing data. Missing fields must not count as zero.
    local reasons=() stopwatch
    jq -e '.gpu_status == "available"' "$out/$tag.profile.txt" >/dev/null ||
        reasons+=("profile gpu_status is not available")
    jq -e '.gpu_disjoint_events == 0' "$out/$tag.profile.txt" >/dev/null ||
        reasons+=("GPU disjoint events present or unreported")
    jq -e --arg s "$style" '[.styles[] | select(.style == $s and .operation == "shared") |
        .gpu.samples] | add // 0 | . > 0' "$out/$tag.profile.txt" >/dev/null ||
        reasons+=("no GPU samples for the $style style scope")
    jq -e --arg s "$style" 'any(.styles[]; .style == $s and .operation == "shared" and
        (.draw_intervals.samples // 0) > 0 and
        ((.presentation_intervals.samples // 0) > 0 or
         (.presentation_callback_intervals.samples // 0) > 0))' "$out/$tag.profile.txt" >/dev/null ||
        reasons+=("no draw and presentation pacing samples for the $style style scope")
    if ((!production)); then
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
        local counter_reasons=()
        mapfile -t counter_reasons < <(awk -v mode="$mode" '
            /indexed-mesh: uninstall;/ { line = $0 }
            END {
                n = split(line, words, /[[:space:]]+/)
                valid = 0
                count = split("elements_draws passthrough uploaded expanded index_bytes compares mismatches bytes_indexed bytes_expanded contract_broken selftest readback_failures", required, " ")
                for (i = 1; i <= n; i++) {
                    split(words[i], pair, "=")
                    if (pair[1] ~ /^(elements_draws|passthrough|uploaded|expanded|index_bytes|compares|mismatches|bytes_indexed|bytes_expanded|contract_broken|selftest|readback_failures)$/ && pair[2] ~ /^[0-9]+$/) {
                        value[pair[1]] = pair[2]
                        valid++
                    }
                }
                for (i = 1; i <= count; i++)
                    if (!(required[i] in value)) print "missing or invalid counter: " required[i]
                if (valid != count) exit
                if (value["selftest"] != 0 && value["selftest"] != 1)
                    print "invalid selftest flag"
                if (value["readback_failures"] != 0)
                    print "compare framebuffer readback failed"
                if (mode != "off" && value["contract_broken"] != 0)
                    print "indexed draw contract broken"
                if (mode == "off" && (value["elements_draws"] > 0 || value["uploaded"] > 0 ||
                    value["index_bytes"] > 0 || value["bytes_indexed"] > 0 || value["compares"] > 0))
                    print "indexed activity in off mode"
                if (mode == "on" && value["elements_draws"] == 0)
                    print "no indexed draws in on mode"
                if (mode != "off" && value["elements_draws"] > 0 && (value["bytes_indexed"] == 0 ||
                    value["bytes_expanded"] == 0 || value["bytes_indexed"] >= value["bytes_expanded"]))
                    print "indexed payload does not reduce bytes"
                if (mode == "compare" && (value["compares"] == 0 ||
                    (value["selftest"] == 0 && value["mismatches"] != 0) ||
                    (value["selftest"] == 1 && value["mismatches"] == 0)))
                    print "compare count or mismatch result invalid"
            }' "$out/$tag.counters.txt")
        reasons+=("${counter_reasons[@]}")
    fi
    if ((${#reasons[@]})); then
        printf '%s\n' "${reasons[@]}" >"$out/$tag.INVALID"
        return 4
    fi
    : >"$out/$tag.VALID"
    echo "$tag: report $out/$tag.profile.txt"
}

# One session = one (mode, round) cell. Each attempt gets a fresh nested compositor (and parent in
# --private-kwin mode). ONLY startup admission failures (parent or nested compositor did not start,
# output not ready, windows not mapped; run_mode returns 3) are retried, up to --max-attempts in
# total. A failure after the workload (result or correctness gates; run_mode returns 4) is final for
# the session and is never retried. Every attempt, with its crash reports, is preserved.
save_crash_reports() {
    local attempt_tag=$1 dir
    for dir in "$XDG_CACHE_HOME/hyprland" "$HOME/.cache/hyprland"; do
        compgen -G "$dir/hyprlandCrashReport*" >/dev/null || continue
        mkdir -p "$out/$attempt_tag.crash"
        cp "$dir"/hyprlandCrashReport* "$out/$attempt_tag.crash/" 2>/dev/null || true
        # Reports are keyed by pid; remove them from the shared cache dir so the next attempt only
        # sees its own.
        [[ -n $private_node ]] && rm -f "$dir"/hyprlandCrashReport*
    done
}

run_session() {
    local mode=$1 round=$2 attempt rc tag
    for attempt in $(seq "$max_attempts"); do
        tag=$mode-$round-a$attempt
        printf 'plugin=%s\nproduction=%s\n' "$plugin" "$production" >"$out/$tag.attempt.txt"
        if [[ -n $private_node ]] && ! start_parent "$tag"; then
            echo "$tag: INVALID (parent did not start)" >&2
            echo "private parent did not start" >"$out/$tag.INVALID"
            stop_parent
            continue
        fi
        rc=0
        run_mode "$mode" "$round" "$tag" || rc=$?
        save_crash_reports "$tag"
        [[ -n $private_node ]] && stop_parent
        if ((rc == 0)); then
            echo "$mode-$round attempts=$attempt valid=1" >>"$out/sessions.txt"
            return 0
        elif ((rc == 3)); then
            echo "$tag: INVALID startup, not counted: $(tr '\n' ';' <"$out/$tag.INVALID")" >&2
            sleep 3
        elif ((rc == 4)); then
            echo "$tag: INVALID after workload, final (no retry): $(tr '\n' ';' <"$out/$tag.INVALID")" >&2
            echo "$mode-$round attempts=$attempt valid=0 reason=result-gate" >>"$out/sessions.txt"
            return 0
        else
            exit "$rc"
        fi
    done
    echo "$mode-$round attempts=$max_attempts valid=0 reason=startup" >>"$out/sessions.txt"
    return 0
}

: >"$out/sessions.txt"
for round in $(seq "$rounds"); do
    order=("${mode_list[@]}")
    # Alternate the order of an off/on pair between rounds so drift does not favour one mode.
    if ((${#mode_list[@]} == 2 && round % 2 == 0)); then order=("${mode_list[1]}" "${mode_list[0]}"); fi
    for mode in "${order[@]}"; do
        run_session "$mode" "$round"
        sleep 3
    done
done
echo "sessions (valid / total):"
for mode in "${mode_list[@]}"; do
    echo "  $mode: $(grep -c "^$mode-[0-9]* attempts=[0-9]* valid=1" "$out/sessions.txt" || true) / $rounds"
done
echo "attempts: $(find "$out" -name '*.runner.log' | wc -l), invalid attempts: $(find "$out" -name '*.INVALID' | wc -l)"
echo "reports in $out"
