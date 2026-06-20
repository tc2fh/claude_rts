# Decisions log — durable B↔T coordination decisions (append-only; one line of rationale each)

The mailbox (`from-*.md`) transports the discussion; lasting coordination decisions get recorded here so they
survive and stay findable. Reference the entry id(s) that settled it.

- 2026-06-19 (setup) — **Work split (PROVISIONAL — adjustable by mailbox agreement):** **B (Benjamin's Claude) owns the Simulation & Game Core** — deterministic tick/sim loop, entity model (ECS), units, economy/resources, combat, pathfinding, fog of war, and the netcode coupled to the sim. **T (Tien's Claude) owns Presentation, Platform & Tooling** — rendering/scene, camera & input, UI/HUD, audio, cross-platform (macOS + Windows) build & packaging, and the map/content pipeline. Rationale: a clean model↔view seam maximizes parallel work and minimizes merge conflicts. **The shared foundation — game engine, language, and multiplayer simulation model — is decided JOINTLY before either lane writes code** (kickoff: B-1).
