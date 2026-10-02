add_executable(mitiru_subsys_renderer main.cpp)
target_link_libraries(mitiru_subsys_renderer PRIVATE mitiru)

mitiru_deploy_dxc(mitiru_subsys_renderer)

if(MSVC)
	target_compile_options(mitiru_subsys_renderer PRIVATE /bigobj)
endif()
