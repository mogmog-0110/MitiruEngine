add_executable(mitiru_subsys_audio main.cpp)
target_link_libraries(mitiru_subsys_audio PRIVATE mitiru)

if(MSVC)
	target_compile_options(mitiru_subsys_audio PRIVATE /bigobj)
endif()
