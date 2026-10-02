# texcompress_impl: 画像を BC7 / BC5 / BC4 の DDS に圧縮する (TextureCompress.hpp の実装)。
# drawModel の import (meshopt_impl) と mitiru_texc が使用する。実行時の読み込み側では不要。
# 親の CMakeLists.txt から add_subdirectory する (cmake/zstd と同じ形)。
#
# Debug でもエンコーダだけは最適化する。初回の drawModel では数十枚の画像を圧縮するため、/Od のままだと
# 起動が数分間停止する。/RTC1 は /O2 と併用できないため、このディレクトリの Debug フラグからだけ外す。
if(MSVC)
	string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
	string(REPLACE "/Od" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
	string(REPLACE "/Ob0" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

set(_bc7enc_dir ${PROJECT_SOURCE_DIR}/external/bc7enc_rdo)
add_library(texcompress_impl STATIC ${PROJECT_SOURCE_DIR}/src/texture_compress_impl.cpp)
target_include_directories(texcompress_impl PUBLIC
	$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
target_include_directories(texcompress_impl PRIVATE ${PROJECT_SOURCE_DIR}/external/stb)
target_compile_features(texcompress_impl PUBLIC cxx_std_20)
target_link_libraries(texcompress_impl PUBLIC stb_impl)

if(EXISTS ${_bc7enc_dir}/bc7enc.cpp)
	set(_bc7enc_sources ${_bc7enc_dir}/bc7enc.cpp ${_bc7enc_dir}/rgbcx.cpp ${_bc7enc_dir}/bc7decomp.cpp)
	target_sources(texcompress_impl PRIVATE ${_bc7enc_sources})
	target_include_directories(texcompress_impl PRIVATE ${_bc7enc_dir})
	target_compile_definitions(texcompress_impl PRIVATE MITIRU_HAS_BC7ENC=1)
	if(MSVC)
		set_source_files_properties(${_bc7enc_sources} PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:/O2>")
	endif()
else()
	message(STATUS "bc7enc_rdo not found - drawModel imports keep PNG/JPEG textures uncompressed")
	target_compile_definitions(texcompress_impl PRIVATE MITIRU_HAS_BC7ENC=0)
endif()

if(MSVC)
	target_compile_options(texcompress_impl PRIVATE /W0 /FS /utf-8)
elseif(NOT EMSCRIPTEN)
	target_compile_options(texcompress_impl PRIVATE -w)
endif()
