# hello3d — 3D ゲームのデモ (GPU 3D facade: s.camera3D / s.drawMesh)。
#
# 実行:  mitiru_host.exe hello3d/hello3d.dll --no-cef

add_library(hello3d SHARED hello3d_dll.cpp)
target_link_libraries(hello3d PRIVATE mitiru)

if(MSVC)
	target_compile_options(hello3d PRIVATE /bigobj)
endif()

if(TARGET mitiru_host)
	set(_h3d_runtime_dir "$<TARGET_FILE_DIR:mitiru_host>/hello3d")
	set_target_properties(hello3d PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${_h3d_runtime_dir}"
		LIBRARY_OUTPUT_DIRECTORY "${_h3d_runtime_dir}")
	add_dependencies(hello3d mitiru_host)
endif()
