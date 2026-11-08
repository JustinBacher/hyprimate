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
-- hl.config({
--     decoration = { wobble = { enabled = false } },
--     plugin = { ["liquid_glass"] = { enabled = true, strength = 0.65, mode = "liquid", viscosity = 0.75,
--                                     elasticity = 0.5, momentum = 0.5, transitions = true,
--                                     first_window_side = "bottom", edge_bounds = "screen", close_side = "auto",
--                                     liquid_color = 0xff000000, emerge_drag = 0.5 } },
-- })
