-- Isolated nested session; no autostart, environment import or primary config.
hl.monitor({ output = "", mode = "preferred", position = "auto", scale = 1 })

hl.config({
    general = {
        gaps_in = 8,
        gaps_out = 24,
        border_size = 2,
        layout = "dwindle",
        col = {
            active_border = "rgba(33ccffee)",
            inactive_border = "rgba(595959aa)",
        },
    },
    decoration = {
        wobble = { enabled = false },
        rounding = 12,
        shadow = { enabled = true, range = 12, render_power = 3 },
        blur = { enabled = true, size = 3, passes = 2 },
    },
    animations = { enabled = true },
    -- The nested GL check uses the compositor log as presentation evidence.
    debug = { disable_logs = false },
    dwindle = { preserve_split = true },
    misc = { disable_hyprland_logo = true, force_default_wallpaper = 0 },
})

hl.curve("liquidMove", { type = "bezier", points = { {0.23, 1}, {0.32, 1} } })
hl.curve("liquidLinear", { type = "bezier", points = { {0, 0}, {1, 1} } })
hl.animation({ leaf = "windows", enabled = true, speed = 4.79, bezier = "liquidMove" })
hl.animation({ leaf = "windowsIn", enabled = true, speed = 4.1, bezier = "liquidMove", style = "popin 87%" })
hl.animation({ leaf = "windowsOut", enabled = true, speed = 1.49, bezier = "liquidLinear", style = "popin 87%" })

hl.bind("ALT + Return", hl.dsp.exec_cmd("kitty"))
hl.bind("ALT + Space", hl.dsp.window.float({ action = "toggle" }))
hl.bind("ALT + mouse:272", hl.dsp.window.drag(), { mouse = true })
hl.bind("ALT + mouse:273", hl.dsp.window.resize(), { mouse = true })
hl.bind("ALT + F1", hl.dsp.layout("togglesplit"))
hl.bind("ALT + C", hl.dsp.window.close())

-- Apps such as kitty request maximize on map; maximized windows bypass deformation like fullscreen.
hl.window_rule({ name = "suppress-maximize-events", match = { class = ".*" }, suppress_event = "maximize" })

-- Only add plugin settings AFTER the first plugin load registers its keys.
-- BEGIN GENERATED PLUGIN CONFIG
-- hl.config({
--     decoration = { wobble = { enabled = false } },
--     plugin = {
--         ["hyprimate"] = {
--             workspace = {
--                 enabled = false,
--                 style = "rotation",
--                 effect = "",
--                 effect_parameters = "{}",
--                 effect_duration = 600,
--                 options = {
--                     rotation = {
--                         duration = 600,
--                         curve = "default",
--                         gap_fill_style = "still",
--                         gap_fill_color = 0xff000000,
--                         gap_fill_blur = 0,
--                         keep_overlays_static = false,
--                     },
--                     planet_hop = {
--                         window_mode = "surface",
--                         overlay_mode = "fade",
--                         planet_layout = "focused",
--                         duration = 800,
--                         curve = "linear",
--                         live_boundary = 0.22,
--                         sampling_quality = 2,
--                         planet_size = 1,
--                         planet_separation = 4,
--                         solar_vertical_spread = 0.65,
--                         solar_background_scale = 0.75,
--                         planet_coverage = 0.7,
--                         indirect_brightness = 0.18,
--                         elevation = 0.2,
--                         fade = 0.2,
--                         rotation_enabled = true,
--                         rotation_limit = 0.35,
--                         stars_enabled = true,
--                         star_density = 0.5,
--                         star_parallax = 1,
--                         atmosphere_enabled = true,
--                         atmosphere_intensity = 0.3,
--                         atmosphere_thickness = 0.035,
--                         lighting_enabled = true,
--                         light_direction_x = -0.6,
--                         light_direction_y = -0.3,
--                         light_direction_z = 1,
--                         night_brightness = 0.45,
--                         clouds_enabled = true,
--                         cloud_coverage = 0.45,
--                         cloud_opacity = 0.18,
--                         cloud_speed = 0.025,
--                         depth_enabled = true,
--                         depth_strength = 0.018,
--                         depth_source = "",
--                     },
--                     cube = {
--                         variant = "solid",
--                         duration = 600,
--                         curve = "default",
--                         solid = {
--                             cube_tilt = 0,
--                             zoom = 0.25,
--                         },
--                         inside_out = {
--                             cube_tilt = 0,
--                             zoom = 0.45,
--                         },
--                     },
--                     lava = {
--                         variant = "crust_plates",
--                         window_behavior = "melt_with_workspace",
--                         viscosity = 0.72,
--                         cooling_rate = 0.8,
--                         crust_amount = 0.68,
--                         glow_intensity = 0.55,
--                         detail_preservation = 0.75,
--                         trail_smear = 0.35,
--                         edge_drip = 0.15,
--                         ash_amount = 0.08,
--                         cooling_flow = {
--                             leading_bulge = 0.45,
--                             trailing_stretch = 0.4,
--                             molten_turbulence = 0.3,
--                         },
--                         basalt_flow = {
--                             crust_thickness = 0.75,
--                             fissure_width = 0.28,
--                             ember_visibility = 0.12,
--                             mass_density = 0.8,
--                         },
--                         pahoehoe = {
--                             rope_strength = 0.65,
--                             fold_scale = 0.5,
--                             ripple_speed = 0.4,
--                             surface_roll = 0.55,
--                         },
--                         crust_plates = {
--                             plate_size = 0.55,
--                             plate_rigidity = 0.7,
--                             seam_glow = 0.6,
--                             plate_drift = 0.35,
--                             plate_compression = 0.25,
--                             submelt_visibility = 0.45,
--                         },
--                     },
--                 },
--             },
--             wallpaper = {
--                 effect = "",
--                 effect_parameters = "{}",
--                 effect_duration = 0,
--                 source_namespace = "",
--             },
--             window = {
--                 enabled = true,
--                 during_workspace_switch = false,
--                 style = "liquid",
--                 open = {
--                     effect = "",
--                     effect_parameters = "{}",
--                     effect_duration = 600,
--                     enabled = true,
--                     options = {
--                         paper = {
--                             unfold_tension = 0.62,
--                             crease_persistence = 0.62,
--                         },
--                         bubble = {
--                             count = 140,
--                             birth_rate = 1,
--                             size_variation = 0.75,
--                             growth_variation = 0.6,
--                             interaction = 1,
--                             damping = 1,
--                             buoyancy = 1,
--                             linger = 0.5,
--                         },
--                     },
--                 },
--                 close = {
--                     effect = "",
--                     effect_parameters = "{}",
--                     effect_duration = 600,
--                     options = {
--                         burn = {
--                             closing_style = "curl",
--                             debris_direction = "natural",
--                             curl_intensity = 0.8,
--                         },
--                     },
--                     enabled = true,
--                 },
--                 move = {
--                     effect = "",
--                     effect_parameters = "{}",
--                     effect_duration = 350,
--                 },
--                 resize = {
--                     effect = "",
--                     effect_parameters = "{}",
--                     effect_duration = 350,
--                 },
--                 options = {
--                     construction = {
--                         worker_size = 1,
--                     },
--                     burn = {
--                         variation = 0.7,
--                         fragment_density = 0.55,
--                         ember_density = 0.65,
--                         wind_direction = 0,
--                         wind_strength = 100,
--                         gravity_strength = 300,
--                         scorch_lifetime = 1.4,
--                         healing_duration = 1.8,
--                     },
--                     paper = {
--                         stiffness = 0.75,
--                         inertia = 0.45,
--                         settle_damping = 0.8,
--                         move = {
--                             style = "flutter",
--                             flutter = {
--                                 air_resistance = 0.65,
--                                 intensity = 0.5,
--                                 frequency = 1.2,
--                                 lift = 0.3,
--                                 torsion = 0.4,
--                             },
--                             glide = {
--                                 friction = 0.6,
--                                 corner_lift = 0.25,
--                                 edge_curl = 0.3,
--                                 shadow_separation = 0.2,
--                             },
--                         },
--                         resize = {
--                             fold_intensity = 0.6,
--                             curl_intensity = 0.45,
--                             crease_density = 0.5,
--                         },
--                     },
--                     swarm = {
--                         sprite = "spider",
--                         count = 250,
--                         speed = 1400,
--                         stagger = 0.6,
--                         cohesion = 0.5,
--                         separation = 0.5,
--                         agility = 0.5,
--                         settling = 0.5,
--                         wander = 0.5,
--                         variation = 0.5,
--                     },
--                     smoke = {
--                         color = 0x00000000,
--                         density = 1,
--                         variation = 0.5,
--                     },
--                 },
--                 movement = {
--                     options = {
--                         burn = {
--                             smoke_density = 0.22,
--                         },
--                         construction = {
--                             variant = "crew_carry",
--                         },
--                         smoke = {
--                             drag_follow = 0,
--                             trail_length = 0,
--                             trail_lifetime = 0,
--                             trail_thickness = 1,
--                             reform = 0.48,
--                             settle_speed = 1,
--                             mass = 0.45,
--                             roundness = 0.7,
--                         },
--                     },
--                     enabled = true,
--                     workspace_movement = true,
--                 },
--             },
--         },
--     },
-- })
-- END GENERATED PLUGIN CONFIG
