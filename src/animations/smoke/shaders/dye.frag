#version 300 es
precision highp float;
precision highp sampler3D;
#include "common.glsl"
// Window-coloured smoke density: premultiplied colour in rgb, density in alpha.
// The window feeds it where its material turns to smoke and absorbs it where it condenses.
uniform sampler2D u_velocity_field, u_dye, u_window, u_predicted;
uniform vec2 u_grid, u_domain, u_shift, u_half, u_carry, u_offset, u_position, u_velocity;
uniform vec2 u_source, u_size, u_tex_origin, u_tex_size;
uniform float u_dt, u_time, u_seed, u_smoke, u_previous_smoke, u_presence;
uniform float u_dispersal, u_trail;
out vec4 frag;

vec4 windowAt(vec2 luv) {
    return texture(u_window, (u_source + clamp(luv, 0.0, 1.0) * u_size - u_tex_origin) / u_tex_size);
}
// Scattered window colour: content survives as broad patches rather than a crisp copy.
vec4 materialAt(vec2 luv, float spread) {
    vec2 r = spread * vec2(1.0, u_half.x / max(u_half.y, 1.0));
    return windowAt(luv) * .36 + windowAt(luv + vec2(r.x, r.y * .3)) * .16 +
        windowAt(luv - vec2(r.x, r.y * .3)) * .16 + windowAt(luv + vec2(-r.x * .3, r.y)) * .16 +
        windowAt(luv - vec2(-r.x * .3, r.y)) * .16;
}

void main() {
    vec2 texel = 1.0 / u_grid;
    vec2 uv = gl_FragCoord.xy * texel;
    // MacCormack correction of the predicted transport, limited to the departure cell's
    // range so filaments stay crisp without ringing. The velocity is already scrolled.
    vec2 v = texture(u_velocity_field, uv).xy +
        gatherDrift((uv - .5) * u_domain + u_offset - u_carry, u_half);
    vec2 source = previousUV(uv, u_shift) - u_dt * v / u_previous_domain;
    vec4 predicted = texture(u_predicted, uv);
    vec4 dye = predicted + .5 * (texture(u_dye, previousUV(uv, u_shift)) -
        texture(u_predicted, uv + u_dt * v / u_domain));
    ivec2 base = ivec2(floor(source * u_grid - .5));
    ivec2 last = ivec2(u_grid) - 1;
    vec4 a = texelFetch(u_dye, clamp(base, ivec2(0), last), 0);
    vec4 b = texelFetch(u_dye, clamp(base + ivec2(1, 0), ivec2(0), last), 0);
    vec4 c = texelFetch(u_dye, clamp(base + ivec2(0, 1), ivec2(0), last), 0);
    vec4 d = texelFetch(u_dye, clamp(base + ivec2(1, 1), ivec2(0), last), 0);
    dye = clamp(dye, min(min(a, b), min(c, d)), max(max(a, b), max(c, d)));
    vec2 inbounds = step(vec2(0.0), source) * step(source, vec2(1.0));
    dye *= inbounds.x * inbounds.y;

    vec2 w = (uv - .5) * u_domain + u_offset;
    vec2 rel = w - u_carry;
    vec2 wuv = rel / (2.0 * u_half) + .5;
    // Window content mapped onto the body: the moving window contracts into it, and a head
    // carries a scattered miniature of the window's colours.
    vec2 buv = bodyUV(rel);
    // Material coordinates: the contracting moving window, or the window itself while a
    // head gathers it up or condenses into it.
    vec2 luv = mix(buv, wuv, u_transit);
    float extent = max(2.0 * min(u_half.x, u_half.y), 48.0);
    float scale = bodyScale();
    float sd = bodyDistance(rel);
    vec2 unused;
    float material = mix(sd, rectDistance(rel, u_half, unused), u_transit);
    float cell = u_domain.x * texel.x;
    float inside = 1.0 - smoothstep(-cell, cell, material);
    float body = bodyMask(rel, u_time, u_seed, .06 + .14 * u_smoke);
    float core = bodyMask(rel, u_time, u_seed, 0.0);
    float phase = conversionPhase(luv, u_smoke, u_seed);
    float previous = conversionPhase(luv, u_previous_smoke, u_seed);
    float massed = max(u_mass, u_transit);

    // Wake smoke thins quickly, the attached mass slowly; condensing window material
    // reabsorbs the smoke over it.
    float wake = mix(2.6, .55, u_trail) * (1.0 + .6 * u_mass);
    // Dissipating smoke breaks up in patches rather than fading evenly.
    float patches = noise3(vec3((w + u_position) / extent * 3.4, u_time * .6 + u_seed * 9.0));
    float decay = mix(wake, .25, body * (1.0 - u_dispersal)) +
        2.4 * u_dispersal * (.1 + 2.6 * patches * patches);
    decay += 10.0 * (1.0 - phase) * inside * (.25 + u_cohesion + .3 * u_settle);
    // Gathering compresses smoke; beyond the body's own density the excess thins away so a
    // gathered head stays billowy instead of packing into an opaque ball.
    decay += 2.5 * u_transit * max(dye.a - 1.3 * (.55 + .6 * massed) * (.6 + .5 * u_density), 0.0);
    // Smoke drifting toward the domain edge thins out across a ragged band instead of
    // meeting a straight cut.
    vec2 edgeDistance = min(uv, 1.0 - uv);
    float ragged = noise3(vec3((w + u_position) / extent * 2.6, u_time * .3 + u_seed * 3.0));
    decay += 9.0 * (1.0 - smoothstep(.015, .04 + .06 * ragged, min(edgeDistance.x, edgeDistance.y)));
    dye *= exp(-u_dt * decay);

    // Material that has just unravelled becomes smoke where it stood, in its own colours.
    vec2 drift = (vec2(noise3(vec3(luv * 3.0, u_time * .4 + 3.0)),
                      noise3(vec3(luv * 3.0 + 9.0, u_time * .4))) - .5) * .05;
    dye += materialAt(luv, .012) * max(phase - previous, 0.0) * inside * 1.7 * (1.0 - .4 * massed);
    // While it remains smoke, the body is kept full: denser the further it has contracted and
    // the higher the density setting, with slow seeded variations through its volume. Only
    // the deep interior of a massed body is refilled, so its wake comes from what peels off
    // the surface rather than from a continuous stream of new material.
    float feed = mix(core, bodyMask(rel, u_time, u_seed, -.14), u_mass);
    float sustain = feed * mix(phase, 1.0, u_transit * u_smoke) * u_presence *
        (1.0 - u_dispersal);
    // Seeded density variation; a head also carries layered folds so it never fills evenly.
    vec2 folded = bodyLocal(rel) / scale;
    float folds = .65 * noise3(vec3(folded * 1.8, u_time * .3 + u_seed * 21.0)) +
        .35 * noise3(vec3(folded * 4.4 + 7.0, u_time * .7 + u_seed * 13.0)) - .5;
    float clumping = max(1.0 + (.6 * u_variation + 1.1 * u_transit) * folds, .15);
    float deficit = max((.55 + .6 * massed) * (.6 + .5 * u_density) * clumping * sustain - dye.a, 0.0);
    dye += materialAt(mix(luv + drift * phase, clamp(buv, 0.0, 1.0), u_transit), .03 + .08 * u_transit) *
        deficit * (1.0 - exp(-u_dt * 5.0));

    // A travelling mass sheds clumps of itself from its rear and flanks at irregular places
    // and times; once outside the body they meet still air and fall behind as the wake.
    vec2 bodyVelocity = mix(u_velocity, u_head_velocity, u_transit) + resizeVelocity(rel);
    float speed = length(bodyVelocity);
    if (massed > 0.0 && speed > 1.0) {
        vec2 q = bodyLocal(rel);
        float rear = dot(q / max(u_body_half, vec2(1.0)), -bodyVelocity / speed);
        float band = smoothstep(-.12 * scale, .02 * scale, sd) *
            (1.0 - smoothstep(.05 * scale, .3 * scale, sd)) * smoothstep(-.2, .6, rear);
        float puffs = smoothstep(.5 - .1 * u_variation, .8,
            noise3(vec3(q / scale * 3.2, u_time * 2.3 + u_seed * 7.0)));
        float shed = band * puffs * massed * mix(phase, 1.0, u_transit) * u_presence *
            smoothstep(40.0, 600.0, speed);
        dye += materialAt(clamp(buv, 0.0, 1.0), .05) * shed * u_trail * u_dt * 9.0;
    }
    // A settling body's rim, the last part to condense, keeps fizzing off wisps of itself.
    if (u_settle > 0.0) {
        float rim = smoothstep(-.25 * scale, -.03 * scale, sd) * (1.0 - smoothstep(0.0, .12 * scale, sd));
        float puffs = smoothstep(.45, .75, noise3(vec3(bodyLocal(rel) / scale * 2.6, u_time * 1.4 + u_seed * 5.0)));
        dye += materialAt(clamp(luv, 0.0, 1.0), .04) * rim * puffs * max(phase, .6 * u_settle) *
            u_presence * u_settle * u_trail * u_dt * 28.0;
    }

    // Opening from an edge: wisps of smoke appear along the screen edge where the head
    // gathers, at staggered places and times, carrying the colours of the window's matching
    // band; the edge flow then carries them in.
    if (u_gather > 0.0) {
        vec2 side = vec2(-u_edge_dir.y, u_edge_dir.x);
        vec2 fromEdge = rel - u_edge_point;
        float depth = -dot(fromEdge, u_edge_dir);
        float along = dot(fromEdge, side);
        float spread = abs(side.x) * u_half.x + abs(side.y) * u_half.y;
        float band = smoothstep(-.1 * scale, .05 * scale, depth) *
            (1.0 - smoothstep(.15 * scale, .6 * scale, depth)) *
            (1.0 - smoothstep(.5 * spread, 1.1 * spread, abs(along)));
        float streams = smoothstep(.5 - .1 * u_variation, .78,
            noise3(vec3(along / scale * 2.4, depth / scale * .8 - u_time * 1.5, u_seed * 13.0)));
        float depthHalf = abs(u_edge_dir.x) * u_half.x + abs(u_edge_dir.y) * u_half.y;
        vec2 facing = side * clamp(dot(rel, side), -spread, spread) + u_edge_dir * .9 * depthHalf;
        vec2 band_uv = facing / (2.0 * u_half) + .5;
        dye += materialAt(band_uv, .06) * band * streams * u_gather * u_dt * 14.0;
    }
    vec2 border = smoothstep(vec2(0.0), 6.0 * texel, uv) * smoothstep(vec2(0.0), 6.0 * texel, 1.0 - uv);
    frag = min(dye * border.x * border.y, vec4(3.0));
}
