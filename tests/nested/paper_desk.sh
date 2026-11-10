#!/usr/bin/env bash
# Nested-Hyprland test: every kitty in the nested session looks like a torn sheet of cream notebook paper
# taped to a dusk wallpaper, so the paper open/close animation has something paper-looking to unfold and crumple.
# Takes the same options as run.sh (--open/--close/--lua/--no-build/...). Your own kitty config is not touched:
# kitty reads a generated one through KITTY_CONFIG_DIRECTORY, which only the nested session's processes inherit.
#
# The sheet (paper.png) is drawn per session by paper_desk/prepare.sh at the size the first window gets, because
# kitty stretches the image over the window; later windows reuse it and stretch.
#
# Environment:
#   PAPER_WALLPAPER  image for the nested desktop (default ~/Wallpapers/wallhaven-lyzdkl.png)
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
dir=${XDG_RUNTIME_DIR:-/tmp}/hyprimate-paper-desk
wallpaper=${PAPER_WALLPAPER:-$HOME/Wallpapers/wallhaven-lyzdkl.png}
[[ -f $wallpaper ]] || { echo "wallpaper not found: $wallpaper (set PAPER_WALLPAPER)" >&2; exit 1; }
command -v awww-daemon >/dev/null || { echo "missing required tool: awww" >&2; exit 1; }
mkdir -p "$dir"

cp "$here/paper_desk/tab_bar.py" "$dir/tab_bar.py"

cat >"$dir/kitty.conf" <<EOF
font_family                FantasqueSansM Nerd Font Mono
font_size                  14
window_padding_width       18 46
hide_window_decorations    yes
confirm_os_window_close    0
enable_audio_bell          no
cursor_blink_interval      0
shell                      $here/paper_desk/shell.sh

# The sheet's torn corners are transparent in the image; the 1% default background is what lets them show through.
background_image           $dir/paper.png
background_image_layout    scaled
background_tint            0
background_opacity         0.01
background                 #ebe1cc
foreground                 #3d3b38
cursor                     #2b2a28
cursor_text_color          #ebe1cc
selection_background       #bcd0ea
selection_foreground       #2b2a28

tab_bar_edge               top
tab_bar_min_tabs           1
tab_bar_style              custom
tab_bar_background         none
tab_title_template         {title}
tab_bar_margin_width       44
tab_bar_margin_height      36 14
active_tab_font_style      normal

color0  #3a3835
color1  #e0776f
color2  #5b8a5a
color3  #c9a24a
color4  #3d7fcf
color5  #9a6bb0
color6  #3a8fa6
color7  #8d867a
color8  #5a5750
color9  #e8837c
color10 #6a9c69
color11 #d8b157
color12 #4a8cdd
color13 #a97bbd
color14 #4a9db3
color15 #3a3835
EOF

wallpaper_cmd="sh -c 'awww-daemon & sleep 1; awww img --resize crop --transition-type none \"$wallpaper\"'"
nested_lua="
hl.config({
    general = { border_size = 0 },
    decoration = { rounding = 0, shadow = { enabled = false }, blur = { enabled = false } },
})
hl.exec_cmd([[$wallpaper_cmd]])
"

export KITTY_CONFIG_DIRECTORY=$dir PAPER_DIR=$dir
exec "$here/run.sh" --eval "$nested_lua" --before-window "$here/paper_desk/prepare.sh" "$@"
