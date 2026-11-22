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

For an opening choreography review, add `--visual-only`. This records ordinary synthetic clients and native controls, not the privacy or allocation checks. The Construction-only default is 620 ms; `--duration` changes the animation tempo without retiming the video. With the same geometry and seed, the structural layout and crew schedule replay identically. A new seed can change the number of bays, their bearings, which ready section goes up next, and who holds or fastens it. The crew reserves stations before admitting a job; upper sections wait for the sections directly beneath them. The wide and compact cuts exercise different crew limits. On a site without enough usable room to stage the crew, the native opening remains in charge.

The null-work-lease check uses a negative-control relink, not a compositor or plugin runtime setting:

```sh
python3 tests/nested/link_construction_fault.py build/construction-review build/construction-fault/hyprimate.so
```

Pass that second path with `--fault-plugin`. Only the Construction renderer object is relinked against the fixture. Normal-speed footage, pixel comparisons and timing must identify the clean artifact, never the negative control. A generated contact sheet or successful material test is not a visual gate verdict.

## Milestone status: held on the experimental branch

The normal-window cuts showed the client marker change on the moving panel and matched native at active identity and handoff. The two-constant authoring trial retimed the tool pickup and moved the receiving fastener without changing the rig. The trial is not evidence that an unprompted viewer can read every beat at desktop speed.

The identity investigation is recorded in `build/construction-slice-13/`, with `verdict.md`, the exact `hyprimate.so`, clips and a measurement file. At 1.25×, 78 of 236,016 interior pixels differed by one channel value on the active identity frame; exact grid-aligned source fetches did not change the discrepancy. Take 11 records the protected native control failure and authoring history; take 12 verifies the denied lease and interruptions. Do not promote this as a supported style or use protected real data to investigate it.

Stock Hyprland `948ac1abcd398f30460a5fb6951c7d46ba16bac7` is the tested revision containing the fractional redaction-mask change. In disposable sessions its upstream geometry regression passed, and actual monitor recordings of the protected synthetic opening showed no client marker at 1× or 1.5×, with Hyprimate unloaded or loaded with Construction opening disabled. All paired public controls showed the same markers. The earlier v0.57.0 transient was not observed on this candidate. This qualifies the tested path on that build, not other capture paths or an installed desktop. Use headers and dependencies matching the exact compositor revision, not just its displayed version.

The pinned host, Hyprland `0aa6745f`, can assert on incomplete GL framebuffer storage *before* `getWorkBuffer()` returns. A simulated null lease verifies the plugin's native fallback, not recovery from that host abort. This study changes neither the host nor its allocation policy. A different animation library would not resolve that boundary.
