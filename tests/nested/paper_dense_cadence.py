#!/usr/bin/env python3
"""Hash each dense window interior separately using verified native client geometry.

python tests/nested/paper_dense_cadence.py build/paper-evidence/<run>

Native source PTS are retained. The first .1s and endpoint holds are excluded.
Each crop is inset 20px from the actual client rect, excluding window borders.
These hashes prove changed presentations inside each window's original extent,
not distinct physics publications. Folded portions outside that crop are not
measured. An overlapping deformed neighbour can contaminate a crop; fixed client
rectangles are checked disjoint, but dynamic fold overlap is not inferred away.
"""
import argparse
import json
from pathlib import Path
import statistics
import subprocess


def metrics(gaps):
    gaps = sorted(gaps)
    return dict(median_ms=statistics.median(gaps) if gaps else None,
                p95_ms=gaps[int((len(gaps) - 1) * .95)] if gaps else None,
                max_ms=max(gaps) if gaps else None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    directory = args.directory
    events = json.loads((directory / 'events.json').read_text())
    origin = next(event['t'] for event in events if event['kind'] == 'capture-start')
    case = next(event for event in events if event['kind'] == 'case' and event['name'] == 'concurrent-dense')
    ids = set(case['ids'])
    geometries = None
    for event in events:
        if event['kind'] != 'hyprctl' or event['command'] != ['-j', 'clients']:
            continue
        clients = json.loads(event['response'])
        if len(clients) == len(ids) and {client['title'] for client in clients} == {f'PaperEvidence-{i}' for i in ids}:
            geometries = clients
            break
    if geometries is None:
        raise RuntimeError('No verified four-window native geometry readback')
    pts = [float(frame['best_effort_timestamp_time']) for frame in
           json.loads((directory / 'frames.json').read_text())['frames']]
    durations = {'open': 1.2, 'close': 1.2}
    transitions = {}
    for event in events:
        if event['kind'] == 'phase-duration-readback':
            durations[event['phase']] = event['actual']['float'] / 1000
        if event['kind'] == 'scene' and event['command']['id'] in ids:
            transitions[(event['command']['id'], event['command']['op'])] = (event['t'] - origin, durations[event['command']['op']])
    records = []
    rectangles = []
    for client in geometries:
        ident = int(client['title'].rsplit('-', 1)[1])
        x, y = client['at']
        width, height = client['size']
        ordinal = case['ids'].index(ident)
        expected = (40 + ordinal % 2 * 610, 40 + ordinal // 2 * 340, case['width'], case['height'])
        if (x, y, width, height) != expected:
            raise RuntimeError(('native client geometry differs from specimen placement', ident, (x, y, width, height), expected))
        rectangle = (x + 20, y + 20, x + width - 20, y + height - 20)
        if width <= 40 or height <= 40:
            raise RuntimeError('Window too small for interior crop')
        for a, b, c, d in rectangles:
            if max(a, rectangle[0]) < min(c, rectangle[2]) and max(b, rectangle[1]) < min(d, rectangle[3]):
                raise RuntimeError('Dense native interior crops overlap')
        rectangles.append(rectangle)
        begin = transitions[(ident, 'open')][0]
        end = sum(transitions[(ident, 'close')])
        indices = [i for i, t in enumerate(pts) if begin <= t <= end]
        first, last = indices[0], indices[-1]
        path = directory / f'dense-window-{ident}-framemd5.txt'
        subprocess.run(['ffmpeg', '-v', 'error', '-i', directory / 'capture.mkv',
            '-vf', f'select=between(n\\,{first}\\,{last}),crop={width - 40}:{height - 40}:{x + 20}:{y + 20}',
            '-frames:v', str(len(indices)), '-fps_mode', 'passthrough', '-f', 'framemd5', '-y', path], check=True)
        hashes = [line.rsplit(',', 1)[-1].strip() for line in path.read_text().splitlines()
                  if line and not line.startswith('#')]
        if len(hashes) != len(indices):
            raise RuntimeError('Dense cropped hash/source PTS count mismatch')
        changed = {first + i for i in range(1, len(hashes)) if hashes[i] != hashes[i - 1]}
        for operation in ('open', 'close'):
            at, duration = transitions[(ident, operation)]
            selected = [i for i in indices if at + .1 <= pts[i] < at + duration]
            changed_pts = [pts[i] for i in selected if i in changed]
            all_pts = [pts[i] for i in selected]
            records.append(dict(id=ident, operation=operation, native_rectangle=[x, y, width, height],
                interior_crop=list(rectangle), nominal_video_start=at, measured_start=at + .1,
                measured_end=at + duration, pts_frames=len(selected), changed_frames=len(changed_pts),
                repeated_frames=len(selected) - len(changed_pts),
                changed_frames_per_second=len(changed_pts) / (duration - .1),
                pts_gaps=metrics([(b - a) * 1000 for a, b in zip(all_pts, all_pts[1:])]),
                changed_gaps=metrics([(b - a) * 1000 for a, b in zip(changed_pts, changed_pts[1:])])))
    begin = min(transitions[(ident, 'open')][0] for ident in ids)
    end = max(sum(transitions[(ident, 'close')]) for ident in ids)
    subprocess.run(['ffmpeg', '-v', 'error', '-ss', str(max(0, begin - .1)), '-i', directory / 'capture.mkv',
        '-t', str(end - max(0, begin - .1) + .1), '-fps_mode', 'passthrough',
        '-enc_time_base', '1:1000', '-c:v', 'ffv1',
        '-y', directory / 'dense-full-native-vfr.mkv'], check=True)
    result = dict(windows=records, full_clip='dense-full-native-vfr.mkv',
        source_plugin=json.loads((directory / 'manifest.json').read_text())['plugin_sha256'],
        limitations=['Native PTS/event offset is approximate', 'Presentation hashes are not physics-state timestamps',
            'Fixed interior crops exclude border/edge motion and outside folds',
            'Native crop rectangles are disjoint; dynamic neighbour overlap cannot be excluded by hashing alone',
            'First .1s and all endpoint holds excluded'])
    (directory / 'dense-window-cadence.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
