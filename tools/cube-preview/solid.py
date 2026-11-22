"""Review mesh only. Two full desktop faces share a radiused vertical corner."""
from math import cos, pi, sin


def draw(scene, p):
    a = p * pi / 2
    envelope = sin(pi * p)
    # The pullback starts with the turn: the near corner needs room immediately.
    tilt = .23 * envelope
    c, s, ct, st = cos(a), sin(a), cos(tilt), sin(tilt)
    radius, h = .012 * envelope, 9 / 16

    def point(x, y, z):
        across, toward = c*x - s*z, s*x + c*z
        return (across, ct*y - st*toward + .17*envelope,
                st*y + ct*toward - 1 - 1.4*envelope)

    def side(l, r, material, light):
        scene.quad([point(l[0], h, l[1]), point(r[0], h, r[1]),
                    point(r[0], -h, r[1]), point(l[0], -h, l[1])], material, shade=light)

    # Traverse the cross-section clockwise from the left end of the A face.
    stops = []
    for i, (x, z) in enumerate(((1, 1), (1, -1), (-1, -1), (-1, 1))):
        for j in range(5):
            t = (i + j / 4) * pi / 2
            stops.append((x*(1-radius) + radius*sin(t), z*(1-radius) + radius*cos(t)))
    for j, l in enumerate(stops):
        k = (j+1) % len(stops)
        r = stops[k]
        if j == 19:
            side(l, r, 'A', 1 - .24*(1-c))
        elif j == 4:
            side(l, r, 'B', 1 - .29*(1-s))
        else:
            # Satin aluminium on the bevel: directional key light, no emissive line.
            t = (j//5 + (j%5 + .5)/4) * pi/2 - a
            light = .15 + .19*max(0, cos(t+.55)) + .32*max(0, cos(t+.26))**14
            side(l, r, (light*.86, light*.95, light), 1)
        # Plain cap; quadrangulate the rounded perimeter as a triangle fan.
        scene.quad([point(0,h,0), point(l[0],h,l[1]), point(r[0],h,r[1]), point(0,h,0)],
                   (.19,.23,.26))
