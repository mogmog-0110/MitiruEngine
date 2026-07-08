# MitiruEngine apps — 製品・インフラ (host / launcher / tool 窓)。
# examples/ はゲームサンプル、apps/ は配布物と開発基盤。target 名は従来どおり。

if(NOT TARGET mitiru)
	message(STATUS "apps/ skipped: mitiru target not defined")
	return()
endif()

# mitiru_host が最初 (examples/ の各 DLL が $<TARGET_FILE_DIR:mitiru_host>/ 配下へ
# deploy するため、root が apps/ → examples/ の順に add_subdirectory する)。
add_subdirectory(mitiru_host)
# 配布用 GUI ランチャ stub (mitiru dist が使う、コンソール窓なし)。
add_subdirectory(mitiru_start)
add_subdirectory(mitiru_launcher)
add_subdirectory(mitiru_dev_companion)
# ツール窓は汎用 CEF ホスト mitiru_tool_cef --page <name> に統一 (ADR 0014)。
add_subdirectory(mitiru_tool_cef)
