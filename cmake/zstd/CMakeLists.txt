# external/zstd/lib の C ソースを mitiru_zstd にまとめる (親の CMakeLists.txt から add_subdirectory)。
# zstd 同梱の CMake (build/cmake/) は snapshot の除外対象 (external/**/build/**) になるため使わない。
# asm 版は .S を必要とするため無効にし、C 実装だけにする。
#
# Debug 構成でも最適化して作る。圧縮モードの巻き戻しリングは毎フレーム zstd を実行し、最適化なしの
# zstd は 1 桁遅い (Debug が既定のこのエンジンでは、そのまま game のフレーム時間に加わる)。
# /RTC1 は /O2 と併用できないため、このディレクトリの C の Debug フラグからだけ外す。
enable_language(C)
if(MSVC)
	string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
	string(REPLACE "/Od" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
	string(REPLACE "/Ob0" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
endif()

set(_zstd_lib "${MITIRU_ZSTD_ROOT}/lib")
file(GLOB _mitiru_zstd_sources CONFIGURE_DEPENDS
	"${_zstd_lib}/common/*.c"
	"${_zstd_lib}/compress/*.c"
	"${_zstd_lib}/decompress/*.c")
add_library(mitiru_zstd STATIC ${_mitiru_zstd_sources})
target_include_directories(mitiru_zstd PUBLIC $<BUILD_INTERFACE:${_zstd_lib}>)
target_compile_definitions(mitiru_zstd PRIVATE ZSTD_DISABLE_ASM=1 ZSTD_MULTITHREAD=0)
if(MSVC)
	set_property(TARGET mitiru_zstd PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
	target_compile_options(mitiru_zstd PRIVATE $<$<CONFIG:Debug>:/O2>)
else()
	target_compile_options(mitiru_zstd PRIVATE $<$<CONFIG:Debug>:-O2>)
endif()
