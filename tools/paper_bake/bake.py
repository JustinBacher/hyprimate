"""Bake one paper-crumple take with Blender's cloth solver.

Run headless:
    blender -b --factory-startup --python tools/paper_bake/bake.py -- --aspect 1.6 --seed 1 --out take.bin

A flat sheet with the plugin's grid topology (PAPER_CELLS + 1 vertices per side, same triangle split) is pulled
towards its centre from everywhere at once, the way two hands squeeze a sheet from all sides. The sheet is stiff
in-plane and soft in bending, so it cannot shrink and has to fold; self-collision lets the folds stack into a solid
ball. The pull grows exponentially: first it only crinkles the sheet in place while the outline pulls in, then it
collapses the sheet into a wad. Heavy air damping keeps the motion a squeeze rather than a flap. Random seed relief
starts folds everywhere: broad bumps for a few soft folds across the sheet, fine ones for the crinkles between them.
Without it the sheet buckles into one or two big tacos; with only the fine bumps it crinkles like foil.

Output (little-endian): magic b"PCRB", u32 version, u32 grid, u32 frames, f32 aspect, then frames * grid * grid
* 3 float32 positions in units of sqrt(W * H), vertex index j * grid + i, frame 0 flat.
"""

import argparse
import math
import random
import struct
import sys

import bpy
import numpy as np

GRID = 57  # PAPER_CELLS + 1


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--aspect", type=float, default=1.6, help="sheet width / height")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--sim-frames", type=int, default=150, help="simulated frames from flat to ball")
    parser.add_argument("--frames", type=int, default=150, help="frames written, evenly spaced over the sim")
    parser.add_argument("--pull-start", type=float, default=2e4, help="central pull at the first frame")
    parser.add_argument("--pull-end", type=float, default=8e6, help="central pull at the last frame")
    parser.add_argument("--relief", type=float, default=0.003, help="std of the broad seed relief, units of sqrt(W*H)")
    parser.add_argument("--relief-scale", type=float, default=3.0, help="size of the broad relief's bumps, in cells")
    parser.add_argument("--crinkle", type=float, default=0.001, help="std of the fine seed relief, units of sqrt(W*H)")
    parser.add_argument("--crinkle-scale", type=float, default=1.2, help="size of the fine relief's bumps, in cells")
    parser.add_argument("--air", type=float, default=15.0, help="air damping; high keeps the sheet from flapping")
    parser.add_argument("--stretch", type=float, default=10000.0, help="tension/compression stiffness")
    parser.add_argument("--bend", type=float, default=3.0, help="bending stiffness; higher rolls into tubes")
    parser.add_argument("--quality", type=int, default=40, help="cloth solver steps per frame")
    parser.add_argument("--out", required=True)
    return parser.parse_args(argv)


def seed_relief(rng, scale):
    """Smooth random height field with unit std: white noise blurred over `scale` cells."""
    white = np.array([[rng.gauss(0.0, 1.0) for _ in range(GRID)] for _ in range(GRID)])
    reach = math.ceil(3.0 * scale)
    kernel = np.exp(-0.5 * (np.arange(-reach, reach + 1) / scale) ** 2)
    kernel /= kernel.sum()
    field = np.apply_along_axis(lambda r: np.convolve(r, kernel, "same"), 0, white)
    field = np.apply_along_axis(lambda r: np.convolve(r, kernel, "same"), 1, field)
    return field / field.std()


def build_sheet(aspect, args, rng):
    w, h = math.sqrt(aspect), 1.0 / math.sqrt(aspect)
    # Broad waves decide where the big folds go. The seed relief starts the folds in between: its broad part buckles
    # the sheet into a few soft folds per side, its fine part adds the small crinkles real paper shows between them.
    phases = [(rng.uniform(0, 2 * math.pi), rng.uniform(1.5, 4.0), rng.uniform(0, 2 * math.pi)) for _ in range(4)]
    relief = args.relief * seed_relief(rng, args.relief_scale) + args.crinkle * seed_relief(rng, args.crinkle_scale)
    verts = []
    for j in range(GRID):
        for i in range(GRID):
            u, v = i / (GRID - 1), j / (GRID - 1)
            z = sum(0.004 * math.sin(f * (u * math.cos(a) + v * math.sin(a)) * 2 * math.pi + p) for a, f, p in phases)
            z += relief[j, i]
            verts.append(((u - 0.5) * w, (v - 0.5) * h, z))
    faces = []
    for j in range(GRID - 1):
        for i in range(GRID - 1):
            a, b = j * GRID + i, j * GRID + i + 1
            c, d = (j + 1) * GRID + i, (j + 1) * GRID + i + 1
            faces.append((a, c, b))  # same split as PaperCrumple's triangleCorners
            faces.append((b, c, d))
    mesh = bpy.data.meshes.new("sheet")
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new("sheet", mesh)
    bpy.context.collection.objects.link(obj)
    return obj


def add_cloth(obj, args):
    mod = obj.modifiers.new("cloth", "CLOTH")
    s = mod.settings
    s.quality = args.quality
    s.mass = 0.02
    s.air_damping = args.air
    s.bending_model = "ANGULAR"
    # Paper: very stiff in-plane, so it folds instead of shrinking; bending stiff enough for broad facets
    # rather than cloth-like fine wrinkles.
    s.tension_stiffness = args.stretch
    s.compression_stiffness = args.stretch
    s.shear_stiffness = 0.5 * args.stretch
    s.bending_stiffness = args.bend
    s.tension_damping = 5.0
    s.compression_damping = 5.0
    s.shear_damping = 5.0
    s.bending_damping = 0.05 * args.bend
    s.effector_weights.gravity = 0.0
    c = mod.collision_settings
    c.use_collision = True
    c.distance_min = 0.004
    c.collision_quality = 4
    c.use_self_collision = True
    c.self_distance_min = 0.003
    c.self_friction = 5.0
    return mod


def add_pull(args):
    """Point force at the origin, the same strength at any distance, growing exponentially over the sim."""
    bpy.ops.object.effector_add(type="FORCE", location=(0, 0, 0))
    field = bpy.context.active_object.field
    field.shape = "POINT"
    field.falloff_type = "SPHERE"
    field.falloff_power = 0.0
    ratio = args.pull_end / args.pull_start
    for f in list(range(1, args.sim_frames, 3)) + [args.sim_frames]:
        t = (f - 1) / (args.sim_frames - 1)
        field.strength = -args.pull_start * ratio ** t  # negative attracts
        field.keyframe_insert("strength", frame=f)


def main():
    args = parse_args()
    rng = random.Random(args.seed)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.frame_start = 1
    scene.frame_end = args.sim_frames
    scene.gravity = (0.0, 0.0, 0.0)

    sheet = build_sheet(args.aspect, args, rng)
    cloth = add_cloth(sheet, args)
    cloth.point_cache.frame_start = 1
    cloth.point_cache.frame_end = args.sim_frames
    add_pull(args)

    wanted = [1 + round(k * (args.sim_frames - 1) / (args.frames - 1)) for k in range(args.frames)]
    frames = {}
    for f in range(1, args.sim_frames + 1):
        scene.frame_set(f)
        if f in wanted:
            depsgraph = bpy.context.evaluated_depsgraph_get()
            mesh = sheet.evaluated_get(depsgraph).to_mesh()
            frames[f] = [tuple(v.co) for v in mesh.vertices]
            sheet.evaluated_get(depsgraph).to_mesh_clear()
            print(f"frame {f}/{args.sim_frames}", flush=True)

    with open(args.out, "wb") as out:
        out.write(b"PCRB")
        out.write(struct.pack("<IIIf", 1, GRID, args.frames, args.aspect))
        for f in wanted:
            for x, y, z in frames[f]:
                out.write(struct.pack("<fff", x, y, z))
    print(f"wrote {args.out}", flush=True)


main()
