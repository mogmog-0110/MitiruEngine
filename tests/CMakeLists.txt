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
)

target_link_libraries(mitiru_tests PRIVATE
	mitiru::mitiru
	Catch2::Catch2WithMain
)

if(WIN32)
	target_link_libraries(mitiru_tests PRIVATE d3d11.lib dxgi.lib ws2_32.lib)
endif()

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(mitiru_tests)
