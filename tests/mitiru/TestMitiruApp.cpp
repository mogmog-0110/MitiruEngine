#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <mitiru/core/MitiruApp.hpp>
#include <mitiru/gfx/null/NullDevice.hpp>

// --- Mock Window ---

class MockWindow : public mitiru::IWindow
{
public:
	int m_pollCount = 0;
	int m_closeAfter = 3; ///< 何回pollEvents後に閉じるか
	int m_width = 800;
	int m_height = 600;
	bool m_closed = false;

	[[nodiscard]] bool shouldClose() const override { return m_closed; }

	void pollEvents() override
	{
		++m_pollCount;
		if (m_pollCount >= m_closeAfter)
		{
			m_closed = true;
		}
	}

	[[nodiscard]] int width() const override { return m_width; }
	[[nodiscard]] int height() const override { return m_height; }
	void setTitle(std::string_view) override {}
	void requestClose() override { m_closed = true; }
};

// --- Mock GameScene ---

class MockScene : public mitiru::IGameScene
{
public:
	int initCount = 0;
	int updateCount = 0;
	int drawCount = 0;
	int shutdownCount = 0;
	float lastDt = 0.0f;

	void onInit(mitiru::MitiruApp&) override { ++initCount; }

	void onUpdate(float dt) override
	{
		lastDt = dt;
		++updateCount;
	}

	void onDraw() override { ++drawCount; }

	void onShutdown() override { ++shutdownCount; }
};

// --- MitiruApp tests ---

TEST_CASE("AppConfig default values", "[mitiru][core][MitiruApp]")
{
	const mitiru::AppConfig config;

	REQUIRE(config.windowTitle == "Mitiru Game");
	REQUIRE(config.width == 1280);
	REQUIRE(config.height == 720);
	REQUIRE(config.gfxBackend == mitiru::gfx::Backend::Null);
	REQUIRE(config.vsync == true);
	REQUIRE(config.fixedTimestep == Catch::Approx(1.0f / 60.0f));
}

TEST_CASE("MitiruApp construction with default config", "[mitiru][core][MitiruApp]")
{
	mitiru::MitiruApp app;

	REQUIRE(app.getWindow() == nullptr);
	REQUIRE(app.getDevice() == nullptr);
}

TEST_CASE("MitiruApp init with injected window and device", "[mitiru][core][MitiruApp]")
{
	MockWindow window;
	mitiru::gfx::NullDevice device;

	mitiru::AppConfig config;
	config.gfxBackend = mitiru::gfx::Backend::Null;
	mitiru::MitiruApp app(config);

	app.setWindow(&window);
	app.setDevice(&device);

	const bool result = app.init();

	REQUIRE(result == true);
	REQUIRE(app.getWindow() == &window);
	REQUIRE(app.getDevice() == &device);
}

TEST_CASE("MitiruApp run drives scene lifecycle", "[mitiru][core][MitiruApp]")
{
	MockWindow window;
	window.m_closeAfter = 3;
	mitiru::gfx::NullDevice device;

	mitiru::AppConfig config;
	config.gfxBackend = mitiru::gfx::Backend::Null;
	mitiru::MitiruApp app(config);

	app.setWindow(&window);
	app.setDevice(&device);
	app.init();

	MockScene scene;
	app.run(scene);

	REQUIRE(scene.initCount == 1);
	REQUIRE(scene.updateCount == window.m_closeAfter);
	REQUIRE(scene.drawCount == window.m_closeAfter);
	REQUIRE(scene.shutdownCount == 1);
}

TEST_CASE("MitiruApp run does nothing before init", "[mitiru][core][MitiruApp]")
{
	mitiru::MitiruApp app;

	MockScene scene;
	app.run(scene);

	/// init() が呼ばれていないので何も実行されない
	REQUIRE(scene.initCount == 0);
	REQUIRE(scene.updateCount == 0);
}

TEST_CASE("MitiruApp getTimer returns timer reference", "[mitiru][core][MitiruApp]")
{
	mitiru::MitiruApp app;

	auto& timer = app.getTimer();
	REQUIRE(timer.getFrameCount() == 0);
}

TEST_CASE("MitiruApp shutdown clears resources", "[mitiru][core][MitiruApp]")
{
	MockWindow window;
	mitiru::gfx::NullDevice device;

	mitiru::MitiruApp app;
	app.setWindow(&window);
	app.setDevice(&device);
	app.init();

	app.shutdown();

	REQUIRE(app.getWindow() == nullptr);
	REQUIRE(app.getDevice() == nullptr);
}

TEST_CASE("MitiruApp getConfig returns config", "[mitiru][core][MitiruApp]")
{
	mitiru::AppConfig config;
	config.windowTitle = "Test App";
	config.width = 640;
	config.height = 480;

	mitiru::MitiruApp app(config);

	REQUIRE(app.getConfig().windowTitle == "Test App");
	REQUIRE(app.getConfig().width == 640);
	REQUIRE(app.getConfig().height == 480);
}

TEST_CASE("MitiruApp fixedTimestep applied to timer", "[mitiru][core][MitiruApp]")
{
	mitiru::AppConfig config;
	config.fixedTimestep = 1.0f / 30.0f;

	MockWindow window;
	mitiru::gfx::NullDevice device;

	mitiru::MitiruApp app(config);
	app.setWindow(&window);
	app.setDevice(&device);
	app.init();

	REQUIRE(app.getTimer().getFixedDeltaTime() == Catch::Approx(1.0f / 30.0f));
}
