add_executable(mitiru_renderer main.cpp)
target_link_libraries(mitiru_renderer PRIVATE mitiru)

# The engine's `mitiru` target unconditionally links libcef.dll's import lib
# (see top-level CMakeLists.txt). Even though this example sets
# `cfg.enableCef = false` and never calls into CEF, Windows still needs the
# loader to find libcef.dll at startup, so we deploy the CEF binary runtime
# alongside. This is the same helper hello_game / hello_cef_overlay use.
# Making CEF link truly optional is tracked separately as an engine task.
if(COMMAND mitiru_add_cef_game)
    mitiru_add_cef_game(mitiru_renderer)
endif()
