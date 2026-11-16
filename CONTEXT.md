# hypr-bubble domain glossary

**Movement**: deformation of a window while it is dragged or moved. Independent of opening and closing.

**Transition**: a window's open or close animation. Each direction (open, close) has its own **Transition settings**: enabled, style, and the per-style options; a few settings exist for only one direction.
_Avoid_: phase (reserved for the paper crumple's internal progress).

**Style**: the one material a user picks for all window animation: liquid, paper, cloth, swarm, or bubble. Movement, open, and close all follow it; each can still be switched off on its own. Paper and bubble have no movement animation, so their windows move undeformed.
_Avoid_: mode, material (as separate per-direction choices).

**Sheet**: the window drawn as a lit, folding surface in 3D. Paper and cloth are the sheet styles; liquid is not.

**Pop-out**: a liquid open; the window emerges from a screen edge.

**Drain**: a liquid close; the window sinks into a screen edge, optionally ending in a **splash**.

**Take**: one baked paper crumple, a flat sheet crumpling into a ball. A paper transition plays a take forward (close) or backward (open).

**Bubble**: a window's soap-film surface. Opening inflates a round outer bubble that ruptures while a clear inner film emerges and flattens into the window; closing ruptures the window's film where it is.

**Splat**: the bubble's brief outward stretch on reaching the screen plane, followed by elastic settlement.

**Rupture**: holes that open at thinning (black-film) spots and grow at constant speed until their rims have consumed a membrane; never a reversed inflation.

**Rim**: the liquid thread at a hole's edge. It gathers the retracting film and sheds ligaments that pinch off into droplets.

**Aureole**: the band of film ahead of a rim that is being drawn into it; content visibly compresses there before the rim consumes it.

**Glass bead**: a droplet that reached the screen glass. It stays at its screen position and evaporates independently of the window.

**Stall**: a gap between compositor frames longer than an animation can play through faithfully. Each animation decides its own response to a stall (abort, clamp, or restart), but what counts as a stall is defined once.

**Substep**: the fixed simulation time step shared by all liquid physics, so movement and transitions advance in lockstep.

**Tile**: the piece of a swarm window one agent carries. The tiles partition the window's capture box.

**Agent**: one member of a swarm: a steered point that carries a tile. **Docked** means resting on its place in the window and riding with it; **loose** means travelling.

**Creature**: what a loose agent is drawn as: its tile seen through a sprite sheet's silhouette. The simulation knows agents, never creatures.

**Lock**: how far an agent's tile has merged into the window: 0 is a free creature, 1 the whole tile.

**Grip**: the window speed a docked agent holds on through; faster movement shakes it loose.

**Body**: the disc around a loose agent that other loose agents in transit cannot enter. Smaller than the creature drawn.

**Stagger**: the time over which a swarm's agents reach their places (opening) or let go (closing) one after another, in random order, rather than all at once.

**Rest direction**: the way an agent faces once settled; random per agent. Its tile is upright when it faces it.

**Blob**: the outline of the tiles merged so far: free corners rounded off, the notches between merged tiles filled in. It squares up into the window only when every tile is in.

Cloth fold lighting has independent shading and shine controls; shine defaults to zero. Paper lighting is unchanged.
