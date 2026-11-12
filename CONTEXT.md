# hypr-bubble domain glossary

**Movement**: deformation of a window while it is dragged or moved. Independent of opening and closing.

**Transition**: a window's open or close animation. Each direction (open, close) has its own **Transition settings**: enabled, style, and the per-style options; a few settings exist for only one direction.
_Avoid_: phase (reserved for the paper crumple's internal progress).

**Style**: the one material a user picks for all window animation: liquid, paper, or cloth. Movement, open, and close all follow it; each can still be switched off on its own. Paper has no movement animation, so paper windows move undeformed.
_Avoid_: mode, material (as separate per-direction choices).

**Sheet**: the window drawn as a lit, folding surface in 3D. Paper and cloth are the sheet styles; liquid is not.

**Pop-out**: a liquid open; the window emerges from a screen edge.

**Drain**: a liquid close; the window sinks into a screen edge, optionally ending in a **splash**.

**Take**: one baked paper crumple, a flat sheet crumpling into a ball. A paper transition plays a take forward (close) or backward (open).

**Stall**: a gap between compositor frames longer than an animation can play through faithfully. Each animation decides its own response to a stall (abort, clamp, or restart), but what counts as a stall is defined once.

**Substep**: the fixed simulation time step shared by all liquid physics, so movement and transitions advance in lockstep.
