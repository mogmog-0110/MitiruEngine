# MitiruEngine consumer samples
#
# The previous sample set was retired in Round 39 pending a redesigned
# example collection. New samples will be added back here under
# `examples/<name>/CMakeLists.txt` and linked with:
#
#   target_link_libraries(<game> PRIVATE Mitiru::mitiru)
#
# Skipped automatically when mitiru target is unavailable (e.g. Emscripten
# builds selecting a different entry path).

if(NOT TARGET mitiru)
	message(STATUS "examples/ skipped: mitiru target not defined")
	return()
endif()

# v0.1.1 examples — minimum runnable samples linked against the mitiru target.
# Build all:
#     cmake --build build --config Debug
# Build one:
#     cmake --build build --config Debug --target hello_window

add_subdirectory(hello_window)
add_subdirectory(hello_input)
add_subdirectory(hello_scene)
add_subdirectory(hello_shapes)
add_subdirectory(hello_cef_overlay)
