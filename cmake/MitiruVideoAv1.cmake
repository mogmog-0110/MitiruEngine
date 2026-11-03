# AV1 の映像 (WebM) を読む部品 (ADR 0116)。MITIRU_WITH_VIDEO が ON のときに CMakeLists.txt から include する。
#   mitiru_dav1d : dav1d (BSD-2-Clause) の 8 ビット用 C 実装。meson の設定を、Windows の x86_64、アセンブリ無し、8 ビットだけの形で写した
#   mitiru_webm  : libwebm の mkvparser (BSD-3-Clause)
# アセンブリ (nasm) を使わないのは、取得を許されたのが C のソースだけで、nasm の道具を足さないため。速さは docs/VIDEO.md の数字を見る。

get_filename_component(_mitiru_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(MITIRU_DAV1D_DIR "${_mitiru_root}/external/dav1d")
set(MITIRU_LIBWEBM_DIR "${_mitiru_root}/external/libwebm")

if(NOT EXISTS "${MITIRU_DAV1D_DIR}/src/lib.c" OR NOT EXISTS "${MITIRU_LIBWEBM_DIR}/mkvparser/mkvparser.cc")
	message(WARNING "AV1 の動画は無効にしました: external/dav1d か external/libwebm がありません")
	set(MITIRU_VIDEO_AV1_READY OFF)
elseif(NOT WIN32)
	# 設定の写しは Windows の x86_64 だけを確かめた。他の環境は多プラットフォームの担当が足す
	message(STATUS "AV1 の動画は Windows だけで有効 (設定の写しを確かめたのは Windows の x86_64)")
	set(MITIRU_VIDEO_AV1_READY OFF)
else()
	set(MITIRU_VIDEO_AV1_READY ON)
endif()

if(NOT MITIRU_VIDEO_AV1_READY)
	return()
endif()

enable_language(C)
# Debug の C のフラグを書き換える (下の dav1d は Debug でも最適化する) ので、別の dir に閉じ込め、他の C の target (freetype など) へ漏らさない
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/av1" "${CMAKE_CURRENT_BINARY_DIR}/av1")
