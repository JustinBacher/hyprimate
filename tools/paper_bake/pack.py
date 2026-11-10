"""Pack baked paper takes into the asset the plugin embeds.

    python tools/paper_bake/pack.py assets/paper_takes.bin take1.bin take2.bin ...

Input takes come from bake.py. Each take is first held still: the simulated sheet drifts and tumbles a little as it
collapses, and windows must crumple in place, facing the viewer. Then it is trimmed to the span where the sheet
actually moves (the pull builds up slowly, and the finished ball barely settles) and resampled to PACK_FRAMES frames.
Every step is smooth in time: the plugin plays the frames back with a cubic spline, and any frame-to-frame noise
added here would show as jitter.

Output (little-endian): magic b"PCRT", u32 version, u32 grid, u32 frames, u32 takes, then per take f32 aspect,
then per take frames * grid * grid * 3 int16 positions, each the position in units of sqrt(W * H) times
POSITION_SCALE. Frame 0 is the exact flat sheet, so playback starts at the window's rest shape.
"""

import struct
import sys

import numpy as np

VERSION = 1
POSITION_SCALE = 16384.0  # int16 covers +-2 sqrt(W*H); sheets reach about 0.85
PACK_FRAMES = 48
STILL = 0.003  # units of sqrt(W*H): largest vertex move that still counts as "not moving yet / any more"
TIME_SHARE = 0.3  # share of playback paced by sim time rather than by shrinking
EASE_FRAMES = 10  # frames over which the creases deepen from the flat sheet
SHAKE_SMOOTHING = 3.5  # sim frames (Gaussian sigma): removes the cloth solver's wobble, not the folds
PACE_SMOOTHING = 8.0  # sim frames (Gaussian sigma) over which the shrink-paced clock is averaged


def read_take(path):
    raw = open(path, "rb").read()
    if raw[:4] != b"PCRB":
        raise SystemExit(f"{path}: not a baked take")
    _, grid, frames, aspect = struct.unpack("<IIIf", raw[4:20])
    positions = np.frombuffer(raw[20:], dtype="<f4").reshape(frames, grid * grid, 3).astype(np.float64)
    return grid, frames, aspect, positions


def flat(grid, aspect):
    u = np.arange(grid) / (grid - 1)
    uu, vv = np.meshgrid(u, u)
    return np.stack([(uu - 0.5) * np.sqrt(aspect), (vv - 0.5) / np.sqrt(aspect), np.zeros_like(uu)], -1).reshape(-1, 3)


def smoothed(values, sigma):
    """Gaussian smoothing along axis 0, holding the end values (no fade towards zero at the ends)."""
    reach = int(np.ceil(3.0 * sigma))
    kernel = np.exp(-0.5 * (np.arange(-reach, reach + 1) / sigma) ** 2)
    kernel /= kernel.sum()
    padded = np.concatenate([np.repeat(values[:1], reach, 0), values, np.repeat(values[-1:], reach, 0)])
    return np.apply_along_axis(lambda r: np.convolve(r, kernel, "valid"), 0, padded)


def best_turn(a, b):
    """Rotation r (row vectors) minimising |a r - b| over centred point sets (Kabsch)."""
    u, _, vt = np.linalg.svd(a.T @ b)
    return u @ np.diag([1.0, 1.0, np.sign(np.linalg.det(u @ vt))]) @ vt



def held_still(positions):
    """Removes the take's rigid drift, spin and shake, keeping only the crumpling.

    The cloth solver shakes the whole sheet a little every frame, besides tumbling it slowly. Each frame is centred
    and turned to best fit the previous, already turned frame (Kabsch), which removes both. What is left still
    wobbles: the stiff springs make each vertex swing back and forth over a few frames on its way in, where paper
    would move steadily. A Gaussian over SHAKE_SMOOTHING frames removes about a fifth of all vertex travel that way;
    folds take longer than that to form and keep their shape."""
    out = positions - positions.mean(axis=1, keepdims=True)
    for k in range(1, len(out)):
        out[k] = out[k] @ best_turn(out[k], out[k - 1])
    return smoothed(out, SHAKE_SMOOTHING)


def trimmed(positions):
    """Resamples the moving span of a take to PACK_FRAMES frames, starting from the flat sheet.

    The sim pulls in hard at first and then stalls before the collapse, so evenly spaced frames would pop and then
    hang. Frames are spaced instead by how much the sheet's size (RMS radius) has changed, with a share of plain
    time kept so creasing that does not shrink the sheet still gets screen time. The clock is smoothed so playback
    speed changes gradually rather than per frame."""
    start_moves = np.abs(positions - positions[0]).max(axis=(1, 2)) > STILL
    end_moves = np.abs(positions - positions[-1]).max(axis=(1, 2)) > STILL
    first = max(int(np.argmax(start_moves)) - 1, 0)
    last = len(positions) - 1 - int(np.argmax(end_moves[::-1])) + 1
    last = min(max(last, first + 1), len(positions) - 1)
    span = positions[first:last + 1]
    radius = np.sqrt((span ** 2).sum(axis=2).mean(axis=1))
    rate = smoothed(np.abs(np.gradient(radius)), PACE_SMOOTHING)
    shrunk = np.concatenate([[0.0], np.cumsum(0.5 * (rate[1:] + rate[:-1]))])
    progress = (1.0 - TIME_SHARE) * shrunk / shrunk[-1] + TIME_SHARE * np.linspace(0.0, 1.0, len(span))
    times = first + np.interp(np.linspace(0.0, 1.0, PACK_FRAMES), progress, np.arange(len(span)))
    lo = np.minimum(np.floor(times).astype(int), len(positions) - 2)
    blend = (times - lo)[:, None, None]
    return positions[lo] + (positions[lo + 1] - positions[lo]) * blend, first, last


def process(grid, aspect, positions):
    """Baked take -> the PACK_FRAMES frames the plugin plays; also returns the sim span used."""
    packed, first, last = trimmed(held_still(positions))
    # The sim creases the whole sheet within its first few frames, so played as is, a window finishing its open
    # would pop from fully creased to flat. Ease the departure from the flat sheet in instead, so the creases deepen
    # gradually; this also removes the bake's seed relief from frame 0.
    rest = flat(grid, aspect)
    for k in range(EASE_FRAMES):
        x = k / EASE_FRAMES
        packed[k] = rest + (packed[k] - rest) * (x * x * (3.0 - 2.0 * x))
    return packed, first, last


def main():
    out, inputs = sys.argv[1], sys.argv[2:]
    takes = [read_take(p) for p in inputs]
    grid = takes[0][0]
    if any(t[0] != grid for t in takes):
        raise SystemExit("takes disagree on grid size")
    with open(out, "wb") as f:
        f.write(b"PCRT")
        f.write(struct.pack("<IIII", VERSION, grid, PACK_FRAMES, len(takes)))
        for _, _, aspect, _ in takes:
            f.write(struct.pack("<f", aspect))
        for path, (_, frames, aspect, positions) in zip(inputs, takes):
            packed, first, last = process(grid, aspect, positions)
            q = np.round(packed * POSITION_SCALE)
            if np.abs(q).max() > 32767:
                raise SystemExit("position out of int16 range")
            f.write(q.astype("<i2").tobytes())
            print(f"{path}: aspect {aspect:g}, frames {first}..{last} of {frames}")
    print(f"wrote {out}: {len(takes)} takes, {PACK_FRAMES} frames, grid {grid}")


main()
