/// @file hello_headless/main.cpp
/// @brief Minimal MitiruEngine consumer — proves headers/link wire up correctly.
///
/// Build (standalone engine checkout):
///     cmake --preset default
///     cmake --build build --config Debug --target mitiru_hello_headless
///     ./build/examples/hello_headless/Debug/mitiru_hello_headless
///
/// Downstream template (your game's CMakeLists.txt):
///     include(FetchContent)
///     FetchContent_Declare(Mitiru
///         GIT_REPOSITORY https://github.com/mogmog-0110/MitiruEngine.git
///         GIT_TAG        main)
///     FetchContent_MakeAvailable(Mitiru)
///     add_executable(MyGame src/main.cpp)
///     target_link_libraries(MyGame PRIVATE Mitiru::mitiru)
///
/// The sample intentionally does not open a window or a device — so it runs on
/// CI without GPU. Real consumers pick a backend + window via mitiru::core::Engine.
#include <cstdio>
#include <mitiru/Mitiru.hpp>

int main() {
    std::printf("MitiruEngine hello_headless — headers and link OK.\n");
    return 0;
}
