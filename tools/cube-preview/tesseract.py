"""Cutaway counterfold: two solid cells exchange the inside of a skewed sleeve."""
from itertools import pairwise
from math import cos, pi, radians, sin


# First open pocket: compare the nested and outside cell sizes here.
REVEAL = .26
_HALF_HEIGHT = 9 / 16
_CAMERA_Z = 3.4
_CORNERS = ((-1., 1.), (1., 1.), (1., -1.), (-1., -1.))
_EDGES = ((0, 1), (1, 2), (2, 3), (3, 0))
_TIMES = (0., .115, REVEAL, .5, 1. - REVEAL, 1.)

_SLEEVE_COLOURS = ((.31, .41, .51), (.32, .40, .47), (.49, .43, .35),
                   (.43, .35, .29), (.47, .35, .27))
# A sleeve station is an open C section: outer roof/floor, two thick lips,
# dark lining, and a solid back. Even/odd vertices are the lip/back rails.
_SLEEVE_SURFACES = ((0, 1, .83), (1, 7, .54), (6, 7, .68),
                    (0, 2, .92), (2, 3, 1.18), (1, 3, .88),
                    (4, 5, 1.04), (4, 6, .77), (5, 7, .63))


def _ease(t):
    t = min(1., max(0., t))
    return t * t * (3. - 2. * t)


def _mix(a, b, t):
    return a + (b - a) * t


def _point_between(a, b, t):
    return tuple(_mix(u, v, t) for u, v in zip(a, b))


def _pose(p, nested):
    # Centre x/z, yaw, linear size and thickness per unit size. The whole cap
    # pivots outward before the other cell advances; neither desktop is cut.
    keys = ((0., 0., 0., 1., .74),
            (-.34, .18, .64, 1., .56),
            (-.86, .21, 1.21, 1., .47),
            (-1.20, .29, .79, .77, .49),
            (-.45 - .47 * nested, -.65, .58, nested, .64),
            (0., -1.83, .50, nested, .62))

    def slope(i, axis):
        if i == 0 or i == len(keys) - 1:
            return 0.
        prev = (keys[i][axis] - keys[i-1][axis]) / (_TIMES[i] - _TIMES[i-1])
        next_ = (keys[i+1][axis] - keys[i][axis]) / (_TIMES[i+1] - _TIMES[i])
        return 2. * prev * next_ / (prev + next_) if prev * next_ > 0. else 0.

    # Monotone Hermite per coordinate. The fold doesn't stop at each posing
    # key, and the zero-size cell neither overshoots nor inverts.
    for i, (start, stop) in enumerate(pairwise(_TIMES)):
        if p <= stop:
            t = (p - start) / (stop - start)
            e, blend = _ease(t), (stop - start) * t * (1. - t)
            return tuple(_mix(a, b, e) + blend * (slope(i, axis) * (1. - t) - slope(i+1, axis) * t)
                         for axis, (a, b) in enumerate(zip(keys[i], keys[i+1])))


def _cell(p, direction, nested):
    x, z, yaw, size, depth = _pose(p, nested)
    x *= direction
    yaw *= direction
    c, s = cos(yaw), sin(yaw)
    height = size * _HALF_HEIGHT

    def ring(back):
        return tuple((x + c * u * size + s * back, v * height, z + s * u * size - c * back)
                     for u, v in _CORNERS)

    front, back = ring(0.), ring(depth * size)
    side = 1 if direction > 0 else 0
    # Seat the sleeve on the adjoining side, not on the textured plane.
    lip = _point_between(front[side], back[side], .40)
    return front, back, (lip, back[side], height, min(v[2] for v in back))


def _section(lip, back, height):
    return tuple((point[0], v * height, point[-1])
                 for v in (1., .84, -.84, -1.) for point in (lip, back))


def _pocket(outside, inside, direction, span):
    lip, back, outer_height, _ = outside
    inset, seat, inner_height, deepest = inside
    x, z = inset[0], deepest - .27
    far = x + direction * span
    near = max(inset[2] + .50, lip[2] - .50)
    height = max(outer_height + .11, inner_height + .13)
    # Four welded stations hook around the inset: shoulder, open mouth,
    # outside jamb, return to the back of the nested cell.
    return (_section((lip[0] + direction * .06, lip[2] - .44), (back[0] + direction * .055, z), height),
            _section((far, near), (far, z), height),
            _section((far + direction * .10, z + .025), (far + direction * .10, z - .25), height * .97),
            _section((x + direction * .07, z + .055), (seat[0] + direction * .08, z - .25),
                     max(inner_height, .21)))


def _sleeve(a, b, p, nested, activity):
    first, last = _section(*a[:3]), _section(*b[:3])
    leg = min(p, 1. - p)
    opening = _ease(leg / REVEAL) * _ease((.5 - leg) / (.5 - REVEAL))
    span = .86 + nested * 1.32
    pocket = _pocket(a, b, 1, span) if p <= .5 else _pocket(b, a, -1, span)[::-1]
    bow = .50 * activity * (1. - opening)

    # Only the waist uses a literal x–w rotation. Rotate opposed rail offsets
    # out of w into x, then project residual w into depth: at halfway the w
    # offsets are edge-on, and on the far side they reverse. The surrounding
    # cells are a staged 3D cutaway, not a rigid projection of a whole 4-cube.
    turn = pi * _ease((p - REVEAL) / (1. - 2. * REVEAL))
    across, towards = .42 * bow * sin(turn), .42 * bow * cos(turn)
    sections = [first]
    for t, ring in zip((.13, .32, .68, .87), pocket):
        station = []
        for i, (u, v, target) in enumerate(zip(first, last, ring)):
            x, y, z = _point_between(u, v, t)
            rail = 1. if i % 2 == 0 else -1.
            # Close the mouth into a deep neck, rather than concealing either
            # desktop with a flat blocker as the pocket opens the other way.
            x += rail * across
            z += rail * towards - bow * (.53 if rail > 0. else 2.12)
            station.append(_point_between((x, y, z), target, opening))
        sections.append(tuple(station))
    sections.append(last)
    return sections


def draw(scene, p, *, cube_tilt=0., zoom=.25, next_workspace_start_size=.32):
    """Draw the same geometry at p whether playing forward or retracing."""
    activity = sin(pi * p) ** 2
    presentation = _ease(min(p, 1. - p) / REVEAL)
    a_front, a_back, a_hinge = _cell(p, 1, next_workspace_start_size)
    b_front, b_back, b_hinge = _cell(1. - p, -1, next_workspace_start_size)
    sections = _sleeve(a_hinge, b_hinge, p, next_workspace_start_size, activity)
    rings = (a_front, a_back, b_front, b_back, *sections)

    # Positive presentation tips the workspace normals up. Apply it to the
    # object before fitting, not by moving the camera to a vertical orbit.
    if cube_tilt:
        angle = radians(cube_tilt) * presentation
        c, s = cos(angle), sin(angle)
        rings = tuple(tuple((x, c * y + s * (z + .5), c * (z + .5) - s * y - .5)
                            for x, y, z in ring) for ring in rings)

    # Solve the horizontal bounds jointly with centre translation, then take
    # the vertical constraint. Perspective maps straight edges to straight
    # edges, so these vertex bounds also fit every triangle between them.
    lo = min(x - z / _CAMERA_Z for ring in rings for x, _, z in ring)
    hi = max(x + z / _CAMERA_Z for ring in rings for x, _, z in ring)
    centre = (hi + lo) * .5
    vertical = max(z + _CAMERA_Z * (abs(y) / _HALF_HEIGHT - 1.) for ring in rings for _, y, z in ring)
    pullback = max(_CAMERA_Z * ((hi - lo) * .5 - 1.), vertical) + 2. * zoom * presentation
    a_front, a_back, b_front, b_back, *sections = (
        tuple((x - centre, y, z - pullback) for x, y, z in ring) for ring in rings
    )

    for front, back, capture, colour in ((a_front, a_back, 'A', (.24, .36, .48)),
                                        (b_front, b_back, 'B', (.43, .30, .24))):
        scene.quad(front, capture, shade=1. - .12 * activity, edge=.45 * activity)
        for (i, j), light in zip(_EDGES, (1., .82, .66, .79)):
            scene.quad((front[i], back[i], back[j], front[j]), colour, shade=light, edge=.5 * activity)
        scene.quad(back, colour, shade=.50, edge=.5 * activity)

    for colour, (u, v) in zip(_SLEEVE_COLOURS, pairwise(sections)):
        for i, j, light in _SLEEVE_SURFACES:
            scene.quad((u[i], v[i], v[j], u[j]), colour, shade=light, edge=.53 * activity)
