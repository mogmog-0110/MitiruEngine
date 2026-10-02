# fbx_import_impl: FBX を glb に変換する (asset/FbxImport.hpp の実装、ufbx 0.23.1)。
# drawModel の import (meshopt_impl) とスキンモデルの読み込みが使う。親の CMakeLists.txt から add_subdirectory する。
#
# ufbx は C の単一ファイル。Debug でも最適化して作る。Mixamo の数 MB の FBX を /Od で読むと
# 初回の起動が目に見えて止まるため。/RTC1 は /O2 と併用できないので、このディレクトリの C の Debug フラグからだけ外す。
enable_language(C)
if(MSVC)
	string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
	string(REPLACE "/Od" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
	string(REPLACE "/Ob0" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
endif()

set(_ufbx_dir ${PROJECT_SOURCE_DIR}/external/ufbx)
add_library(mitiru_ufbx STATIC ${_ufbx_dir}/ufbx.c)
target_include_directories(mitiru_ufbx PUBLIC $<BUILD_INTERFACE:${_ufbx_dir}>)
if(MSVC)
	target_compile_options(mitiru_ufbx PRIVATE /W0 /FS $<$<CONFIG:Debug>:/O2>)
else()
	target_compile_options(mitiru_ufbx PRIVATE -w $<$<CONFIG:Debug>:-O2>)
endif()

add_library(fbx_import_impl STATIC ${PROJECT_SOURCE_DIR}/src/fbx_import_impl.cpp)
target_link_libraries(fbx_import_impl PRIVATE mitiru_ufbx)
target_include_directories(fbx_import_impl PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
target_include_directories(fbx_import_impl PRIVATE
	${PROJECT_SOURCE_DIR}/external
	${PROJECT_SOURCE_DIR}/external/sgc/include)
target_compile_features(fbx_import_impl PUBLIC cxx_std_20)
if(MSVC)
	target_compile_options(fbx_import_impl PRIVATE /W4 /FS /utf-8)
	target_compile_definitions(fbx_import_impl PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
