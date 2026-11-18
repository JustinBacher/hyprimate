#version 300 es
precision highp float;
precision highp sampler3D;
#include "common.glsl"
// Composite: the remaining solid window under lit, self-shadowed fluid smoke.
uniform sampler2D u_window, u_dye, u_velocity_field, u_age;
uniform vec2 u_origin, u_source, u_size, u_tex_origin, u_tex_size;  // device pixels
uniform vec2 u_logical, u_domain, u_grid, u_carry, u_velocity, u_position, u_offset;  // logical
uniform float u_time, u_seed, u_smoke, u_fade, u_life;
out vec4 frag;

vec4 windowAt(vec2 luv) {
    return texture(u_window, (u_source + luv * u_size - u_tex_origin) / u_tex_size);
}
float dyeAt(vec2 uv) { return texture(u_dye, uv).a; }

void main() {
    vec2 pixel = gl_FragCoord.xy + u_origin;
    vec2 local = (pixel - u_source) / u_size;
    bool within = all(greaterThanEqual(local, vec2(0.0))) && all(lessThan(local, vec2(1.0)));
    if (u_smoke <= 0.0 && u_fade <= 0.0 && u_mass <= 0.0) {
        frag = within ? windowAt(local) : vec4(0.0);
        return;
    }
    vec2 w = (local - .5) * u_logical;
    vec2 rel = w - u_carry;
    // The still-solid window is the contracting body itself: its content is mapped onto the
    // body's shrinking, rounding, stretching shape, so window and smoke stay one material.
    // While a head gathers the window up or condenses into it, the window stays in place.
    vec2 wuv = rel / u_logical + .5;
    vec2 luv = mix(bodyUV(rel), wuv, u_transit);
    float extent = max(min(u_logical.x, u_logical.y), 48.0);

    // Solid material: still-attached content is dragged along the flow before it lets go,
    // and its boundary frays rather than fading as a rectangle.
    vec4 solid = vec4(0.0);
    float phase = conversionPhase(luv, u_smoke, u_seed);
    {  // Unconditional: fwidth needs uniform control flow. Fray is zero once fully smoke.
        float speed = length(u_velocity);
        vec2 heading = speed > .001 ? u_velocity / speed : vec2(0.0);
        float moving = smoothstep(30.0, 700.0, speed);
        float grain = noise3(vec3(luv * vec2(u_logical.x / extent, u_logical.y / extent) * 9.0,
            u_seed * 23.0));
        vec2 flow = vec2(noise3(vec3(luv * 4.0, u_time * .5 + 2.0)),
                        noise3(vec3(luv * 4.0 + 5.0, u_time * .5))) - .5;
        vec2 stretch = heading * moving * (.35 + grain) * .16;
        // A contracting window's outline boils like the smoke it is becoming.
        vec2 boil = vec2(noise3(vec3(luv * 3.1, u_time * .9 + 7.0)),
                        noise3(vec3(luv * 3.1 + 11.0, u_time * .9))) - .5;
        vec2 tuv = luv + ((flow * .09 + stretch) * phase + boil * .22 * u_mass) * phase *
            extent / u_logical + boil * .12 * u_mass * extent / u_logical;
        float fray = 1.0 - smoothstep(.1 + .55 * grain, .3 + .6 * grain, phase);
        vec2 aa = fwidth(tuv);
        vec2 edge = smoothstep(vec2(0.0), aa, tuv) * (1.0 - smoothstep(1.0 - aa, vec2(1.0), tuv));
        float corner = 1.0;
        if (u_body_radius > 0.0) {
            float sd = bodyDistance(rel);
            float width = max(fwidth(sd), 1e-3);
            corner = mix(1.0 - smoothstep(-width, width, sd), 1.0, u_transit);
        }
        solid = windowAt(clamp(tuv, vec2(.0005), vec2(.9995))) * edge.x * edge.y * corner * fray;
    }

    vec2 texel = 1.0 / u_grid;
    vec2 guv = (w - u_offset) / u_domain + .5;
    vec4 dye = texture(u_dye, guv);
    vec4 smoke = vec4(0.0);
    if (dye.a > .002) {
        // Density gradient gives billows a surface; marching toward the light gives
        // dense folds their own shadow.
        vec2 tap = 1.5 * texel;
        vec2 gradient = vec2(dyeAt(guv + vec2(tap.x, 0)) - dyeAt(guv - vec2(tap.x, 0)),
            dyeAt(guv + vec2(0, tap.y)) - dyeAt(guv - vec2(0, tap.y)));
        vec3 normal = normalize(vec3(-gradient * 2.2, .5));
        const vec3 light = vec3(-.38, -.72, .58);
        float diffuse = max(dot(normal, normalize(light)), 0.0);
        float occlusion = 0.0;
        for (int i = 1; i <= 6; ++i) {
            occlusion += dyeAt(guv + normalize(light.xy) * texel * float(i * i) * 1.6);
        }
        // Density thickens the smoke optically: dense folds shade and hide more of what is
        // behind them while thin smoke stays translucent.
        float thickness = .5 + .75 * u_density;
        float shadow = exp(-occlusion * .38 * thickness);
        // Sub-grid fibres: noise smeared back along the local flow, so fast smoke breaks
        // into streaming strands and slow smoke into curdled folds.
        vec2 flow = texture(u_velocity_field, guv).xy;
        vec2 world = w + u_position;
        float fibres = 0.0;
        for (int i = 0; i < 4; ++i) {
            vec2 q = (world - flow * (.03 * float(i))) / extent * 10.0;
            fibres += noise3(vec3(q, u_time * .35 + u_seed * 5.0));
        }
        fibres = smoothstep(.22, .78, fibres * .25);
        float thin = 1.0 - smoothstep(.15, 1.3, dye.a);
        // Ageing smoke is drawn out into ever finer strands as it thins away.
        float aged = smoothstep(.3, .9, texture(u_age, guv).r / dye.a / max(u_life, .05));
        float density = dye.a * mix(1.0, .2 + 1.6 * fibres, min(.3 + .55 * thin + .3 * aged, 1.0));
        // The fluid ends at its domain, which travels with the window. Smoke thins out across
        // a ragged, world-anchored band before that edge, so no straight cut shows however
        // quickly the domain sweeps over a wake.
        vec2 edge = min(guv, 1.0 - guv) * u_domain;
        float band = max(.1 * min(u_domain.x, u_domain.y), 64.0);
        float ragged = noise3(vec3(world / extent * 2.3 + 31.0, u_time * .3 + u_seed * 3.0)) * .7 +
            noise3(vec3(world / extent * 6.1 + 47.0, u_time * .5 + u_seed * 7.0)) * .3;
        float reach = band * (.15 + 1.2 * ragged);
        float faded = smoothstep(reach - .3 * band, reach + .3 * band, min(edge.x, edge.y));
        density *= faded * faded;
        vec3 colour = dye.rgb / dye.a;
        float luma = dot(colour, vec3(.299, .587, .114));
        colour = mix(vec3(luma), colour, .85);
        colour *= .45 + .45 * shadow + .4 * diffuse * shadow;
        float alpha = (1.0 - exp(-density * 2.3 * thickness)) * u_fade;
        smoke = vec4(colour * alpha, alpha);
    }
    frag = smoke + solid * (1.0 - smoke.a);
}
