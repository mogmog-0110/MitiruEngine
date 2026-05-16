# MitiruEngine consumer samples
#
# These small projects demonstrate the supported link pattern:
#   target_link_libraries(<game> PRIVATE Mitiru::mitiru)
#
# Skipped automatically when mitiru target is unavailable (e.g. Emscripten builds
# selecting a different entry path).

if(NOT TARGET mitiru)
	message(STATUS "examples/ skipped: mitiru target not defined")
	return()
endif()

add_subdirectory(hello_headless)
add_subdirectory(hello_sprite)
add_subdirectory(cef_minimal)
add_subdirectory(cef_overlay)
add_subdirectory(cef_state_bridge)
add_subdirectory(cef_transition)
add_subdirectory(cpp_gameplay_minimal)
add_subdirectory(cpp_data_driven_minimal)
# Siv3D-aligned runtime samples (games / 2d / ui categories).
add_subdirectory(breakout)
add_subdirectory(shoot_em_up)
add_subdirectory(particles)
add_subdirectory(hp_bar)
add_subdirectory(pause_menu)
add_subdirectory(pulldown_menu)
