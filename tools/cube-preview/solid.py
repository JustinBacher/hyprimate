"""Review mesh only. Two full desktop faces share a radiused vertical corner."""
from math import cos, pi, radians, sin


def draw(scene, p, *, cube_tilt=0., zoom=.25):
    a = p * pi / 2
    envelope = sin(pi * p)
    tilt = radians(cube_tilt) * envelope
    c, s, ct, st = cos(a), sin(a), cos(tilt), sin(tilt)
    radius, h = .012 * envelope, 9 / 16

    # Fit the turned silhouette inside the view. At zero tilt this is a
    # level dolly; zoom adds distance in fractions of one full face width.
    retreat = 1.42 * envelope
    if tilt:
        for x in (-1, 1):
            for z in (-1, 1):
                across, toward = c*x - s*z, s*x + c*z
                for y in (-h, h):
                    height, depth = ct*y + st*toward, ct*toward - st*y
                    retreat = max(retreat, depth - 1 + 3.4*(max(abs(across), abs(height)/h)-1))
    retreat += 2 * zoom * envelope

    def point(x, y, z):
        across, toward = c*x - s*z, s*x + c*z
        return (across, ct*y + st*toward, ct*toward - st*y - 1 - retreat)

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
        # A positive nose-up presentation exposes the underside; a negative
        # one exposes the crown. Neither requires moving the camera in Y.
        if tilt:
            level = -h if tilt > 0 else h
            center = point(0,level,0)
            scene.quad((center, point(l[0],level,l[1]), point(r[0],level,r[1]), center),
                       (.19,.23,.26) if level > 0 else (.14,.17,.20))
