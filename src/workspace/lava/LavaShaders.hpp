#pragma once

namespace hyprimate::workspace::lava {
    inline constexpr const char *VERTEX_SHADER = R"GLSL(#version 300 es
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

    inline constexpr const char *FRAGMENT_SHADER = R"GLSL(#version 300 es
precision highp float;
precision highp int;
uniform highp sampler2D sourceImage, destinationImage;
uniform highp sampler2D sourceBottom, sourceWindows, destinationBottom, destinationWindows;
// The captures stay in their original screen coordinates.
uniform vec2 size;
uniform float progress, direction;
uniform int variant, burnUp;
// Viscosity, cooling rate, crust amount, glow; detail, trail smear, edge drip, ash.
uniform vec4 sharedA, sharedB, specialA;
uniform vec2 specialB;
out vec4 color;

struct Surface {
    vec2 slip;
    vec2 slope;
    float relief;
    float fissure;
};

vec2 hashCell(vec2 cell) {
    vec3 p = fract(cell.xyx * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yzx + 33.33);
    return fract((p.xx + p.yz) * p.zy);
}

// Continuous value and slope of one material octave. The higher-frequency octave
// gives the rock relief without neighbor lookups or screen-space grain.
vec3 noiseField(vec2 p) {
    vec2 cell = floor(p), t = fract(p);
    vec2 w = t * t * (3.0 - 2.0 * t), dw = 6.0 * t * (1.0 - t);
    float a = hashCell(cell).x;
    float b = hashCell(cell + vec2(1.0, 0.0)).x;
    float c = hashCell(cell + vec2(0.0, 1.0)).x;
    float d = hashCell(cell + 1.0).x;
    float low = mix(a, b, w.x), high = mix(c, d, w.x);
    return vec3(mix(low, high, w.y), dw.x * mix(b - a, d - c, w.y), dw.y * (high - low));
}

float noise(vec2 p) {
    return noiseField(p).x;
}

float channel(float distance, float width, float pixel) {
    return 1.0 - smoothstep(0.30 * width, width + pixel, distance);
}

// Closed-form backward flow: positive travel selects pigment upstream. Its
// fixed edge points need no padding; the map stretches the inlet side and
// compresses the other, with differential shear across the rows.
float upstream(float x, float travel) {
    return x / (1.0 + travel * (1.0 - x));
}

vec2 toCapture(vec2 stream) {
    float x = direction > 0.0 ? stream.x : 1.0 - stream.x;
    return (vec2(x, stream.y) * (size - 1.0) + 0.5) / size;
}

Surface coolingFlow(vec2 q, float skin, float melt, float pixel) {
    float bulge = specialA.x, stretch = specialA.y, turbulence = specialA.z;
    float eddy = noise(q * vec2(5.7, 8.1) + 1.7) - 0.5;
    float shoulder = sin(15.0 * q.y + 2.8 * eddy + 0.85 * sin(8.1 * q.x));
    float swell = noise(vec2(q.x * mix(11.8, 7.2, stretch) + 1.1 * shoulder, q.y * 9.6));
    float shingle = noise(q * vec2(14.1, 16.2) + vec2(3.1 * eddy, 1.7 * swell));
    float stress = 0.67 * swell + 0.33 * shingle + (0.04 + 0.13 * bulge) * shoulder;
    float lobe = smoothstep(0.31, 0.73, stress);
    float opening = channel(abs(stress - (0.45 + 0.11 * eddy)), mix(0.14, 0.046, skin), 11.0 * pixel);
    float split = channel(abs(shingle - 0.53), mix(0.088, 0.035, skin), 15.0 * pixel);
    split *= smoothstep(0.59, 0.83, swell);
    vec2 slip = vec2(-0.54 * lobe * stretch + 0.31 * bulge * shoulder, (0.43 + 1.07 * turbulence) * eddy);
    vec2 slope = vec2(2.2 * (swell - 0.5), 1.7 * (shingle - 0.5)) +
                 vec2(-0.82, 0.61) * shoulder * (0.21 + 0.48 * bulge);
    return Surface(slip, slope, 0.15 + 0.47 * lobe + 0.27 * shingle,
                   max(opening * (0.53 + 0.47 * shingle), split * (0.46 + 0.36 * melt)));
}

Surface basaltFlow(vec2 q, float skin, float pixel) {
    float thickness = specialA.x, width = specialA.y, embers = specialA.z, density = specialA.w;
    q = vec2(q.x + 0.38 * q.y, q.y - 0.46 * q.x) * mix(11.0, 6.2, density);
    float stress = noise(q);
    float grain = noise(q * vec2(2.7, 2.1) + vec2(4.1, 8.3));
    float fault = abs(stress - (0.43 + 0.14 * grain));
    float aperture = mix(0.034, 0.16, width) * (1.0 - 0.48 * skin);
    float rift = channel(fault, aperture, pixel * 9.0) * smoothstep(0.16, 0.57, grain);
    float split = channel(abs(grain - 0.50), aperture * 0.56, pixel * 13.0) *
                  (1.0 - smoothstep(0.04, 0.18, fault));
    float ember = smoothstep(0.76, 0.91, grain) * (1.0 - smoothstep(0.07, 0.25, fault));
    float fissure = max(max(rift, 0.68 * split), embers * ember);
    float relief = (0.31 + 0.33 * stress + 0.25 * thickness) * (1.0 - 0.49 * fissure) + 0.18 * grain;
    vec2 slip = vec2(stress - 0.5, grain - 0.5) * mix(0.75, 0.36, density);
    vec2 wall = vec2(sign(stress - 0.43 - 0.14 * grain), grain - 0.5) * (rift + split);
    return Surface(slip, wall * (0.5 + thickness) + 0.46 * slip, relief, fissure);
}

Surface pahoehoe(vec2 q, float skin, float parcelAge, float pixel) {
    float strength = specialA.x, scale = specialA.y, speed = specialA.z, roll = specialA.w;
    float packet = noise(q * vec2(4.0, 6.4));
    float nose = noise(vec2(q.x * 4.5, q.y * 5.5 + 2.13 * packet));
    float spacing = mix(111.0, 48.0, scale);
    // A parcel carries its ropes; the compressed flank rolls with its local age.
    float phase = spacing * (0.49 * q.x + 0.38 * nose + 0.20 * packet)
                + min(parcelAge, 1.8) * (0.15 + 3.3 * speed) * (0.41 + 0.59 * packet);
    float curl = sin(phase);
    float fold = 0.5 + 0.5 * cos(phase + 0.69 * curl);
    float shoulder = smoothstep(0.21, 0.73, packet + 0.34 * nose);
    float tear = noise(q * vec2(17.0, 23.0) + 2.6 * packet);
    float rupture = smoothstep(0.37, 0.79, 0.63 * tear + 0.37 * nose);
    float seam = abs(fract(phase * 0.159155 + 0.18) - 0.5);
    float opening = channel(seam, mix(0.18, 0.051, skin), 0.18 * spacing * pixel);
    vec2 slip = vec2(0.61 * strength * curl * shoulder + 0.49 * roll * (nose - 0.5),
                    roll * (packet - 0.5 + 0.29 * curl * shoulder));
    return Surface(slip, vec2(-curl, (nose - packet) * curl) * strength * shoulder * 2.0,
                   0.17 + 0.33 * nose + (0.20 + 0.54 * strength) * fold * shoulder,
                   opening * (0.09 + 0.91 * rupture));
}

Surface crustPlates(vec2 q, float skin, float strain, float parcelAge, float pixel) {
    float rigidity = specialA.y, seamGlow = specialA.z, drift = specialA.w;
    float compression = specialB.x, submelt = specialB.y;
    float scale = mix(15.2, 5.2, specialA.x);
    q.x *= 1.0 + 0.58 * compression * strain;
    q = vec2(0.89 * q.x + 0.46 * q.y, -0.46 * q.x + 0.89 * q.y) * scale;
    float stress = noise(q * 0.86);
    q += vec2(stress - 0.5, noise(q * 0.91 + vec2(4.2, 6.8)) - 0.5) * (0.31 + 0.56 * (1.0 - rigidity));
    vec2 cell = floor(q), local = fract(q);
    float nearest = 9.0, next = 9.0;
    vec2 center = vec2(0.0), seed = vec2(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 offset = vec2(float(x), float(y));
            vec2 id = hashCell(cell + offset);
            vec2 approach = vec2(id.y - 0.5, 0.5 - id.x);
            vec2 delta = offset + 0.5 + (id - 0.5) * 0.74 +
                         approach * (0.29 * drift * min(parcelAge, 1.35)) - local;
            float d = dot(delta, delta);
            if (d < nearest) {
                next = nearest;
                nearest = d;
                center = delta;
                seed = id;
            } else {
                next = min(next, d);
            }
        }
    }
    float seam = sqrt(next) - sqrt(nearest);
    float width = mix(0.24, 0.073, skin) * (1.0 - 0.57 * compression * strain) * mix(0.67, 1.51, stress);
    float rim = smoothstep(0.0, 0.10 + 0.69 * width, seam);
    float seams = channel(seam, width, scale * pixel) * (0.19 + 0.81 * seamGlow);
    float pockets = (1.0 - rim) * smoothstep(0.22, 0.83, noise(q * vec2(1.5, 3.2) + 2.0));
    float relief = 0.13 + rim * (0.30 + 0.42 * seed.y + (0.65 - 0.33 * rigidity) * dot(center, seed - 0.5));
    vec2 slip = (seed - 0.5) * (0.25 + 1.31 * drift) + (1.0 - rigidity) * 0.55 * center;
    vec2 bevel = center * (1.0 - rim) * (1.0 + compression) + (seed - 0.5) * (0.25 + rigidity);
    return Surface(slip * rim, bevel, relief, seams * (0.60 + 0.81 * submelt * pockets) + 0.48 * submelt * pockets);
}

float cinders(vec2 q, float age, float pixel) {
    // One brief, rising cinder per eligible material cell; no wall-clock state.
    const float FREQUENCY = 28.0;
    vec2 lattice = q * FREQUENCY;
    vec2 id = hashCell(floor(lattice) + vec2(37.0, 19.0));
    float flight = (age - (0.12 + 0.64 * id.y)) / (0.14 + 0.22 * id.x);
    float life = smoothstep(0.0, 0.21, flight) * (1.0 - smoothstep(0.53, 1.0, flight));
    vec2 center = vec2(0.32) + 0.34 * id - vec2(-0.10, 0.27) * clamp(flight, 0.0, 1.0);
    float radius = length((fract(lattice) - center) * vec2(0.87, 1.31));
    float grainRadius = 0.055 + 0.045 * id.y;
    float grain = 1.0 - smoothstep(grainRadius * 0.46, grainRadius + min(0.09, FREQUENCY * pixel), radius);
    return grain * life * smoothstep(1.0 - 0.85 * sharedB.w, 1.0, id.x);
}

vec4 windowDeposit(vec3 delta, vec3 bottom, float coverage, vec2 at,
                   vec2 roll, float attack, float fracture, float grain, float heat,
                   highp sampler2D topImage, highp sampler2D bottomImage, highp sampler2D windowsImage) {
    if (attack <= 0.0 || coverage <= 0.0) {
        return vec4(delta, coverage);
    }
    if (attack >= 1.0) {
        return vec4(0.0);
    }

    // Probe the curled section against actual coverage, including its top
    // occluders. Attacked edges and stress lines break up into rock-sized flakes.
    vec2 curlAt = at + at * (1.0 - at) * roll * (0.045 + 0.18 * attack);
    vec2 curlUV = toCapture(curlAt);
    vec4 curled = textureLod(windowsImage, curlUV, 0.0);
    float curledVisibility = 1.0 - textureLod(topImage, curlUV, 0.0).a;
    float curledCoverage = curled.a * curledVisibility;
    float edge = clamp(2.1 * (coverage - curledCoverage) + 0.40 * (1.0 - coverage), 0.0, 1.0);
    float strength = clamp(0.28 + 0.46 * grain - 0.24 * fracture - 0.29 * edge, 0.09, 0.78);
    float scorch = smoothstep(strength - 0.16, strength + 0.22, attack);
    float hole = smoothstep(strength + 0.01, strength + 0.15, attack);
    float lift = scorch * (1.0 - hole) * coverage;
    if (lift > 0.0) {
        vec3 curledBottom = textureLod(bottomImage, curlUV, 0.0).rgb;
        vec3 curledDelta = (curled.rgb - curledBottom) * curledVisibility;
        // Relocate a premultiplied contribution onto this lower scene, not the
        // old wallpaper sampled with it. Coverage bounds the lift at occluders.
        delta = mix(delta, curledDelta + curledCoverage * (curledBottom - bottom), lift);
    }
    float soot = scorch * (1.0 - 0.87 * hole) * (1.0 - smoothstep(0.53, 0.96, attack));
    float fringe = scorch * (1.0 - hole) * heat * min(1.0, edge + 0.65 * fracture);
    float intact = (1.0 - scorch) * (1.0 - hole);
    vec3 windowPigment = delta * intact +
                       coverage * (soot * (vec3(0.055, 0.025, 0.014) - bottom) + fringe * vec3(0.88, 0.13, 0.025));
    return vec4(windowPigment, coverage * intact);
}

// Peel only the visible contribution of the window pass from the full scene.
// All planes are accumulated RGB; alpha instead describes coverage of the pass.
// An opaque top erases both the window delta and its burn coverage.
void pigment(vec2 at, vec2 roll, float attack, float fracture, float grain, float heat,
             highp sampler2D fullImage, highp sampler2D bottomImage, highp sampler2D windowsImage,
             out vec3 bed, out vec4 windowPigment) {
    vec2 uv = toCapture(at);
    vec4 top = textureLod(fullImage, uv, 0.0);
    bed = top.rgb;
    windowPigment = vec4(0.0);
    if (burnUp != 0) {
        vec3 bottom = textureLod(bottomImage, uv, 0.0).rgb;
        vec4 windows = textureLod(windowsImage, uv, 0.0);
        float visibility = 1.0 - top.a;
        vec3 delta = (windows.rgb - bottom) * visibility;
        bed -= delta;
        windowPigment = windowDeposit(delta, bottom, windows.a * visibility, at, roll, attack,
                                      fracture, grain, heat, fullImage, bottomImage, windowsImage);
    }
}

void main() {
    ivec2 texel = ivec2(gl_FragCoord.xy);
    if (progress <= 0.0) {
        color = vec4(texelFetch(sourceImage, texel, 0).rgb, 1.0);
        return;
    }
    if (progress >= 0.99) {
        color = vec4(texelFetch(destinationImage, texel, 0).rgb, 1.0);
        return;
    }

    vec2 screen = (gl_FragCoord.xy - 0.5) / max(size - 1.0, 1.0);
    if (direction < 0.0) {
        screen.x = 1.0 - screen.x;
    }
    float pixel = 1.0 / min(size.x, size.y);
    vec2 metric = vec2(clamp(size.x / size.y, 0.65, 2.5), 1.0);
    float viscosity = sharedA.x, detail = sharedB.x, smear = sharedB.y;

    // Fast transport followed by a slower coast in the same direction. Cooling
    // never unwinds this map; the last mobile parcels keep moving as they harden.
    float travel = 0.72 * smoothstep(0.052, 0.69, progress) +
                   0.28 * smoothstep(0.30, 0.986, progress);
    float buckle = smoothstep(0.049, 0.41, progress);
    float row = sin(13.8 * screen.y + 1.12 * sin(5.8 * screen.y + 0.8));
    float load = mix(1.84, 1.19, viscosity) * travel;
    vec2 carrier = vec2(upstream(screen.x, load * (1.0 + 0.18 * row)), screen.y);
    float pressure = noise(carrier * metric * vec2(5.5, 8.2) + vec2(1.7, 2.8));
    float roll = sin(20.1 * carrier.y + 4.6 * carrier.x + 5.4 * pressure);
    float push = 1.0 + 0.17 * row + 0.33 * (pressure - 0.5) + 0.10 * roll;
    push += smear * (0.10 + 0.32 * (1.0 - carrier.x) * (0.57 + 0.43 * row));
    float crossFlow = (0.11 + 0.17 * (1.0 - viscosity)) * row + 0.44 * (pressure - 0.5) - 0.13 * roll;
    if (variant == 0) {
        push += 0.20 * specialA.x * row + 0.28 * specialA.y * (1.0 - carrier.x);
        push += 0.19 * specialA.z * roll;
        crossFlow += 0.23 * specialA.z * roll;
    } else if (variant == 1) {
        push *= 1.10 - 0.25 * specialA.w;
    }
    float edge = 1.0 - smoothstep(0.0, 0.33, min(screen.y, 1.0 - screen.y));
    crossFlow -= sharedB.z * edge * (0.69 + 0.46 * row);
    float shear = mix(1.28, 0.86, detail);
    vec2 q = vec2(upstream(screen.x, load * push),
                  screen.y + screen.y * (1.0 - screen.y) * buckle * clamp(shear * crossFlow, -0.88, 0.88));
    vec2 rockAt = q * metric;

    // Local clocks vary in two dimensions and by row, never by distance from
    // a moving front. These same parcels carry their heat and stress through
    // source consumption, the molten peak and destination accretion.
    float province = noise(rockAt * vec2(4.4, 6.3) + vec2(2.1, 8.3));
    vec3 grit = noiseField(rockAt * vec2(23.0, 37.0) + 2.8 * province);
    float rowHeat = sin(14.6 * q.y + 3.8 * province + 0.9 * sin(3.6 * rockAt.x));
    float lag = 0.088 * (province - 0.5) + 0.029 * rowHeat + 0.041 * (grit.x - 0.5);
    float stage = progress + lag;
    float ignition = smoothstep(0.0, 0.13, progress);
    float warming = ignition * smoothstep(-0.022, 0.24, stage);
    float rupture = ignition * smoothstep(0.078, 0.335, stage);
    float melt = smoothstep(0.206, 0.447, stage);
    float coasting = 1.0 - smoothstep(0.86, 0.986, progress);
    // The wake hardens before the nose, while every row keeps its local clock.
    float cooling = progress + coasting * (lag + 0.10 * (sharedA.y - 0.5) + 0.12 * (0.5 - q.x));
    float deposit = smoothstep(0.578, 0.858, cooling + coasting * 0.13 * (province - 0.5));
    float hardened = smoothstep(0.685, 0.986, cooling);
    float heat = sharedA.w * max(0.56 * warming, rupture) * (1.0 - hardened);
    float skin = clamp(0.09 + 0.50 * sharedA.z + 0.48 * hardened - 0.22 * melt +
                       0.19 * (grit.x - 0.5), 0.0, 1.0);
    if (variant == 1) {
        skin = min(1.0, skin + 0.27 * specialA.x);
    }

    Surface surface;
    if (variant == 0) {
        surface = coolingFlow(rockAt, skin, melt, pixel);
    } else if (variant == 1) {
        surface = basaltFlow(rockAt, skin, pixel);
    } else if (variant == 2) {
        surface = pahoehoe(rockAt, skin, travel, pixel);
    } else {
        surface = crustPlates(rockAt, skin, melt, travel, pixel);
    }

    // Rolling folds and slipping plates must remain inside the captures and can
    // never move the source lookup downstream of its current fragment.
    float room = min(q.x, max(0.0, screen.x - q.x));
    vec2 pigmentAt = vec2(q.x + shear * 0.52 * room * clamp(surface.slip.x, -1.0, 1.0),
                         q.y + shear * 0.44 * q.y * (1.0 - q.y) * buckle * clamp(surface.slip.y, -1.0, 1.0));
    float fault = abs(grit.x - (0.42 + 0.16 * province));
    float split = channel(fault, 0.064 + 0.043 * melt, 25.0 * pixel) *
                  smoothstep(0.17, 0.71, province);
    float fissure = clamp(max(surface.fissure * (0.68 + 0.61 * grit.x), 0.50 * split), 0.0, 1.0);
    fissure *= rupture * (0.66 + 0.34 * melt) * (1.0 - 0.55 * hardened);
    deposit -= 0.43 * fissure * deposit * (1.0 - deposit);

    vec3 bed = vec3(0.0);
    vec4 windowPigment = vec4(0.0);
    vec2 curl = clamp(0.54 * surface.slope + 0.38 * grit.yz + vec2(0.19, -0.16), -1.0, 1.0);
    if (deposit < 1.0) {
        float attack = smoothstep(0.13, 0.47, progress + 0.62 * lag);
        pigment(pigmentAt, curl, attack, fissure, grit.x, heat,
                sourceImage, sourceBottom, sourceWindows, bed, windowPigment);
    }
    if (deposit > 0.0) {
        // Destination texels are where this material WILL land. Looking them
        // up upstream then unwarping drags every incoming feature backwards.
        // Instead look downstream by the remaining travel: an incoming mark
        // approaches its resting x from upstream, slowing to zero without crossing.
        float coast = 1.0 - smoothstep(0.54, 0.986, progress);
        float remaining = coast * (0.69 + 0.18 * row + 0.28 * (1.0 - viscosity));
        vec2 arrivingAt = vec2(1.0 - upstream(1.0 - screen.x, remaining),
                               screen.y + 0.15 * coast * (pigmentAt.y - screen.y));
        float attack = 1.0 - smoothstep(0.73, 0.983, cooling - 0.024 * fissure * coasting);
        vec3 arrivingBed;
        vec4 arrivingWindows;
        pigment(arrivingAt, -curl, attack, fissure, grit.x, heat,
                destinationImage, destinationBottom, destinationWindows, arrivingBed, arrivingWindows);
        bed = mix(bed, arrivingBed, deposit);
        windowPigment = mix(windowPigment, arrivingWindows, deposit);
    }

    // Recombine before relighting: a dark window is a signed RGB contribution,
    // not a separately lit color. Intact sections retain more contrast until they char.
    bed += windowPigment.rgb;
    // Matte directional relief and porous crust keep the slab readable even away
    // from an open fissure. Pigment remains part of the rock, including above 1.0.
    vec2 slope = surface.slope * (0.62 + 0.38 * surface.relief) +
                 grit.yz * (0.23 + 0.33 * sharedA.z);
    vec3 normal = normalize(vec3(-slope, 1.0));
    float light = 0.68 + 0.36 * dot(normal, vec3(-0.43, -0.63, 0.65));
    float film = clamp(0.28 + 0.35 * sharedA.z + 0.47 * skin + 0.35 * (grit.x - 0.5) -
                       0.21 * melt * (0.44 + 0.56 * province), 0.0, 1.0);
    // Darkening alone leaves a bright print behind a network of hot lines. As
    // the print dissolves, carry its luminance into the mineral grains; chroma
    // survives mainly on colder islands. Both come from the transported texel.
    float luma = dot(bed, vec3(0.24, 0.64, 0.12));
    float island = smoothstep(0.36, 0.78, 0.50 * surface.relief + 0.50 * grit.x);
    float survival = mix(0.14, 0.76, detail) * (0.35 + 0.65 * island);
    vec3 mineral = mix(vec3(luma) * vec3(0.91, 0.78, 0.70), bed, survival);
    float grain = noise(rockAt * vec2(71.0, 109.0) + 3.1 * grit.x);
    float depth = (variant == 1 ? 0.62 : 0.80) * (0.50 + 0.50 * detail);
    vec3 crust = mineral * depth * (0.25 + 0.49 * island) +
                 vec3(0.053, 0.045, 0.042) * (0.28 + 0.72 * grain);
    vec3 fluid = mineral * depth * (0.28 + 0.37 * grit.x) +
                 vec3(0.112, 0.035, 0.013) * (0.44 + 0.56 * grain);
    vec3 rock = mix(fluid, crust, film) * light *
                (0.53 + 0.46 * surface.relief + 0.38 * grain) * (1.0 - 0.61 * fissure);
    float exposed = max(0.42 * warming, rupture) * (1.0 - hardened);
    vec3 body = mix(bed, rock, exposed * (1.0 - 0.76 * windowPigment.a));

    float core = fissure * fissure;
    vec3 radiation = vec3(0.068, 0.013, 0.003) * (0.35 + 0.65 * province);
    radiation += vec3(0.26, 0.052, 0.012) * (1.0 - film) * melt * (0.55 + 0.45 * grain);
    radiation += fissure * vec3(0.40, 0.046, 0.008) +
                 core * core * (vec3(1.64, 0.25, 0.022) + core * vec3(0.58, 0.83, 0.22));
    body += heat * radiation * (1.0 - 0.57 * windowPigment.a);
    if (sharedB.w > 0.0) {
        float ash = cinders(rockAt, progress, pixel) * exposed;
        body = body * (1.0 - 0.72 * ash) + vec3(0.46, 0.14, 0.035) * heat * ash;
    }
    color = vec4(body, 1.0);
}
)GLSL";
}  // namespace hyprimate::workspace::lava
