#!/usr/bin/env python3
"""Generates a torn sheet of cream notebook paper as a transparent PNG for kitty's background_image.

Usage: make_paper.py OUT.png [--size WxH] [--seed N]

The sheet has torn, fibrous edges that leave the corners transparent, a soft drop shadow, a strip of
tape, a hand-drawn cat and a handwritten note. Generate it at the window's real size: kitty stretches the
image over the whole window, so the doodles would distort at any other size.
"""
import argparse
import math

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

MARGIN = 16  # transparent border that holds the shadow and the tear
INK = (74, 70, 64, 255)
TITLE_RULE_Y = 80  # under the kitty tab bar, which paper_desk.sh places by tab_bar_margin_height and the font size
FONT = "/usr/share/fonts/TTF/FantasqueSansMNerdFontMono-Italic.ttf"


def value_noise(rng, w, h, cells):
    gh = max(2, round(cells * h / max(w, h)) + 2)
    gw = max(2, round(cells * w / max(w, h)) + 2)
    grid = rng.random((gh, gw)).astype(np.float32)
    img = Image.fromarray((grid * 255).astype(np.uint8), "L").resize((w, h), Image.BICUBIC)
    return np.asarray(img, dtype=np.float32) / 255.0


def fbm(rng, w, h, base_cells, octaves, gain=0.5):
    total = np.zeros((h, w), np.float32)
    amp, norm = 1.0, 0.0
    for o in range(octaves):
        total += amp * value_noise(rng, w, h, base_cells * 2**o)
        norm += amp
        amp *= gain
    return total / norm


def shade(strength, tint):
    """Per-pixel multiplier: 1 where strength is 0, `tint` where it is 1."""
    return 1.0 - strength[..., None] * (1.0 - tint)


def tear_profile(rng, n, amp):
    """Inward offset in pixels along one edge: linear interpolation of coarse-to-fine random steps, so the tear is jagged."""
    out = np.zeros(n, np.float32)
    for cells, a in ((5, 0.35), (22, 0.30), (80, 0.22), (260, 0.13)):
        g = rng.random(cells + 2).astype(np.float32)
        out += a * np.interp(np.linspace(0, cells, n), np.arange(cells + 2), g)
    return out * amp


def paper_layer(rng, w, h):
    """RGBA of the sheet itself (straight alpha) and its float coverage mask."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    left = tear_profile(rng, h, 16)
    right = tear_profile(rng, h, 16)
    top = tear_profile(rng, w, 16)
    bottom = tear_profile(rng, w, 16)
    d = np.minimum.reduce([
        xx - (MARGIN + left[yy.astype(int)]),
        (w - 1 - MARGIN - right[yy.astype(int)]) - xx,
        yy - (MARGIN + top[xx.astype(int)]),
        (h - 1 - MARGIN - bottom[xx.astype(int)]) - yy,
    ])
    cover = np.clip(d + 0.5, 0, 1)

    base = np.array([237, 228, 208], np.float32)
    mottle = fbm(rng, w, h, 3, 5)
    img = base * (1.0 + 0.07 * (mottle - 0.5))[..., None]
    img *= (1.0 + 0.045 * (fbm(rng, w, h, 160, 3, 0.6) - 0.5))[..., None]            # fibres
    img *= (1.0 + rng.normal(0, 0.008, (h, w)).astype(np.float32))[..., None]          # grain

    # Torn rim: pale exposed fibres just inside the edge, a hairline of shadow right at it.
    fibre_n = 0.55 + 0.45 * value_noise(rng, w, h, 400)
    rim = np.exp(-np.clip(d, 0, None) / 2.4) * fibre_n * (d > 0.5)
    img = img + rim[..., None] * (np.array([250, 245, 232], np.float32) - img) * 0.65
    line = np.exp(-(((d - 0.6) / 0.8) ** 2))
    img *= (1.0 - 0.10 * line)[..., None]
    # Faint ageing toward the border.
    age = np.clip(1.0 - d / 70.0, 0, 1) ** 2
    img *= shade(0.5 * age, np.array([0.95, 0.92, 0.80], np.float32))

    rgba = np.dstack([np.clip(img, 0, 255), cover * 255]).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA"), cover


def smooth(pts, iterations=2, closed=False):
    """Chaikin corner cutting."""
    p = [tuple(q) for q in pts]
    for _ in range(iterations):
        out = []
        n = len(p)
        rng_ = range(n) if closed else range(n - 1)
        for i in rng_:
            a, b = p[i], p[(i + 1) % n]
            out.append((0.75 * a[0] + 0.25 * b[0], 0.75 * a[1] + 0.25 * b[1]))
            out.append((0.25 * a[0] + 0.75 * b[0], 0.25 * a[1] + 0.75 * b[1]))
        p = out if closed else [p[0]] + out + [p[-1]]
    return p


class Sketch:
    """Draws wobbly pen strokes onto a supersampled canvas, then downsamples it."""

    def __init__(self, rng, w, h, ss=4):
        self.rng, self.w, self.h, self.ss = rng, w, h, ss
        self.img = Image.new("RGBA", (w * ss, h * ss), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.img)

    def stroke(self, pts, width=2.0, color=INK, closed=False, wobble=0.7):
        ss = self.ss
        pts = smooth(pts, 3, closed)
        if closed:
            pts = pts + [pts[0]]
        out = []
        for i, (x, y) in enumerate(pts):
            out.append(((x + math.sin(i * 0.9) * wobble * 0.5 + self.rng.normal(0, wobble * 0.25)) * ss,
                        (y + math.cos(i * 1.1) * wobble * 0.5 + self.rng.normal(0, wobble * 0.25)) * ss))
        self.d.line(out, fill=color, width=round(width * ss), joint="curve")
        r = width * ss / 2
        for x, y in (out[0], out[-1]):
            self.d.ellipse((x - r, y - r, x + r, y + r), fill=color)

    def dot(self, x, y, r, color=INK):
        s = self.ss
        self.d.ellipse(((x - r) * s, (y - r) * s, (x + r) * s, (y + r) * s), fill=color)

    def done(self):
        return self.img.resize((self.w, self.h), Image.LANCZOS)


def ellipse_pts(cx, cy, rx, ry, n=14, start=0.0, sweep=2 * math.pi):
    return [(cx + rx * math.cos(start + sweep * i / n), cy + ry * math.sin(start + sweep * i / n)) for i in range(n + 1)]


def cat(rng):
    """A sitting cat with a heart, about 190x190."""
    s = Sketch(rng, 190, 190)
    # Head outline broken at the ears, then the ears.
    s.stroke([(30, 84), (24, 62), (30, 38), (48, 28), (70, 24), (95, 24), (118, 28), (136, 38), (142, 62), (136, 84)])
    s.stroke([(30, 38), (28, 12), (46, 24)])
    s.stroke([(142, 38), (146, 12), (126, 26)])
    # Face.
    s.dot(63, 54, 3.2)
    s.dot(109, 54, 3.2)
    s.stroke([(82, 64), (90, 64), (86, 70), (82, 64)], 1.6)
    s.stroke([(86, 70), (80, 77), (74, 74)], 1.6)
    s.stroke([(86, 70), (92, 77), (99, 74)], 1.6)
    for dy, tilt in ((60, -4), (68, 0), (76, 5)):
        s.stroke([(54, dy), (36, dy + tilt)], 1.3)
        s.stroke([(118, dy), (136, dy + tilt)], 1.3)
    # Body, front paws, tail.
    s.stroke([(34, 84), (28, 120), (36, 160), (52, 170), (88, 172)], 2.0)
    s.stroke([(136, 84), (144, 120), (138, 160), (122, 170), (88, 172)], 2.0)
    s.stroke([(70, 150), (72, 172)], 1.6)
    s.stroke([(104, 150), (102, 172)], 1.6)
    s.stroke([(138, 150), (160, 156), (176, 142), (172, 120), (160, 112)], 1.8)
    # Heart.
    heart = [(0.0, -0.35), (0.35, -0.75), (0.8, -0.5), (0.7, 0.0), (0.0, 0.7), (-0.7, 0.0), (-0.8, -0.5), (-0.35, -0.75), (0.0, -0.35)]
    s.stroke([(166 + 11 * x, 40 + 11 * y) for x, y in heart], 1.8, (206, 98, 98, 255), wobble=0.4)
    return s.done()


def text_note(lines, size, angle, color):
    font = ImageFont.truetype(FONT, size)
    pad = 20
    tmp = ImageDraw.Draw(Image.new("L", (1, 1)))
    wmax = max(tmp.textlength(l, font=font) for l in lines)
    lh = size * 1.25
    canvas = Image.new("RGBA", (int(wmax) + 2 * pad, int(lh * len(lines)) + 2 * pad), (0, 0, 0, 0))
    d = ImageDraw.Draw(canvas)
    for i, l in enumerate(lines):
        d.text((pad, pad + i * lh), l, font=font, fill=color)
    return canvas.rotate(angle, resample=Image.BICUBIC, expand=True)


def smiley(rng):
    s = Sketch(rng, 60, 50)
    s.dot(20, 12, 1.5)
    s.dot(32, 12, 1.5)
    s.stroke(ellipse_pts(26, 14, 11, 10, 10, math.pi * 0.15, math.pi * 0.7), 1.6)
    return s.done()


def tape(rng, length, width, angle):
    ss = 3
    pad = 12
    w, h = (length + 2 * pad) * ss, (width + 2 * pad) * ss
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    # Torn ends: a zig-zag on each short side.
    top, bot = pad * ss, (pad + width) * ss
    left_pts = [(pad * ss + int(rng.integers(0, 4 * ss)), top + (bot - top) * i // 6) for i in range(7)]
    right_pts = [((pad + length) * ss - int(rng.integers(0, 4 * ss)), top + (bot - top) * i // 6) for i in range(7)]
    poly = left_pts + right_pts[::-1]
    d = ImageDraw.Draw(img)
    d.polygon(poly, fill=(226, 208, 164, 150))
    d.line(poly + [poly[0]], fill=(196, 176, 130, 110), width=ss)
    img = img.filter(ImageFilter.GaussianBlur(0.6 * ss)).resize((w // ss, h // ss), Image.LANCZOS)
    return img.rotate(angle, resample=Image.BICUBIC, expand=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--size", default="1284x987")
    ap.add_argument("--seed", type=int, default=3)
    args = ap.parse_args()
    w, h = (int(v) for v in args.size.lower().split("x"))
    rng = np.random.default_rng(args.seed)

    paper, cover = paper_layer(rng, w, h)

    shadow_mask = Image.fromarray((cover * 255).astype(np.uint8), "L")
    shadow_mask = shadow_mask.transform(shadow_mask.size, Image.AFFINE, (1, 0, -3, 0, 1, -6))  # shifted down-right
    shadow_mask = shadow_mask.filter(ImageFilter.GaussianBlur(7))
    shadow = Image.new("RGBA", (w, h), (40, 30, 20, 0))
    shadow.putalpha(shadow_mask.point(lambda v: int(v * 0.38)))

    out = Image.alpha_composite(shadow, paper)

    note = text_note(["Good", "Ideas", "Take", "Time..."], 34, 12, (96, 92, 86, 255))
    out.alpha_composite(note, (int(w * 0.86 - note.width / 2), int(h * 0.47)))
    face = smiley(rng)
    out.alpha_composite(face, (int(w * 0.91), int(h * 0.72)))
    out.alpha_composite(cat(rng), (int(w - MARGIN - 34 - 190), int(h - MARGIN - 28 - 190)))
    out.alpha_composite(tape(rng, 120, 30, 18), (int(w * 0.80), MARGIN - 18))
    rule = Sketch(rng, w, 14)
    x0, x1 = MARGIN + 36, w - MARGIN - 36
    rule.stroke([(x0 + (x1 - x0) * i / 16, 7) for i in range(17)], 1.4, (150, 140, 120, 130), wobble=0.5)
    out.alpha_composite(rule.done(), (0, TITLE_RULE_Y - 7))

    out.save(args.out)


if __name__ == "__main__":
    main()
