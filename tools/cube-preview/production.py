"""Encode the approved R2 shots with the real production GLES cube material."""

import argparse
import os
from pathlib import Path
import subprocess as sp
import tempfile

from PIL import Image, ImageChops, ImageStat

from desktops import make_desktops
from render import CLEAN_LEAD, CLEAN_TAIL, CUE_FADE, CUE_HOLD, ENCODE, slate, timeline


VARIANTS = ('solid', 'inside-out')


def verify_endpoints(frame, images, name):
    endpoints = []
    for backward in (False, True):
        for p in (0, 1):
            expected = images[p ^ backward]
            rgb = frame(backward, p)
            if rgb != expected.tobytes():
                raise ValueError(f'{name}: endpoint p={p}, backward={backward} is not pixel-exact')
            near = Image.frombytes('RGB', expected.size, frame(backward, .9999 if p else .0001))
            error = sum(ImageStat.Stat(ImageChops.difference(near, expected)).mean) / 3
            if error > 1.25:
                raise ValueError(f'{name}: discontinuity at p={p}, backward={backward} ({error:.2f} RGB levels)')
            print(f'{name} backward={backward} p={p}: exact endpoint, near error={error:.3f}', flush=True)
            if not backward:
                endpoints.append(Image.frombytes('RGB', expected.size, rgb))
    return endpoints


def encode(path, driver, name, images, shots):
    width, height = size = images[0].size
    frame_bytes = width * height * 3
    with sp.Popen([driver, name, str(width), str(height)], stdin=sp.PIPE, stdout=sp.PIPE,
                  env={**os.environ, 'EGL_PLATFORM': 'surfaceless'}) as renderer:
        for image in images:
            renderer.stdin.write(image.convert('RGBA').tobytes())
        renderer.stdin.flush()

        def frame(backward, progress):
            renderer.stdin.write(f'{int(backward)} {progress:.17g}\n'.encode('ascii'))
            renderer.stdin.flush()
            rgb = renderer.stdout.read(frame_bytes)
            if len(rgb) != frame_bytes:
                raise RuntimeError(f'{name}: incomplete production frame at backward={backward}, p={progress}')
            return rgb

        endpoints = verify_endpoints(frame, images, name)
        args = ['ffmpeg', '-v', 'error', '-n', '-f', 'rawvideo', '-pixel_format', 'rgb24',
                '-video_size', f'{width}x{height}', '-framerate', '60', '-i', '-',
                '-vf', 'scale=out_color_matrix=bt709:out_range=tv', *ENCODE,
                '-metadata', 'title=Hyprimate / production / ' + name, path]
        with sp.Popen(args, stdin=sp.PIPE) as encoder:
            count = 0

            def hold(rgb, n):
                nonlocal count
                for _ in range(n):
                    encoder.stdin.write(rgb)
                count += n

            for (slow, shot, backward), samples in shots.items():
                base = endpoints[int(backward)]
                cue = slate(base, name, slow, shot)
                hold(cue.tobytes(), CUE_HOLD)
                for k in range(1, CUE_FADE + 1):
                    hold(Image.blend(cue, base, k / (CUE_FADE + 1)).tobytes(), 1)
                hold(base.tobytes(), CLEAN_LEAD)
                print(f'{name} {slow=} {shot=} motion {count}..{count + len(samples) - 1}', flush=True)
                for progress in samples:
                    hold(frame(backward, progress), 1)
                hold(endpoints[round(samples[-1]) ^ int(backward)].tobytes(), CLEAN_TAIL)
            encoder.stdin.close()
            if encoder.wait() != 0:
                raise RuntimeError(f'{name}: FFmpeg encode failed')
        renderer.stdin.close()
        if renderer.wait() != 0:
            raise RuntimeError(f'{name}: production driver failed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--driver', type=Path, required=True, help='built cube-production-driver executable')
    parser.add_argument('--only', nargs='+', choices=VARIANTS, default=VARIANTS)
    args = parser.parse_args()
    targets = {name: args.output / f'production-{name}.mp4' for name in args.only}
    for target in targets.values():
        if target.exists():
            parser.error(f'{target} already exists; choose a new output directory')
    args.output.mkdir(parents=True, exist_ok=True)
    driver = args.driver.resolve(strict=True)
    images = make_desktops()
    shots = timeline()
    with tempfile.TemporaryDirectory(prefix='.cube-production-', dir=args.output) as staging:
        for name, target in targets.items():
            encoded = Path(staging) / target.name
            encode(encoded, driver, name, images, shots)
            target.hardlink_to(encoded)


if __name__ == '__main__':
    main()
