# mitiru_padcheck は、つないだパッドをエンジンと同じ経路で読んで表示するコンソールツール (SDL3)。
add_executable(mitiru_padcheck main.cpp)
target_link_libraries(mitiru_padcheck PRIVATE mitiru)
mitiru_deploy_sdl3(mitiru_padcheck)
if(MSVC)
	target_compile_options(mitiru_padcheck PRIVATE /bigobj)
endif()
