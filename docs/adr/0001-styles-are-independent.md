# Styles are independent of each other

Each Style (liquid, paper, cloth, swarm, bubble, smoke) lives in its own `src/window/<style>/` directory and never includes another Style's directory, not even to borrow a type or a resource. When two Styles genuinely share behaviour, it lives in `src/window/shared/` as a module named after the domain concept it implements (for example edge transitions, or the Sheet that paper and cloth both draw). A shared module never includes any Style. Orchestration (runtime, transformer, close renderer) knows Styles only through the generated Style list and the Style interface, never by name.

Workspace styles follow the same rule: each lives in `src/workspace/<style>/`, never includes another, and generic workspace orchestration (and the runtime) knows them only through the workspace style list.

We chose independence over reuse, so some duplication between Styles is accepted. Adding or reworking one Style must not reach into the others. Shared types that only one Style used (liquid's parameters travelling through every Style's operations, every renderer receiving every Style's GPU resources) made each Style's changes spread across all of them.

Each Style receives only its own grouped settings. Its runtime-owned resources cache the last
settings and a prepared representation, rebuilt only when those settings change. Live and close
operations consume that Style's resources rather than reading the runtime's configuration.

Settings adapters include `config_settings_generated.hpp`, not the host-registration header
`config_generated.hpp`; they must not depend on another header supplying Hyprland configuration types first.
