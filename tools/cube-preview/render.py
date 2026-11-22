"""Render isolated review MP4s: python render.py OUTPUT_DIR [--only solid ...].

Requires Python with moderngl, numpy and Pillow; EGL, a C++20 compiler, ffmpeg
(with libx264, drawtext and xstack), and DejaVu Sans. No plugin build or install.
"""
import argparse
from collections import defaultdict
from functools import partial
from importlib import import_module
import json
import math
from pathlib import Path
import subprocess as sp
import tempfile

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from desktops import make_desktops
from renderer import BackwardView, Scene

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
NAMES = ('solid', 'inside-out', 'tesseract', 'dimensional-fold')
REVISIONS = {'solid': '02', 'inside-out': '02', 'dimensional-fold': '03', 'tesseract': '03'}
CUE_HOLD, CUE_FADE, CLEAN_LEAD, CLEAN_TAIL = 56, 9, 8, 18
SHOTS = ('Forward   ·   02 → 03', 'Backward   ·   03 → 02', 'Interrupt   ·   return to 02 at halfway')
ENCODE = ['-an', '-c:v', 'libx264', '-preset', 'fast', '-crf', '17', '-threads', '6', '-pix_fmt', 'yuv420p',
          '-color_primaries', 'bt709', '-colorspace', 'bt709', '-color_trc', 'bt709', '-movflags', '+faststart']


def timeline():
    settings = json.loads((ROOT / 'config/settings.json').read_text())
    duration = next(s['default'] for s in settings if s['path'] == 'workspace.options.rotation.duration')
    with tempfile.TemporaryDirectory() as work:
        sampler = Path(work)/'timeline'
        sp.run(['c++', '-std=c++20', '-O2', '-I', ROOT/'src', HERE/'timeline.cpp',
                ROOT/'src/workspace/WorkspaceTransition.cpp', '-o', sampler], check=True)
        rows = sp.check_output([sampler, str(duration)], text=True).splitlines()
    shots = defaultdict(list)
    for row in rows:
        slow, shot, backward, p = row.split()
        shots[int(slow), int(shot), bool(int(backward))].append(float(p))
    return shots


def slate(base, name, slow, shot):
    # Review metadata lives on the cue only. Even the screen edges remain free
    # of editorial overlays during the actual movement, touchdown and handoff.
    size = (780, 138)
    x, y = 50, base.height - size[1] - 54
    card = Image.new('RGBA', size)
    d = ImageDraw.Draw(card)
    d.rounded_rectangle((0, 0, size[0]-1, size[1]-1), 18, fill=(20, 26, 31, 247), outline=(96, 109, 115), width=1)
    def text(xy, value, font, px, colour):
        d.text(xy, value, font=ImageFont.truetype(font + '.ttf', px), fill=colour)
    number = NAMES.index(name)+1
    text((24,14), f'HYPRIMATE    /    STUDY {number:02}    /    REVISION {REVISIONS[name]}', 'DejaVuSansMono', 17, '#a7c5c8')
    text((22,38), name.replace('-', ' ').title(), 'DejaVuSans-Bold', 38, '#f3f3ef')
    text((24,96), SHOTS[shot], 'DejaVuSans', 22, '#d7dbd9')
    text((676,22), '1×' if slow == 1 else '¼×', 'DejaVuSans-Bold', 38, '#c3e2da')
    image = base.copy()
    image.paste(card, (x,y), card)
    return image


def variant_draw(name, **options):
    """Bind preview-only Python arguments once, not a compositor config reader."""
    if name not in NAMES:
        raise ValueError(f'unknown preview variant: {name}')
    for key, value in options.items():
        if key == 'show_other_workspaces':
            if type(value) is not bool:
                raise TypeError('show_other_workspaces must be boolean')
        elif key in ('zoom', 'cube_tilt', 'next_workspace_start_size'):
            if type(value) not in (int, float):
                raise TypeError(f'{key} must be numeric')
            low, high = (-20.,20.) if key == 'cube_tilt' else (0.,1.)
            # Same float read behavior as generate_config.py's clamp_float.
            options[key] = min(max(low, value), high) if math.isfinite(value) else low
        else:
            raise TypeError(f'unknown preview parameter: {key}')
    return partial(import_module(name.replace('-', '_')).draw, **options)


def encode(name, out, scene, images, shots):
    draw = variant_draw(name)
    reverse_view = BackwardView(scene)
    def backward_draw(_, p):
        draw(reverse_view, p)
    # Keep the GL pipeline on the endpoints too. Detect an orientation, lighting,
    # sizing or sampling mistake rather than papering it over with a still.
    endpoints = []
    for p in (0,1):
        rgb = scene.frame(draw, p)
        delta = abs(np.frombuffer(rgb, np.uint8).astype(np.int16) - np.asarray(images[p]).reshape(-1))
        print(f'{name} endpoint {p}: RGB error mean {delta.mean():.5f}, peak {delta.max()}', flush=True)
        if delta.max() > 1:
            raise ValueError(f'{name}: endpoint mismatch')
        # Check the approach as well: a hidden lining at almost equal depth
        # can flash immediately next to a perfectly correct endpoint.
        near = scene.frame(draw, .0001 if p == 0 else .9999)
        jump = abs(np.frombuffer(near, np.uint8).astype(np.int16) - np.frombuffer(rgb, np.uint8)).mean()
        if jump > 1.25:
            raise ValueError(f'{name}: discontinuity approaching endpoint {p} ({jump:.2f} RGB levels)')
        endpoints.append(Image.frombytes('RGB', scene.size, rgb))
    args = ['ffmpeg','-v','error','-y','-f','rawvideo','-pixel_format','rgb24','-video_size','1920x1080',
            '-framerate','60','-i','-', '-vf','scale=out_color_matrix=bt709:out_range=tv', *ENCODE,
            '-metadata','title=Hyprimate / '+name, out/(name+'.mp4')]
    n = 0
    with sp.Popen(args, stdin=sp.PIPE) as encoder:
        try:
            for (slow, shot, backward), path in shots.items():
                base = endpoints[int(backward)]
                active_draw = backward_draw if backward else draw
                cue = slate(base, name, slow, shot)
                def hold(rgb, frames):
                    nonlocal n
                    for _ in range(frames):
                        encoder.stdin.write(rgb)
                    n += frames
                hold(cue.tobytes(), CUE_HOLD)
                for k in range(1, CUE_FADE + 1):
                    hold(Image.blend(cue, base, k/(CUE_FADE+1)).tobytes(), 1)
                hold(base.tobytes(), CLEAN_LEAD)
                print(f'{name} {slow=} {shot=} motion {n}..{n+len(path)-1}', flush=True)
                for p in path:
                    hold(scene.frame(active_draw, p), 1)
                hold(endpoints[round(path[-1]) ^ int(backward)].tobytes(), CLEAN_TAIL)
        finally:
            encoder.stdin.close()
        if encoder.wait():
            raise RuntimeError('ffmpeg encode failed')


def comparison(out):
    args = ['ffmpeg','-v','error','-y','-filter_complex_threads','4']
    filters = []
    for i, name in enumerate(NAMES):
        args += ['-threads','2','-i',out/(name+'.mp4')]
        filters.append(f'[{i}:v]scale=960:540:flags=lanczos,pad=960:600:0:60:color=0x141a1f,'
                       f"drawtext=font='DejaVu Sans':fontsize=25:fontcolor=0xe1eae8:x=28:y=16:"
                       f"text='{i+1:02}     {name.upper()}'[v{i}]")
    filters += ['[v0][v1][v2][v3]xstack=inputs=4:layout=0_0|960_0|0_600|960_600[v]']
    sp.run([*args,'-filter_complex',';'.join(filters),'-map','[v]',*ENCODE, out/'comparison.mp4'], check=True)


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('output', type=Path)
    cli.add_argument('--only', choices=NAMES, nargs='+', default=NAMES)
    cli.add_argument('--compare-only', action='store_true')
    args = cli.parse_args()
    args.output.mkdir(exist_ok=True, parents=True)
    if not args.compare_only:
        images = make_desktops()
        scene = Scene(images)
        shots = timeline()
        for name in args.only:
            encode(name, args.output, scene, images, shots)
    if args.compare_only or tuple(args.only) == NAMES:
        comparison(args.output)


if __name__ == '__main__':
    main()
