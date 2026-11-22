"""Preview only: a recessed desktop corner everts through its portal."""
from math import cos, hypot, radians, sin, tau


CAMERA_Z = 3.4
HALF_HEIGHT = 9 / 16

# p, projected crease x, A/B yaw in the horizontal section, B's far-edge
# depth, and target projected height of the nearest desktop column.
POSES = (
    (0.,   1.,    0, 105,  0.,    1.),
    (.055, .980,  0, 102.35, -.0271, .9871),
    (.14,  .72,   0,  98, -.07,  .965),
    (.30,  .47,   0,  87, -.16,  .895),
    (.41,  .355,  4,  80, -.10,  .90),
    (.49,  .15,  40,  72, -.05,  .92),
    (.56, -.25,  67,  58,  .015, .94),
    (.62, -.61,  69,  22,  .065, .84),
    (.65, -.72,  70,   0,  .085, .83),
    (.68, -.76,  72, -22,  .10,  .94),
    (.77, -.79,  78, -39,  .18,  .96),
    (.88, -.90,  91, -22,  .14,  .979),
    (1., -1.,   100,   0,  0.,   1.),
)


def pose_at(p):
    # Local monotone cubic segments keep wall velocity continuous without
    # overshooting these framing poses or easing to a stop at each key.
    i = next((i for i in range(len(POSES) - 1) if p < POSES[i + 1][0]), len(POSES) - 2)
    a, b = POSES[i:i + 2]
    prev = POSES[i - 1] if i else None
    after = POSES[i + 2] if i + 2 < len(POSES) else None
    span = b[0] - a[0]
    t = (p - a[0]) / span
    blend = t * t * (3 - 2 * t)

    def tangent(u, v):
        return 2 * u * v / (u + v) if u * v > 0 else 0.

    values = []
    for j in range(1, len(a)):
        delta = b[j] - a[j]
        start = tangent(delta, span * (a[j] - prev[j]) / (a[0] - prev[0])) if prev else delta
        end = tangent(delta, span * (after[j] - b[j]) / (after[0] - b[0])) if after else delta
        values.append(a[j] + blend * delta + t * (1 - t) * ((1 - t) * start - t * end))
    return values


def section(screen_x, z):
    """A point in the horizontal section, placed by its projected x."""
    return screen_x * (1 - z / CAMERA_Z), z


def wall(scene, left, right, height, material, shade=1., edge=0.):
    lx, lz = left
    rx, rz = right
    scene.quad(((lx, height, lz), (rx, height, rz),
                (rx, -height, rz), (lx, -height, lz)), material, shade=shade, edge=edge)


def soffits(scene, a, b, height):
    """Join a wall's top and bottom to the aperture plane at z=0."""
    ax, az = a
    bx, bz = b
    if az * bz < 0:
        # A wall passing through the lip has two caps meeting at a point.
        # Splitting here keeps each quad simple instead of drawing a bow tie.
        crossing = (ax + (bx - ax) * az / (az - bz), 0.)
        soffits(scene, a, crossing, height)
        soffits(scene, crossing, b, height)
        return
    if az == bz == 0:
        return

    fa = ax / (1 - az / CAMERA_Z)
    fb = bx / (1 - bz / CAMERA_Z)
    exterior = az + bz > 0
    ceiling = (.32, .40, .49) if exterior else (.115, .17, .24)
    floor = (.42, .50, .59) if exterior else (.17, .23, .31)
    for y, color in ((height, ceiling), (-height, floor)):
        scene.quad(((fa, y, 0.), (fb, y, 0.), (bx, y, bz), (ax, y, az)), color)


def bevel(scene, opening, height, depth):
    inner = ((-opening, height, 0.), (opening, height, 0.),
             (opening, -height, 0.), (-opening, -height, 0.))
    inset = 1 - depth / CAMERA_Z
    x = (opening + .40 * depth) * inset
    y = (height + .22 * depth) * inset
    outer = ((-x, y, depth), (x, y, depth), (x, -y, depth), (-x, -y, depth))
    colors = ((.27, .32, .39), (.24, .29, .36), (.36, .42, .49), (.31, .37, .45))
    for i, color in enumerate(colors):
        j = (i + 1) % 4
        scene.quad((outer[i], outer[j], inner[j], inner[i]), color)


def ease(p, start, end):
    t = min(1., max(0., (p - start) / (end - start)))
    return t * t * (3 - 2 * t)


def draw(scene, p):
    # Spend more of the fixed shot on the depth crossing, where both walls
    # cover the most screen distance. Monotone, reversible, same endpoints.
    p += .086 * sin(tau * p) + .024 * (1 - cos(tau * p))
    activity = 4 * p * (1 - p)
    screen_x, angle_a, angle_b, right_z, nearest_height = pose_at(p)
    a, b = radians(angle_a), radians(angle_b)

    # B deploys from the right boundary edge-on. Once deployed both panels
    # have the desktop's true aspect ratio and share a single dolly scale.
    b_width = 2 * ease(p, 0., .32)
    depth_span = b_width * sin(b)
    # Solve the perspective height bound for the common scale. A smoothed
    # positive part keeps the dolly continuous when the nearer B edge swaps.
    lead = (-depth_span + hypot(depth_span, .04 * activity)) / 2
    scale = nearest_height * (1 - right_z / CAMERA_Z) / (1 + nearest_height * lead / CAMERA_Z)
    crease_z = right_z - scale * depth_span
    crease = section(screen_x, crease_z)
    left = (crease[0] - 2 * scale * cos(a), crease_z - 2 * scale * sin(a))
    right = (crease[0] + scale * b_width * cos(b), right_z)
    height = HALF_HEIGHT * scale

    opening = min(1. - .04 * activity, scale + .02 * activity)
    lip_l, lip_r = (-opening, 0.), (opening, 0.)
    jamb = section(-opening + .16 * ease(p, .50, .90), -.10 * p - .055 * activity)
    bevel(scene, opening, height, .05 * activity)
    wall(scene, lip_l, jamb, height, (.16, .22, .30))
    wall(scene, jamb, left, height, (.13, .18, .25))
    wall(scene, right, lip_r, height, (.20, .27, .35))

    # B thrusts through the lip as the signed depth span changes. The jamb
    # hides the receding edge of A; after A turns edge-on it lies behind B.
    light_a_outer = 1 - .10 * activity
    light_a_seam = 1 - .14 * activity + .15 * crease_z / CAMERA_Z
    light_b_seam = 1 - .13 * activity + .25 * crease_z / CAMERA_Z
    light_b_outer = 1 - .08 * activity
    wall(scene, left, crease, height, 'A',
         (light_a_outer, light_a_seam, light_a_seam, light_a_outer), .28 * activity)
    wall(scene, crease, right, height, 'B',
         (light_b_seam, light_b_outer, light_b_outer, light_b_seam), .28 * activity)

    perimeter = (lip_l, jamb, left, crease, right, lip_r)
    for a, b in zip(perimeter, perimeter[1:]):
        soffits(scene, a, b, height)
