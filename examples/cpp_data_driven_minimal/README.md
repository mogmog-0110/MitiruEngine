# cpp_data_driven_minimal

A headless example that exercises every data-driven primitive in the engine
in one runnable file: typed JSON content loading, versioned save-data with
migrations, JS-bridge input routing, view-state push, JSON-Schema-backed
validation, and draft-07 schema import. Pure C++, no GPU, no window, no
CEF — every input is a `constexpr` string literal.

## What you'll see

The demo runs six stages and prints a labelled section per stage:

1. **ContentLoader** — parses a small `RecipeBook` JSON, prints each recipe.
2. **SaveSchema** — saves a `GameProgress` v1, migrates v1 -> v2 via a
   `MigrationChain`, prints the migrated value.
3. **BridgeInputAdapter -> InputMapper** — feeds a fake `ui.button.start`
   signal into the bridge, watches `InputMapper` produce the mapped action.
4. **BridgeViewPush** — pushes a `state="Cooking"` change into a `printf`
   stub sink.
5. **SchemaValidator** — `ContentLoader<T>::loadStringValidated` with both
   a valid blob (passes) and an invalid one (rejected with diagnostic).
6. **SchemaImporter** — converts a tiny draft-07 JSON Schema into a
   `mitiru::data::Schema` and validates against it.

Final line: `Demo complete.` Exit code `0`.

## Build and run

Configured via the top-level `examples/CMakeLists.txt`. Build target: `mitiru_cpp_data_driven_minimal`.

```bash
cmake --build build --config Debug --target mitiru_cpp_data_driven_minimal
./build/examples/cpp_data_driven_minimal/Debug/mitiru_cpp_data_driven_minimal
```

## Key APIs used

- `mitiru::data::ContentLoader<T>` — `loadString`, `loadFile`, `loadStringValidated`
- `mitiru::data::SaveSchema<T>` — versioned save with `MigrationChain`
- `mitiru::data::SchemaValidator` — JSON-Schema-style content validation
- `mitiru::data::SchemaImporter` — draft-07 JSON Schema -> `Schema`
- `mitiru::data::JsonBinding` (`NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE`)
- `mitiru::input::BridgeInputAdapter` + `mitiru::InputMapper`
- `mitiru::input::BridgeActionRouter`
- `mitiru::bridge::BridgeViewPush`

## Assets

None — every JSON payload lives inline as a `constexpr` literal.
