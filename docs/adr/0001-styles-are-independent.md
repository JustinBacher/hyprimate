# Styles are independent of each other

Each Style (liquid, paper, cloth, swarm, bubble, smoke) lives in its own `src/animations/<style>/` directory and never includes another Style's directory, not even to borrow a type or a resource. When two Styles genuinely share behaviour, it lives in `src/animations/shared/` as a module named after the domain concept it implements (for example edge transitions, or the Sheet that paper and cloth both draw). A shared module never includes any Style. Orchestration (runtime, transformer, close renderer) knows Styles only through the generated Style list and the Style interface, never by name.

We chose independence over reuse, so some duplication between Styles is accepted. Adding or reworking one Style must not reach into the others. Shared types that only one Style used (liquid's parameters travelling through every Style's operations, every renderer receiving every Style's GPU resources) made each Style's changes spread across all of them.
