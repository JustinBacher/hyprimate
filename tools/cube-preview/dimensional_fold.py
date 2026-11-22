"""Preview only: a moving common hinge joins two three-leaf desktop nets."""

from math import atan2, cos, pi, sin

_HEIGHT = 9 / 16
_CAMERA = 3.4
_FLAT = ((-1, _HEIGHT, 0), (1, _HEIGHT, 0), (1, -_HEIGHT, 0), (-1, -_HEIGHT, 0))
_CUT_X, _CUT_Y = 1 / 8, 1 / 16


def _tile(left, right, top, bottom, hinge_x, hinge_y):
    """Keep desktop UVs fixed while the corners rotate in hinge-local space."""
    corners = ((left, top), (right, top), (right, bottom), (left, bottom))
    local = tuple((x - hinge_x, y - hinge_y) for x, y in corners)
    uv = tuple(((x + 1) / 2, (_HEIGHT - y) / (2 * _HEIGHT)) for x, y in corners)
    return local, uv


_OLD = (
    _tile(-_CUT_X, 1, _HEIGHT, -_HEIGHT, 1, 0),
    _tile(-1, -_CUT_X, _HEIGHT, -_CUT_Y, -_CUT_X, -_CUT_Y),
    _tile(-1, -_CUT_X, -_CUT_Y, -_HEIGHT, -_CUT_X, -_CUT_Y),
)
_NEW = (
    _tile(-1, _CUT_X, _HEIGHT, -_HEIGHT, -1, 0),
    _tile(_CUT_X, 1, _CUT_Y, -_HEIGHT, _CUT_X, _CUT_Y),
    _tile(_CUT_X, 1, _HEIGHT, _CUT_Y, _CUT_X, _CUT_Y),
)
_OLD_BACK = (0.29, 0.20, 0.15)
_NEW_BACK = (0.17, 0.26, 0.36)
_EDGE_YAW = atan2(_CAMERA, 1)


def _ease(start, end, p):
    t = min(1.0, max(0.0, (p - start) / (end - start)))
    return t * t * (3 - 2 * t)


def _leaf(scene, tile, hinge, yaw_basis, pitch, capture, backing, motion):
    local, uv = tile
    x, y, z = hinge
    cy, sy = yaw_basis
    cp, sp = cos(pitch), sin(pitch)
    # Rotate about local X first, then world Y: the pitch axis follows the wing.
    verts = tuple((x + cy * u + sy * sp * v,
                   y + cp * v,
                   z - sy * u + cy * sp * v) for u, v in local)
    facing = -x * sy * cp + y * sp + (_CAMERA - z) * cy * cp
    shade = 1 - motion * (0.07 + 0.22 * (1 - abs(cy * cp)))
    scene.quad(verts, capture if facing >= 0 else backing, uv, shade=shade, edge=0.32 * motion)


def _net(scene, tiles, capture, backing, root, side, folded):
    # Fold the outer flap back first. The two backfolds finish only at the
    # endpoint, when the entire closed net projects to the viewport edge.
    spine = _EDGE_YAW * (1 - (1 - folded) ** 3.4)
    counterfold = folded + 0.42 * folded * (1 - folded) * (2 * folded - 1)
    wing = spine - pi * _ease(0.0, 1.0, counterfold)
    flap = 0.94 * _ease(0.15, 0.73, folded) + 0.06 * _ease(0.10, 1.0, folded)
    spine_basis = cos(spine), side * sin(spine)
    wing_basis = cos(wing), side * sin(wing)
    x, y, z = root
    span = 1 + _CUT_X
    joint = x + side * span * spine_basis[0], y + side * _CUT_Y, z - span * sin(spine)
    # The secondary vertical hinge follows the tall leaf; the transverse hinge
    # follows the wing's rotated X axis. Shared edges are never interpolated.
    _leaf(scene, tiles[0], root, spine_basis, 0.0, capture, backing, folded)
    _leaf(scene, tiles[1], joint, wing_basis, 0.0, capture, backing, folded)
    _leaf(scene, tiles[2], joint, wing_basis, -side * pi * flap, capture, backing, folded)


def draw(scene, p):
    if p <= 0:
        scene.quad(_FLAT, "A")
        return
    if p >= 1:
        scene.quad(_FLAT, "B")
        return

    # The outgoing right edge is the incoming left edge at every instant.
    # Both tall leaves rotate about that exact vertical line, on opposite sides.
    z = -0.84 * 4 * p * (1 - p)
    root = ((1 - 2 * p) * (1 - z / _CAMERA), 0.0, z)
    _net(scene, _OLD, "A", _OLD_BACK, root, -1, p)
    _net(scene, _NEW, "B", _NEW_BACK, root, 1, 1 - p)
