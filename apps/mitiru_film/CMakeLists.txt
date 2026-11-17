# mitiru_film: 映像の JSON (docs/FILM.md) を検査し、要約し、JSON patch で直す。描かない読み取りと書き換えだけのコンソールツール
add_executable(mitiru_film main.cpp)
mitiru_use_utf8_codepage(mitiru_film)
target_link_libraries(mitiru_film PRIVATE mitiru::mitiru)
target_compile_features(mitiru_film PRIVATE cxx_std_20)
if(MSVC)
	target_compile_options(mitiru_film PRIVATE /utf-8)
endif()

# export-blender が scene.json の隣へ書くスクリプト (tools/film_blender_scene.py) を実行ファイルに埋める。
# 実行ファイルだけ持ち出しても書き出せるように、パスで探さず中身を持つ
set(FILM_BLENDER_PY "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/film_blender_scene.py")
set(FILM_BLENDER_HPP "${CMAKE_CURRENT_BINARY_DIR}/FilmBlenderScript.hpp")
file(READ "${FILM_BLENDER_PY}" FILM_BLENDER_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," FILM_BLENDER_BYTES "${FILM_BLENDER_HEX}")
file(WRITE "${FILM_BLENDER_HPP}" "#pragma once\nstatic const unsigned char kFilmBlenderScript[] = {${FILM_BLENDER_BYTES}0x00};\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FILM_BLENDER_PY}")
target_include_directories(mitiru_film PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
