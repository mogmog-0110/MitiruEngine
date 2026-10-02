# mitiru_navbake は、レベルのメッシュからナビメッシュ (.navmesh) を生成するコンソールツール (Recast)。
add_executable(mitiru_navbake main.cpp)
target_link_libraries(mitiru_navbake PRIVATE mitiru mitiru_nav_bake)
if(MSVC)
	target_compile_options(mitiru_navbake PRIVATE /bigobj)
endif()
