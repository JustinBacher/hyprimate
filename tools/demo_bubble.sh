#!/usr/bin/env bash
# Record a slow bubble walkthrough in an isolated nested Hyprland session.
# Usage: tools/demo_bubble.sh 'window = { style = "bubble", movement = { ... } }'
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
config=${1:?pass the Hyprimate config table string as the first argument}
out=${2:-$root/bubble-animations-demo.mp4}
for cmd in hyprctl grim ffmpeg jq noctalia systemd-run systemctl; do
    command -v "$cmd" >/dev/null || { echo "missing required tool: $cmd" >&2; exit 1; }
done
[[ -n ${WAYLAND_DISPLAY:-} ]] || { echo 'run this inside the active Wayland session' >&2; exit 1; }
[[ -d /home/justin/Wallpapers ]] || { echo 'wallpaper directory not found: /home/justin/Wallpapers' >&2; exit 1; }

# Use the existing isolated nested-compositor harness; it leaves the user's compositor untouched.
# The supplied string is a Lua table merged into plugin.hyprimate by the harness.
instance_log=$(mktemp)
noctalia_log=$(mktemp)
frames=$(mktemp -d)
existing=$(hyprctl instances -j | jq -r '.[].instance // empty' | sort)
plugins_before=$(pgrep -f '/home/justin/.local/state/noctalia/plugins/materialized/' || true)
run_pid=''
nested_pid=''
noctalia_launcher_pid=''
noctalia_unit="hyprimate-demo-noctalia-$$"
capture_pid=''
cleanup() {
    [[ -n $capture_pid ]] && kill "$capture_pid" 2>/dev/null || true
    [[ -n $noctalia_unit ]] && systemctl --user stop "$noctalia_unit.scope" >/dev/null 2>&1 || true
    [[ -n $noctalia_launcher_pid ]] && wait "$noctalia_launcher_pid" 2>/dev/null || true
    for pid in $(pgrep -f '/home/justin/.local/state/noctalia/plugins/materialized/' || true); do
        case " $plugins_before " in *" $pid "*) continue ;; esac
        kill "$pid" 2>/dev/null || true
    done
    [[ -n $nested_pid ]] && kill "$nested_pid" 2>/dev/null || true
    [[ -n $run_pid ]] && kill "$run_pid" 2>/dev/null || true
    [[ -n $run_pid ]] && wait "$run_pid" 2>/dev/null || true
    rm -f "$instance_log" "$noctalia_log"
    rm -rf "$frames"
}
trap cleanup EXIT INT TERM
"$root/tests/nested/run.sh" --style bubble --lua "$config" --no-build --no-window >"$instance_log" 2>&1 &
run_pid=$!
for _ in $(seq 100); do
    instance=$(hyprctl instances -j 2>/dev/null | jq -r '.[].instance // empty' | sort | while read -r sig; do
        [[ -n $sig ]] || continue
        if ! grep -Fxq "$sig" <<<"$existing" && hyprctl -i "$sig" version >/dev/null 2>&1; then printf '%s' "$sig"; break; fi
    done)
    [[ -n $instance ]] && break
    sleep 0.2
done
nested_pid=$(hyprctl instances -j | jq -r --arg sig "$instance" '.[] | select(.instance == $sig) | .pid')
[[ -n $nested_pid && $nested_pid != null ]] || { echo "no process id found for nested instance $instance" >&2; exit 1; }
hc() { hyprctl -i "$instance" "$@"; }
# Resolve the nested compositor's private Wayland socket. Connecting grim to this socket captures
# its one virtual output directly, rather than the outer desktop's collection of monitors.
capture_display=$(hyprctl instances -j | jq -r --arg sig "$instance" '.[] | select(.instance == $sig) | .wl_socket')
[[ -n $capture_display && $capture_display != null ]] || { echo "no Wayland socket found for nested instance $instance" >&2; exit 1; }

# Run a second Noctalia in a transient systemd scope, attached to this nested Wayland socket.
# The scope keeps its plugin helpers contained and lets cleanup stop only this demo instance.
systemd-run --user --scope --unit="$noctalia_unit" --property=KillMode=control-group \
    env WAYLAND_DISPLAY="$capture_display" HYPRLAND_INSTANCE_SIGNATURE="$instance" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:?}" noctalia >"$noctalia_log" 2>&1 &
noctalia_launcher_pid=$!
wallpaper=''
for _ in $(seq 100); do
    wallpaper=$(env WAYLAND_DISPLAY="$capture_display" HYPRLAND_INSTANCE_SIGNATURE="$instance" \
        XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" noctalia msg wallpaper-get 2>/dev/null || true)
    [[ -n $wallpaper ]] && break
    sleep 0.2
done
[[ $wallpaper == /home/justin/Wallpapers/* ]] || {
    cat "$noctalia_log" >&2
    echo "nested Noctalia wallpaper is missing or outside /home/justin/Wallpapers: $wallpaper" >&2
    exit 1
}

# Record only pixels rendered by the nested compositor's virtual output.
record_frames() {
    local i=0
    while :; do
        WAYLAND_DISPLAY="$capture_display" XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" grim "$frames/$(printf '%06d' "$i").png" 2>/dev/null || true
        ((i+=1))
        sleep 0.08
    done
}
record_frames & capture_pid=$!
wait_for() { sleep "$1"; }
open_window() { hc dispatch 'hl.dsp.exec_cmd("kitty")' >/dev/null; wait_for 4; }
move_window() { hc dispatch "hl.dsp.window.move({ x = $1, y = $2, relative = true })" >/dev/null; wait_for 3; }

# Open four windows, leaving a visible pause after each normal-speed opening animation.
for _ in 1 2 3 4; do open_window; done
wait_for 3

# Move the focused tiled window through each other window's tile.
tiled_before=$(hc clients -j | jq -c 'map({ address, at, floating }) | sort_by(.address)')
focused_address=$(hc activewindow -j | jq -r '.address')
while read -r address; do
    [[ -n $address ]] || continue
    hc dispatch "hl.dsp.window.swap({ target = \"address:$address\" })" >/dev/null
    wait_for 2.5
done < <(hc clients -j | jq -r --arg active "$focused_address" '.[] | select(.address != $active) | .address')
tiled_after=$(hc clients -j | jq -c 'map({ address, at, floating }) | sort_by(.address)')
[[ $tiled_before != "$tiled_after" ]] || { echo 'tiled swap actions did not change window positions' >&2; exit 1; }

# Float the same focused window, then move it by a deliberately irregular path.
floating_address=$(hc activewindow -j | jq -r '.address')
hc dispatch 'hl.dsp.window.float({ action = "toggle" })' >/dev/null
floating=$(hc clients -j | jq -r --arg address "$floating_address" '.[] | select(.address == $address) | .floating')
[[ $floating == true ]] || { echo 'focused window did not become floating' >&2; exit 1; }
wait_for 3
position_before=$(hc clients -j | jq -r --arg address "$floating_address" '.[] | select(.address == $address) | .at | @tsv')
for delta in '280 90' '-360 170' '420 -260' '-240 -140' '110 320' '-310 -220' '180 -100'; do move_window $delta; done
position_after=$(hc clients -j | jq -r --arg address "$floating_address" '.[] | select(.address == $address) | .at | @tsv')
[[ $position_before != "$position_after" ]] || { echo 'floating move actions did not change window position' >&2; exit 1; }
wait_for 3
# Close all remaining demo windows through the nested compositor.
for _ in 1 2 3 4; do hc dispatch 'hl.dsp.window.close()' >/dev/null; wait_for 4; done
wait_for 2
kill "$capture_pid" 2>/dev/null || true
capture_pid=''
ffmpeg -hide_banner -loglevel error -y -framerate 4 -i "$frames/%06d.png" -vf 'scale=trunc(iw/2)*2:trunc(ih/2)*2,format=yuv420p' -c:v libx264 -preset medium -crf 18 -movflags +faststart "$out"
printf 'Saved demo: %s\nNested wallpaper: %s\n' "$out" "$wallpaper"
