#!/usr/bin/env python3
"""Export a native-pixel square printed open/close MP4 from retained evidence.

python tests/nested/paper_delivery.py build/paper-evidence/<run>

No scene rerun, scaling, interpolation or frame-rate conversion. The MP4 uses
lossy H.264 yuv420p for playback compatibility; original lossless RGB VFR MKV,
frame timestamps and quantitative comparisons remain the verification source.
Dense runs also export a full-output four-window H.264 MP4; first run
paper_dense_cadence.py to retain its lossless native VFR source snippet.
"""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    directory = args.directory
    events = json.loads((directory / 'events.json').read_text())
    origin = next(event['t'] for event in events if event['kind'] == 'capture-start')
    case = next(event for event in events if event['kind'] == 'case' and event['name'] == 'native-text')
    ident = case['ids'][0]
    geometry = None
    for event in events:
        if event['kind'] == 'hyprctl' and event['command'] == ['-j', 'clients']:
            geometry = next((client for client in json.loads(event['response'])
                             if client['title'] == f'PaperEvidence-{ident}'), None)
            if geometry:
                break
    if geometry is None:
        raise RuntimeError('No native square-window geometry readback')
    x, y = geometry['at']
    width, height = geometry['size']
    if width != height:
        raise RuntimeError(('Printed delivery specimen is not square', width, height))
    duration = 1.2
    start = end = None
    for event in events:
        if event['kind'] == 'phase-duration-readback' and event['phase'] == 'close':
            duration = event['actual']['float'] / 1000
        if event['kind'] == 'scene' and event['command']['id'] == ident:
            if event['command']['op'] == 'open':
                start = max(0, event['t'] - origin - .1)
            elif event['command']['op'] == 'close':
                end = event['t'] - origin + duration + .2
                break
    if start is None or end is None:
        raise RuntimeError('Missing printed opening/closing transition bounds')
    video = directory / 'square-printed-open-close-native.mp4'
    subprocess.run(['ffmpeg', '-v', 'error', '-ss', str(start), '-i', directory / 'capture.mkv',
        '-t', str(end - start), '-vf', f'crop={width}:{height}:{x}:{y}', '-fps_mode', 'passthrough',
        '-enc_time_base', '1:1000', '-c:v', 'libx264', '-preset', 'medium', '-crf', '15',
        '-pix_fmt', 'yuv420p', '-video_track_timescale', '1000', '-movflags', '+faststart', '-y', video], check=True)
    dense_video = None
    if any(event['kind'] == 'case' and event['name'] == 'concurrent-dense' for event in events):
        dense_source = directory / 'dense-full-native-vfr.mkv'
        if not dense_source.is_file():
            raise RuntimeError('Run paper_dense_cadence.py first to retain the native dense VFR source')
        dense_video = directory / 'dense-four-open-close-native.mp4'
        subprocess.run(['ffmpeg', '-v', 'error', '-i', dense_source, '-fps_mode', 'passthrough',
            '-enc_time_base', '1:1000', '-c:v', 'libx264', '-preset', 'medium', '-crf', '15',
            '-pix_fmt', 'yuv420p', '-video_track_timescale', '1000', '-movflags', '+faststart',
            '-y', dense_video], check=True)
    result = dict(path=str(video), source='capture.mkv', source_pts_bounds=[start, end],
        source_plugin=json.loads((directory / 'manifest.json').read_text())['plugin_sha256'],
        specimen=ident, crop=[x, y, width, height], resized=False,
        dense_path=str(dense_video) if dense_video else None,
        limitation='Lossy playback artifact only; original RGB VFR capture is authoritative.')
    (directory / 'delivery.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
