// Shared by every smoke pass. Positions are logical pixels from the window centre.
uniform sampler3D u_noise;
float noise3(vec3 p) { return texture(u_noise, (p + .5) / 32.0).r; }

// Previous fluid coordinates: resizing changes the domain without stretching detached
// wisps or throwing away their velocity and density.
uniform vec2 u_previous_domain, u_domain_scale, u_resize_rate;
uniform vec3 u_drag_motion;  // actual window displacement (logical px), followed by drag strength
vec2 previousUV(vec2 uv, vec2 shift) { return (uv - .5) * u_domain_scale + .5 + shift; }

// Opening and closing (see SmokeFrame): how far the body is the travelling head, its velocity,
// the window point conversion is ordered from, entering-edge streams, and the style-wide
// density and variation.
uniform vec2 u_head_velocity, u_origin_uv, u_edge_point, u_edge_dir;
uniform float u_transit, u_order, u_gather, u_density, u_variation, u_cohesion, u_attract;

// Static order in which the material unravels: rim first in movement, or outward
// from u_origin_uv in a close. A fixed material point does not feed or drain the fluid.
float conversionPhase(vec2 luv, float s, float seed) {
    vec2 c = clamp(luv, 0.0, 1.0) - .5;
    float edge = smoothstep(.2, .98, 2.0 * max(abs(c.x), abs(c.y)));
    float broad = .62 * noise3(vec3(luv * 2.6, seed * 31.0)) +
        .38 * noise3(vec3(luv * 7.3, seed * 31.0 + 7.0));
    float threshold = .7 - .36 * edge;
    float reach = clamp(length(clamp(luv, 0.0, 1.0) - u_origin_uv) / 1.1, 0.0, 1.0);
    threshold = mix(threshold, .3 + .55 * reach, u_order);
    threshold = clamp(threshold + .5 * (broad - .5), .25, .9);
    return s <= 0.0 ? 0.0 : smoothstep(threshold - .25, threshold + .07, s);
}

// Signed distance to the window rectangle and its outward direction.
float rectDistance(vec2 rel, vec2 halfsize, out vec2 outward) {
    vec2 q = abs(rel) - halfsize;
    if (max(q.x, q.y) > 0.0) {
        outward = normalize(max(q, 0.0)) * sign(rel);
    } else {
        outward = q.x > q.y ? vec2(sign(rel.x), 0.0) : vec2(0.0, sign(rel.y));
    }
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

// The smoke body (see SmokeBody): half extents, corner radius, inertial offset and stretch,
// plus how far it has contracted. At u_mass zero it is exactly the window rectangle.
uniform vec2 u_body_half, u_body_offset, u_body_stretch;
uniform float u_body_radius, u_mass, u_contraction, u_settle;

// Window-relative point to the body's own unstretched frame. The trailing side elongates
// much more than the leading side, giving a moving body a compressed front and drawn-out tail.
vec2 bodyLocal(vec2 rel) {
    vec2 p = rel - u_body_offset;
    float s = length(u_body_stretch);
    if (s < 1e-4) {
        return p;
    }
    vec2 dir = u_body_stretch / s;
    vec2 side = vec2(-dir.y, dir.x);
    float along = dot(p, dir);
    along /= along < 0.0 ? 1.0 + 1.1 * s : 1.0 + .2 * s;
    return along * dir + dot(p, side) * (1.0 + .3 * s) * side;
}
// Texture coordinate of the contracted window at a window-relative point.
vec2 bodyUV(vec2 rel) { return bodyLocal(rel) / (2.0 * u_body_half) + .5; }
// Exact signed distance to the body's rounded rectangle, in its own frame.
float bodyDistance(vec2 rel) {
    vec2 outward;
    float r = min(u_body_radius, min(u_body_half.x, u_body_half.y));
    return rectDistance(bodyLocal(rel), u_body_half - r, outward) - r;
}
// Size against which body turbulence is measured.
float bodyScale() { return max(2.0 * min(u_body_half.x, u_body_half.y), 48.0); }
// Full attachment through the rounded body's core, feathering to ordinary world-space flow.
float dragAttachment(vec2 rel) {
    float feather = max(.18 * bodyScale(), 4.0);
    return 1.0 - smoothstep(-feather, feather, bodyDistance(rel));
}
float dragRetention(vec2 rel, vec2 displacement) {
    float destination = dragAttachment(rel) * u_drag_motion.z;
    float source = dragAttachment(rel + u_drag_motion.xy - displacement) * u_drag_motion.z;
    return 1.0 - max(source - destination, 0.0);
}

// Content resolves only inside the growing head and where live smoke has gathered.
// The last part of the clock settles any remaining thin pockets into the exact surface.
float openingMaterial(vec2 rel, vec2 luv, float dye, float smoke, float seed, float extent) {
    if (smoke <= 0.0) {
        return 1.0;
    }
    float progress = 1.0 - smoke;
    vec2 farthest = max(u_origin_uv, 1.0 - u_origin_uv);
    float reach = clamp(length(luv - u_origin_uv) / length(farthest), 0.0, 1.0);
    float grain = .68 * noise3(vec3(luv * 4.0, seed * 31.0)) +
                  .32 * noise3(vec3(luv * 13.0, seed * 31.0 + 7.0));
    float arrival = .50 * reach * reach + .12 * grain;
    float body = 1.0 - smoothstep(-.06, .07, bodyDistance(rel) / extent + .1 * (grain - .5));
    float contact = smoothstep(.025, .30, dye) * body;
    return smoothstep(arrival, arrival + .31, progress) *
        mix(contact, 1.0, smoothstep(.70, 1.0, progress));
}

// The smoke body's dense core: the body shape with a boundary that boils with two octaves of
// noise travelling with the body, so no straight window edge survives in the smoke.
float bodyMask(vec2 rel, float t, float seed, float grow) {
    vec2 q = bodyLocal(rel);
    float scale = bodyScale();
    float shortest = min(u_body_half.x, u_body_half.y);
    float rounding = mix(.45 * shortest, max(u_body_radius, .2 * shortest), u_mass);
    vec2 outward;
    float sd = rectDistance(q, u_body_half - rounding, outward) - rounding;
    float broad = noise3(vec3(q / scale * 1.9, t * .4 + seed * 5.0)) - .5;
    float fine = noise3(vec3(q / scale * 5.3 + 3.0, t * .9 + seed * 3.0)) - .5;
    float d = sd + (broad * .7 + fine * .3) * scale - grow * scale;
    return 1.0 - smoothstep(-.16 * scale, .02 * scale, d);
}

// Local edge motion exists even when a centred resize has no translation. Keep it
// separate from the incompressible velocity solve, which would cancel this dilation.
vec2 resizeVelocity(vec2 rel) {
    return (rel - u_body_offset) * u_resize_rate * (1.0 - u_transit);
}

// Transport of the density alone, outside the incompressible flow: the pressure solve cancels
// any flow converging on a point, so smoke gathering into the head is moved directly. Opening
// draws loose smoke into the growing head; closing pulls the unravelled window after it while
// it travels, so the remainder trails behind the leading mass.
vec2 gatherDrift(vec2 rel, vec2 halfsize) {
    float resizeReach = 1.0 - smoothstep(0.0, .8 * bodyScale(), bodyDistance(rel));
    vec2 resizing = resizeVelocity(rel) * resizeReach;
    if (u_attract <= 0.0 && u_cohesion <= 0.0) {
        return resizing;
    }
    vec2 fromHead = rel - u_body_offset;
    float distance = length(fromHead);
    vec2 toHead = -fromHead / max(distance, 1.0);
    float scale = bodyScale();
    float sd = bodyDistance(rel);
    // Gathered smoke collects in a loose envelope round the head rather than packing into it.
    float outside = smoothstep(.1 * scale, .6 * scale, sd);
    float extent = max(2.0 * min(halfsize.x, halfsize.y), 48.0);
    float reach = 1.0 - smoothstep(1.2 * extent, 2.8 * extent, distance);
    vec2 drift = u_attract * reach * (toHead * outside * scale * 1.8 * min(distance / scale, 2.0) +
        u_head_velocity * .7 * smoothstep(0.0, .3 * scale, sd));
    drift += u_cohesion * outside * toHead * scale * (1.2 + 1.5 * min(sd / scale, 1.5));
    return drift + resizing;
}

// Divergence-free turbulence: curl of two octaves of evolving value noise.
vec2 curlNoise(vec3 p) {
    const float e = .07;
    float dx = noise3(p + vec3(e, 0, 0)) - noise3(p - vec3(e, 0, 0));
    float dy = noise3(p + vec3(0, e, 0)) - noise3(p - vec3(0, e, 0));
    return vec2(dy, -dx) / (2.0 * e);
}
