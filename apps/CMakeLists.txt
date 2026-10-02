# MitiruEngine apps は製品・インフラ (host / launcher / ツール窓)。
# examples/ はゲームサンプル、apps/ は配布物と開発基盤。target 名は従来どおり。

if(NOT TARGET mitiru)
	message(STATUS "apps/ skipped: mitiru target not defined")
	return()
endif()

# mitiru_host を最初に追加する (examples/ の各 DLL が $<TARGET_FILE_DIR:mitiru_host>/ 配下へ
# deploy するため、root が apps/ → examples/ の順に add_subdirectory する)。
add_subdirectory(mitiru_host)
# 配布用 GUI ランチャの stub (mitiru dist が使う、コンソール窓なし)。
add_subdirectory(mitiru_start)
add_subdirectory(mitiru_launcher)
add_subdirectory(mitiru_dev_companion)
# 1 ファイル配布用の自己展開ランチャーと、その梱包ツール。
add_subdirectory(mitiru_selfrun)
add_subdirectory(mitiru_selfpack)
# ツール窓は汎用ホスト mitiru_tool --page <name> (RmlUi) が開く。
add_subdirectory(mitiru_tool)
# ナビメッシュを生成するコンソールツール (Recast/Detour がある時だけ)。
if(TARGET mitiru_nav_bake)
	add_subdirectory(mitiru_navbake)
endif()
# 2 つのゲーム DLL をロールバック対戦させ、食い違いを確かめるツール (-DMITIRU_WITH_GEKKONET=ON の時だけ)。
if(TARGET mitiru_rollback_net)
	add_subdirectory(mitiru_rollback)
endif()
# RmlUi 同梱の DX12 backend で RML の画面を撮る道具 (既定のビルドには入らない)。
if(TARGET mitiru_rmlui_dx12 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mitiru_rml_capture/CMakeLists.txt")
	add_subdirectory(mitiru_rml_capture)
endif()
# つないだパッドを読んで表示するツール (SDL3 がある時だけ)。
if(SDL3_FOUND)
	add_subdirectory(mitiru_padcheck)
endif()
# 画像を BC7/BC5/BC4 DDS に圧縮するツール (drawModel の import と同じ圧縮)。
add_subdirectory(mitiru_texc)
# music.json の曲を台本どおりに WAV へ書き出すツール (曲の切り替えを耳で聞かずに確かめる)。
add_subdirectory(mitiru_musicrender)
