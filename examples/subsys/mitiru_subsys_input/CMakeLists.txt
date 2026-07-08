add_executable(mitiru_subsys_input main.cpp)
target_link_libraries(mitiru_subsys_input PRIVATE mitiru)

# Engine's `mitiru` target unconditionally links libcef.dll's import lib (see
# top-level CMakeLists.txt). Even though this subsystem-only example disables
# CEF via cfg.enableCef=false and never calls into it, the Windows loader still
# needs to find libcef.dll at startup, so we deploy the CEF binary runtime
# alongside the exe. Same helper hello_game / mitiru_subsys_renderer use.
if(COMMAND mitiru_add_cef_game)
    mitiru_add_cef_game(mitiru_subsys_input)
endif()

if(MSVC)
	target_compile_options(mitiru_subsys_input PRIVATE /bigobj)
endif()
