# Construction: installation study

This worktree contains an opening experiment, not a supported four-behavior style. It does not acquire moves or fadeouts. The default remains `liquid`. Construction uses the current transformer's framebuffer for that draw only; the site stores geometry, not a window reference or client texture.

## Art and reproduction

`src/animations/construction/art/construction.svg` is original vector artwork authored for this study. `ConstructionArt.hpp` specifies the part anchors in the same 64-texel cell coordinates. The 512×256 atlas contains 24 painted cells; its decoded RGBA texture is 524,288 bytes. Art is premultiplied at upload, not in the PNG. No fonts, external images, asset downloads or rasterizer are needed to build the plugin.

To regenerate the committed PNG (authoring only), from the repository root:

```sh
resvg src/animations/construction/art/construction.svg src/animations/construction/art/construction.png
```

Exporter: resvg 0.49.0. The normal build embeds the PNG via `tools/embed_construction_atlas.py`.

## Private review

Use matching Hyprland headers and executable. Do not load this study into a production compositor. Build `build/construction-review` with Ninja and `BUILD_TESTING=ON`. `tests/nested/construction_evidence.py --slice --record` uses its own runtime, home, GTK clients and KWin → Hyprland output. Supply a new output directory as the positional argument and the absolute built artifact via `--plugin`.

The null-work-lease check uses a negative-control relink, not a compositor or plugin runtime setting:

```sh
python3 tests/nested/link_construction_fault.py build/construction-review build/construction-fault/hyprimate.so
```

Pass that second path with `--fault-plugin`. Only the Construction renderer object is relinked against the fixture. Normal-speed footage, pixel comparisons and timing must identify the clean artifact, never the negative control. A generated contact sheet or successful material test is not a visual gate verdict.

## Milestone status: held on the experimental branch

The normal-window cuts showed the client marker change on the moving panel and matched native at active identity and handoff. The two-constant authoring trial retimed the tool pickup and moved the receiving fastener without changing the rig. The trial is not evidence that an unprompted viewer can read every beat at desktop speed.

The final take is `build/construction-slice-13/`, with `verdict.md`, the exact `hyprimate.so`, clips and a measurement file. At 1.25×, 78 of 236,016 interior pixels differed by one channel value on the active identity frame; exact grid-aligned source fetches did not change the discrepancy. Take 11 records the protected native control failure and authoring history; take 12 verifies the denied lease and interruptions. Do not promote this as a supported style or use protected real data to investigate it.

The pinned host, Hyprland `0aa6745f`, can assert on incomplete GL framebuffer storage *before* `getWorkBuffer()` returns. A simulated null lease verifies the plugin's native fallback, not recovery from that host abort. This study changes neither the host nor its allocation policy. A different animation library would not resolve that boundary.
