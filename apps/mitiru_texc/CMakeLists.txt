# mitiru_texc: 画像を BC7 / BC5 / BC4 の DDS へ圧縮するコンソールツール
add_executable(mitiru_texc main.cpp)
target_link_libraries(mitiru_texc PRIVATE texcompress_impl)
target_compile_features(mitiru_texc PRIVATE cxx_std_20)
if(MSVC)
	target_compile_options(mitiru_texc PRIVATE /utf-8)
endif()
