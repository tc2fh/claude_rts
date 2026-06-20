# tests/ — headless sim tests (owner: B)

Determinism & correctness tests for `sim/`, runnable without Godot:

- **State-hash oracle** — the per-N-tick canonical hash; same `seed + command log` must yield an identical hash sequence on macOS-arm64 and Windows-x64.
- **Golden replays** — record `(seed, command log)`, re-simulate, assert bit-identical state.

CI runs these automatically once `sim/CMakeLists.txt` exists (see `.github/workflows/ci.yml`).
