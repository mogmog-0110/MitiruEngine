# cpp_gameplay_minimal

A headless mini-game that wires together every P0 C++ gameplay primitive
in one runnable file. Reads as the canonical "if you're writing gameplay
in pure C++ per the engine's hybrid-runtime stance, here's what your loop
looks like" sample. No GPU, no font, no window — the demo runs a fake
60 fps update loop, prints state transitions, and exits.

## What you'll see

A short scripted run printed to stdout:

- `TitleScene` enters, prints `Welcome to KaeruCrape demo!` after a
  `Sequence` waits 0.5 s, then `Press start...` after another 0.3 s.
- A simulated `ui.button.start` action is dispatched through
  `BridgeActionRouter`; the title scene reacts by pushing `CookingScene`.
- `CookingScene` ticks: `Cooldown` gates the first order for ~1 s, then
  `Timer` runs a 2 s bake countdown printed every frame.
- `StateMachine<CookState>` transitions `Idle -> Cooking -> Done`. Each
  transition is forwarded to a `BridgeViewPush` sink that prints
  `[ViewPush] state = ...`.
- `CookingScene` pops itself off the `SceneRouter` once `Done`.
- The demo exits 0 once the scene stack drains.

## Build and run

Configured via the top-level `examples/CMakeLists.txt`. Build target: `mitiru_cpp_gameplay_minimal`.

```bash
cmake --build build --config Debug --target mitiru_cpp_gameplay_minimal
./build/examples/cpp_gameplay_minimal/Debug/mitiru_cpp_gameplay_minimal
```

## Key APIs used

- `mitiru::scene::SceneRouter` + `mitiru::scene::IScene` — push / pop scene stack
- `mitiru::fsm::StateMachine<EnumT>` — guarded transitions, `setOnTransition`
- `mitiru::time::Timer` — single-shot countdown (`reset`, `tick`, `expired`)
- `mitiru::time::Cooldown` — gate that re-arms (`tick`, `ready`, `trigger`)
- `mitiru::time::Sequence` — fluent timeline (`wait`, `action`, chained)
- `mitiru::input::BridgeActionRouter` — dispatch named UI actions to handlers
- `mitiru::bridge::BridgeViewPush` — push state changes to a view sink

## Assets

None.
