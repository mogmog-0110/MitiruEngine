# mitiru_deps_guard が build のたびに走らせる検査 (cmake -P)。
# 入力: BUILD_DIR (Ninja の build dir)、GUARD_DIR (configure が置いた probe と記録)、CL (cl.exe)
#
# Ninja と同じ比べ方をする。rules.ninja の msvc_deps_prefix のバイト列が、今の端末で cl が出す
# /showIncludes の行頭と一致するかを見る。CMake が文字列として読み直した値で比べると、
# コードページが違っても一致して見えて、食い違いを見逃す。

file(GLOB _rules "${BUILD_DIR}/CMakeFiles/rules*.ninja")
set(_prefix "")
foreach(_f IN LISTS _rules)
	file(READ "${_f}" _text)
	if(_text MATCHES "msvc_deps_prefix = ([^\r\n]*)")
		set(_prefix "${CMAKE_MATCH_1}")
		break()
	endif()
endforeach()
if(_prefix STREQUAL "" OR NOT _prefix MATCHES "[^ -~]")
	return()
endif()

execute_process(
	COMMAND "${CL}" /nologo /showIncludes /c "${GUARD_DIR}/probe.cpp" "/Fo${GUARD_DIR}/probe.obj"
	OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc ENCODING NONE)
if(NOT _rc EQUAL 0)
	return() # 確かめられないことでビルド全体を落とさない
endif()
string(FIND "${_out}" "${_prefix}" _pos)
if(_pos GREATER_EQUAL 0)
	return()
endif()

set(_then "")
if(EXISTS "${GUARD_DIR}/configure_codepage.txt")
	file(READ "${GUARD_DIR}/configure_codepage.txt" _then)
endif()
execute_process(COMMAND cmd /c chcp OUTPUT_VARIABLE _now ERROR_QUIET ENCODING NONE)
string(REGEX MATCH "[0-9]+[ \r\n]*$" _now "${_now}")
string(STRIP "${_now}" _now)

# ninja は失敗したコマンドの出力を、そのままのバイト列で端末へ流す。CMake の文字列は UTF-8 なので、
# chcp 932 の端末では日本語が化ける。端末が chcp 932 でも読めるよう、英語を先に置く。
set(_bar "==============================================================")
message(FATAL_ERROR
	"\n${_bar}\n"
	"BUILD STOPPED: header changes would not trigger rebuilds (cl /showIncludes prefix mismatch).\n"
	"This build dir was configured under code page ${_then}; this terminal is ${_now}.\n"
	"Run `chcp ${_then}` and build again, or build with `mitiru build`.\n"
	"\n"
	"ビルドを止めました。このまま組むと、ヘッダを直しても組み直されません。\n"
	"この build は chcp ${_then} の端末で configure されましたが、今の端末は chcp ${_now} です。\n"
	"chcp ${_then} の端末で組むか、mitiru build で組んでください。\n"
	"${_bar}\n")
