# cl の /showIncludes の行頭が configure と build で食い違うと、Ninja はヘッダの依存を拾えず、
# ヘッダを直しても組み直さない。エラーにならず古いコードのまま動く物ができるので、組む前に止める。
#
# 食い違うのは、日本語だけの MSVC で configure と build のコードページ (chcp) が違うとき。cl は行頭を
# コンソールの出力コードページで書き、Ninja は configure のときに rules.ninja へ書いた行頭とバイト列で比べる。
# mitiru-cli は chcp 65001 にそろえて組むが、手で組む・別のシェルで組むとずれる (ADR 0089)。
#
# 行頭が ASCII だけ (英語の cl) なら、コードページに左右されないので何も足さない。
#
# 使い方: トップレベルの CMakeLists.txt から mitiru_guard_showincludes() を 1 度呼ぶ。
# 全ターゲットが guard の後に組まれるよう、トップレベルの最後に依存を付ける。

set(_MITIRU_DEPS_GUARD_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(mitiru_guard_showincludes)
	if(NOT MSVC OR NOT CMAKE_GENERATOR MATCHES "Ninja")
		return()
	endif()
	if(TARGET mitiru_deps_guard)
		return()
	endif()
	if(NOT CMAKE_CXX_CL_SHOWINCLUDES_PREFIX MATCHES "[^ -~]")
		return()
	endif()
	set(_dir "${CMAKE_BINARY_DIR}/CMakeFiles/mitiru_deps_guard")
	file(MAKE_DIRECTORY "${_dir}")
	file(WRITE "${_dir}/probe.h" "// cl の /showIncludes が行頭を出すための空のヘッダ\n")
	file(WRITE "${_dir}/probe.cpp" "#include \"probe.h\"\n")
	# 知らせる文に出すだけ。判定は build のときに実際の cl の出力で行う
	execute_process(COMMAND cmd /c chcp OUTPUT_VARIABLE _cp ERROR_QUIET ENCODING NONE)
	string(REGEX MATCH "[0-9]+[ \r\n]*$" _cp "${_cp}")
	string(STRIP "${_cp}" _cp)
	file(WRITE "${_dir}/configure_codepage.txt" "${_cp}")
	add_custom_target(mitiru_deps_guard ALL
		COMMAND ${CMAKE_COMMAND}
			"-DBUILD_DIR=${CMAKE_BINARY_DIR}" "-DGUARD_DIR=${_dir}" "-DCL=${CMAKE_CXX_COMPILER}"
			-P "${_MITIRU_DEPS_GUARD_DIR}/MitiruDepsGuardCheck.cmake"
		COMMENT "checking cl /showIncludes against the code page this build dir was configured with"
		VERBATIM)
	cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _mitiru_attach_deps_guard "${CMAKE_SOURCE_DIR}")
endfunction()

# ディレクトリを辿って、組むターゲット全部に guard への依存を付ける。
function(_mitiru_attach_deps_guard dir)
	get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
	foreach(_t IN LISTS _targets)
		get_target_property(_type ${_t} TYPE)
		if(_type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
			add_dependencies(${_t} mitiru_deps_guard)
		endif()
	endforeach()
	get_property(_subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
	foreach(_s IN LISTS _subs)
		_mitiru_attach_deps_guard("${_s}")
	endforeach()
endfunction()
