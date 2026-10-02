# mitiru_rollback は、ゲーム DLL 2 つを loopback 回線でロールバック対戦させ、食い違いを確かめるコンソールツール (GekkoNet)。
add_executable(mitiru_rollback main.cpp)
target_link_libraries(mitiru_rollback PRIVATE mitiru mitiru_rollback_net)
mitiru_deploy_sdl3(mitiru_rollback)
if(MSVC)
	target_compile_options(mitiru_rollback PRIVATE /bigobj)
endif()
