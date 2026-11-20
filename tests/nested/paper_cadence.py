#!/usr/bin/env python3
"""Measure exact decoded frame changes independently of recorder presentation PTS.

Usage: python tests/nested/paper_cadence.py build/paper-evidence/<run>
Identical consecutive RGB frames are repeats. Whole-frame changes are evidence
of a changed presentation, not proof that the physics state advanced: animation
opacity, other compositor effects or a cursor can also change pixels.
"""
import json
from pathlib import Path
import statistics
import subprocess
import sys


def analyze(directory):
    directory = Path(directory)
    hashes = directory / 'framemd5.txt'
    subprocess.run(['ffmpeg', '-v', 'error', '-i', directory / 'capture.mkv',
        '-map', '0:v:0', '-fps_mode', 'passthrough', '-f', 'framemd5', '-y', hashes], check=True)
    checksums = [line.rsplit(',', 1)[-1].strip() for line in hashes.read_text().splitlines()
                 if line and not line.startswith('#')]
    frames = json.loads((directory / 'frames.json').read_text())['frames']
    if len(checksums) != len(frames):
        raise RuntimeError(('decoded hash/frame count mismatch', len(checksums), len(frames)))
    pts = [float(frame['best_effort_timestamp_time']) for frame in frames]
    changed = [i for i in range(1, len(checksums)) if checksums[i] != checksums[i - 1]]
    changed_set = set(changed)
    events = json.loads((directory / 'events.json').read_text())
    origin = next(event['t'] for event in events if event['kind'] == 'capture-start')
    samples = []
    stalls = []
    durations = {'open': 1.2, 'close': 1.2}
    for event in events:
        if event['kind'] == 'phase-duration-readback':
            durations[event['phase']] = event['actual']['float']
        if event['kind'] != 'scene' or event['command']['op'] not in ('open', 'close'):
            continue
        begin = event['t'] - origin
        duration = durations[event['command']['op']]
        selected = [i for i, t in enumerate(pts) if begin <= t < begin + duration]
        changes = [pts[i] for i in selected if i in changed_set]
        gaps = sorted((b - a) * 1000 for a, b in zip(changes, changes[1:]))
        stalls.extend(dict(id=event['command']['id'], op=event['command']['op'],
                           start_pts=a, end_pts=b, gap_ms=(b - a) * 1000)
                      for a, b in zip(changes, changes[1:]) if b - a > .1)
        samples.append(dict(id=event['command']['id'], op=event['command']['op'],
            approximate_video_start=begin, duration_seconds=duration,
            pts_frames=len(selected), changed_frames=len(changes),
            repeated_frames=len(selected) - len(changes), changed_frames_per_second=len(changes) / duration,
            changed_gap_median_ms=statistics.median(gaps) if gaps else None,
            changed_gap_p95_ms=gaps[int((len(gaps) - 1) * .95)] if gaps else None,
            changed_gap_max_ms=max(gaps) if gaps else None))
    result = dict(total_frames=len(frames), changed_frames=len(changed),
        repeated_frames=len(frames) - 1 - len(changed), animation_windows=samples,
        presentation_stalls_over_100ms=stalls,
        limitation='Full decoded frame hashes; not solver-state timestamps. Window boundaries approximate first-frame origin.')
    (directory / 'changed-cadence.json').write_text(json.dumps(result, indent=2))
    return result


if __name__ == '__main__':
    print(json.dumps(analyze(sys.argv[1]), indent=2))
