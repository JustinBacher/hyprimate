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

When every piece is seated and the last worker or tool has cleared, Hyprland presents the window directly for the remainder of the opening. This removes an unnecessary texture copy without ending the timeline early. Windows already marked `no_screen_share` use the ordinary Hyprland opening throughout; enabling Construction does not override that rule.

The null-work-lease check uses a negative-control relink, not a compositor or plugin runtime setting:

```sh
python3 tests/nested/link_construction_fault.py build/construction-review build/construction-fault/hyprimate.so
```

Pass that second path with `--fault-plugin`. Only the Construction renderer object is relinked against the fixture. Normal-speed footage, pixel comparisons and timing must identify the clean artifact, never the negative control. A generated contact sheet or successful material test is not a visual gate verdict.

## Milestone status: held on the experimental branch

The normal-window cuts showed the client marker change on the moving panel and matched native at active identity and handoff. The two-constant authoring trial retimed the tool pickup and moved the receiving fastener without changing the rig. The trial is not evidence that an unprompted viewer can read every beat at desktop speed.

The earlier 1.25× installation study (`build/construction-slice-13/`) measured 78 of 236,016 interior pixels off by one channel value while the transformer was drawing the seated client. With the native presentation handoff, the first seated frame, the next frame before the timeline ended, the first post-end frame and the settled result all matched the native control, including edges and background (`build/construction-handoff-20/ordinary-02/results.json`). This establishes the visible result, not that an extra offscreen copy is lossless.

Stock Hyprland `948ac1abcd398f30460a5fb6951c7d46ba16bac7` is the tested revision containing the fractional redaction-mask change. In disposable sessions with Hyprimate unloaded or Construction opening disabled, the archived v0.57.0 transient did not recur in 354 protected monitor-capture frames. With the guarded Construction opening **enabled**, the effective rule on the synthetic protected client was `true` at map and rest: all 95 frames at 1× and all 95 at 1.5× contained only the gray background and native black mask. In the matched public recordings, the rule was `false`, the workers moved identifiable live client imagery, and native presentation took over at seating. See `build/construction-guarded-capture-01/verdict.md` for the six recordings, exact plugin hash and sampling limits. Use SDK headers and dependencies matching the exact compositor revision, not just its displayed version.

This is compatibility evidence for windows protected before opening on the tested monitor-capture path. It does not qualify changing the rule on an already-rendered output, another capture API, retained closing content or the installed desktop. No host was patched. Stock Hyprland can abort when allocating framebuffer storage before the plugin can return a native fallback, including on `948ac1ab`; the null-lease test does not establish recovery from that host failure. Production integration remains gated.
