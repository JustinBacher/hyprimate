# hyprimate

Hyprimate is a [Hyprland](https://hyprland.org/) plugin that animates windows as they open, move, and close. Choose a style for your windows:

- **Liquid** makes windows flow and stretch.
- **Cloth** drapes and folds windows like a curtain.
- **Paper** crumples windows as they open and close; movement remains undeformed.
- **Swarm** transforms windows into creatures that scatter and regroup.
- **Bubble** gives windows a soap-film surface that inflates, moves, and bursts.

## Demo

![Demo of hyprimate's window animations](hyprimate-demo.webp)

[Watch the full-quality video](hyprimate-demo-closing.mp4)

## Installation

Hyprimate has been tested against **Hyprland 0.57.0**; the [Nix flake](flake.nix) pins **0.57.2** and builds against that version. For hyprpm, use development headers that match your running Hyprland build, and rebuild the plugin after updating Hyprland.

`hyprpm` builds the plugin from source. In addition to [hyprpm's own prerequisites](https://wiki.hypr.land/Plugins/Using-Plugins/), the build needs:

- CMake 3.25+, Ninja, a C++26-capable compiler, and `pkg-config`.
- The matching `Hyprland` executable plus `nm` and `ldd` for the post-link import check.
- nlohmann/json 3.11+ headers, libpng, OpenGL ES 3 and EGL (including development headers), and Lua headers and libraries matching Hyprland's Lua ABI.

On Arch Linux, the library packages are `nlohmann-json`, `libpng`, `libglvnd`, and `lua`. Some are already installed with Hyprland. Python is only needed for development and tests; Jolt is no longer used.

Add the project's Git repository with hyprpm, then enable and load the plugin:

```sh
hyprpm add https://github.com/JustinBacher/hyprimate
hyprpm enable hyprimate
hyprpm reload
```

The plugin is registered with hyprpm as `hyprimate`. Access to the repository is required while it is private.

If you used the former `liquid-glass` plugin name, update your hyprpm entry and Lua configuration to `hyprimate`. The settings now use `plugin:hyprimate:*`; the former keys are no longer registered.

To load enabled plugins at startup, add `hyprpm reload` to your Hyprland autostart. See the [Hyprland plugin guide](https://wiki.hypr.land/Plugins/Using-Plugins/) for hyprpm dependencies, updates, and permission settings.

## Configuration

Hyprland's built-in wobble conflicts with Hyprimate. If enabled, disable `decoration.wobble.enabled` before loading the plugin. Hyprland's regular window and workspace animations can remain enabled.

The example below shows the default settings and disables wobble. After loading the plugin for the first time, add the settings to your Hyprland Lua configuration. Set `window.style` to choose an animation style; movement, opening, and closing can be enabled separately. Style options under `window.options` apply globally; an option under `window.open.options`, `window.close.options`, or `window.movement.options` overrides it for that animation.

Swarm's former `creature` and `responsiveness` settings are now named `sprite` and `agility`; update both global settings and any per-animation overrides.

During a mouse move gesture, swarm agents keep wandering even when the pointer pauses. `wander` controls this motion; zero retains formation when stationary. Releasing the gesture lets the agents settle back into the window.

For example, to set a shared bubble duration while giving closing its own duration:

```lua
hl.config({ plugin = { hyprimate = { window = {
    options = { bubble = { duration = 0.8 } },
    close = { options = { bubble = { duration = 0.3 } } },
} } } })
```

<!-- BEGIN GENERATED CONFIGURATION -->
After the first load has registered the settings, add this Lua configuration:

```lua
hl.config({
    decoration = { wobble = { enabled = false } },
    plugin = {
        ["hyprimate"] = {
            window = {
                options = {
                    construction = {
                        -- duration = 620,
                        worker_size = 1,
                    },
                    burn = {
                        -- duration = 1000,
                        variation = 0.7,
                        fragment_density = 0.55,
                        ember_density = 0.65,
                        wind_direction = 0,
                        wind_strength = 100,
                        gravity_strength = 300,
                        scorch_lifetime = 1.4,
                        healing_duration = 1.8,
                    },
                    liquid = {
                        strength = 0.65,
                        viscosity = 0.75,
                        elasticity = 0.5,
                        momentum = 0.5,
                        variation = 0.5,
                        -- duration = 0,
                        color = 0xff000000,
                        default_animation_edge = "bottom",
                        edge_bounds = "screen",
                        emerge_drag = 0.5,
                        side = "auto",
                        splash = true,
                    },
                    cloth = {
                        strength = 0.65,
                        springiness = 0,
                        gravity = 1,
                        flutter = 0.5,
                        shading = 0.7,
                        shine = 0,
                        variation = 0.5,
                        -- duration = 1000,
                        default_animation_edge = "bottom",
                        edge_bounds = "screen",
                        side = "auto",
                    },
                    bubble = {
                        strength = 0.65,
                        microbubbles = 0.5,
                        trail = 0.5,
                        shape = "pillow",
                        roundness = 1,
                        shape_speed = 1200,
                        transparency = 0.88,
                        -- duration = 1000,
                    },
                    smoke = {
                        strength = 0.85,
                        turbulence = 0.65,
                        trail = 0.65,
                        -- duration = 1400,
                        -- direction = "inside",
                        color = 0x00000000,
                        density = 1,
                        variation = 0.5,
                    },
                    paper = {
                        -- duration = 600,
                        crumpliness = 0.6,
                        shading = 0.7,
                        variation = 0.5,
                        fold_density = 0.55,
                        -- paper_stiffness = 0.72,
                        crumple_size = 0.18,
                        crumple_origin = "center",
                        force_direction = "inward",
                        rotation_intensity = 0.22,
                        -- settling = 0.45,
                        simulation_quality = "adaptive",
                        stiffness = 0.75,
                        inertia = 0.45,
                        settle_damping = 0.8,
                        move = {
                            style = "flutter",
                            flutter = {
                                air_resistance = 0.65,
                                intensity = 0.5,
                                frequency = 1.2,
                                lift = 0.3,
                                torsion = 0.4,
                            },
                            glide = {
                                friction = 0.6,
                                corner_lift = 0.25,
                                edge_curl = 0.3,
                                shadow_separation = 0.2,
                            },
                        },
                        resize = {
                            fold_intensity = 0.6,
                            curl_intensity = 0.45,
                            crease_density = 0.5,
                        },
                    },
                    swarm = {
                        -- duration = 0,
                        lone_window_sides = "all",
                        sprite = "spider",
                        count = 250,
                        speed = 1400,
                        stagger = 0.6,
                        cohesion = 0.5,
                        separation = 0.5,
                        agility = 0.5,
                        settling = 0.5,
                        wander = 0.5,
                        variation = 0.5,
                    },
                },
                enabled = true,
                style = "liquid",
                open = {
                    effect = "",
                    effect_parameters = "{}",
                    effect_duration = 600,
                    enabled = true,
                    options = {
                        paper = {
                            unfold_tension = 0.62,
                            crease_persistence = 0.62,
                        },
                        bubble = {
                            count = 140,
                            birth_rate = 1,
                            size_variation = 0.75,
                            growth_variation = 0.6,
                            interaction = 1,
                            damping = 1,
                            buoyancy = 1,
                            linger = 0.5,
                        },
                    },
                },
                close = {
                    effect = "",
                    effect_parameters = "{}",
                    effect_duration = 600,
                    options = {
                        burn = {
                            closing_style = "curl",
                            debris_direction = "natural",
                            curl_intensity = 0.8,
                        },
                    },
                    enabled = true,
                },
                move = {
                    effect = "",
                    effect_parameters = "{}",
                    effect_duration = 350,
                },
                resize = {
                    effect = "",
                    effect_parameters = "{}",
                    effect_duration = 350,
                },
                movement = {
                    options = {
                        burn = {
                            smoke_density = 0.22,
                        },
                        construction = {
                            variant = "crew_carry",
                        },
                        smoke = {
                            drag_follow = 0,
                            trail_length = 0,
                            trail_lifetime = 0,
                            trail_thickness = 1,
                            reform = 0.48,
                            settle_speed = 1,
                            mass = 0.45,
                            roundness = 0.7,
                        },
                    },
                    enabled = true,
                    workspace_movement = true,
                },
            },
            workspace = {
                enabled = false,
                style = "rotation",
                effect = "",
                effect_parameters = "{}",
                effect_duration = 600,
                options = {
                    rotation = {
                        duration = 600,
                        curve = "default",
                        gap_fill_style = "still",
                        gap_fill_color = 0xff000000,
                        gap_fill_blur = 0,
                        keep_overlays_static = false,
                    },
                    planet_hop = {
                        window_mode = "surface",
                        overlay_mode = "fade",
                        planet_layout = "focused",
                        duration = 800,
                        curve = "linear",
                        live_boundary = 0.22,
                        sampling_quality = 2,
                        planet_size = 1,
                        planet_separation = 4,
                        solar_vertical_spread = 0.65,
                        solar_background_scale = 0.75,
                        planet_coverage = 0.7,
                        indirect_brightness = 0.18,
                        elevation = 0.2,
                        fade = 0.2,
                        rotation_enabled = true,
                        rotation_limit = 0.35,
                        stars_enabled = true,
                        star_density = 0.5,
                        star_parallax = 1,
                        atmosphere_enabled = true,
                        atmosphere_intensity = 0.3,
                        atmosphere_thickness = 0.035,
                        lighting_enabled = true,
                        light_direction_x = -0.6,
                        light_direction_y = -0.3,
                        light_direction_z = 1,
                        night_brightness = 0.45,
                        clouds_enabled = true,
                        cloud_coverage = 0.45,
                        cloud_opacity = 0.18,
                        cloud_speed = 0.025,
                        depth_enabled = true,
                        depth_strength = 0.018,
                        depth_source = "",
                    },
                    cube = {
                        variant = "solid",
                        duration = 600,
                        curve = "default",
                        solid = {
                            cube_tilt = 0,
                            zoom = 0.25,
                        },
                        inside_out = {
                            cube_tilt = 0,
                            zoom = 0.45,
                        },
                    },
                    lava = {
                        variant = "crust_plates",
                        window_behavior = "melt_with_workspace",
                        viscosity = 0.72,
                        cooling_rate = 0.8,
                        crust_amount = 0.68,
                        glow_intensity = 0.55,
                        detail_preservation = 0.75,
                        trail_smear = 0.35,
                        edge_drip = 0.15,
                        ash_amount = 0.08,
                        cooling_flow = {
                            leading_bulge = 0.45,
                            trailing_stretch = 0.4,
                            molten_turbulence = 0.3,
                        },
                        basalt_flow = {
                            crust_thickness = 0.75,
                            fissure_width = 0.28,
                            ember_visibility = 0.12,
                            mass_density = 0.8,
                        },
                        pahoehoe = {
                            rope_strength = 0.65,
                            fold_scale = 0.5,
                            ripple_speed = 0.4,
                            surface_roll = 0.55,
                        },
                        crust_plates = {
                            plate_size = 0.55,
                            plate_rigidity = 0.7,
                            seam_glow = 0.6,
                            plate_drift = 0.35,
                            plate_compression = 0.25,
                            submelt_visibility = 0.45,
                        },
                    },
                },
            },
            wallpaper = {
                effect = "",
                effect_parameters = "{}",
                effect_duration = 0,
                source_namespace = "",
            },
        },
    },
})
```

Use `hyprimate` under `plugin` in `hl.config`; the matching `hyprctl` keys begin with `plugin:hyprimate:`. Each nested Lua table maps to colon-separated key segments.

For Lava, set `workspace.enabled = true` and `workspace.style = "lava"`. Under `workspace.options.lava`, `variant` selects the material and `window_behavior` selects whether windows melt with the desktop or burn away. The eight shared numeric controls apply to every material; only the subtable named by `variant` is active. See [Lava](docs/lava.md) for examples and capture limits.

Style settings under `window.options.<style>` are shared; set `window.<animation>.options.<style>` to override one animation. When a shared setting is unset, each animation uses its own default. Shared values are clamped to the receiving animation’s range. Example lines with differing animation defaults are commented out to preserve them.

Window opening, window closing, and workspace `duration` values are milliseconds with no upper limit. `0` skips the plugin transition and leaves Hyprland’s native animation unchanged. Leave Liquid and Swarm durations unset to retain their natural timing. Multiply old seconds values by 1000, including workspace Rotation (for example, `0.6` becomes `600`).

Paper live movement and resizing use `window.options.paper.stiffness`, `inertia` and `settle_damping`; movement behavior is selected by `window.options.paper.move.style`, with separate `move.flutter`, `move.glide` and `resize` tables. These live controls do not create per-animation overrides. Opening and closing retain their historical defaults when the new material keys are unset. For stiffness and damping the precedence is an explicitly set legacy per-animation `paper_stiffness` / `settling`, then an explicitly set legacy shared key, then an explicitly set new `stiffness` / `settle_damping`, then the historical transition default. Shared inertia has no legacy override.

Formation controls listed under `window.open.options.bubble` apply only to opening and have no global or closing counterpart. They use the same options table; shared settings such as `duration` still inherit normally.

| Setting | Type / default | Numeric range | Meaning |
|---|---|---|---|
| `window:options:construction:duration` | float / animation defaults: open `620`, close `850` | ≥ 0 | Open: Time in milliseconds for the builders to raise and fasten the window panels. Close: Time in milliseconds for the crew to clear the window and its printed facade to collapse. |
| `window:options:burn:duration` | float / animation defaults: open `1000`, close `1500` | ≥ 0 | Open: Combustion time in milliseconds for the hole to reveal the window. Close: Combustion time in milliseconds for the closing sheet. Detached material cools on its own clock. |
| `window:options:liquid:strength` | float / animation defaults: `0.65` | 0–1.5 | Movement: Liquid movement deformation strength. Zero moves windows without deformation. Open: Liquid pop-out size and deformation strength when opening. Zero removes the pop-out. Close: Liquid drain deformation strength when closing. Zero closes the window natively. |
| `window:options:liquid:viscosity` | float / animation defaults: `0.75` | 0–1 | Liquid resistance to flow during movement. Higher values make it thicker and more cohesive. |
| `window:options:liquid:elasticity` | float / animation defaults: `0.5` | 0–1 | Movement: Liquid stiffness and recovery speed during movement. Higher values snap back faster. Open: Liquid pop-out stiffness and recovery speed. Higher values snap back faster. Close: Liquid drain stiffness and recovery speed. Higher values snap back faster. |
| `window:options:liquid:momentum` | float / animation defaults: `0.5` | 0–1 | Movement: Liquid inertia after movement stops. Zero settles without overshoot. Open: Liquid pop-out inertia. Zero settles without overshoot. Close: Liquid closing dive acceleration. Higher values make the dive advance faster. |
| `window:options:liquid:variation` | float / animation defaults: `0.5` | 0–1 | Random variation between windows and movements. Zero makes liquid movement identical. |
| `window:options:cloth:strength` | float / animation defaults: `0.65` | 0–1.5 | Cloth movement deformation strength. Zero moves windows without deformation. |
| `window:options:cloth:springiness` | float / animation defaults: `0` | 0–1 | Horizontal springiness of the curtain's top edge during movement. Zero keeps it stiff; higher values allow more gathering and rebound. The lower curtain remains free-flowing. |
| `window:options:cloth:gravity` | float / animation defaults: `1` | 0–3 | Cloth movement gravity multiplier. One is normal gravity; zero disables gravity. Lower values let fabric lift more easily. Opening and closing are unaffected. |
| `window:options:cloth:flutter` | float / animation defaults: `0.5` | 0–1 | Movement: Cloth movement ripple strength. Zero removes varying gusts, but movement still bends the sheet. Open: Cloth gust strength when opening. Zero removes flutter, but gravity still moves the sheet. Close: Cloth gust strength when closing. Zero removes flutter, but gravity still moves the sheet. |
| `window:options:cloth:shading` | float / animation defaults: `0.7` | 0–1 | Movement: Light and shadow on cloth movement folds. Zero draws them unlit. Open: Light and shadow on cloth folds when opening. Zero draws them unlit. Close: Light and shadow on cloth folds when closing. Zero draws them unlit. |
| `window:options:cloth:shine` | float / animation defaults: `0` | 0–1 | Movement: Highlight strength on cloth movement folds. Zero removes highlights. Open: Highlight strength on cloth folds when opening. Zero removes highlights. Close: Highlight strength on cloth folds when closing. Zero removes highlights. |
| `window:options:cloth:variation` | float / animation defaults: `0.5` | 0–1 | Movement: Variation in cloth movement between windows. Zero gives every window the same motion. Open: Variation in cloth opening motion between windows. Zero gives every window the same motion. Close: Variation in cloth closing motion between windows. Zero gives every window the same motion. |
| `window:options:bubble:strength` | float / animation defaults: `0.65` | 0–1.5 | How much the trailing bubble film lags, ripples and overshoots during movement. Zero moves windows without deformation. |
| `window:options:bubble:microbubbles` | float / animation defaults: `0.5` | 0–1 | Rate at which small bubbles swell out of the moving film, mostly on its trailing side and during acceleration, ride its outline, and then pinch off into the trail, sink back into the film or pop. Zero forms none. |
| `window:options:bubble:trail` | float / animation defaults: `0.5` | 0–1 | Amount of soap film a fast-moving window draws out of its trailing side as thin streaks, which dry from their edges inward and break up. Zero leaves none. |
| `window:options:bubble:shape` | string / animation defaults: `"pillow"` | — | Shape surface tension pulls a moving bubble toward. Pillow draws the window in slightly and rounds its corners while keeping its proportions and straight sides; ellipse keeps the window's proportions; circle makes a disc regardless of its proportions. Invalid names are rejected. Allowed: `pillow`, `ellipse`, `circle`. |
| `window:options:bubble:roundness` | float / animation defaults: `1` | 0–1 | How far a window moving at shape_speed contracts toward its bubble shape before returning to a rectangle when stopped. Zero keeps it rectangular. |
| `window:options:bubble:shape_speed` | float / animation defaults: `1200` | 100–6000 | Window speed in logical pixels per second at which bubble shape contraction reaches its full amount. Slower windows contract proportionally less. |
| `window:options:bubble:transparency` | float / animation defaults: `0.88` | 0–1 | Window transparency while in bubble form. Zero keeps the window imagery opaque; one shows only the refracted desktop through the film. Highlights and iridescence remain visible. The normal window is unchanged when movement settles. |
| `window:options:smoke:strength` | float / animation defaults: `0.85` | 0–1.5 | How readily motion unravels the window into flowing smoke. Zero disables movement transformation without affecting opening or closing. |
| `window:options:smoke:turbulence` | float / animation defaults: `0.65` | 0–1 | Movement: Strength of evolving smoke folds and smaller curling structures. Shared with opening and closing unless overridden. Open: Strength of rolling folds and curling filaments during smoke materialization. Close: Strength of rolling folds and curling filaments during smoke disintegration. |
| `window:options:smoke:trail` | float / animation defaults: `0.65` | 0–1 | Amount of content-coloured smoke shed into the wake. Zero stops new wake emission; existing wisps still dissipate over their remaining lifetime. |
| `window:options:liquid:duration` | float / unset (natural timing) | ≥ 0 | Open: Time in milliseconds for the liquid pop-out. Close: Time in milliseconds for the window to drain to the screen edge. |
| `window:options:liquid:color` | color / animation defaults: `0xff000000` | — | Open: Colour of the liquid at the opening edge, in ARGB. Zero alpha draws no liquid. Close: Colour of the liquid at the closing edge, in ARGB. Zero alpha draws no liquid. |
| `window:options:liquid:default_animation_edge` | string / animation defaults: `"bottom"` | — | Open: Preferred opening edge for the first window on a workspace. Later windows use their placement. Invalid edges fall back to a valid edge. Close: Preferred closing edge for the last window on a workspace when side is auto. Other windows use their placement. Invalid edges fall back to a valid edge. Allowed: `bottom`, `top`, `left`, `right`. |
| `window:options:liquid:edge_bounds` | string / animation defaults: `"screen"` | — | Open: Choose monitor edges (screen) or edges inside reserved areas (work_area) for the liquid pop-out. Close: Choose monitor edges (screen) or edges inside reserved areas (work_area) for the liquid exit. Allowed: `screen`, `work_area`. |
| `window:options:liquid:emerge_drag` | float / animation defaults: `0.5` | 0–1 | Share of liquid pop-out travel applied as drag to the movement field. Zero makes the pop rigid. |
| `window:options:paper:duration` | float / animation defaults: open `600`, close `500` | ≥ 0 | Open: Time in milliseconds for the paper ball to uncrumple into the window. Close: Time in milliseconds for the window to crumple into a paper ball and vanish. |
| `window:options:paper:crumpliness` | float / animation defaults: `0.6` | 0–1 | Strength of the distributed pressure that gathers and creases the sheet. |
| `window:options:paper:shading` | float / animation defaults: `0.7` | 0–1 | Strength of lighting and fold shadows; zero leaves the geometry unlit. |
| `window:options:paper:variation` | float / animation defaults: `0.5` | 0–1 | Variation in grip locations, loads and timing. Zero retains subtle differences between events. |
| `window:options:paper:fold_density` | float / animation defaults: `0.55` | 0–1 | Frequency of folds: low gives broad flaps, high gives more closely spaced creases. |
| `window:options:paper:paper_stiffness` | float / animation defaults: `0.72` | 0–1 | Elastic resistance to bending before the sheet takes a crease. Overrides shared stiffness when explicitly set. |
| `window:options:paper:crumple_size` | float / animation defaults: `0.18` | 0.1–0.45 | Target extent of the packed paper relative to the window material length scale. |
| `window:options:paper:crumple_origin` | string / animation defaults: `"center"` | — | Where the packed sheet converges. Cursor samples the nearest point on the capture at event time. Allowed: `center`, `random`, `cursor`, `left`, `right`, `top`, `bottom`. |
| `window:options:paper:force_direction` | string / animation defaults: `"inward"` | — | Gather around the origin, press along a preferred axis, or twist the grips. Allowed: `inward`, `directional`, `rotational`. |
| `window:options:paper:rotation_intensity` | float / animation defaults: `0.22` | 0–1 | Rotational impulse applied by the grips. |
| `window:options:paper:settling` | float / animation defaults: `0.45` | 0–1 | Higher values damp residual motion and increase friction between folded layers. Overrides shared settle_damping when explicitly set. |
| `window:options:paper:simulation_quality` | string / animation defaults: `"adaptive"` | — | Physical mesh detail. Adaptive selects detail from capture size and active workload. Allowed: `adaptive`, `low`, `medium`, `high`. |
| `window:options:cloth:duration` | float / animation defaults: open `1000`, close `900` | ≥ 0 | Open: Time in milliseconds for the gathered curtain to spread from its top edge and settle flat. Close: Time in milliseconds for the top rod to carry the curtain beyond the selected screen edge. |
| `window:options:cloth:default_animation_edge` | string / animation defaults: `"bottom"` | — | Open: Opening origin for the first cloth window on a workspace, used when the closing side is origin. Later windows use their placement. Cloth still opens in place. Close: Preferred cloth closing edge for the last window on a workspace when side is auto. Invalid edges fall back to a valid edge. Allowed: `bottom`, `top`, `left`, `right`. |
| `window:options:cloth:edge_bounds` | string / animation defaults: `"screen"` | — | Open: Choose monitor edges (screen) or edges inside reserved areas (work_area) for the cloth opening origin. Close: Choose monitor edges (screen) or edges inside reserved areas (work_area) for cloth edge selection. Cloth travels beyond the actual screen before disappearing. Allowed: `screen`, `work_area`. |
| `window:options:swarm:duration` | float / unset (natural timing) | ≥ 0 | Open: Time in milliseconds for the swarm to arrive and form the window. Close: Time in milliseconds for the swarm to break up and leave the window. |
| `window:options:swarm:lone_window_sides` | set / animation defaults: `"all"` | — | Open: Screen sides swarm creatures enter from for the first window on a workspace. Later windows use the side where they appear (see Edge choice). Close: Screen sides swarm creatures leave through for the last window on a workspace. Otherwise they use the side where the window sits (see Edge choice). Comma-separated list of: `top`, `bottom`, `left`, `right`; `all` selects every one and overrides the rest. |
| `window:options:bubble:duration` | float / animation defaults: open `1000`, close `420` | ≥ 0 | Open: Time in milliseconds for the opening: small bubbles surface and interact for about the first half, the window bubble grows out of them, then inflates into the window's shape and settles. Close: Time in milliseconds for the bubble film to thin, tear, retract into strands and droplets, and for the droplets to evaporate. |
| `window:options:smoke:duration` | float / animation defaults: open `1400`, close `1600` | ≥ 0 | Open: Time in milliseconds for smoke to gather, grow into a mass and condense into the window's full-resolution contents. Close: Time in milliseconds for the window to gather into a smoke mass and leave. Trailing wisps may decay briefly afterward. |
| `window:options:smoke:direction` | string / animation defaults: open `"inside"`, close `"bottom"` | — | Open: Where opening smoke comes from. Inside grows the window from a faint wisp at a random point within it; an edge streams the smoke in from that screen edge. An edge the window does not face falls back to the opening-edge choice of liquid. Invalid names are rejected. Close: Where closing smoke leaves. Inside collapses the window into a swirling mass at a random point within it that dissipates; an edge gathers a mass on the opposite side, rears and turns it toward that screen edge and pulls the rest of the window after it. An edge the window does not face falls back to the closing-edge choice of liquid. Invalid names are rejected. Allowed: `inside`, `top`, `bottom`, `left`, `right`. |
| `window:options:liquid:side` | string / animation defaults: `"auto"` | — | Liquid exit edge: the side of appearance (auto), nearest valid edge, opening origin, or a fixed edge. Invalid preferred edges fall back to the normal edge selection. Allowed: `auto`, `nearest`, `origin`, `top`, `bottom`, `left`, `right`. |
| `window:options:liquid:splash` | boolean / animation defaults: `true` | — | Show a small edge-liquid splash where a window closes. When false, it closes without liquid. |
| `window:options:cloth:side` | string / animation defaults: `"auto"` | — | Cloth exit edge: the side of appearance (auto), nearest valid edge, opening origin, or a fixed edge. Invalid preferred edges fall back to the normal edge selection. Allowed: `auto`, `nearest`, `origin`, `top`, `bottom`, `left`, `right`. |
| `workspace:enabled` | boolean / `false` | — | Enable workspace transition effects independently of window effects. |
| `workspace:style` | string / `"rotation"` | — | Choose the workspace transition style. Invalid names are rejected. Allowed: `rotation`, `planet_hop`, `cube`, `lava`. |
| `workspace:effect` | string / `""` | — | Path to a native GLES shader transition manifest. Empty uses workspace.style. |
| `workspace:effect_parameters` | string / `"{}"` | — | JSON object overriding declared workspace shader parameters. |
| `workspace:effect_duration` | float / `600` | 0–60000 | External workspace transition time in milliseconds. Zero uses the built-in workspace style. |
| `workspace:options:rotation:duration` | float / `600` | ≥ 0 | Time in milliseconds for the workspace rotation. |
| `workspace:options:rotation:curve` | string / `"default"` | — | Name of a Hyprland Bezier animation curve. Unknown names are rejected and the previous valid curve is retained. Curve overshoot is clipped to the wheel's rotation range. |
| `workspace:options:rotation:gap_fill_style` | string / `"still"` | — | Gap treatment: morph stretches an auxiliary incoming-wallpaper bridge; background uses gap_fill_color; wallpaper blends enlarged, optionally blurred wallpaper copies; still (default) holds the outgoing wallpaper stationary while windows rotate. In still mode a different incoming wallpaper rotates in with its windows; matching texture resources with unchanged content stay stationary. No mode changes the rigid circular workspace motion. Configuration uses underscores: gap_fill_style, gap_fill_color, gap_fill_blur. Invalid styles are rejected. Allowed: `morph`, `background`, `wallpaper`, `still`. |
| `workspace:options:rotation:gap_fill_color` | color / `0xff000000` | — | Gap colour for background mode, in ARGB; defaults to opaque black. |
| `workspace:options:rotation:gap_fill_blur` | float / `0` | 0–64 | Blur radius in physical pixels for the duplicated background imagery in wallpaper mode only; zero disables blur. Original wallpapers, windows and rotation geometry are unaffected. |
| `workspace:options:rotation:keep_overlays_static` | boolean / `false` | — | Keep layer-shell bars, overlays and their popups fixed on screen while the workspace rotates. When false, they rotate with the workspace as before. |
| `workspace:options:planet_hop:window_mode` | string / `"surface"` | — | Place windows on the planet surface, float them above it, or fade them during the hop. Allowed: `surface`, `floating`, `fade`. |
| `workspace:options:planet_hop:overlay_mode` | string / `"fade"` | — | Render overlays in the world, as a HUD, or fade them during the hop. Allowed: `world`, `hud`, `fade`. |
| `workspace:options:planet_hop:planet_layout` | string / `"focused"` | — | Show the two journey endpoints, this monitor's workspaces, every monitor's workspaces, or every monitor's workspaces in a spatial solar system, respectively. Replaces linear and dynamic; use focused for endpoint-only travel. Allowed: `focused`, `monitor`, `all`, `solar_system`. |
| `workspace:options:planet_hop:duration` | float / `800` | ≥ 0 | Total departure-to-landing time in milliseconds, including skipped workspaces. |
| `workspace:options:planet_hop:curve` | string / `"linear"` | — | Named Hyprland Bezier or spring for the complete journey. Planet Hop also accepts style-local spring definitions. |
| `workspace:options:planet_hop:live_boundary` | float / `0.22` | 0.08–0.35 | Fraction adjoining each endpoint during which live workspace imagery is refreshed. |
| `workspace:options:planet_hop:sampling_quality` | int / `2` | 0–2 | Material sampling quality: 0 low, 1 medium, 2 high. |
| `workspace:options:planet_hop:planet_size` | float / `1` | 0.1–4 | Relative planet size. |
| `workspace:options:planet_hop:planet_separation` | float / `4` | 1–20 | Separation between planets in planet-radius units. |
| `workspace:options:planet_hop:solar_vertical_spread` | float / `0.65` | 0–2 | Vertical spread of solar-system workspace positions, relative to planet spacing and the output aspect. |
| `workspace:options:planet_hop:solar_background_scale` | float / `0.75` | 0.25–1 | Radius of inactive workspace planets relative to the source and destination, before perspective projection. Applies to monitor, all and solar_system. |
| `workspace:options:planet_hop:planet_coverage` | float / `0.7` | 0.1–1 | Spread of central desktop imagery on the front hemisphere. Larger values give the centre more surface area; the desktop edges always reach the limb. |
| `workspace:options:planet_hop:indirect_brightness` | float / `0.18` | 0–1 | Indirect illumination of the planet. |
| `workspace:options:planet_hop:elevation` | float / `0.2` | 0–1 | Elevation of floating windows above the planet surface in scene units; world overlays use the same elevation. |
| `workspace:options:planet_hop:fade` | float / `0.2` | 0–1 | Fraction of the hop used for fading windows and overlays. |
| `workspace:options:planet_hop:rotation_enabled` | boolean / `true` | — | Enable planet rotation during the hop. |
| `workspace:options:planet_hop:rotation_limit` | float / `0.35` | 0–3.14 | Maximum planet rotation in radians. |
| `workspace:options:planet_hop:stars_enabled` | boolean / `true` | — | Enable the star field. |
| `workspace:options:planet_hop:star_density` | float / `0.5` | 0–1 | Density of background stars. |
| `workspace:options:planet_hop:star_parallax` | float / `1` | 0–2 | Strength of background star parallax. |
| `workspace:options:planet_hop:atmosphere_enabled` | boolean / `true` | — | Enable the planet atmosphere. |
| `workspace:options:planet_hop:atmosphere_intensity` | float / `0.3` | 0–1 | Atmospheric glow intensity. |
| `workspace:options:planet_hop:atmosphere_thickness` | float / `0.035` | 0–1 | Atmospheric thickness relative to planet radius. |
| `workspace:options:planet_hop:lighting_enabled` | boolean / `true` | — | Enable directional planet lighting. |
| `workspace:options:planet_hop:light_direction_x` | float / `-0.6` | -1–1 | Horizontal component of the light direction. |
| `workspace:options:planet_hop:light_direction_y` | float / `-0.3` | -1–1 | Vertical component of the light direction. |
| `workspace:options:planet_hop:light_direction_z` | float / `1` | -1–1 | Depth component of the light direction. |
| `workspace:options:planet_hop:night_brightness` | float / `0.45` | 0–1 | Brightness of the planet's night side. |
| `workspace:options:planet_hop:clouds_enabled` | boolean / `true` | — | Enable the cloud layer. |
| `workspace:options:planet_hop:cloud_coverage` | float / `0.45` | 0–1 | Fraction of the planet covered by clouds. |
| `workspace:options:planet_hop:cloud_opacity` | float / `0.18` | 0–1 | Cloud layer opacity. |
| `workspace:options:planet_hop:cloud_speed` | float / `0.025` | 0–1 | Cloud drift speed in radians per second. |
| `workspace:options:planet_hop:depth_enabled` | boolean / `true` | — | Enable planet surface depth. |
| `workspace:options:planet_hop:depth_strength` | float / `0.018` | 0–0.5 | Surface relief strength in planet-radius units. |
| `workspace:options:planet_hop:depth_source` | string / `""` | — | Optional PNG depth image path; empty uses procedural depth. PNG images are limited to 4096 pixels per dimension and 16 MiB encoded. |
| `workspace:options:cube:variant` | string / `"solid"` | — | Turn between full workspaces on the outer faces of a solid cube or the inner walls of a room. Allowed: `solid`, `inside_out`. |
| `workspace:options:cube:duration` | float / `600` | ≥ 0 | Time in milliseconds for the cube turn. Zero skips the transition. |
| `workspace:options:cube:curve` | string / `"default"` | — | Name of a native Hyprland Bezier curve for the cube turn. Unknown names retain the previous valid curve, or fall back to the native default. |
| `workspace:options:cube:solid:cube_tilt` | float / `0` | -20–20 | Mid-turn tilt in degrees. Positive angles tip the workspace face up; zero keeps it level. |
| `workspace:options:cube:solid:zoom` | float / `0.25` | 0–1 | Extra camera retreat at mid-turn, measured in workspace widths. |
| `workspace:options:cube:inside_out:cube_tilt` | float / `0` | -20–20 | Mid-turn tilt in degrees. Positive angles tip the workspace face up; zero keeps it level. |
| `workspace:options:cube:inside_out:zoom` | float / `0.45` | 0–1 | Fraction of the safe camera retreat inside the room at mid-turn; 1 uses the full safe distance. |
| `workspace:options:lava:variant` | string / `"crust_plates"` | — | Choose one Lava surface model. Only its matching subtable affects the transition. Allowed: `cooling_flow`, `basalt_flow`, `pahoehoe`, `crust_plates`. |
| `workspace:options:lava:window_behavior` | string / `"melt_with_workspace"` | — | Melt the complete scene as one slab, or burn away visible app-window pixels while the rest of the desktop melts. Allowed: `melt_with_workspace`, `burn_up`. |
| `workspace:options:lava:viscosity` | float / `0.72` | 0–1 | Make the flowing workspace look thicker and more cohesive. |
| `workspace:options:lava:cooling_rate` | float / `0.8` | 0–1 | Control how quickly the visible surface cools over the course of travel. |
| `workspace:options:lava:crust_amount` | float / `0.68` | 0–1 | Increase the coverage of cooled crust relative to exposed melt. |
| `workspace:options:lava:glow_intensity` | float / `0.55` | 0–1 | Brighten the hot material visible between cooler regions. |
| `workspace:options:lava:detail_preservation` | float / `0.75` | 0–1 | Keep desktop details more legible through the molten surface. |
| `workspace:options:lava:trail_smear` | float / `0.35` | 0–1 | Draw desktop detail into longer streaks behind the flow. |
| `workspace:options:lava:edge_drip` | float / `0.15` | 0–1 | Increase the sag and dripping of the molten edge. |
| `workspace:options:lava:ash_amount` | float / `0.08` | 0–1 | Add dark ash flecks to the cooling surface. |
| `workspace:options:lava:cooling_flow:leading_bulge` | float / `0.45` | 0–1 | Round out the advancing front of the cooling flow. |
| `workspace:options:lava:cooling_flow:trailing_stretch` | float / `0.4` | 0–1 | Pull the rear of the cooling flow into a longer tail. |
| `workspace:options:lava:cooling_flow:molten_turbulence` | float / `0.3` | 0–1 | Increase local warping within the molten flow. |
| `workspace:options:lava:basalt_flow:crust_thickness` | float / `0.75` | 0–1 | Thicken the dark basalt surface around fissures. |
| `workspace:options:lava:basalt_flow:fissure_width` | float / `0.28` | 0–1 | Widen cracks in the basalt crust. |
| `workspace:options:lava:basalt_flow:ember_visibility` | float / `0.12` | 0–1 | Make hot embers more visible in the basalt. |
| `workspace:options:lava:basalt_flow:mass_density` | float / `0.8` | 0–1 | Give the basalt flow a denser, heavier appearance. |
| `workspace:options:lava:pahoehoe:rope_strength` | float / `0.65` | 0–1 | Emphasize the rope-like ridges on the flowing surface. |
| `workspace:options:lava:pahoehoe:fold_scale` | float / `0.5` | 0–1 | Set the size of individual rope folds. |
| `workspace:options:lava:pahoehoe:ripple_speed` | float / `0.4` | 0–1 | Advance the surface ripples further during the same workspace travel. |
| `workspace:options:lava:pahoehoe:surface_roll` | float / `0.55` | 0–1 | Increase the rolling deformation of the folded surface. |
| `workspace:options:lava:crust_plates:plate_size` | float / `0.55` | 0–1 | Set the size of individual crust plates. |
| `workspace:options:lava:crust_plates:plate_rigidity` | float / `0.7` | 0–1 | Keep imagery on each moving plate more rigid. |
| `workspace:options:lava:crust_plates:seam_glow` | float / `0.6` | 0–1 | Brighten the hot seams between plates. |
| `workspace:options:lava:crust_plates:plate_drift` | float / `0.35` | 0–1 | Increase the relative displacement of neighboring plates. |
| `workspace:options:lava:crust_plates:plate_compression` | float / `0.25` | 0–1 | Increase how tightly the plates press together during flow. |
| `workspace:options:lava:crust_plates:submelt_visibility` | float / `0.45` | 0–1 | Reveal more of the molten layer beneath the plates. |
| `wallpaper:effect` | string / `""` | — | Path to a native GLES wallpaper manifest. Empty keeps the native background. |
| `wallpaper:effect_parameters` | string / `"{}"` | — | JSON object overriding declared wallpaper shader parameters. |
| `wallpaper:effect_duration` | int / `0` | 0–60000 | Milliseconds of wallpaper simulation before holding the final frame. Zero runs continuously. |
| `wallpaper:source_namespace` | string / `""` | — | Exact layer-shell BACKGROUND namespace allowed as a shader wallpaper source. Empty allows only the unobscured stock wallpaper. |
| `window:enabled` | boolean / `true` | — | Enable window movement, opening and closing effects. When false, none of these effects run. |
| `window:style` | string / `"liquid"` | — | Choose the built-in style for movement and for transitions without an external effect override. Allowed: `liquid`, `paper`, `cloth`, `swarm`, `bubble`, `smoke`, `burn`, `construction`. |
| `window:open:effect` | string / `""` | — | Path to a native GLES shader effect manifest for window opening. Empty uses window.style. |
| `window:close:effect` | string / `""` | — | Path to a native GLES shader effect manifest for window closing. Empty uses window.style. |
| `window:open:effect_parameters` | string / `"{}"` | — | JSON object overriding the opening effect's declared parameters. |
| `window:close:effect_parameters` | string / `"{}"` | — | JSON object overriding the closing effect's declared parameters. |
| `window:open:effect_duration` | float / `600` | 0–60000 | Opening effect duration in milliseconds. Zero uses the compositor's native opening. |
| `window:close:effect_duration` | float / `600` | 0–60000 | Closing effect duration in milliseconds. Zero uses the compositor's native closing. |
| `window:move:effect` | string / `""` | — | Path to a native GLES shader manifest for movement. Empty uses window.style. |
| `window:move:effect_parameters` | string / `"{}"` | — | JSON object overriding declared movement shader parameters. |
| `window:move:effect_duration` | float / `350` | 0–60000 | Movement settling time in milliseconds; zero disables the external effect. |
| `window:resize:effect` | string / `""` | — | Path to a native GLES shader manifest for resizing. Empty uses window.style. |
| `window:resize:effect_parameters` | string / `"{}"` | — | JSON object overriding declared resizing shader parameters. |
| `window:resize:effect_duration` | float / `350` | 0–60000 | Resize settling time in milliseconds; zero disables the external effect. |
| `window:options:construction:worker_size` | float / `1` | 0–2 | Worker size multiplier for opening, movement, resizing and closing. Workers scale with the window's shorter dimension; 1 is normal size, 0 hides the people, and 2 doubles their size. |
| `window:options:burn:variation` | float / `0.7` | 0–1 | Irregularity of the combustion front, from even propagation to strongly uneven burning. |
| `window:options:burn:fragment_density` | float / `0.55` | 0–1 | Relative emission of detached printed fragments. Zero disables them. |
| `window:options:burn:ember_density` | float / `0.65` | 0–1 | Relative emission of luminous embers. Zero disables them. |
| `window:options:burn:wind_direction` | float / `0` | 0–360 | Environmental airflow heading in degrees clockwise from screen right (90 is down, 270 is up). Does not steer the combustion front. |
| `window:options:burn:wind_strength` | float / `100` | 0–1000 | Wind speed in logical pixels per second. Light ash follows airflow more readily than paper. |
| `window:options:burn:gravity_strength` | float / `300` | 0–1800 | Downward gravitational acceleration in logical pixels per second squared. |
| `window:options:burn:scorch_lifetime` | float / `1.4` | 0–10 | Seconds a fresh movement or closing scorch cools on the wallpaper before regeneration. |
| `window:options:burn:healing_duration` | float / `1.8` | 0.1–10 | Seconds of local wallpaper recovery after the scorch lifetime. No wallpaper file is changed. |
| `window:movement:options:burn:smoke_density` | float / `0.22` | 0–1 | Faint smoke released by newly charred wallpaper. Zero leaves the damaged paper and falling fragments visible without smoke. |
| `window:close:options:burn:closing_style` | string / `"curl"` | — | Consume burns the printed sheet in place; curl also bends the brittle edges and exposed paper. Allowed: `consume`, `curl`. |
| `window:close:options:burn:debris_direction` | string / `"natural"` | — | Motion of close debris: material-dependent natural forces, falling debris, rising debris or directional wind. Fire propagation is independent. Allowed: `natural`, `fall`, `rise`, `wind`. |
| `window:close:options:burn:curl_intensity` | float / `0.8` | 0–1 | Strength of local bending at the combustion front in the curl closing style. |
| `window:options:paper:stiffness` | float / `0.75` | 0–1 | Shared resistance to bending; higher values keep the sheet more rigid. Explicit values also supply opening and closing paper_stiffness unless a legacy shared or per-animation override is set. |
| `window:options:paper:inertia` | float / `0.45` | 0–1 | Shared retention of bending momentum across paper movement, resizing and transitions. Zero minimizes residual motion; higher values prolong directional lag without stretching the sheet. |
| `window:options:paper:settle_damping` | float / `0.8` | 0–1 | Shared dissipation of residual bending motion; higher values settle more quickly. Explicit values also supply opening and closing settling unless a legacy shared or per-animation override is set. |
| `window:options:paper:move:style` | string / `"flutter"` | — | Movement behavior: flutter lifts the sheet into airflow; glide keeps it close to the desktop. Inactive movement styles do not simulate. Allowed: `flutter`, `glide`. |
| `window:options:paper:move:flutter:air_resistance` | float / `0.65` | 0–1 | Flutter-only airflow coupling to movement velocity and acceleration. Zero removes airflow-driven bending. |
| `window:options:paper:move:flutter:intensity` | float / `0.5` | 0–1 | Flutter-only strength of movement-induced bends and ripples. Zero disables these bends, not pickup lift. |
| `window:options:paper:move:flutter:frequency` | float / `1.2` | 0.1–4 | Flutter-only ripple frequency multiplier; higher values produce faster, more closely spaced transient ripples. |
| `window:options:paper:move:flutter:lift` | float / `0.3` | 0–1 | Flutter-only apparent pickup height and shadow separation. Zero keeps the body at the desktop surface. |
| `window:options:paper:move:flutter:torsion` | float / `0.4` | 0–1 | Flutter-only twisting from acceleration and direction changes. Zero disables the torsional response. |
| `window:options:paper:move:glide:friction` | float / `0.6` | 0–1 | Glide-only surface friction damping local bends; higher values flatten residual curls sooner. |
| `window:options:paper:move:glide:corner_lift` | float / `0.25` | 0–1 | Glide-only corner lifting in response to acceleration. Zero keeps the corners grounded. |
| `window:options:paper:move:glide:edge_curl` | float / `0.3` | 0–1 | Glide-only localized leading-edge curling from movement speed. Zero removes edge curling. |
| `window:options:paper:move:glide:shadow_separation` | float / `0.2` | 0–1 | Glide-only apparent contact-shadow separation under locally lifted edges and corners. Zero removes separation. |
| `window:options:paper:resize:fold_intensity` | float / `0.6` | 0–1 | Resize-only strength of localized compression buckles and folds. Zero disables compression folding. |
| `window:options:paper:resize:curl_intensity` | float / `0.45` | 0–1 | Resize-only strength of edge curls released during expansion. Zero disables expansion curling. |
| `window:options:paper:resize:crease_density` | float / `0.5` | 0–1 | Resize-only spacing of compression creases: lower values make broad folds, higher values make finer pleats. |
| `window:options:swarm:sprite` | string / `"spider"` | — | Swarm sprite name in the sprites directory, with or without .png, or an explicit PNG path. The sheet has four columns and two rows of frames; transparency defines the silhouette. |
| `window:options:swarm:count` | int / `250` | 1–400 | Number of swarm creatures per window. Each carries a piece of the window, so fewer creatures mean larger pieces. |
| `window:options:swarm:speed` | float / `1400` | 100–6000 | Swarm creature travel speed in logical pixels per second. |
| `window:options:swarm:stagger` | float / `0.6` | 0–3 | Time in seconds over which swarm creatures arrive or leave in random order when a window opens or closes. Movement releases them over half this time. Zero makes them arrive or leave together. |
| `window:options:swarm:cohesion` | float / `0.5` | 0–1 | How strongly nearby swarm creatures travel together. Zero lets each find its own way. |
| `window:options:swarm:separation` | float / `0.5` | 0–1 | How much swarm creatures avoid and bump off each other while travelling. Zero lets them pass through each other. |
| `window:options:swarm:agility` | float / `0.5` | 0–1 | Swarm acceleration and grip. Higher values hold formation during faster movement and turn more sharply; lower values break apart sooner and trail farther. |
| `window:options:swarm:settling` | float / `0.5` | 0–1 | How quickly swarm creatures regroup and merge back into the window. Lower values make them mill about longer. |
| `window:options:swarm:wander` | float / `0.5` | 0–1 | How much swarm creatures wander and hesitate while travelling. Zero makes them follow their routes steadily. |
| `window:options:swarm:variation` | float / `0.5` | 0–1 | Variation in each swarm creature's speed, grip and route curvature. Zero makes them travel straight and alike. |
| `window:options:smoke:color` | color / `0x00000000` | — | Base tint for the smoke body and wisps when moving, opening and closing, in ARGB. Opaque tint supplies 80% of the colour; the window supplies the rest. Alpha scales tint strength, not smoke opacity. Zero alpha keeps the window-derived colour. |
| `window:options:smoke:density` | float / `1` | 0.25–2 | Optical thickness of the smoke body when moving, opening and closing. Higher values build a denser, more layered interior while thin outer smoke stays translucent; lower values give a lighter, wispier body. |
| `window:options:smoke:variation` | float / `0.5` | 0–1 | Randomness of smoke between windows and animations: where smoke gathers or leaves, how it turns and swirls, and how its density and wisps vary. Zero makes every animation alike while the smoke still moves naturally. Configured directions are always respected. |
| `window:movement:enabled` | boolean / `true` | — | Deform windows while they move. This does not affect opening or closing effects. |
| `window:movement:workspace_movement` | boolean / `true` | — | Let workspace slides trigger movement effects. When false, ordinary movement, opening and closing are unaffected. |
| `window:movement:options:construction:variant` | string / `"crew_carry"` | — | Choose how Construction moves a window: carry it with workers (default), lift it with a crane, or relocate its pieces. All three use workers to resize the intact window. Native explicitly opts out of movement and resize effects. Allowed: `native`, `crew_carry`, `crane`, `piece_by_piece`. |
| `window:movement:options:smoke:drag_follow` | float / `0` | 0–1 | How closely the main smoke core follows the dragged window: 0 preserves its original lag and coast, 1 tracks the pointer exactly, and intermediate values partially attach the core. The loose tail remains unaffected. |
| `window:movement:options:smoke:trail_length` | float / `0` | 0–2400 | Maximum wake distance beyond the smoke body in logical pixels. Zero uses the automatic reach. |
| `window:movement:options:smoke:trail_lifetime` | float / `0` | 0–4000 | Lifetime of emitted wake smoke in milliseconds. Zero restores the original amount-based lifetime (550–1150 ms, 940 ms with the default trail amount). Positive overrides have a 100 ms minimum; configured values from 1 to 99 ms are clamped to 100 ms. |
| `window:movement:options:smoke:trail_thickness` | float / `1` | 0.25–2.5 | Wake width multiplier, not the solid main smoke body. One is neutral. |
| `window:movement:options:smoke:reform` | float / `0.48` | 0.15–1.5 | Time scale in seconds for moving smoke to condense back into the exact window after movement stops. |
| `window:movement:options:smoke:settle_speed` | float / `1` | 0.1–2 | Speed multiplier for the smoke body's expansion and condensation after movement or resizing stops. One preserves the reform time scale; higher values reform faster and lower values linger. Opening and closing durations are unchanged. |
| `window:movement:options:smoke:mass` | float / `0.45` | 0.15–1 | Size of the moving smoke body as a fraction of the window's width and height. One keeps roughly the window's footprint; smaller values contract a moving window into a denser mass that expands back when it stops. |
| `window:movement:options:smoke:roundness` | float / `0.7` | 0–1 | Shape of the moving smoke body, independent of its size. Zero keeps the window's rectangular proportions; higher values round its corners and even out its proportions toward a turbulent, roughly circular mass. |
| `window:open:enabled` | boolean / `true` | — | Animate windows as they open using the selected style. When false, windows open without this effect; movement is unaffected. |
| `window:open:options:paper:unfold_tension` | float / `0.62` | 0–1 | Strength of pulling forces as folded sections emerge. |
| `window:open:options:paper:crease_persistence` | float / `0.62` | 0–1 | How long released bends retain their preferred angle before flattening. |
| `window:open:options:bubble:count` | int / `140` | 0–160 | Opening-only budget for simultaneously living small bubbles, not a fixed population. Slots are reused as bubbles merge and burst. Zero disables them and the window bubble forms on its own. |
| `window:open:options:bubble:birth_rate` | float / `1` | 0.25–2 | Opening-only multiplier for how often bubbles surface. Higher values produce a busier raft within the count budget. |
| `window:open:options:bubble:size_variation` | float / `0.75` | 0–1 | Opening-only spread of surfacing bubble sizes. Coalescence still grows larger bubbles at zero. |
| `window:open:options:bubble:growth_variation` | float / `0.6` | 0–1 | Opening-only variation in air supply and in how quickly bubbles surface. Even at zero, coalescence and boundary pressure change inflation speed. |
| `window:open:options:bubble:interaction` | float / `1` | 0.25–2 | Opening-only strength of bubble contact and adhesion forces, including their push on the window bubble. |
| `window:open:options:bubble:damping` | float / `1` | 0.5–2 | Opening-only drag of the liquid on bubbles. Higher values calm the raft faster; lower values retain more inertia. |
| `window:open:options:bubble:buoyancy` | float / `1` | 0–2 | Opening-only upward drift of small bubbles. Zero removes it, not the liquid's flow or contact-driven motion. |
| `window:open:options:bubble:linger` | float / `0.5` | 0–1 | Opening-only lifetime of small bubbles before their films drain and burst. Higher values keep bubbles around the landed window longer; all burst within duration. |
| `window:close:enabled` | boolean / `true` | — | Animate windows as they close using the selected style. When false, windows close natively; movement is unaffected. |
<!-- END GENERATED CONFIGURATION -->

### Edge choice

The first window on a workspace opens from `first_window_side`. Later tiled windows normally open from the screen edge on the side where they appear; floating windows and windows without a clear neighboring window use the nearest valid edge. Set `edge_bounds` to `"work_area"` to use edges inside reserved areas instead of the screen edges.

Liquid and cloth share the existing `window.close.options.liquid.side` and `edge_bounds` controls. For closing windows, `side = "auto"` chooses the side of appearance, `"nearest"` uses the nearest valid edge, and `"origin"` uses the opening edge when available. Fixed `"top"`, `"bottom"`, `"left"` and `"right"` overrides prefer that edge; an invalid preferred edge falls back to the normal resolver. Cloth opens in place but records the same logical opening origin as liquid. Its exit choice can use the work area, and its travel always continues beyond the actual monitor boundary before the sheet disappears.

Swarm can use several edges at once; each creature picks one of them at random, and a random point along it. A window that is alone on its workspace (the first to open, the last to close) uses `lone_window_sides`: a comma-separated list such as `"top,left"`, or `"all"`, which overrides anything listed with it. Otherwise a tiled window uses the screen edges facing away from its neighbours, both when it opens and when it closes; a window wedged between neighbours uses its free edges, and floating windows use the nearest edge. Swarm always uses the monitor's own edges.

