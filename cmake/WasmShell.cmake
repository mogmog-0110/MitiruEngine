# web/shell.html の {{GAME_JS}} プレースホルダを、ビルドされた game target の
# 実出力ファイル名へ自動置換する (docs/TODO_ENGINE_2026_09_17.md U1)。
# これまでは web/shell.html:82 のコメントの通り手置換だった。

# この include() が実行される時点 (エンジンの top-level CMakeLists.txt から) の
# CMAKE_CURRENT_SOURCE_DIR で確定させる。function 内で毎回参照すると、downstream
# project から add_subdirectory() 経由で呼ばれた際に呼び出し側の source dir に
# ずれるため、ここで一度だけ捕まえておく。
set(_mitiru_wasm_shell_src "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
set(_mitiru_wasm_sw_src "${CMAKE_CURRENT_SOURCE_DIR}/web/sw.js")
set(_mitiru_wasm_shell_script "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateWasmShell.cmake")

# GenerateWasmShell.cmake は `cmake -DIN= -DOUT= -DGAME_JS= -P` の script mode で
# 動く別ファイル — TARGET_FILE_DIR 等の generator expression は build 時にしか
# 解決できないため、置換自体を POST_BUILD の子プロセスへ延期する必要がある。
# shell.html と sw.js は同じ {{GAME_JS}} プレースホルダを使う (web/sw.js:5-6) ので
# 同じ script で両方処理する。
function(mitiru_generate_wasm_shell TARGET_NAME)
	add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND}
			-DIN=${_mitiru_wasm_shell_src}
			-DOUT=$<TARGET_FILE_DIR:${TARGET_NAME}>/shell.html
			-DGAME_JS=$<TARGET_FILE_BASE_NAME:${TARGET_NAME}>.js
			-P ${_mitiru_wasm_shell_script}
		COMMAND ${CMAKE_COMMAND}
			-DIN=${_mitiru_wasm_sw_src}
			-DOUT=$<TARGET_FILE_DIR:${TARGET_NAME}>/sw.js
			-DGAME_JS=$<TARGET_FILE_BASE_NAME:${TARGET_NAME}>.js
			-P ${_mitiru_wasm_shell_script}
		COMMENT "wasm: ${TARGET_NAME} 用に {{GAME_JS}} を置換した shell.html / sw.js を生成")
endfunction()
