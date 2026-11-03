# tinyexr_impl: OpenEXR (.exr) を読む (HdrEnvironment.hpp の decodeExrImage の実装)。tinyexr と同梱の miniz。
# basisu_impl: KTX2 (Basis Universal の ETC1S / UASTC) を BC へ transcode する (Ktx2Texture.hpp の decodeKtx2 の実装)。
# 親の CMakeLists.txt から add_subdirectory する (cmake/texcompress と同じ形)。
#
# どちらも読み込みのときに 1 回走るだけだが、/Od のままだと 2k の EXR の PIZ の復号や 2k の UASTC の transcode が
# 秒単位で止まる。texcompress と同じく、このディレクトリの Debug フラグからだけ /RTC1 と /Od を外して最適化する。
if(MSVC)
	foreach(_lang CXX C)
		string(REPLACE "/RTC1" "" CMAKE_${_lang}_FLAGS_DEBUG "${CMAKE_${_lang}_FLAGS_DEBUG}")
		string(REPLACE "/Od" "" CMAKE_${_lang}_FLAGS_DEBUG "${CMAKE_${_lang}_FLAGS_DEBUG}")
		string(REPLACE "/Ob0" "" CMAKE_${_lang}_FLAGS_DEBUG "${CMAKE_${_lang}_FLAGS_DEBUG}")
	endforeach()
endif()
enable_language(C)

# ── tinyexr (v1 の単一ヘッダー) + miniz ──
set(_tinyexr_dir ${PROJECT_SOURCE_DIR}/external/tinyexr)
add_library(tinyexr_impl STATIC
	${PROJECT_SOURCE_DIR}/src/exr_image_impl.cpp
	${_tinyexr_dir}/deps/miniz/miniz.c)
target_include_directories(tinyexr_impl PUBLIC
	$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${_tinyexr_dir}>
	$<BUILD_INTERFACE:${_tinyexr_dir}/deps/miniz>)
# tinyexr が使う miniz は mz_compress / mz_uncompress / mz_compressBound だけ。zip の書庫と stdio は要らない
target_compile_definitions(tinyexr_impl PRIVATE
	MINIZ_NO_ARCHIVE_APIS MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
target_compile_features(tinyexr_impl PUBLIC cxx_std_20)
# HdrEnvironment.hpp は HdrCubemap.hpp 経由で Texture.hpp (stb_image.h) を引く
target_link_libraries(tinyexr_impl PUBLIC sgc::sgc stb_impl)

# ── basis_universal の transcoder ──
set(_basisu_dir ${PROJECT_SOURCE_DIR}/external/basis_universal)
add_library(basisu_impl STATIC
	${PROJECT_SOURCE_DIR}/src/ktx2_texture_impl.cpp
	${_basisu_dir}/transcoder/basisu_transcoder.cpp)
target_include_directories(basisu_impl PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
target_include_directories(basisu_impl PRIVATE ${_basisu_dir}/transcoder)
# 使う出力は BC7 / BC5 / BC4 (と 4 の倍数でない画像の RGBA32)。携帯向けの形式は落として、表の分だけ小さくする
target_compile_definitions(basisu_impl PRIVATE
	BASISD_SUPPORT_KTX2=1 BASISD_SUPPORT_KTX2_ZSTD=1
	BASISD_SUPPORT_PVRTC1=0 BASISD_SUPPORT_PVRTC2=0 BASISD_SUPPORT_ATC=0 BASISD_SUPPORT_FXT1=0
	BASISD_SUPPORT_ETC2_EAC_A8=0 BASISD_SUPPORT_ETC2_EAC_RG11=0)
target_compile_features(basisu_impl PUBLIC cxx_std_20)
# transcoder は "../zstd/zstd.h" (同梱の 1.5 系のヘッダー) を引く。実装はエンジンの zstd (external/zstd、1.6) に
# 任せて、同じ関数が 2 つ link されないようにする。エンジンの zstd が無いときだけ同梱の zstd.c を組む
if(TARGET mitiru_zstd)
	target_link_libraries(basisu_impl PRIVATE mitiru_zstd)
else()
	target_sources(basisu_impl PRIVATE ${_basisu_dir}/zstd/zstd.c)
endif()

foreach(_t tinyexr_impl basisu_impl)
	if(MSVC)
		target_compile_options(${_t} PRIVATE /W0 /FS /utf-8 $<$<CONFIG:Debug>:/O2>)
	elseif(NOT EMSCRIPTEN)
		target_compile_options(${_t} PRIVATE -w $<$<CONFIG:Debug>:-O2>)
	endif()
endforeach()
