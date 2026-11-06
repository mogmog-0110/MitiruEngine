# ゲーム DLL の翻訳単位に、Game.hpp と標準ヘッダーの precompiled header を付ける。
#
# 小さいゲームでは Game.hpp の読みがコンパイル時間のほとんどを占める。PCH にすると、読みが 1 回の事前
# コンパイルに寄る。mitiru_deploy_runtime(host game) が game を受け取るので、mitiru-cli が作る既存の
# CMakeLists.txt も何も変えずにこの PCH を得る。
#
# PCH は Game.hpp の分だけで 400 MB 台になる。DLL を多数作る build (エンジンの examples) は、PCH を共有する
# 仕組みを作るまで付けない。既定は OFF (ディスクを食うため)。速くしたいゲームは configure で
# -DMITIRU_GAME_PCH=ON。

option(MITIRU_GAME_PCH "ゲーム DLL に Game.hpp と標準ヘッダーの precompiled header を付ける" OFF)

function(mitiru_game_pch target)
	if(NOT MITIRU_GAME_PCH OR NOT TARGET ${target})
		return()
	endif()
	get_target_property(_type ${target} TYPE)
	get_target_property(_imported ${target} IMPORTED)
	get_target_property(_done ${target} MITIRU_GAME_PCH_APPLIED)
	if(NOT _type STREQUAL "SHARED_LIBRARY" OR _imported OR _done)
		return()
	endif()
	set_target_properties(${target} PROPERTIES MITIRU_GAME_PCH_APPLIED ON)
	target_precompile_headers(${target} PRIVATE
		"$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_FUNCTION_LIST_DIR}/MitiruGamePch.hpp>")
endfunction()
