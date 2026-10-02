# SDL2::SDL2 は shared の imported target であるため、mitiru を link した exe は起動時に SDL2.dll を必要とする
# (無い場合は 0xC0000135 により起動前に起動できなくなる)。PATH に頼らず、exe の隣に配置する。
# POST_BUILD は追加順に実行されるため、catch_discover_tests より先に呼べば、テストを列挙する前に配置される。
# SDL2 が見つからなかった構成では何もしない。
function(mitiru_deploy_sdl2 target)
	if(WIN32 AND TARGET SDL2::SDL2)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"$<TARGET_FILE:SDL2::SDL2>"
				"$<TARGET_FILE_DIR:${target}>/SDL2.dll"
			COMMENT "${target}: deploying SDL2.dll")
	endif()
endfunction()
