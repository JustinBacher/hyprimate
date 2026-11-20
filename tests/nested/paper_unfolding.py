#!/usr/bin/env python3
"""Measure the last 35% of --unfolding captures without changing compositor state.

Requires NumPy, OpenCV, Pillow and FFmpeg. Track real printed details backwards
from the native crop, rejecting occluded/ambiguous tracks with a round trip.
Optical flow measures presentation, not solver velocity. A partially hidden
fold may not yield any track; use the material-space tests for that case.
"""
import argparse
import csv
import json
from pathlib import Path
import subprocess

import cv2
import numpy as np
from PIL import Image


def read_crops(directory, pts, start, end, box, native):
    left, top, width, height = box
    indices = np.flatnonzero((pts >= start) & (pts < end))
    crop = f'crop={width}:{height}:{left}:{top}'
    select = f'select=between(n\\,{indices[0]}\\,{indices[-1]})'
    frames, errors = [], []
    with subprocess.Popen(['ffmpeg', '-v', 'error', '-i', directory / 'capture.mkv',
            '-vf', f'{select},{crop}', '-frames:v', str(len(indices)), '-fps_mode', 'passthrough',
            '-enc_time_base', '1:1000', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'],
            stdout=subprocess.PIPE) as decoder:
        for _ in indices:
            data = decoder.stdout.read(width * height * 3)
            if len(data) != width * height * 3:
                raise ValueError('Incomplete lossless capture crop')
            rgb = np.frombuffer(data, np.uint8).reshape(height, width, 3)
            errors.append(cv2.norm(rgb, native, cv2.NORM_L1) / native.size)
            frames.append(cv2.cvtColor(rgb, cv2.COLOR_RGB2GRAY))
        if decoder.wait() != 0:
            raise RuntimeError('FFmpeg crop decode failed')
    return pts[indices], frames, np.array(errors)


def analyze_case(directory, case, start, hold_end, duration, pts, background):
    name = case['name']
    width, height = case['width'], case['height']
    native = np.asarray(Image.open(directory / f'{name}-native.png').convert('RGB'))
    # --unfolding positions each float at 40,40 with no decorations, scale 1.
    native = native[40:40 + height, 40:40 + width, :].copy()
    # The wall-clock event is approximate: recording can start hundreds of ms
    # after launch. Include the empty interval between successive windows.
    times, gray, errors = read_crops(directory, pts, start - 1.4, hold_end,
                                     (40, 40, width, height), native)
    exact = next((i for i in range(len(times) - 2) if np.all(errors[i:i+3] == 0)), None)
    if exact is None:
        raise ValueError(f'{name}: no three consecutive native matches while the effect was enabled')
    blank = background[40:40 + height, 40:40 + width]
    last_blank = next((i for i in range(exact - 1, -1, -1)
                       if np.count_nonzero(cv2.absdiff(gray[i], blank) > 8) <= 50), None)
    if last_blank is None or last_blank + 1 == exact:
        raise ValueError(f'{name}: recording lacks a visible opening after an empty crop')
    visible = times[last_blank + 1]
    cut = min(exact, np.searchsorted(times, visible + .65 * duration))
    if exact <= cut:
        raise ValueError(f'{name}: native crop already static by 65%')
    landmarks = cv2.goodFeaturesToTrack(gray[exact], maxCorners=160, qualityLevel=.01,
                                        minDistance=max(10, min(width, height) / 18))
    if landmarks is None:
        raise ValueError(f'{name}: native crop contains no printable landmarks')
    points = landmarks.copy()
    valid = np.ones(len(points), dtype=bool)
    poses = np.zeros((exact - cut + 1, len(points), 2), np.float32)
    poses[-1] = points[:, 0]
    for i in range(exact - 1, cut - 1, -1):
        prev, status, err = cv2.calcOpticalFlowPyrLK(gray[i+1], gray[i], points, None)
        back, back_status, _ = cv2.calcOpticalFlowPyrLK(gray[i], gray[i+1], prev, None)
        roundtrip = np.linalg.norm(points[:, 0] - back[:, 0], axis=1)
        valid &= status[:, 0].astype(bool) & back_status[:, 0].astype(bool) & (roundtrip < .6) & (err[:, 0] < 22)
        poses[i - cut] = prev[:, 0]
        points = prev
    if valid.sum() < 8:
        raise ValueError(f'{name}: insufficient tracks survived ({valid.sum()})')
    poses = poses[:, valid]
    distance = np.linalg.norm(poses - landmarks[valid, 0], axis=2)
    movement = np.linalg.norm(np.diff(poses, axis=0), axis=2)
    retreat = np.maximum(0, np.diff(distance, axis=0)).sum(axis=0)
    samples = []
    for j, i in enumerate(range(cut, exact + 1)):
        samples.append(dict(pts=float(times[i]), fraction=float((times[i] - visible) / duration),
                            rms_offset_px=float(np.sqrt(np.mean(distance[j]**2))),
                            p95_offset_px=float(np.percentile(distance[j], 95)),
                            rms_step_px=float(np.sqrt(np.mean(movement[j-1]**2))) if j else None,
                            native_rgb_mae=float(errors[i])))
    with (directory / f'{name}-late-trajectory.csv').open('w') as file:
        writer = csv.DictWriter(file, fieldnames=samples[0].keys())
        writer.writeheader()
        writer.writerows(samples)
    return dict(case=name, size=[width, height], duration=duration, visible_pts=float(visible),
                first_exact_pts=float(times[exact]), visible_to_exact_s=float(times[exact] - visible),
                tracked_features=int(valid.sum()), available_features=len(landmarks),
                p95_cumulative_retreat_px=float(np.percentile(retreat, 95)),
                last_live_mae=float(errors[exact-1]), last_live_p95_offset_px=float(np.percentile(distance[-2], 95)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    directory = parser.parse_args().directory
    cv2.setNumThreads(1)
    manifest = json.loads((directory / 'manifest.json').read_text())
    if not manifest.get('unfolding'):
        parser.error('Expected a --unfolding capture')
    events = json.loads((directory / 'events.json').read_text())
    origin = next(e['t'] for e in events if e['kind'] == 'capture-start')
    pts = np.array([float(f['best_effort_timestamp_time']) for f in json.loads((directory / 'frames.json').read_text())['frames']])
    output = manifest['capture_dimensions'][0]
    first_rgb = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', directory / 'capture.mkv',
                                         '-frames:v', '1', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'])
    background = cv2.cvtColor(np.frombuffer(first_rgb, np.uint8).reshape(
        output['height'], output['width'], 3), cv2.COLOR_RGB2GRAY)
    windows = json.loads((directory / 'changed-cadence.json').read_text())['animation_windows']
    reports = []
    for case in (e for e in events if e['kind'] == 'case'):
        specimen = case['ids'][0]
        opened = next(w for w in windows if w['id'] == specimen and w['op'] == 'open')
        # Do not count disabling Paper to acquire the reference as a successful physical handoff.
        held = next(e['t'] for e in events if e['kind'] == 'mapped-open-hold' and e['name'] == case['name'])
        reports.append(analyze_case(directory, case, opened['approximate_video_start'],
                                    held - origin, opened['duration_seconds'], pts, background))
    result = {'sha256': manifest['plugin_sha256'], 'first_seed': manifest['replay_seed'], 'cases': reports}
    text = json.dumps(result, indent=2)
    (directory / 'late-unfolding.json').write_text(text + '\n')
    print(text)


if __name__ == '__main__':
    main()
