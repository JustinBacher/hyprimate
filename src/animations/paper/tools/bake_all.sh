#!/usr/bin/env bash
# Rebakes every paper take and packs them into src/animations/paper/assets/paper_takes.bin. Needs Blender, either on PATH or as the
# org.blender.Blender flatpak. Takes a few minutes; takes run in parallel.
set -euo pipefail

paper_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if command -v blender >/dev/null; then
    blender=(blender)
else
    blender=(flatpak run --filesystem="$paper_dir" --filesystem="$work" --command=blender org.blender.Blender)
fi

# Aspects cover square to ultrawide; portrait windows play a landscape take with its axes swapped. Seeds were picked
# from 1-5 per aspect for crumples that stay face-on; the others fold into a taco or a strip seen edge-on.
takes=(1.0:3 1.0:4 1.6:2 1.6:5 2.4:2 2.4:3)
for take in "${takes[@]}"; do
    aspect=${take%%:*} seed=${take##*:}
    "${blender[@]}" -b --factory-startup --python "$paper_dir/tools/bake.py" -- \
        --aspect "$aspect" --seed "$seed" --out "$work/take-$aspect-$seed.bin" >"$work/take-$aspect-$seed.log" 2>&1 &
done
wait

files=()
for take in "${takes[@]}"; do
    file="$work/take-${take%%:*}-${take##*:}.bin"
    [[ -f $file ]] || { echo "bake failed: $take"; cat "${file%.bin}.log"; exit 1; }
    files+=("$file")
done
mkdir -p "$paper_dir/assets"
python3 "$paper_dir/tools/pack.py" "$paper_dir/assets/paper_takes.bin" "${files[@]}"
