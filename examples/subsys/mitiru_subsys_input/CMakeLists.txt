add_executable(mitiru_subsys_input main.cpp)
target_link_libraries(mitiru_subsys_input PRIVATE mitiru)

if(MSVC)
	target_compile_options(mitiru_subsys_input PRIVATE /bigobj)
endif()
