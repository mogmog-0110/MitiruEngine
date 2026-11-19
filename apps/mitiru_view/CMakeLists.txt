# mitiru_view: glb を並べて見る DLL。古い host とのリンクを避けるため、host の隣に置き、host と同じ build で組み直す。

add_library(mitiru_view SHARED mitiru_view_dll.cpp)
target_link_libraries(mitiru_view PRIVATE mitiru)

if(MSVC)
	target_compile_options(mitiru_view PRIVATE /bigobj)
endif()

if(TARGET mitiru_host)
	set(_view_runtime_dir "$<TARGET_FILE_DIR:mitiru_host>/mitiru_view")
	set_target_properties(mitiru_view PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${_view_runtime_dir}"
		LIBRARY_OUTPUT_DIRECTORY "${_view_runtime_dir}")
	# host だけを組み直したときに DLL が古いまま残らないよう、host から依存させる。
	add_dependencies(mitiru_host mitiru_view)
endif()
