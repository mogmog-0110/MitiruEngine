# hello_headless

The smallest possible MitiruEngine consumer. Links against `Mitiru::mitiru`,
includes `<mitiru/Mitiru.hpp>`, prints one line to stdout, and exits 0. Use
this as a copy-paste link-check when you suspect a CMake or include-path
regression — if it builds and runs, the public surface is intact.

## What you'll see

- A single line on stdout: `MitiruEngine hello_headless - headers and link OK.`
- Exit code `0`.
- No window, no GPU, no audio device.

## Build and run

Configured via the top-level `examples/CMakeLists.txt`. Build target: `mitiru_hello_headless`.

```bash
cmake --build build --config Debug --target mitiru_hello_headless
./build/examples/hello_headless/Debug/mitiru_hello_headless
```

## Key APIs used

- `<mitiru/Mitiru.hpp>` umbrella header (link-check only)

## Assets

None.

## Downstream template

The header comment in `main.cpp` shows the `FetchContent` snippet that
downstream consumers should drop into their own `CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(Mitiru
    GIT_REPOSITORY https://github.com/mogmog-0110/MitiruEngine.git
    GIT_TAG        main)
FetchContent_MakeAvailable(Mitiru)
add_executable(MyGame src/main.cpp)
target_link_libraries(MyGame PRIVATE Mitiru::mitiru)
```
