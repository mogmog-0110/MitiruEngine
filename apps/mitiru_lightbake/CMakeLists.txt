# mitiru_lightbake は、*.lighting.json とレベルから放射照度のプローブと反射のプローブを焼くコンソールツール。
add_executable(mitiru_lightbake main.cpp)
target_link_libraries(mitiru_lightbake PRIVATE mitiru)
mitiru_deploy_sdl3(mitiru_lightbake)
if(MSVC)
	target_compile_options(mitiru_lightbake PRIVATE /bigobj)
endif()
