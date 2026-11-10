#!/usr/bin/env bash
# run.sh --before-window hook: draws the sheet at the size the first kitty window will get, because kitty
# stretches the image over the window and the doodles would distort otherwise.
# $1 = nested instance signature; PAPER_DIR = where kitty.conf expects paper.png.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# One tiled window fills the output minus gaps_out (24) on each side; the border is 0 in this test.
read -r w h < <(hyprctl -i "$1" monitors -j | jq -r '.[0] | "\((.width / .scale | floor) - 48) \((.height / .scale | floor) - 48)"')
python3 "$here/make_paper.py" "$PAPER_DIR/paper.png" --size "${w}x${h}"
