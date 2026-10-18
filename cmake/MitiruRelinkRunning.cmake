# 動いている exe を、止めずに link し直せるようにする (Windows)。
#
# 動いている exe は消すことも上書きすることもできないので、mitiru run で遊んでいる間に mitiru build すると、
# mitiru_host.exe やツール窓の exe の link が LNK1168 で止まり、ゲームの DLL まで組まれない。名前を変えることは
# できるので、link の前に今の exe を <exe>.<印>.old へ退け、同じ中身を元の名前へ写し戻す。動いている process は
# 退けたファイルから読み続け、リンカは写した方を書き換える (写し戻すので差分の link も効いたまま)。
# 退けた .old のうち、もう動いていない物は次の link の前に消す。
#
# ゲームの DLL は host が temp へ写してから読む (module/ModuleHost.hpp) ので、元の DLL はいつでも書ける。

if(CMAKE_SCRIPT_MODE_FILE)
	# cmake -DEXE=<exe> -P MitiruRelinkRunning.cmake (PRE_LINK から呼ぶ)
	if(EXISTS "${EXE}")
		string(RANDOM LENGTH 8 _tag)
		set(_moved "${EXE}.${_tag}.old")
		file(RENAME "${EXE}" "${_moved}" RESULT _rc)
		if(_rc EQUAL 0)
			file(COPY_FILE "${_moved}" "${EXE}" RESULT _rc)
			if(NOT _rc EQUAL 0)
				message(WARNING "${EXE} を写し戻せなかったので、差分でなく最初から link します (${_rc})")
			endif()
		endif()
	endif()
	file(GLOB _olds "${EXE}.*.old")
	foreach(_old IN LISTS _olds)
		execute_process(COMMAND "${CMAKE_COMMAND}" -E rm -f "${_old}" OUTPUT_QUIET ERROR_QUIET)
	endforeach()
	return()
endif()

# target が実行ファイルなら、link の前に今の exe を退ける (2 度呼んでも 1 度だけ付ける)
function(mitiru_relink_while_running target)
	if(NOT WIN32)
		return()
	endif()
	get_target_property(_type ${target} TYPE)
	get_target_property(_done ${target} MITIRU_RELINK_WHILE_RUNNING)
	if(NOT _type STREQUAL "EXECUTABLE" OR _done)
		return()
	endif()
	set_target_properties(${target} PROPERTIES MITIRU_RELINK_WHILE_RUNNING ON)
	add_custom_command(TARGET ${target} PRE_LINK
		COMMAND "${CMAKE_COMMAND}" "-DEXE=$<TARGET_FILE:${target}>" -P "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
		VERBATIM)
endfunction()

# dir で作った実行ファイル全部に mitiru_relink_while_running を付ける。エンジンを add_subdirectory で取り込んだ
# ゲームの CMakeLists は、エンジンを読んだ後で host とツール窓の exe を作るので、エンジンはこれを
# cmake_language(DEFER) でゲームの CMakeLists の終わりに呼ぶ
function(mitiru_relink_while_running_in_dir dir)
	get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
	foreach(_t IN LISTS _targets)
		mitiru_relink_while_running(${_t})
	endforeach()
	get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
	foreach(_sub IN LISTS _subdirs)
		mitiru_relink_while_running_in_dir("${_sub}")
	endforeach()
endfunction()
