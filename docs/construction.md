# Construction: installation study

Construction is an experimental window style. A miniature crew builds the opening, optional transport and resize follow Hyprland's geometry, and closing uses regional implosion. The plugin default remains `liquid`; Construction movement defaults to `native`.

## Art and reproduction

`src/animations/construction/art/construction.svg` is original vector artwork authored for this study. `ConstructionArt.hpp` specifies the part anchors in the same 64-texel cell coordinates. The 512×256 atlas contains 24 painted cells; its decoded RGBA texture is 524,288 bytes. Art is premultiplied at upload, not in the PNG. No fonts, external images, asset downloads or rasterizer are needed to build the plugin.

To regenerate the committed PNG (authoring only), from the repository root:

```sh
resvg src/animations/construction/art/construction.svg src/animations/construction/art/construction.png
```

Exporter: resvg 0.49.0. The normal build embeds the PNG via `tools/embed_construction_atlas.py`.

## Private review

Use matching Hyprland headers and executable. Do not load this study into a production compositor. For the opening study, build `build/construction-review` with Ninja and `BUILD_TESTING=ON`. `tests/nested/construction_evidence.py --slice --record` uses its own runtime, home, GTK clients and KWin → Hyprland output. Supply a new output directory as the positional argument and the absolute built artifact via `--plugin`.

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

## Movement, resize and closing studies

Select `window.style = "construction"`. With `window.movement.enabled = true`, choose a `window.movement.options.construction.variant`:

| Variant | Translation | Resize |
|---|---|---|
| `native` (default) | Hyprland | Hyprland |
| `crew_carry` | A crew supports the intact window | Workers stretch the intact window |
| `crane` | A crane lifts the window on two corner cables, then detaches and drives off | Same intact stretch; no crane |
| `piece_by_piece` | Workers carry regions of the live window to its changing destination; the gaps stay empty | Same intact stretch; no detached pieces |

None of these variants moves the actual client or queues pointer updates. Opening stays worker-only. `window.close.enabled` controls the implosion separately; its duration is `window.close.options.construction.duration` (seconds). A localized flash leads a staggered collapse through a connected image surface, not a replay of opening in reverse.

Hyprland chooses which output draws a window from its native rectangle, not from the extent of the animated artwork. The mapped crane therefore needs a route to an exposed display edge. Transport that would strand the artwork on an output which cannot draw it falls back to native; the plugin does not cache another display's client pixels to extend the trip.

Closing during a posed movement or opening requires a matching physical draw and a snapshot that contains the print being carried. An attached crane, unsupported scene color or blur, and print posed into view from outside the snapshot stay on Hyprland's native close. Once crane transport has finished, the ordinary regional close is eligible again.

The paired movement test uses a public synthetic client, a private pointer and a nested stock compositor. With the matching host under `build/upstream-stock-948ac1ab/` and the plugin under `build/construction-live-motion-01/obj/`, run `python3 -B tests/nested/construction_motion_evidence.py --sealed-sha256 <plugin-sha256> --take take-02` (choose an unused take number). Each scale and behavior has a native control, an animated recording, geometry traces and a decoded pixel comparison. These recordings do not validate protected-window capture.

Installed-package evidence (`0.57.0`, `0aa6745f`): `build/construction-remaining-01/take-06` contains public 1× and 1.5× paired crane, piece relocation, idle close and interrupted-opening close recordings. The terminal frames reproduce the corresponding native window or empty wallpaper. `oracle-attribution.json` explains two timing flags using the original observations; it does not replace the raw verdict. `take-07` exercises two adjacent outputs: the crane drives off the exposed outer edge, and cross-output piece transport yields to native. See `hold-interval-analysis.json` for the physical pause across the marker change. These runs do not certify capture privacy, driver allocation failure, or high-refresh frame pacing on the installed desktop.

To exercise the three remaining behaviors on the installed package ABI, build the matching plugin at `build/construction-remaining-01/obj/hyprimate.so`. The installed-ABI fixture requires hashes of `/usr/bin/Hyprland`, `/usr/bin/hyprctl`, and the package's `libaquamarine.so.0.15.1` in `build/construction-remaining-01/installed-host.sha256`, plus the timestamp-enabled test recorder at `build/construction-remaining-01/wf-recorder`. Run `python3 -B tests/nested/construction_motion_evidence.py --installed-abi --sealed-sha256 <plugin-sha256> --take take-NN` with an unused take number. It starts private KWin and Hyprland instances in a disposable namespace, without the desktop's Wayland or IPC sockets. Output includes a native control and normal-speed review videos for each effect at 1× and 1.5×.

Add `--seam` for the adjacent-output trial or `--close-alpha` for a non-unit native fade at handoff. In `take-08`, effective pixel opacity was 0.342 before close and 0.344 on the first aligned Construction frame; applying the host fade twice would have reduced it to approximately 0.117.

On stock `948ac1ab`, both variants passed at 1× and 1.5× (`build/construction-live-motion-01/take-02/`). Each used a continuous pointer grab with a pause and reversal. During the grab the client changed its test marker. At release the workers cleared, the vacated window footprint returned to wallpaper, and the settled output matched the paired native control with zero differing pixels. The resize trial grabbed the top-left corner and kept the opposite corner fixed.

Set-down and crew exit are separate: after pointer release, the carry window's interior (22 logical pixels inset, compared in the lossless recording) matched native continuously from 178 ms at 1× and 173 ms at 1.5×. The last worker left about 0.59 s after release. The extra time is visible crew departure, not a delay to window input.

| Model | What the model publishes |
|---|---|
| `IntactTransport` | One posed window and either its carrying crew or the crane with two corner cables. |
| `PieceRelocation` | Source regions of the current window, their destinations, and the workers carrying them. Vacated space contains no placeholder or moving aid. |
| `EdgeStretch` | The authoritative resize rectangle, with workers pushing or pulling its edges. No subdivision. |
| `RegionalClose` | A connected deformation of the faces visible when closing began. A local cue starts a staggered regional collapse. Draw authorization and crop restrict the source geometry each time it is requested. |

The models keep no client images or GL resources. Movement borrows only the current transform input. Closing hands off the source regions and poses currently visible, not a saved opening image; an idle window contributes one intact face. The renderer borrows the host's retained close image under the existing fadeout identity and draw guards. If that image cannot be acquired, or protection or lock state forbids acquisition, the effect does not take ownership of the native fadeout.
