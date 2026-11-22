"""Preview only: whole desktop leaves folding back from a shared spine."""

from math import cos, pi, radians, sin

_HEIGHT = 9 / 16
_FLAT = ((-1, _HEIGHT, 0), (1, _HEIGHT, 0), (1, -_HEIGHT, 0), (-1, -_HEIGHT, 0))
_CLOSED_YAW = radians(160)
_CAMERA_Z = 3.4  # Match the fixed perspective eye in renderer.py.
_BACK = (0.12, 0.13, 0.14)


def _panel(scene, left, right, material, shade, rim):
    lx, lz = left
    rx, rz = right
    # The sign of projected horizontal run tells us which side faces the eye.
    # Submit one opaque surface so the desktop and its back never fight for depth.
    if (rx - lx) * (_CAMERA_Z - lz) + lx * (rz - lz) < 0:
        material = _BACK
    scene.quad(((lx, _HEIGHT, lz), (rx, _HEIGHT, rz),
                (rx, -_HEIGHT, rz), (lx, -_HEIGHT, lz)),
               material, shade=shade, edge=rim)


def _fold_yaw(amount):
    # Keep the first leaf broad while the other opens. Mirroring this delayed
    # turn gives motion from nearly closed to wide open and back to closed.
    t = amount * amount * (3 - 2 * amount)
    return _CLOSED_YAW * t ** 2.23


def _geometry(p):
    a, b = _fold_yaw(p), _fold_yaw(1 - p)
    # Keep the crease near the broad leaf's endpoint until its neighbor opens.
    # Translation is in world space; the camera never tracks the crease.
    x = (b - a) / (a + b)
    clearance = 4 * p * (1 - p)
    z = -1.25 * clearance ** 3
    return ((x - 2 * cos(a), z - 2 * sin(a)), (x, z),
            (x + 2 * cos(b), z - 2 * sin(b)))


def draw(scene, p, *, show_other_workspaces=False):
    if p <= 0:
        scene.quad(_FLAT, "A")
        return
    if p >= 1:
        scene.quad(_FLAT, "B")
        return

    left, hinge, right = _geometry(p)
    # The horizontal runs are the cosines of the two yaws times desktop width.
    x, _ = hinge
    ca, cb = abs(x - left[0]) / 2, abs(right[0] - x) / 2
    travel = sin(pi * p)
    neighbors = []
    if show_other_workspaces:
        outer = radians(42 + 11 * travel)
        run, depth = 2 * cos(outer), 2 * sin(outer)
        for joint, side, material in ((left, -1, scene.before_source), (right, 1, scene.after_destination)):
            if material is not None:
                tip = joint[0] + side * run, joint[1] - depth
                neighbors.append((tip, joint, material) if side < 0 else (joint, tip, material))

    # An inventory that extends beyond the two wide-open covers needs a
    # wider shot. Translate all attached leaves together, never the camera.
    # A missing capture contributes neither a face nor a framing constraint.
    retreat = max((z + _CAMERA_Z * (abs(x)/.97 - 1)
                   for u, v, _ in neighbors for x, z in (u, v)), default=0.)
    retreat = max(0., retreat) * travel**4
    rim = 0.48 * travel

    def panel(u, v, material, shade):
        _panel(scene, (u[0], u[1]-retreat), (v[0], v[1]-retreat), material, shade, rim)
    panel(left, hinge, "A", 1 - 0.18 * (1 - ca))
    panel(hinge, right, "B", 1 - 0.18 * (1 - cb))
    for u, v, material in neighbors:
        panel(u, v, material, 0.9)
