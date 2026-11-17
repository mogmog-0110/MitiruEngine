# mitiru_film_pt: 映像の 1 コマを DXR 1.1 (inline ray query) のパストレーサーで撮る試作 (ADR 0154)。ウィンドウなし、記述子ヒープなし。
add_executable(mitiru_film_pt main.cpp)
mitiru_use_utf8_codepage(mitiru_film_pt)
target_link_libraries(mitiru_film_pt PRIVATE mitiru::mitiru d3d12 dxgi dxguid)
target_compile_features(mitiru_film_pt PRIVATE cxx_std_20)
if(MSVC)
	target_compile_options(mitiru_film_pt PRIVATE /utf-8)
endif()
