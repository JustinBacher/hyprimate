# Open question: who owns a window animation's state?

Status: pinned during the 2026-11-18 architecture review (candidate #2, "pull the live frame out of the runtime"). Needs its own grilling session before #2 can be designed in detail.

## The question

Each window's animation has a per-style state (`SStyleState`, a variant of `SLiquidStyle`, `SPaperStyle`, … `SSmokeStyle`). Where should that state live for its whole life, from the opening, through movement, to the end of its close?

## How it works today

The state has two homes, handed over when the window closes.

1. **While the window is mapped**, it lives in `CAnimationTransformer::m_style` (`src/AnimationTransformer.hpp`). That transformer is stored in Hyprland's per-window transformer list (`window->effects().transformers()`) and is created on first use by `CPluginRuntime::acquire_transformer` (`src/PluginRuntime.cpp`). Every frame looks it up through `transformer_of(window)` in `update_window`, `snapshot_of`, `record_movement` and `step_window`; there are 19 `transformer_of`/`acquire_transformer` call sites across the runtime and the style files. The transformer also holds shared pointers to every style's GPU resources (`CSolidFill`, `CPaperShade`, `CSwarmSprites`), and bubble/smoke states hold their renderer inside the state.
2. **When the window closes**, the `window.close` listener (`PluginRuntime.cpp`, around line 103) stages a close before detaching the transformer. If the transformer exists and was built for the current settings, its state is handed to `CAnimationCloseRenderer::stage(...)` with `state_active = transformer->active()`; otherwise a fresh state is built with `initial_style(...)`. Bubble and smoke renderers are re-injected into whichever state is used. The close renderer keeps it as `SPending`, matches it to Hyprland's fadeout when `CWindowFadeout::create` fires, and turns it into an `SClosing` record (`src/AnimationCloseRenderer.hpp`) that it advances and draws until the close finishes.

The transformer cannot carry the close because Hyprland destroys it with the window. That is why the second home and the move exist.

## Why this deserves more than yes/no

Decisions already settled in the review push toward a single owner:

- **Window animation as one continuous state** (review Q19). Opening, movement and close are one animation that may outlive the window. CONTEXT.md's **Drag** already says the same state runs across movement, the end of an opening and the start of a close.
- **A close always continues from the current state** (Q21). Today it continues only if the state is active at staging time.
- **One draw operation per style** (Q22), whose input is either the live window buffer or the retained snapshot.
- **The frame module owns monitor and frame rules** (Q17), and talks to Hyprland through a narrow port templated over two adapters, Hyprland and an in-memory fake (Q14).
- **Styles own their resources** (#1 Q3). The state stays plain data, without renderer pointers.

## Options

### A. The window animation module owns the state

Per-window state lives in stable storage owned by the module, keyed by its own window-animation id rather than by `PHLWINDOW`, because it outlives the window. The transformer becomes a thin rendering adapter that refers to the module's entry, and the fadeout substitution does the same after the window dies.

- Correctness: one home for the whole life, so there is no handoff, no `state_active` special case, no fresh-state fallback at close, and no re-injection of renderers.
- Efficiency: one lookup per window per frame instead of 3–4 `transformer_of` list lookups, and no move of the (for bubble, large and array-heavy; size not measured) style state at close.
- Testability: frame-sequence tests reach the state through the module, with no need for Hyprland's transformer list.
- Costs and risks to settle:
  - Storage must keep addresses stable while the transformer and fadeout adapters hold references (no reallocation of `std::vector<T>` with live pointers); what is the container?
  - The lifetime contract between the module's entry and Hyprland's transformer: the transformer can be removed by Hyprland (fullscreen, unmap, `removeInactive`) while the entry still exists, and the reverse.
  - Matching a dying animation to its fadeout (today `pending_for(window)` with a timeout) must key on the module's id.
  - What clears entries on config reload, monitor removal, scale change, workspace destruction, and plugin unload (today `clear()`, the `detach_all` destructor ordering, and "must outlive close records" for `m_cloth_physics`).
  - Threading/reentrancy: Hyprland hooks call into the renderer. Confirm everything stays on the compositor thread.

### B. The transformer keeps owning it

The port exposes get, create and remove for transformers, and close staging keeps moving the state into the close renderer.

- Smaller change and no new lifetime contract.
- Keeps two homes and the handoff, so it works against Q19/Q21/Q22, and frame tests need a fake transformer list.

### C. Hybrid

The transformer owns the state while the window is mapped; the module takes it at close. This is today's shape, with the handoff moved into the module. It is listed for completeness, since it keeps the handoff that Q19 removes.

## To settle in the follow-up session

1. A or B (or C), given the costs above.
2. If A: the storage container and the reference type the adapters hold (index plus generation, pointer into a stable container, …).
3. If A: the full lifetime table, covering who creates, who may drop, and what happens to the other side for map, unmap, fullscreen, close, destroy, reload, style change, monitor removal and unload.
4. Whether a window that reopens (same `PHLWINDOW` address reused) can ever be confused with a dying entry.
5. How the decision affects #3 (the close renderer becomes the adapter that draws dying animations) and #4 (GL pass ownership of draw resources).
