# AMD FidelityFX FSR 3.1 の upscaler (SDK v1.1.4、MIT) と、エンジン側の窓口である mitiru_fsr3。
# SDK の CMake は全エフェクトと sample を作り、シェーダーの permutation もビルド済みの FidelityFX_SC.exe で
# 作るので使わない。FSR3 upscaler と DX12 backend のソースだけを集め、FidelityFX_SC もソースから作る。
#
# 入力: MITIRU_FSR3_ROOT (external/fsr3)、MITIRU_DXC_BIN と MITIRU_DXC_INC (external/dxc)

set(_ffx "${MITIRU_FSR3_ROOT}/sdk")
set(_sc "${_ffx}/tools/ffx_shader_compiler")
set(_gpu "${_ffx}/include/FidelityFX/gpu")
set(_shader_out "${CMAKE_CURRENT_BINARY_DIR}/shaders")

# ── FidelityFX_SC: HLSL を permutation ごとに DXC へ通し、DXIL と反映情報をヘッダに書く (ビルド時だけ使う) ──
enable_language(C)   # SPIRV-Reflect が C で書かれている
file(GLOB _sc_sources CONFIGURE_DEPENDS "${_sc}/src/*.cpp")
add_executable(mitiru_ffx_sc
	${_sc_sources}
	"${_sc}/libs/MD5/md5.cpp"
	"${_sc}/libs/SPIRV-Reflect/spirv_reflect.c"
	"${_sc}/libs/tiny-process-library/process.cpp"
	"${_sc}/libs/tiny-process-library/process_win.cpp")
target_include_directories(mitiru_ffx_sc PRIVATE
	"${_sc}/libs/MD5" "${_sc}/libs/SPIRV-Reflect" "${_sc}/libs/tiny-process-library" "${MITIRU_DXC_INC}")
target_compile_features(mitiru_ffx_sc PRIVATE cxx_std_17)
target_link_libraries(mitiru_ffx_sc PRIVATE dxguid)
if(MSVC)
	target_compile_options(mitiru_ffx_sc PRIVATE /W0)   # 上流のコードの警告はこちらで直せない
endif()
# FidelityFX_SC は dxcompiler.dll を名前だけで読むので、exe の隣に置く
add_custom_command(TARGET mitiru_ffx_sc POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${MITIRU_DXC_BIN}/dxcompiler.dll" "${MITIRU_DXC_BIN}/dxil.dll" "$<TARGET_FILE_DIR:mitiru_ffx_sc>"
	COMMENT "Deploying DXC next to mitiru_ffx_sc")

# ── FSR3 upscaler のシェーダー (SDK の CMakeCompileFSR3UpscalerShaders.txt と同じ引数) ──
set(_sc_base_args
	-reflection -DFFX_GPU=1
	-DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0
	-DFFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0
	-DFFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1
	-DFFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0
	-DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2
	-E CS -Wno-for-redefinition -Wno-ambig-lit-shift -DFFX_HLSL=1
	-DFFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE={0,1}
	-DFFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT={0,1}
	-DFFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS={0,1}
	-DFFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS={0,1}
	-DFFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH={0,1}
	-DFFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING={0,1}
	-I "${_gpu}" -I "${_gpu}/fsr3upscaler")
set(_sc_wave32 -DFFX_HLSL_SM=62 -T cs_6_2)
set(_sc_wave64 "-DFFX_PREFER_WAVE64=[WaveSize(64)]" -DFFX_HLSL_SM=66 -T cs_6_6)

file(GLOB _fsr3_passes CONFIGURE_DEPENDS "${_ffx}/src/backends/dx12/shaders/fsr3upscaler/*.hlsl")
file(GLOB _fsr3_gpu_headers CONFIGURE_DEPENDS "${_gpu}/*.h" "${_gpu}/fsr3upscaler/*.h" "${_gpu}/fsr1/*.h" "${_gpu}/spd/*.h")
file(MAKE_DIRECTORY "${_shader_out}")
set(_fsr3_headers)
foreach(_pass ${_fsr3_passes})
	get_filename_component(_name "${_pass}" NAME_WE)
	foreach(_variant "" "_wave64" "_16bit" "_wave64_16bit")
		set(_args ${_sc_base_args})
		if(_variant MATCHES "wave64")
			list(APPEND _args ${_sc_wave64})
		else()
			list(APPEND _args ${_sc_wave32})
		endif()
		if(_variant MATCHES "16bit")
			list(APPEND _args -DFFX_HALF=1 -enable-16bit-types)
		else()
			list(APPEND _args -DFFX_HALF=0)
		endif()
		set(_out "${_shader_out}/${_name}${_variant}_permutations.h")
		add_custom_command(
			OUTPUT "${_out}"
			COMMAND mitiru_ffx_sc ${_args} -name=${_name}${_variant} "-output=${_shader_out}" "${_pass}"
			DEPENDS mitiru_ffx_sc "${_pass}" ${_fsr3_gpu_headers}
			WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
			COMMENT "FSR3 shader permutations: ${_name}${_variant}"
			VERBATIM)
		list(APPEND _fsr3_headers "${_out}")
	endforeach()
endforeach()
add_custom_target(mitiru_fsr3_shaders DEPENDS ${_fsr3_headers})

# ── FFX の DX12 backend と FSR3 upscaler (FFX_FSR3UPSCALER だけを有効にして他のエフェクトを除く) ──
add_library(mitiru_ffx_fsr3 STATIC
	"${_ffx}/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp"
	"${_ffx}/src/backends/dx12/ffx_dx12.cpp"
	"${_ffx}/src/backends/shared/ffx_shader_blobs.cpp"
	"${_ffx}/src/backends/shared/blob_accessors/ffx_fsr3upscaler_shaderblobs.cpp"
	"${_ffx}/src/shared/ffx_assert.cpp"
	"${_ffx}/src/shared/ffx_message.cpp"
	"${_ffx}/src/shared/ffx_breadcrumbs_list.cpp"
	"${_ffx}/src/shared/ffx_object_management.cpp")
add_dependencies(mitiru_ffx_fsr3 mitiru_fsr3_shaders)
target_include_directories(mitiru_ffx_fsr3
	PUBLIC "${_ffx}/include"
	PRIVATE "${_ffx}/libs" "${_ffx}/src/shared" "${_ffx}/src/backends/shared" "${_ffx}/src/components" "${_shader_out}")
target_compile_definitions(mitiru_ffx_fsr3 PRIVATE FFX_FSR3UPSCALER NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_features(mitiru_ffx_fsr3 PUBLIC cxx_std_17)
target_link_libraries(mitiru_ffx_fsr3 PUBLIC d3d12 dxgi)
if(MSVC)
	target_compile_options(mitiru_ffx_fsr3 PRIVATE /W0)
endif()

# ── エンジン側の窓口 (render/dx12/Fsr3Upscaler.hpp の実装)。FFX のヘッダはこの 1 つの翻訳単位だけが見る ──
add_library(mitiru_fsr3 STATIC "${PROJECT_SOURCE_DIR}/src/fsr3/Fsr3Upscaler.cpp")
target_include_directories(mitiru_fsr3 PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(mitiru_fsr3 PUBLIC MITIRU_HAS_FSR3=1)
target_compile_features(mitiru_fsr3 PUBLIC cxx_std_20)
target_link_libraries(mitiru_fsr3 PRIVATE mitiru_ffx_fsr3)
if(MSVC)
	target_compile_options(mitiru_fsr3 PRIVATE /utf-8)   # このディレクトリはルートの /utf-8 より先に作られる
endif()
