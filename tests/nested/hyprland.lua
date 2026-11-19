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
--                 options = {
--                     rotation = {
--                         duration = 0.6,
--                         curve = "default",
--                         gap_fill_style = "still",
--                         gap_fill_color = 0xff000000,
--                         gap_fill_blur = 0,
--                     },
--                     planet_hop = {
--                         window_mode = "surface",
--                         overlay_mode = "fade",
--                         planet_layout = "linear",
--                         skip_duration_mode = "adaptive",
--                         duration = 0.8,
--                         curve = "linear",
--                         distance_scale = 0.25,
--                         max_duration = 2.2,
--                         adaptive_boundary = 3,
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
--                 },
--             },
--             window = {
--                 enabled = true,
--                 style = "liquid",
--                 options = {
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
--                         density = 1,
--                         variation = 0.5,
--                     },
--                 },
--                 movement = {
--                     enabled = true,
--                     workspace_movement = true,
--                     options = {
--                         smoke = {
--                             reform = 0.48,
--                             settle_speed = 1,
--                             mass = 0.45,
--                             roundness = 0.7,
--                         },
--                     },
--                 },
--                 open = {
--                     enabled = true,
--                     options = {
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
--                     enabled = true,
--                 },
--             },
--         },
--     },
-- })
-- END GENERATED PLUGIN CONFIG
