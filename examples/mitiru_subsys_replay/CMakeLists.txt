# mitiru_subsys_replay — axis 4 (deterministic + replay) P4 deliverable.
# 1 binary, 2 modes:
#   --record <path>  : run dummy game for 300 frames + record InputSnapshot
#   --replay <path>  : feed recorded InputSnapshot back through same game
# Visual output must be identical between record and replay at the same
# frame index — that is the headline demo for axis 4.

add_executable(mitiru_subsys_replay main.cpp)
target_link_libraries(mitiru_subsys_replay PRIVATE mitiru)

# Engine's `mitiru` target unconditionally links libcef.dll's import lib (see
# top-level CMakeLists.txt). This subsystem demo disables CEF via
# cfg.enableCef=false and never calls into it, but the Windows loader still
# needs libcef.dll discoverable at startup, so deploy the CEF runtime
# alongside the exe — same as mitiru_subsys_renderer.
if(COMMAND mitiru_add_cef_game)
    mitiru_add_cef_game(mitiru_subsys_replay)
endif()

if(MSVC)
	target_compile_options(mitiru_subsys_replay PRIVATE /bigobj)
endif()
