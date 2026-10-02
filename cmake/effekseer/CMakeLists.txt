# Effekseer ランタイム (Effekseer 本体 + EffekseerRendererDX12 + LLGI) と、エンジン側の窓口である mitiru_effekseer。
# 同梱の CMake は CRT を /MT に固定し、glfw などを ExternalProject で取得するため使わず、DX12 で必要な
# ソースだけをまとめる。エディタ (Effekseer の GUI) は別のツールなので取り込まない。
#
# 入力: MITIRU_EFFEKSEER_ROOT (external/Effekseer)

set(_efk "${MITIRU_EFFEKSEER_ROOT}/Dev/Cpp")
set(_llgi "${_efk}/3rdParty/LLGI/src")

# ── Effekseer 本体 (描画 API に依らない部分) ──
file(GLOB_RECURSE _efk_core_sources CONFIGURE_DEPENDS "${_efk}/Effekseer/Effekseer/*.cpp")
add_library(mitiru_effekseer_core STATIC ${_efk_core_sources})
target_include_directories(mitiru_effekseer_core PUBLIC "${_efk}/Effekseer")
target_compile_features(mitiru_effekseer_core PUBLIC cxx_std_17)
target_compile_definitions(mitiru_effekseer_core PUBLIC __EFFEKSEER_BUILD_DX12__)

# ── LLGI (EffekseerRendererDX12 が下に敷く薄い描画層。DX12 分だけ) ──
file(GLOB _llgi_sources CONFIGURE_DEPENDS
	"${_llgi}/*.cpp" "${_llgi}/PC/*.cpp" "${_llgi}/Win/*.cpp" "${_llgi}/DX12/*.cpp")
add_library(mitiru_llgi STATIC ${_llgi_sources})
target_include_directories(mitiru_llgi PUBLIC "${_llgi}")
target_compile_definitions(mitiru_llgi PUBLIC NOMINMAX)
target_compile_features(mitiru_llgi PUBLIC cxx_std_17)
target_link_libraries(mitiru_llgi PUBLIC d3d12 dxgi d3dcompiler)

# ── EffekseerRendererDX12 (+ 共通部、LLGI 経由の描画、DX12 のマテリアル変換) ──
file(GLOB _efk_dx12_sources CONFIGURE_DEPENDS
	"${_efk}/EffekseerRendererDX12/EffekseerRenderer/*.cpp"
	"${_efk}/EffekseerRendererCommon/*.cpp"
	"${_efk}/EffekseerRendererLLGI/*.cpp"
	"${_efk}/EffekseerMaterialCompiler/DirectX12/*.cpp"
	"${_efk}/EffekseerMaterialCompiler/HLSLGenerator/*.cpp"
	"${_efk}/EffekseerMaterialCompiler/Common/*.cpp")
add_library(mitiru_effekseer_dx12 STATIC ${_efk_dx12_sources})
target_include_directories(mitiru_effekseer_dx12 PUBLIC "${_efk}/EffekseerRendererDX12")
target_link_libraries(mitiru_effekseer_dx12 PUBLIC mitiru_effekseer_core mitiru_llgi)

foreach(_t mitiru_effekseer_core mitiru_llgi mitiru_effekseer_dx12)
	if(MSVC)
		target_compile_options(${_t} PRIVATE /W0)   # 上流のコードの警告はこちらで直せない
	endif()
endforeach()

# ── エンジン側の窓口 (render/dx12/EffekseerRuntime.hpp の実装) ──
add_library(mitiru_effekseer STATIC "${CMAKE_SOURCE_DIR}/src/effekseer/EffekseerRuntime.cpp")
target_include_directories(mitiru_effekseer PUBLIC "${CMAKE_SOURCE_DIR}/include" PRIVATE "${_efk}")
target_compile_definitions(mitiru_effekseer PUBLIC MITIRU_HAS_EFFEKSEER=1)
target_compile_features(mitiru_effekseer PUBLIC cxx_std_20)
target_link_libraries(mitiru_effekseer PUBLIC sgc::sgc PRIVATE mitiru_effekseer_dx12)
if(MSVC)
	target_compile_options(mitiru_effekseer PRIVATE /utf-8)   # このディレクトリはルートの /utf-8 より先に作られる
endif()
