# mikktspace_impl: 法線マップ用の接線を MikkTSpace で作る (render/MeshTangents.hpp の実装)。
# glTF の読み込みが TANGENT の無いメッシュに使う。親の CMakeLists.txt から add_subdirectory する。
# external/mikktspace は mmikk/MikkTSpace の 3e895b49 (2020-03-25) をそのまま置いた物 (C の 2 ファイル、zlib 型の表記)。
enable_language(C)

set(_mikk_dir ${PROJECT_SOURCE_DIR}/external/mikktspace)
add_library(mitiru_mikktspace STATIC ${_mikk_dir}/mikktspace.c)
target_include_directories(mitiru_mikktspace PUBLIC $<BUILD_INTERFACE:${_mikk_dir}>)
if(MSVC)
	target_compile_options(mitiru_mikktspace PRIVATE /W0 /FS)
else()
	target_compile_options(mitiru_mikktspace PRIVATE -w)
endif()

add_library(mikktspace_impl STATIC ${PROJECT_SOURCE_DIR}/src/mesh_tangents.cpp)
target_link_libraries(mikktspace_impl PRIVATE mitiru_mikktspace)
target_include_directories(mikktspace_impl PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
target_include_directories(mikktspace_impl PRIVATE ${PROJECT_SOURCE_DIR}/external/sgc/include)
target_compile_features(mikktspace_impl PUBLIC cxx_std_20)
if(MSVC)
	target_compile_options(mikktspace_impl PRIVATE /W4 /FS /utf-8)
endif()
