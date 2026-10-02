add_executable(mitiru_subsys_audio main.cpp)
target_link_libraries(mitiru_subsys_audio PRIVATE mitiru)
mitiru_deploy_sdl3(mitiru_subsys_audio)

if(MSVC)
	target_compile_options(mitiru_subsys_audio PRIVATE /bigobj)
endif()
