"""Preview only: an axial exchange of two cells joined by a nested aperture."""
from math import cos, radians, sin


# The parameter reel holds this path position for the exact cell-width ratio.
REVEAL = .26
_HALF_HEIGHT = 9 / 16
_CAMERA_Z = 3.4
_CORNERS = ((-1., 1.), (1., 1.), (1., -1.), (-1., -1.))
_EDGES = ((0, 1), (1, 2), (2, 3), (3, 0))
_SIDE_LIGHT = (.98, .78, .59, .86)
_SHELL = (.28, .40, .53)
_LINING = (.32, .34, .36)
_INSERT = (.54, .34, .23)


def _ease(t):
    return t * t * (3. - 2. * t)


def _outer_size(travel):
    return 1. - travel * (.007 + .653 * travel**5)


_REVEAL_OUTER = _outer_size(_ease(REVEAL))
_EARLY_BIAS = (1. / _REVEAL_OUTER - 1.) * (REVEAL / (1. - REVEAL))**6


def _panel(outer, inner, i, j):
    return outer[i], outer[j], inner[j], inner[i]


def _uv(fraction):
    return tuple((.5 + .5 * fraction * x, .5 - .5 * fraction * y) for x, y in _CORNERS)


_RIM_UV = _uv(1.)


def draw(scene, p, *, cube_tilt=0., zoom=.25, next_workspace_start_size=.32):
    """Draw the same geometry at p whether playing forward or retracing."""
    travel = _ease(p)
    presentation = 4. * travel * (1. - travel)
    outer = _outer_size(travel)
    # Blend two monotone growth curves. At REVEAL the early curve is exactly
    # as wide as A and the late one is zero. The knob does not retime the turn.
    weight = p**6
    early = weight / (weight + _EARLY_BIAS * (1. - p)**6)
    late = _ease(max(0., (p - REVEAL) / (1. - REVEAL)))
    start = next_workspace_start_size
    inner = start * early + (1. - start) * late

    # A recedes as B advances along the same axis. The opening never closes
    # to reset the mechanism: the four lining panels stay seated on B's rear
    # ring as the inset passes through and eventually covers the old frame.
    old_z, new_z = -.96 * travel, -.94 * (1. - travel)**1.5
    opening = 1. - (1. - .84 * (1. - (1. - travel)**4)) * (1. - start * early)
    yaw = -.36 * presentation
    pitch = radians(cube_tilt) * presentation
    cy, sy = cos(yaw), sin(yaw)
    cp, sp = cos(pitch), sin(pitch)
    # A nearly full-width insert needs a straighter chamber around it.
    # Keep the face normals (tilt and yaw) regardless of its clearance.
    rake = 1. - start**4

    def ring(size, z, back=0.):
        # Pitch the faces, not the axis the inset follows. Keep its centre
        # near the opening under tilt while the extrusion still shows depth.
        centre_x = sy * (.25 * (z + .42) - cp * back) * rake
        centre_y = sp * (.12 * (z + .42) - back) * rake
        points = []
        for u, v in _CORNERS:
            x, y = size * u, size * v * _HALF_HEIGHT
            depth = -cp * back - sp * y
            points.append((cy * x - sy * sp * y + centre_x, cp * y + centre_y,
                           z + cy * depth - sy * x))
        return tuple(points)

    rim = ring(outer, old_z)
    mouth = ring(outer * opening, old_z - .24 * opening)
    shell_back = ring(outer, old_z, .52 * outer + .68 * (1. - travel))
    insert = ring(inner, new_z)
    insert_back = ring(inner, new_z, .36 * inner)

    # Keep the axial framing; retreat only as far as needed to clear the view
    # before adding the requested dolly. Vertex bounds fit the full panels.
    rings = rim, mouth, shell_back, insert, insert_back
    clearance = max(z + _CAMERA_Z * (max(abs(x), abs(y) / _HALF_HEIGHT) - 1.)
                    for points in rings for x, y, z in points)
    retreat = max(0., clearance) + 2. * zoom * presentation
    rim, mouth, shell_back, insert, insert_back = (
        tuple((x, y, z - retreat) for x, y, z in points) for points in rings
    )

    # Crop A in its own coordinates. At the start the four panels lie flat
    # and tile A around a single point; no replacement desktop or fade.
    mouth_uv = _uv(opening)
    for (i, j), light in zip(_EDGES, _SIDE_LIGHT):
        scene.quad(_panel(rim, mouth, i, j), 'A', uv=_panel(_RIM_UV, mouth_uv, i, j),
                   shade=1. - .28 * opening * (1. - light))
        scene.quad(_panel(rim, shell_back, i, j), _SHELL,
                   shade=(light, light, .68 * light, .68 * light))
        scene.quad(_panel(mouth, insert_back, i, j), _LINING,
                   shade=(.82 * light, .82 * light, .48 * light, .48 * light))
        scene.quad(_panel(insert, insert_back, i, j), _INSERT,
                   shade=(light, light, .78 * light, .78 * light))
    scene.quad(shell_back, _SHELL, shade=.46)
    scene.quad(insert_back, _INSERT, shade=.55)
    scene.quad(insert, 'B', shade=1. - .07 * presentation)
