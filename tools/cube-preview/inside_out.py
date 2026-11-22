"""Preview only: look around an inside corner of a fixed workspace room."""
from math import cos, pi, radians, sin


_CAPTURE_HALF_HEIGHT = 9 / 16
_FOCAL_LENGTH = .8125
_MAX_EYE_AXIS = .90
_BASE_RETREAT = .22
_RENDERER_EYE_Z = 3.4

# A display-aspect room: the floor plan is square, and the height fits
# a complete desktop. The pictures ARE the walls from floor to ceiling.
_A_LEFT, _CORNER, _B_RIGHT, _REAR = (-1., -1.), (1., -1.), (1., 1.), (-1., 1.)


def _wall(left, right, top, bottom):
    lx, lz = left
    rx, rz = right
    # TL, TR, BR, BL as seen from the room, including the capture UVs.
    return ((lx, top, lz), (rx, top, rz), (rx, bottom, rz), (lx, bottom, lz))


def _surfaces():
    h = _CAPTURE_HALF_HEIGHT
    yield _wall(_A_LEFT, _CORNER, h, -h), 'A', 1.
    yield _wall(_CORNER, _B_RIGHT, h, -h), 'B', 1.
    yield _wall(_B_RIGHT, _REAR, h, -h), (.24, .26, .28), (.92, .77, .77, .92)
    yield _wall(_REAR, _A_LEFT, h, -h), (.28, .29, .30), (.77, .92, .92, .77)

    floor = (_A_LEFT, _CORNER, _B_RIGHT, _REAR)
    yield tuple((x, -h, z) for x, z in floor), (.31, .33, .35), (.92, .65, .82, 1.09)
    yield tuple((x, h, z) for x, z in reversed(floor)), (.16, .18, .20), (.96, .79, .65, .82)


_ROOM = tuple(_surfaces())


def draw(scene, p, *, cube_tilt=0., zoom=.45):
    # The retreat starts at second order, the turn at third: we begin and
    # end square to a wall, with the largest reveal at the diagonal.
    reveal = (4 * p * (1 - p)) ** 2
    yaw = (pi / 2) * p**3 * (10 + p * (-15 + 6 * p))
    s, c = sin(yaw), cos(yaw)
    stand = 1 - _FOCAL_LENGTH

    # Retreat along the view axis. Its fourth norm is a smooth upper bound
    # on either axis component, leaving clearance from both rear walls.
    max_retreat = _MAX_EYE_AXIS / (s**4 + c**4)**.25
    rear = _BASE_RETREAT + zoom * (max_retreat - _BASE_RETREAT)
    radius = stand - (stand + rear) * reveal
    eye_x, eye_z = s * radius, -c * radius

    # Change presentation by pitching at the eye, not moving it vertically.
    pitch = radians(cube_tilt) * reveal
    sp, cp = sin(pitch), cos(pitch)

    def view(point):
        x, y, z = point
        x, z = x - eye_x, z - eye_z
        depth = s * x - c * z
        view_depth = cp * depth + sp * y
        # The shared shader emits w = 3.4 - z. Feed it signed view depth
        # so hardware clips primitives crossing the eye/near plane. Both
        # head-on poses have view_depth == focal length, giving a 1:1 quad.
        return c * x + s * z, cp * y - sp * depth, _RENDERER_EYE_Z * (1 - view_depth / _FOCAL_LENGTH)

    for corners, material, shade in _ROOM:
        scene.quad(tuple(view(point) for point in corners), material, shade=shade)
