include(FetchContent)

FetchContent_Declare(
	Catch2
	GIT_REPOSITORY https://github.com/catchorg/Catch2.git
	GIT_TAG v3.8.0
	GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(mitiru_tests
	mitiru/TestMitiruEngine.cpp
	mitiru/TestGfx.cpp
	mitiru/TestRender.cpp
	mitiru/TestObserve.cpp
	mitiru/TestInput.cpp
	mitiru/TestControl.cpp
	mitiru/TestValidate.cpp
	mitiru/TestEcs.cpp
	mitiru/TestScene.cpp
	mitiru/TestAudioResource.cpp
	mitiru/TestDebugOverlay.cpp
	mitiru/TestNetwork.cpp
	mitiru/TestScripting.cpp
	mitiru/TestRender3D.cpp
	mitiru/TestBridges.cpp
	mitiru/TestAudioMixer.cpp
	mitiru/TestShaders3D.cpp
	mitiru/TestObjLoader.cpp
	mitiru/TestScriptEnhanced.cpp
	mitiru/TestTcpTransport.cpp
	mitiru/TestImGuiBackend.cpp
	mitiru/TestEntityInspector.cpp
	mitiru/TestSceneView.cpp
	mitiru/TestAssetBrowser.cpp
	mitiru/TestProfilerOverlay.cpp
	mitiru/TestGpuProfiler.cpp
	mitiru/TestVulkanConfig.cpp
	mitiru/TestSdl2Config.cpp
	mitiru/TestGlfwConfig.cpp
	mitiru/TestPlatformInfo.cpp
	mitiru/TestFrameTimer.cpp
	mitiru/TestMitiruApp.cpp
	mitiru/TestGameLoop.cpp
	mitiru/TestServiceRegistry.cpp
	mitiru/TestDx12Config.cpp
	mitiru/TestWin32InputConfig.cpp
	mitiru/TestGamepadConfig.cpp
	mitiru/TestInputMapper.cpp
	mitiru/TestWin32AudioConfig.cpp
	mitiru/TestCamera3D.cpp
	mitiru/TestRenderState3D.cpp
	mitiru/TestRenderer3DConfig.cpp
	mitiru/TestSceneGraph.cpp
	mitiru/TestGameWorld.cpp
	mitiru/TestAssetRegistry.cpp
	mitiru/TestMeshCache.cpp
	mitiru/TestSystemRunner.cpp
)

target_link_libraries(mitiru_tests PRIVATE
	mitiru::mitiru
	Catch2::Catch2WithMain
)

if(WIN32)
	target_link_libraries(mitiru_tests PRIVATE d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib ws2_32.lib winmm.lib xinput.lib)
endif()

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(mitiru_tests)
