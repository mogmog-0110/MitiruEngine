# script mode 専用 (cmake -DIN=<src> -DOUT=<dst> -DGAME_JS=<name>.js -P GenerateWasmShell.cmake)。
# WasmShell.cmake の mitiru_generate_wasm_shell() から POST_BUILD で呼ばれる。

if(NOT DEFINED IN OR NOT DEFINED OUT OR NOT DEFINED GAME_JS)
	message(FATAL_ERROR "GenerateWasmShell.cmake: -DIN / -DOUT / -DGAME_JS が必要")
endif()

file(READ "${IN}" _mitiru_shell_html)
string(REPLACE "{{GAME_JS}}" "${GAME_JS}" _mitiru_shell_html "${_mitiru_shell_html}")
file(WRITE "${OUT}" "${_mitiru_shell_html}")
