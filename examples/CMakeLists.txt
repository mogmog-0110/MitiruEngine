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
# mitiru_host MUST come before hello_game — the latter installs its DLL into
# $<TARGET_FILE_DIR:mitiru_host>/hello_game/ and references mitiru_host's
# location during configure-time generator expression resolution.
add_subdirectory(mitiru_host)
add_subdirectory(hello_game)
add_subdirectory(showcase_platformer)
add_subdirectory(breakout)
add_subdirectory(dodge)
add_subdirectory(mitiru_launcher)
add_subdirectory(mitiru_dev_companion)
add_subdirectory(mitiru_renderer)
add_subdirectory(mitiru_subsys_renderer)
add_subdirectory(mitiru_subsys_replay)
add_subdirectory(mitiru_subsys_audio)
add_subdirectory(mitiru_subsys_input)
add_subdirectory(mitiru_subsys_scene)
add_subdirectory(mitiru_audio)
add_subdirectory(mitiru_inspector)

# MYSHOOTER / my_shooter are dev-only skeletons excluded from the public
# release snapshot. Guard with EXISTS so the prod top-level build (which lacks
# these dirs) configures cleanly; the dev tree has them and still builds them.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/MYSHOOTER/CMakeLists.txt")
	add_subdirectory(MYSHOOTER)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/my_shooter/CMakeLists.txt")
	add_subdirectory(my_shooter)
endif()
