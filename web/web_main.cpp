// ブラウザには DLL が無いので、MITIRU_GAME が作る mitiru_module_load をリンクして直接渡す
#include <mitiru/core/Engine.hpp>

#include "mitiru_web_config.hpp"

extern "C" void mitiru_module_load(mitiru::module::ModuleApi* api, void** memory);

int main()
{
	mitiru::EngineConfig cfg;
	cfg.title        = MITIRU_WEB_TITLE;
	cfg.windowWidth  = MITIRU_WEB_WIDTH;
	cfg.windowHeight = MITIRU_WEB_HEIGHT;
	cfg.audioDir     = MITIRU_WEB_AUDIO_DIR;

	mitiru::Engine engine;
	return engine.runModuleStatic(&mitiru_module_load, cfg) ? 0 : 1;
}
