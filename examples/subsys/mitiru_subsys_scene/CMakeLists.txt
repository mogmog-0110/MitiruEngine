add_executable(mitiru_subsys_scene main.cpp)
target_link_libraries(mitiru_subsys_scene PRIVATE mitiru)

if(MSVC)
	target_compile_options(mitiru_subsys_scene PRIVATE /bigobj)
endif()
