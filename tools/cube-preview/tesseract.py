"""Preview only: a 4D-inspired exchange of nested cubic shells, built in 3D."""
from math import cos, sin


_CORNERS = ((-1., 9/16), (1., 9/16), (1., -9/16), (-1., -9/16))
_UV_DIRECTIONS = ((-1., -1.), (1., -1.), (1., 1.), (-1., 1.))
_SHELL = (.33, .39, .48)
_LINING = (.38, .31, .26)
_LIP = (.56, .45, .34)
_INSERT = (.30, .55, .57)
_SIDE_LIGHT = (.96, .72, .56, .81)


def _ease(p, start, end):
    x = max(0., min(1., (p - start) / (end - start)))
    return x * x * (3. - 2. * x)


def _panel(outer, inner, i, j):
    return outer[i], outer[j], inner[j], inner[i]


def draw(scene, p):
    turn = _ease(p, 0., .38) * (1. - _ease(p, .55, 1.))
    pullback = .64 * _ease(p, 0., .29) * (1. - _ease(p, .63, 1.))
    cy, sy = cos(-.50 * turn), sin(-.50 * turn)
    cp, sp = cos(.17 * turn), sin(.17 * turn)

    def ring(size, z):
        points = []
        for dx, dy in _CORNERS:
            x, y = size * dx, size * dy
            pitched_z = sp * y + cp * z
            points.append((cy * x + sy * pitched_z - .13 * turn, cp * y - sp * z - .015 * turn,
                           cy * pitched_z - sy * x - pullback))
        return points

    fold = _ease(p, .40, .85)
    old_size = 1. - .57 * _ease(p, .51, .90)
    old_z = -.86 * _ease(p, .51, .93)
    mouth_fraction = .86 * _ease(p, .06, .47) + .11 * _ease(p, .46, .65)
    mouth_z = old_z - .82 * fold
    rim = ring(old_size, old_z)
    mouth = ring(old_size * mouth_fraction, mouth_z)
    # Both boxes have physical thickness even head-on. Their hidden backs
    # never coincide with the visible skin on the first or last display frame.
    throat = ring(old_size * mouth_fraction * .95, mouth_z - .044)
    backing = ring(old_size, old_z - .032)
    shell_back = ring(old_size, old_z - 1.10)

    # The insert starts fully inside the chamber and advances through the
    # aperture before the surrounding shell contracts behind it.
    insert_size = .26 + .25 * _ease(p, .08, .44) + .49 * _ease(p, .41, 1.)
    insert_z = -.55 + .75 * _ease(p, .25, .69) - .20 * _ease(p, .75, 1.)
    insert_depth = .18 + .34 * (1. - _ease(p, .66, 1.))
    insert_front = ring(insert_size, insert_z)
    insert_back = ring(insert_size, insert_z - insert_depth)

    # Cropping follows the aperture in A's own coordinates, not the camera.
    # Sinking those same UV-bearing edges into the chamber folds the four
    # panels together without mirroring, retiling, or blending either desktop.
    mouth_uv = tuple((.5 + .5 * mouth_fraction * u, .5 + .5 * mouth_fraction * v)
                     for u, v in _UV_DIRECTIONS)
    rim_uv = ((0., 0.), (1., 0.), (1., 1.), (0., 1.))

    # The front skin owns the uncut portion of the A image.
    for i, j in enumerate((1, 2, 3, 0)):
        scene.quad(_panel(rim, mouth, i, j), 'A', uv=_panel(rim_uv, mouth_uv, i, j),
                   shade=1. - .06 * turn - .42 * fold * (1. - _SIDE_LIGHT[i]))
    scene.quad(shell_back, _SHELL, shade=.46)
    for i, j in enumerate((1, 2, 3, 0)):
        light = _SIDE_LIGHT[i]
        scene.quad(_panel(rim, shell_back, i, j), _SHELL,
                   shade=(light, light, light * .79, light * .79))
        scene.quad(_panel(backing, throat, i, j), _LINING, shade=light * .83)
        scene.quad(_panel(mouth, throat, i, j), _LIP, shade=light)
        # Solid corresponding-edge links seat the insert in the throat. The
        # links steepen, then trail behind the insert as its rear clears it.
        scene.quad(_panel(throat, insert_back, i, j), _LINING,
                   shade=(light, light, light * .61, light * .61))
        scene.quad(_panel(insert_front, insert_back, i, j), _INSERT,
                   shade=(light, light, light * .83, light * .83))
    scene.quad(insert_front, 'B', shade=1. - .10 * turn)
