#!/usr/bin/env python3
"""Compare early live closing texture orientation against the native window crop.

Uses existing lossless capture only; never changes compositor state. Each sampled
frame gets independent upright, vertical-flip, horizontal-flip and 180-degree
errors. Deformation raises all errors, so only near-flat samples establish FBO
orientation. Crops retain native pixels; row-strip errors expose labeled rows.
"""
import argparse
import json
from pathlib import Path
import subprocess
from PIL import Image, ImageChops, ImageStat


def error(a, b):
    return sum(ImageStat.Stat(ImageChops.difference(a, b)).mean) / 3


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--native', default='native-text-native.png')
    parser.add_argument('--box', type=int, nargs=4, default=(40, 40, 660, 660))
    parser.add_argument('--phase', choices=('opening', 'closing'), default='closing')
    parser.add_argument('--duration', type=float, default=1.2, help='Nominal configured opening duration')
    parser.add_argument('--id', type=int, help='Specimen id; defaults to the native-text case id')
    args = parser.parse_args()
    directory = args.directory
    native = Image.open(directory / args.native).convert('RGB').crop(args.box)
    native.save(directory / 'orientation-native-crop.png')
    variants = {'upright': native, 'vertical_flip': native.transpose(Image.Transpose.FLIP_TOP_BOTTOM),
                'horizontal_flip': native.transpose(Image.Transpose.FLIP_LEFT_RIGHT),
                'rotate_180': native.transpose(Image.Transpose.ROTATE_180)}
    events = json.loads((directory / 'events.json').read_text())
    origin = next(e['t'] for e in events if e['kind'] == 'capture-start')
    operation = 'open' if args.phase == 'opening' else 'close'
    specimen = args.id
    if specimen is None:
        case = next(e for e in events if e['kind'] == 'case' and e['name'] == 'native-text')
        specimen = case['ids'][0]
    transition = next(e['t'] - origin for e in events if e['kind'] == 'scene'
                      and e['command']['op'] == operation and e['command']['id'] == specimen)
    if args.phase == 'opening':
        transition += args.duration
    pts = [float(f['best_effort_timestamp_time']) for f in json.loads((directory / 'frames.json').read_text())['frames']]
    offsets = (-.15, -.10, -.067, -.034, -.017, 0, .017, .05) if args.phase == 'opening' else (-.05, -.017, 0, .017, .034, .067, .10, .15)
    indices = sorted({min(range(len(pts)), key=lambda i: abs(pts[i] - transition - delta)) for delta in offsets})
    samples = []
    for i in indices:
        path = directory / f'orientation-{args.phase}-frame-{i:04d}.png'
        subprocess.run(['ffmpeg', '-v', 'error', '-i', directory / 'capture.mkv',
            '-vf', f'select=eq(n\\,{i})', '-frames:v', '1', '-y', path], check=True)
        crop = Image.open(path).convert('RGB').crop(args.box)
        crop.save(directory / f'orientation-{args.phase}-crop-{i:04d}.png')
        strips = ((0, 0, crop.width, 80), (0, crop.height - 80, crop.width, crop.height))
        samples.append(dict(frame=i, pts=pts[i], relative_to_transition=pts[i] - transition,
            full_crop_error={key: error(crop, value) for key, value in variants.items()},
            labeled_row_strip_error=[{key: error(crop.crop(strip), value.crop(strip))
                                     for key, value in variants.items()} for strip in strips]))
    result = dict(window_extent=args.box, native_reference=args.native, phase=args.phase, specimen=specimen, samples=samples,
                  limitation='Clock-to-first-frame offset approximate; opening samples use nominal duration, not a verified worker/native handoff. Use earliest near-flat changed frame, not settled/native fallback. Deformed samples alone cannot prove orientation.')
    if args.phase == 'opening':
        # Inspect every native frame around nominal completion, not only the
        # sparse witness samples, to retain the actual penultimate live crop.
        scan = [i for i, t in enumerate(pts) if transition - .4 <= t <= transition + .2]
        if scan:
            left, top, right, bottom = args.box
            width, height = right - left, bottom - top
            raw = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', directory / 'capture.mkv',
                '-vf', f'select=between(n\\,{scan[0]}\\,{scan[-1]}),crop={width}:{height}:{left}:{top}',
                '-frames:v', str(len(scan)), '-fps_mode', 'passthrough', '-f', 'rawvideo', '-pix_fmt', 'rgb24', 'pipe:1'])
            stride = width * height * 3
            if len(raw) != len(scan) * stride:
                raise RuntimeError('Opening handoff frame count mismatch')
            handoff = []
            penultimate = None
            for offset, index in enumerate(scan):
                frame = Image.frombytes('RGB', (width, height), raw[offset * stride:(offset + 1) * stride])
                record = dict(frame=index, pts=pts[index], upright_mae=error(frame, native),
                              vertical_flip_mae=error(frame, variants['vertical_flip']))
                handoff.append(record)
                if record['upright_mae'] == 0:
                    if offset:
                        penultimate = handoff[-2]
                        previous = Image.frombytes('RGB', (width, height), raw[(offset - 1) * stride:offset * stride])
                        previous.save(directory / 'opening-penultimate-native-crop.png')
                    break
            result['handoff'] = dict(samples=handoff, penultimate=penultimate,
                first_exact_native=handoff[-1] if handoff[-1]['upright_mae'] == 0 else None,
                limitation='Exact equality cannot identify renderer versus compositor handoff.')
    (directory / f'orientation-{args.phase}.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
