"""Preview only: intact desktop planes with a recessed, traveling spine."""

from math import cos, pi, radians, sin

_HEIGHT = 9 / 16
_CAMERA = 3.4
_FLAT = ((-1, _HEIGHT, 0), (1, _HEIGHT, 0), (1, -_HEIGHT, 0), (-1, -_HEIGHT, 0))
_FOLD_YAW = radians(66)


def _panel(scene, left, right, material, shade, rim):
    lx, lz = left
    rx, rz = right
    scene.quad(((lx, _HEIGHT, lz), (rx, _HEIGHT, rz),
                (rx, -_HEIGHT, rz), (lx, -_HEIGHT, lz)),
               material, shade=shade, edge=rim)


def _fold_yaw(amount):
    # Each side turns early, so their combined yaw peaks at midpoint.
    return _FOLD_YAW * (1 - (1 - amount) ** 3.25)


def draw(scene, p, *, show_other_workspaces=False):
    if p <= 0:
        scene.quad(_FLAT, "A")
        return
    if p >= 1:
        scene.quad(_FLAT, "B")
        return

    # Retreat while turning the free edges toward the viewer: the shared
    # spine is always the bottom of a valley, never the front of a solid.
    travel = sin(pi * p)
    z = -3.8 * travel * (0.7 + 0.3 * travel)
    # Slow the spine's horizontal travel during the deepest part of the fold
    # so both outer edges have room to enter the frame.
    screen_x = (1 - 2 * p) * (1 - 0.8 * travel * travel)
    x = screen_x * (1 - z / _CAMERA)
    hinge = x, z
    a, b = _fold_yaw(p), _fold_yaw(1 - p)
    ca, cb = cos(a), cos(b)
    left = x - 2 * ca, z + 2 * sin(a)
    right = x + 2 * cb, z + 2 * sin(b)
    rim = 0.48 * travel
    _panel(scene, left, hinge, "A", 1 - 0.18 * (1 - ca), rim)
    _panel(scene, hinge, right, "B", 1 - 0.18 * (1 - cb), rim)

    if show_other_workspaces:
        before = getattr(scene, "before_source", None)
        after = getattr(scene, "after_destination", None)
        # The outer leaves counterfold behind the primary free edges. Increase
        # their yaw as the spine retreats so the full chain fits at midpoint.
        outer = radians(36 + 24 * travel)
        run, depth = 2 * cos(outer), 2 * sin(outer)
        if before is not None:
            lx, lz = left
            _panel(scene, (lx - run, lz - depth), left, before, 0.9, rim)
        if after is not None:
            rx, rz = right
            _panel(scene, right, (rx + run, rz - depth), after, 0.9, rim)
