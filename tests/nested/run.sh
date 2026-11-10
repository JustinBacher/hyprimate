#!/usr/bin/env bash
# Launches a disposable nested Hyprland (a window inside your current session) with this checkout's plugin loaded.
# Your own compositor, config and installed plugins are not touched. Close the nested window or press Ctrl+C here to stop.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tests/nested/run.sh [options]

  --open STYLE        open style: liquid | paper     (default: paper)
  --close STYLE       close style: liquid | paper    (default: paper)
  --movement STYLE    movement style: liquid | wiggle (default: liquid)
  --lua TABLE         extra settings merged into plugin.liquid_glass, e.g.
                      --lua 'window = { close = { options = { paper = { shading = 0 } } } }'
  --eval LUA          Lua run in the nested compositor once the plugin is loaded, before the first window,
                      e.g. --eval 'hl.config({ general = { border_size = 0 } })'
  --before-window CMD executable run with the nested instance signature as $1, after --eval and before the
                      first window opens (e.g. to size generated assets to the window)
  --no-build          skip `cmake --build build`
  --no-window         don't open a kitty window on start
  -h, --help          show this help

Inside the nested compositor: ALT+Return opens kitty, ALT+C closes the focused window,
ALT+Space toggles floating, ALT+F1 toggles the split, ALT+drag moves, ALT+right-drag resizes.
EOF
}

open_style=paper close_style=paper movement_style=liquid extra_lua="" eval_lua="" before_window="" build=1 window=1
while (($#)); do
    case "$1" in
        --open) open_style=$2; shift 2 ;;
        --close) close_style=$2; shift 2 ;;
        --movement) movement_style=$2; shift 2 ;;
        --lua) extra_lua=$2; shift 2 ;;
        --eval) eval_lua=$2; shift 2 ;;
        --before-window) before_window=$2; shift 2 ;;
        --no-build) build=0; shift ;;
        --no-window) window=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
plugin=$root/build/liquid-glass.so
config=$root/tests/nested/hyprland.lua

for tool in Hyprland hyprctl jq loginctl; do
    command -v "$tool" >/dev/null || { echo "missing required tool: $tool" >&2; exit 1; }
done
[[ -n ${WAYLAND_DISPLAY:-} ]] || { echo "WAYLAND_DISPLAY is not set: run this from inside your Wayland session" >&2; exit 1; }

if ((build)); then
    [[ -d $root/build ]] || cmake -S "$root" -B "$root/build" -G Ninja
    cmake --build "$root/build"
fi
[[ -f $plugin ]] || { echo "plugin not built: $plugin" >&2; exit 1; }

# The nested compositor must bind to the active graphical session; a shell started elsewhere (tty, ssh, an agent)
# may inherit an inactive XDG_SESSION_ID, and the nested instance then renders nothing.
session="" vt=""
while read -r id _; do
    props=$(loginctl show-session "$id" -p Active -p Seat -p Type -p Name -p VTNr 2>/dev/null) || continue
    if grep -qx 'Active=yes' <<<"$props" && grep -qx 'Seat=seat0' <<<"$props" \
        && grep -qx 'Type=wayland' <<<"$props" && grep -qx "Name=$USER" <<<"$props"; then
        session=$id
        vt=$(sed -n 's/^VTNr=//p' <<<"$props")
        break
    fi
done < <(loginctl list-sessions --no-legend)
[[ -n $session ]] || { echo "no active wayland session on seat0 for $USER" >&2; exit 1; }

log=${XDG_RUNTIME_DIR:-/tmp}/hyprimate-nested.log
env XDG_SESSION_ID="$session" XDG_VTNR="$vt" HYPRLAND_NO_SD_VARS=1 HYPRLAND_NO_SD_NOTIFY=1 \
    Hyprland -c "$config" >"$log" 2>&1 &
pid=$!
cleanup() { kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; }
trap cleanup EXIT
trap 'exit 130' INT TERM

instance=""
for _ in $(seq 100); do
    kill -0 "$pid" 2>/dev/null || { echo "nested Hyprland exited; log: $log" >&2; exit 1; }
    instance=$(hyprctl instances -j 2>/dev/null | jq -r --argjson pid "$pid" '.[] | select(.pid == $pid) | .instance')
    [[ -n $instance ]] && hyprctl -i "$instance" version >/dev/null 2>&1 && break
    instance=""
    sleep 0.1
done
[[ -n $instance ]] || { echo "nested Hyprland did not come up; log: $log" >&2; exit 1; }

hc() { hyprctl -i "$instance" "$@"; }

result=$(hc plugin load "$plugin")
[[ $result == ok ]] || { echo "plugin load failed: $result" >&2; exit 1; }

settings="window = { movement = { style = \"$movement_style\" }, open = { style = \"$open_style\" }, close = { style = \"$close_style\" } }"
result=$(hc eval "hl.config({ plugin = { liquid_glass = { $settings } } })")
[[ $result == ok ]] || { echo "settings rejected: $result" >&2; exit 1; }
if [[ -n $extra_lua ]]; then
    result=$(hc eval "hl.config({ plugin = { liquid_glass = { $extra_lua } } })")
    [[ $result == ok ]] || { echo "--lua settings rejected: $result" >&2; exit 1; }
fi

if [[ -n $eval_lua ]]; then
    result=$(hc eval "$eval_lua")
    [[ $result == ok ]] || { echo "--eval Lua rejected: $result" >&2; exit 1; }
fi

if [[ -n $before_window ]]; then
    "$before_window" "$instance" || { echo "--before-window hook failed: $before_window" >&2; exit 1; }
fi

if ((window)); then
    hc dispatch 'hl.dsp.exec_cmd("kitty")' >/dev/null
fi

cat <<EOF

Nested Hyprland is running (pid $pid), plugin loaded from $plugin.
  open=$open_style  close=$close_style  movement=$movement_style

Change settings live from another terminal:
  hyprctl -i $instance eval 'hl.config({ plugin = { liquid_glass = { window = { open = { options = { paper = { duration = 1.5 } } } } } } })'
  hyprctl -i $instance getoption plugin:liquid-glass:window:close:style
Reload after rebuilding:
  hyprctl -i $instance plugin unload $plugin && hyprctl -i $instance plugin load $plugin
  (settings reset to defaults on reload)

Log: $log
Close the nested window or press Ctrl+C to stop.
EOF

wait "$pid" || true
