# dav1d と libwebm の target (ADR 0116)。cmake/MitiruVideoAv1.cmake が add_subdirectory する。
# ここだけの dir にするのは、下で CMAKE_C_FLAGS_DEBUG を書き換えるため (dir の外の C の target には届かない)
get_filename_component(_mitiru_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(MITIRU_DAV1D_DIR "${_mitiru_root}/external/dav1d")
set(MITIRU_LIBWEBM_DIR "${_mitiru_root}/external/libwebm")

set(_dav1d_gen "${CMAKE_CURRENT_BINARY_DIR}/dav1d_gen")
file(MAKE_DIRECTORY "${_dav1d_gen}")

# meson が config.h に書く値。ARCH_* と HAVE_ASM を 0 にして、アセンブリの入口を全部閉じる
file(CONFIGURE OUTPUT "${_dav1d_gen}/config.h" CONTENT [=[
#pragma once
#define CONFIG_8BPC 1
#define CONFIG_16BPC 0
#define HAVE_ASM 0
#define TRIM_DSP_FUNCTIONS 1
#define CONFIG_LOG 0
#define ENDIANNESS_BIG 0
#define _WIN32_WINNT 0x0601
#define UNICODE 1
#define _UNICODE 1
#define _CRT_DECLARE_NONSTDC_NAMES 1
#define HAVE_CLOCK_GETTIME 0
#define HAVE_SIGACTION 0
#define HAVE_POSIX_MEMALIGN 0
#define HAVE_MEMALIGN 0
#define HAVE_ALIGNED_ALLOC 0
#define HAVE_DLSYM 0
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 0
#define HAVE_IO_H 1
#define HAVE_PTHREAD_NP_H 0
#define HAVE_PTHREAD_GETAFFINITY_NP 0
#define HAVE_PTHREAD_SETAFFINITY_NP 0
#define HAVE_PTHREAD_SETNAME_NP 0
#define HAVE_PTHREAD_SET_NAME_NP 0
#define HAVE_C11_GENERIC 0
#define HAVE_GETAUXVAL 0
#define HAVE_ELF_AUX_INFO 0
#define ARCH_AARCH64 0
#define ARCH_ARM 0
#define ARCH_X86 0
#define ARCH_X86_64 0
#define ARCH_X86_32 0
#define ARCH_PPC64LE 0
#define ARCH_RISCV 0
#define ARCH_RV32 0
#define ARCH_RV64 0
#define ARCH_LOONGARCH 0
#define ARCH_LOONGARCH32 0
#define ARCH_LOONGARCH64 0
]=])
file(CONFIGURE OUTPUT "${_dav1d_gen}/vcs_version.h" CONTENT "#define DAV1D_VERSION \"1.5.4\"\n")

set(_dav1d_common_sources
	cdf.c cpu.c ctx.c data.c decode.c dequant_tables.c getbits.c intra_edge.c itx_1d.c lf_mask.c lib.c log.c mem.c msac.c obu.c
	pal.c picture.c qm.c ref.c refmvs.c scan.c tables.c thread_task.c warpmv.c wedge.c win32/thread.c)
set(_dav1d_tmpl_sources
	cdef_apply_tmpl.c cdef_tmpl.c fg_apply_tmpl.c filmgrain_tmpl.c ipred_prepare_tmpl.c ipred_tmpl.c itx_tmpl.c lf_apply_tmpl.c
	loopfilter_tmpl.c looprestoration_tmpl.c lr_apply_tmpl.c mc_tmpl.c recon_tmpl.c)
list(TRANSFORM _dav1d_common_sources PREPEND "${MITIRU_DAV1D_DIR}/src/")
list(TRANSFORM _dav1d_tmpl_sources PREPEND "${MITIRU_DAV1D_DIR}/src/")

# bitdepth ごとの翻訳単位は BITDEPTH を付けて別に作る (8 ビットだけ)
add_library(mitiru_dav1d_8bpc OBJECT ${_dav1d_tmpl_sources})
target_compile_definitions(mitiru_dav1d_8bpc PRIVATE BITDEPTH=8)
add_library(mitiru_dav1d STATIC ${_dav1d_common_sources} $<TARGET_OBJECTS:mitiru_dav1d_8bpc>)

# C の素の実装は Debug のままだと 720p が実時間に間に合わない。dav1d は Debug でも最適化し、assert も外す
# (/RTC1 は /O2 と同居できないので、この dir の C の Debug の既定を置き換える)
if(MSVC)
	set(CMAKE_C_FLAGS_DEBUG "/Z7 /O2 /Ob2 /DNDEBUG")
endif()

foreach(_t mitiru_dav1d_8bpc mitiru_dav1d)
	target_include_directories(${_t}
		PRIVATE "${_dav1d_gen}" "${MITIRU_DAV1D_DIR}" "${MITIRU_DAV1D_DIR}/include" "${MITIRU_DAV1D_DIR}/include/dav1d"
		        "${MITIRU_DAV1D_DIR}/include/compat/msvc")
	set_target_properties(${_t} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
	if(MSVC)
		# 外の部品の警告は出さない (meson の wd4028 / 4090 / 4996 / 5287 を含め /W0)
		target_compile_options(${_t} PRIVATE /W0 /FS /utf-8)
	endif()
endforeach()
target_include_directories(mitiru_dav1d PUBLIC "${MITIRU_DAV1D_DIR}/include")

add_library(mitiru_webm STATIC
	"${MITIRU_LIBWEBM_DIR}/mkvparser/mkvparser.cc"
	"${MITIRU_LIBWEBM_DIR}/mkvparser/mkvreader.cc")
target_include_directories(mitiru_webm PUBLIC "${MITIRU_LIBWEBM_DIR}")
target_compile_features(mitiru_webm PUBLIC cxx_std_20)
if(MSVC)
	target_compile_options(mitiru_webm PRIVATE /W0 /FS /utf-8)
endif()
set_target_properties(mitiru_webm PROPERTIES POSITION_INDEPENDENT_CODE ON)
