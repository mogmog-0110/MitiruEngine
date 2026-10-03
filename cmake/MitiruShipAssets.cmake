# 出荷するゲームの host の隣に置く engine の同梱物と、第三者ライセンスの表記。mitiru_deploy_runtime から呼ぶ。
# 表記は exe の隣の DLL も見るので、DLL を置き終えた後 (mitiru_deploy_runtime の最後) に作る。
#
#   assets/glyphs/   RML の <img src="glyph:jump"/> が引くボタンの絵柄 (tools/gen_input_glyphs.py、CC0)
#   assets/ui/*.rml  host の確認画面 (クラッシュ報告を送るか聞く画面など)
#   assets/ui/*.json engine の UI の訳の表 (mitiru_strings.json)
#   THIRD_PARTY_NOTICES.txt  そのビルドが link した物と exe の隣に置いた物だけの表記
#
# 表記は link した target の compile definitions (MITIRU_HAS_JOLT=1 など) と、exe の隣の DLL から
# tools/release/gen_third_party_notices.py が作る。ゲームの DLL も渡せば、その DLL が link した物も載る。

# mitiru-cli の host はエンジンを add_subdirectory で取り込むので、ここで set した変数は見えない。
# パスは関数を定義したこのファイルの場所から引く。

function(mitiru_deploy_notices target)
	set(_mitiru_ship_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
	if(NOT Python3_EXECUTABLE)
		find_package(Python3 COMPONENTS Interpreter QUIET)
	endif()
	set(_script "${_mitiru_ship_root}/tools/release/gen_third_party_notices.py")
	if(NOT Python3_EXECUTABLE OR NOT EXISTS "${_script}")
		message(WARNING "${target}: Python か ${_script} が無いので THIRD_PARTY_NOTICES.txt を作れない (mitiru dist は表記が無いと止まる)")
		return()
	endif()
	set(_defs "")
	# ゲームの DLL は host より後で作られることがあるので、有無は generate の時に見る
	foreach(_t IN ITEMS ${target} ${ARGN})
		string(APPEND _defs "$<$<TARGET_EXISTS:${_t}>:$<TARGET_PROPERTY:${_t},COMPILE_DEFINITIONS>>;")
	endforeach()
	set(_features "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${target}_link_features.txt")
	file(GENERATE OUTPUT "${_features}" CONTENT "${_defs}")
	add_custom_command(TARGET ${target} POST_BUILD
		COMMAND "${Python3_EXECUTABLE}" "${_script}"
			--features "${_features}" --deploy-dir "$<TARGET_FILE_DIR:${target}>"
			--out "$<TARGET_FILE_DIR:${target}>/THIRD_PARTY_NOTICES.txt"
		COMMENT "${target}: writing THIRD_PARTY_NOTICES.txt for the linked libraries")
endfunction()

function(mitiru_deploy_ship_assets target)
	set(_mitiru_ship_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
	file(GLOB _glyphs CONFIGURE_DEPENDS "${_mitiru_ship_root}/assets/glyphs/*.png" "${_mitiru_ship_root}/assets/glyphs/LICENSE.txt")
	file(GLOB _ui CONFIGURE_DEPENDS "${_mitiru_ship_root}/assets/ui/*.rml" "${_mitiru_ship_root}/assets/ui/*.json")
	set(_dir "$<TARGET_FILE_DIR:${target}>/assets")
	if(_glyphs)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E make_directory "${_dir}/glyphs"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_glyphs} "${_dir}/glyphs"
			COMMENT "${target}: deploying input glyphs")
	endif()
	if(_ui)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E make_directory "${_dir}/ui"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_ui} "${_dir}/ui"
			COMMENT "${target}: deploying engine UI documents and strings")
	endif()
endfunction()
