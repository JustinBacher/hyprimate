#version 300 es
precision highp float;
precision highp sampler3D;
#include "common.glsl"
// Semi-Lagrangian velocity advection plus the forces that give smoke its self-directed motion.
uniform sampler2D u_velocity_field, u_dye, u_age;
uniform vec2 u_grid, u_domain, u_shift, u_half, u_carry, u_velocity, u_lag, u_offset, u_position;
uniform float u_dt, u_time, u_seed, u_smoke, u_dispersal, u_turbulence, u_wind, u_life;
out vec4 frag;

float vorticity(vec2 uv, vec2 texel) {
    float r = texture(u_velocity_field, uv + vec2(texel.x, 0)).y;
    float l = texture(u_velocity_field, uv - vec2(texel.x, 0)).y;
    float t = texture(u_velocity_field, uv + vec2(0, texel.y)).x;
    float b = texture(u_velocity_field, uv - vec2(0, texel.y)).x;
    return .5 * ((r - l) - (t - b));
}

void main() {
    vec2 texel = 1.0 / u_grid;
    vec2 uv = gl_FragCoord.xy * texel;
    // Reproject the old field before advecting: world-anchored smoke survives both
    // translation and changing domain extents.
    vec2 p = previousUV(uv, u_shift);
    vec2 v = texture(u_velocity_field, p - u_dt * texture(u_velocity_field, p).xy / u_previous_domain).xy;
    vec2 inbounds = step(vec2(0.0), p) * step(p, vec2(1.0));
    v *= inbounds.x * inbounds.y;

    vec2 w = (uv - .5) * u_domain + u_offset;
    vec2 world = w + u_position;
    vec2 rel = w - u_carry;
    float extent = max(2.0 * min(u_half.x, u_half.y), 48.0);
    float scale = bodyScale();
    vec2 q = bodyLocal(rel);
    vec2 outward;
    float radius = min(u_body_radius, min(u_body_half.x, u_body_half.y));
    float sd = rectDistance(q, u_body_half - radius, outward) - radius;
    // The body moves with the window, or during opening and closing with its head.
    vec2 bodyVelocity = mix(u_velocity, u_head_velocity, u_transit);
    vec2 localVelocity = bodyVelocity + resizeVelocity(rel);
    float speed = length(localVelocity);
    float moving = smoothstep(30.0, 700.0, speed);
    float surge = smoothstep(80.0, 900.0, length(u_velocity - u_lag));
    vec2 heading = speed > .001 ? localVelocity / speed : vec2(0.0);
    float density = texture(u_dye, p).a;
    // A travelling mass: the moving window's contracted body or an opening/closing head.
    float massed = max(u_mass, u_transit);

    // The dense core travels with its body. A massed body holds together as one volume with
    // its own rolling circulation; its outer rear layer lags, so motion peels it away.
    float body = bodyMask(rel, u_time, u_seed, .06 + .14 * u_smoke);
    float interior = bodyMask(rel, u_time, u_seed, -.1) * massed * (1.0 - u_dispersal);
    float rear = clamp(dot(q / max(u_body_half, vec2(1.0)), -heading), 0.0, 1.0);
    float follow = body * (1.0 - .9 * moving * rear * (1.0 - interior)) * (1.0 - u_dispersal);
    vec2 circulation = curlNoise(vec3(q / scale * 1.7, u_time * .55 + u_seed * 4.0)) *
        scale * (.5 + 1.1 * u_turbulence) * (1.0 + .8 * surge) * massed;
    v = mix(v, bodyVelocity + circulation, 1.0 - exp(-u_dt * (6.0 * follow + 18.0 * interior)));
    // Loose smoke, outside the body and away from an opening or closing head, and how many
    // lifetimes it has aged since leaving the body (see dye.frag).
    float loose = (1.0 - body) * (1.0 - u_transit) * smoothstep(.004, .06, density);
    float life = clamp(texture(u_age, p).r / max(density, 1e-3) / max(u_life, .05), 0.0, 1.5);
    float rolling = smoothstep(.06, .4, life);
    float shredding = smoothstep(.35, .85, life);
    // Fresh smoke near the mass is still carried by its momentum, which lags the window, so
    // a stop or a turn leaves it coasting on the old heading rather than braking with the window.
    float near = 1.0 - smoothstep(.15 * scale, 1.2 * scale, sd);
    v = mix(v, .7 * u_lag, 1.0 - exp(-u_dt * 3.0 * loose * (1.0 - rolling) * near * u_mass));
    // Only air and smoke around the body feel the window's motion directly. Smoke that has
    // drifted away keeps its own motion however the window is moved.
    float vicinity = 1.0 - smoothstep(.3 * scale, 1.6 * scale, sd);
    // While the body contracts or expands, the material it is drawing in moves with that
    // deformation, so freshly unravelled content joins the mass rather than being left
    // behind. Only that fresh material follows, across a ragged band rather than the window's
    // outline, so neither detached smoke nor a straight edge is dragged along with the window.
    vec2 unused;
    float ragged = (noise3(vec3(world / extent * 3.1, u_time * .5 + u_seed * 15.0)) - .5) * .3 * extent;
    float region = 1.0 - smoothstep(-.1 * extent, .1 * extent, rectDistance(rel, u_half, unused) + ragged);
    float fresh = (1.0 - smoothstep(.02, .12, life)) * smoothstep(.004, .06, density);
    float deforming = region * fresh * smoothstep(.1, .8, abs(u_contraction)) * (1.0 - u_dispersal);
    vec2 squeeze = -(rel - u_body_offset) * u_contraction;
    v = mix(v, u_velocity + squeeze, 1.0 - exp(-u_dt * 16.0 * deforming));

    // Cohesion draws loose smoke into the body along curling paths; condensed smoke settles.
    // A travelling mass also keeps the smoke just outside its front and flanks attached.
    float outside = smoothstep(0.0, .06 * scale, sd);
    vec2 inward = -outward;
    float handed = noise3(vec3(rel / extent * .9, u_seed * 9.0 + u_time * .15)) * 2.0 - 1.0;
    vec2 swirl = vec2(-inward.y, inward.x) * handed;
    float pull = u_cohesion * outside * scale * (3.0 + 3.0 * min(sd / scale, 1.5));
    float shell = massed * outside * (1.0 - smoothstep(.2 * scale, .55 * scale, sd)) *
        (1.0 - .85 * rear) * (1.0 - u_dispersal) * (1.0 - .8 * u_settle);
    v += u_dt * (pull * (inward + .45 * swirl) + shell * scale * 5.0 * (inward + .6 * swirl));
    v *= exp(-u_dt * 4.0 * u_cohesion * (1.0 - outside));

    // Closing: the unravelled window streams after its head, pulled toward it while carrying
    // on with its momentum, so the remainder of the window trails behind the leading mass.
    vec2 fromHead = rel - u_body_offset;
    float headDistance = length(fromHead);
    if (u_attract > 0.0) {
        vec2 toHead = -fromHead / max(headDistance, 1.0);
        float reach = outside * (1.0 - smoothstep(1.2 * extent, 2.6 * extent, headDistance));
        vec2 target = u_head_velocity * .8 + toHead * scale * 3.0 * min(headDistance / scale, 1.6);
        v = mix(v, target, 1.0 - exp(-u_dt * 5.0 * u_attract * reach * min(density * 4.0, 1.0)));
    }

    // Opening from an edge: streams leave the edge inward at staggered speeds.
    if (u_gather > 0.0) {
        vec2 side = vec2(-u_edge_dir.y, u_edge_dir.x);
        vec2 fromEdge = rel - u_edge_point;
        float depth = -dot(fromEdge, u_edge_dir);
        float along = dot(fromEdge, side);
        float spread = abs(side.x) * u_half.x + abs(side.y) * u_half.y;
        float band = (1.0 - smoothstep(.2 * scale, 1.5 * scale, abs(depth))) *
            (1.0 - smoothstep(.6 * spread, 1.3 * spread, abs(along)));
        float pace = .5 + noise3(vec3(along / scale * 1.3, u_seed * 11.0, u_time * .7));
        v += u_dt * u_gather * band * scale * 6.0 * pace * (-u_edge_dir + .3 * side * (pace - 1.0));
    }

    // The air turns round the head: a gentle, gusty current, strongest a little outside the
    // mass, with a slight inflow and lift, that wraps tendrils round it before they escape.
    if (u_wind != 0.0) {
        float core = .7 * scale;
        float profile = 2.0 * headDistance * core / (headDistance * headDistance + core * core);
        vec2 radial = fromHead / max(headDistance, 1.0);
        float gust = 1.0 + .6 * (1.0 + u_variation) *
            (noise3(vec3(world / extent * 1.3, u_time * .45 + u_seed * 17.0)) - .5);
        v += u_dt * u_wind * scale * 5.0 * profile * gust *
            (vec2(-radial.y, radial.x) - .3 * radial + vec2(0.0, -.15));
    }

    // Settling: where the noise puffs out, the reforming body's rim throws off rising, curling
    // wisps that leave it; elsewhere, smoke within reach is drawn back in along curling paths
    // and condenses into the window. Smoke beyond that reach is left to dissipate.
    if (u_settle > 0.0) {
        float puffs = smoothstep(.45, .75,
            noise3(vec3(q / scale * 2.6, u_time * 1.4 + u_seed * 5.0)));
        float rim = smoothstep(-.08 * scale, .02 * scale, sd) *
            (1.0 - smoothstep(.15 * scale, .8 * scale, sd));
        v += u_dt * u_settle * rim * puffs * scale * 4.0 *
            (outward + vec2(0.0, -.7) + .9 * swirl);
        float reach = outside * (1.0 - smoothstep(.4 * scale, 1.4 * scale, sd)) * (1.0 - puffs);
        v += u_dt * u_settle * reach * scale * (2.5 + 2.0 * min(sd / scale, 1.0)) *
            (inward + .7 * swirl);
    }

    // Broad slow folds plus small fast eddies anchored to the screen; dispersal, and near the
    // body motion and acceleration, stir them harder.
    vec2 broad = curlNoise(vec3(world / extent * 1.4, u_time * .3 + u_seed * 13.0));
    vec2 fine = curlNoise(vec3(world / extent * 4.6 + 19.0, u_time * 1.1 + u_seed * 7.0));
    float stir = u_turbulence * extent * (.7 + (1.0 * moving + 1.2 * surge) * vicinity + 2.2 * u_dispersal);
    v += u_dt * stir * (broad * .8 + fine * .55) * (.35 + .65 * min(density * 2.0, 1.0));
    // Shed smoke breaks into smaller curling tendrils as it falls behind a travelling mass;
    // the freshest smoke holds together a little longer.
    vec2 tendrils = curlNoise(vec3(world / extent * 9.0 + 41.0, u_time * 1.8 + u_seed * 3.0));
    v += u_dt * u_turbulence * extent * 8.0 * max(moving * massed * vicinity, u_settle) * (1.0 - body) *
        tendrils * min(density * 3.0, 1.0) * mix(1.0, .4 + .6 * rolling, loose);
    // Behind a travelling mass the shed layer is kicked sideways at irregular intervals, so
    // the wake meanders and rolls up into curls instead of trailing as straight streaks.
    if (massed > 0.0 && speed > 1.0) {
        vec2 side = vec2(-heading.y, heading.x);
        float behind = smoothstep(.2, .9, dot(normalize(q + vec2(1e-4)), -heading));
        float zone = smoothstep(0.0, .1 * scale, sd) * (1.0 - smoothstep(.4 * scale, 2.0 * scale, sd));
        float kick = noise3(vec3(world / extent * 2.2, u_time * 1.6 + u_seed * 9.0)) * 2.0 - 1.0;
        float girth = u_transit <= 0.0 ? u_trail_girth : 1.0;
        v += u_dt * side * kick * speed * 6.0 * zone * behind * moving * massed * girth;
    }
    // Loose smoke develops motion of its own as it ages, whatever the window does. Shortly
    // after leaving the body it is wound up into eddies; the flow carries those eddies with
    // the smoke, so the wake rolls up into curls and smaller masses that keep circulating
    // wherever they drift. Clumps keep spinning in the sense they already turn, and older
    // smoke stretches and shreds into fine tendrils.
    float omega = vorticity(p, texel);
    vec2 slope = vec2(texture(u_dye, p + vec2(texel.x, 0)).a - texture(u_dye, p - vec2(texel.x, 0)).a,
        texture(u_dye, p + vec2(0, texel.y)).a - texture(u_dye, p - vec2(0, texel.y)).a);
    float seeded = noise3(vec3(world / extent * 1.7, u_seed * 29.0 + u_time * .25)) * 2.0 - 1.0;
    float spin = clamp(.6 * seeded - omega / 25.0, -1.0, 1.0);
    float vigour = (.5 + u_turbulence) * extent * loose;
    v += u_dt * vigour * 22.0 * rolling * (1.0 - .5 * shredding) * spin * vec2(-slope.y, slope.x);
    float windup = smoothstep(.03, .15, life) * (1.0 - smoothstep(.3, .55, life));
    vec2 roll = curlNoise(vec3(world / extent * 2.8 + 23.0, u_time * .9 + u_seed * 11.0));
    vec2 shred = curlNoise(vec3(world / extent * 8.0 + 57.0, u_time * 2.0 + u_seed * 5.0));
    v += u_dt * vigour * (roll * 7.0 * windup + shred * 2.5 * shredding) * min(density * 3.0, 1.0);
    // Ageing smoke lifts and rolls over as it loosens.
    v.y -= u_dt * extent * .35 * loose * rolling * min(density, 1.0);

    // Smoke is lighter than air, and dissipating smoke breaks apart from its body.
    v.y -= u_dt * extent * min(density, 1.5) * (.15 + .9 * u_dispersal);
    v += u_dt * u_dispersal * (1.0 - u_attract) * extent * .6 * normalize(fromHead + vec2(.001)) *
        (1.0 - .5 * outside);

    // Vorticity confinement restores the curls numerical diffusion removes; loosened smoke
    // keeps more of its small-scale circulation.
    vec2 gradient = vec2(abs(vorticity(p + vec2(texel.x, 0), texel)) -
            abs(vorticity(p - vec2(texel.x, 0), texel)),
        abs(vorticity(p + vec2(0, texel.y), texel)) - abs(vorticity(p - vec2(0, texel.y), texel)));
    gradient /= length(gradient) + 1e-4;
    v += u_dt * 14.0 * (.4 + u_turbulence) * (1.0 + .5 * loose * rolling) *
        vec2(gradient.y, -gradient.x) * omega;

    // Still air around a travelling mass holds back what it disturbs. Wake smoke instead
    // carries the momentum it left the body with, overshooting a window that stops or turns,
    // and its own currents slow only gradually as it ages while the forces above keep it moving.
    float air = .6 + 4.5 * moving * vicinity * (1.0 - body) * (1.0 - u_dispersal);
    v *= exp(-u_dt * mix(air, .5 + .9 * life, loose));
    vec2 border = smoothstep(vec2(0.0), 4.0 * texel, uv) * smoothstep(vec2(0.0), 4.0 * texel, 1.0 - uv);
    frag = vec4(v * border.x * border.y, 0.0, 1.0);
}
