#include <catch2/catch_test_macros.hpp>
#include <sgc/imgui/ImGuiAdapter.hpp>

#include <string>
#include <vector>

using namespace sgc::imgui;

// テスト用バックエンド実装
class MockImGuiBackend : public IImGuiBackend
{
public:
	void newFrame() override { m_frameCount++; }
	void render() override { m_renderCount++; }
	void shutdown() override { m_isShutdown = true; }

	[[nodiscard]]
	bool isInitialized() const override { return !m_isShutdown; }

	int frameCount() const { return m_frameCount; }
	int renderCount() const { return m_renderCount; }
	bool isShutdown() const { return m_isShutdown; }

private:
	int m_frameCount = 0;
	int m_renderCount = 0;
	bool m_isShutdown = false;
};

// テスト用ワールド
struct MockWorld
{
	bool isAlive(uint32_t entity) const
	{
		return entity < 10;
	}
};

// テスト用プロファイラーエントリ
struct MockProfileEntry
{
	std::string m_name;
	double m_avgMs;
	uint32_t m_callCount;

	std::string name() const { return m_name; }
	double averageMs() const { return m_avgMs; }
	uint32_t callCount() const { return m_callCount; }
};

struct MockProfiler
{
	std::vector<MockProfileEntry> m_entries;

	const std::vector<MockProfileEntry>& getEntries() const { return m_entries; }
};

TEST_CASE("IImGuiBackend lifecycle", "[imgui]")
{
	MockImGuiBackend backend;
	REQUIRE(backend.isInitialized());

	backend.newFrame();
	backend.render();
	REQUIRE(backend.frameCount() == 1);
	REQUIRE(backend.renderCount() == 1);

	backend.shutdown();
	REQUIRE_FALSE(backend.isInitialized());
}

TEST_CASE("ImGuiHelper collectEntityInfo", "[imgui]")
{
	MockWorld world;

	const auto alive = ImGuiHelper::collectEntityInfo(world, uint32_t{5});
	REQUIRE(alive.entityId == 5);
	REQUIRE(alive.isAlive == true);

	const auto dead = ImGuiHelper::collectEntityInfo(world, uint32_t{15});
	REQUIRE(dead.entityId == 15);
	REQUIRE(dead.isAlive == false);
}

TEST_CASE("ImGuiHelper collectProfileData", "[imgui]")
{
	MockProfiler profiler;
	profiler.m_entries.push_back({"Update", 1.5, 60});
	profiler.m_entries.push_back({"Render", 8.3, 60});

	const auto data = ImGuiHelper::collectProfileData(profiler);
	REQUIRE(data.size() == 2);
	REQUIRE(data[0].name == "Update");
	REQUIRE(data[0].timeMs == 1.5f);
	REQUIRE(data[0].callCount == 60);
	REQUIRE(data[1].name == "Render");
}

TEST_CASE("ProfileEntry and EntityDisplayInfo default values", "[imgui]")
{
	ProfileEntry pe;
	pe.name = "test";
	pe.timeMs = 0.0f;
	pe.callCount = 0;
	REQUIRE(pe.name == "test");

	EntityDisplayInfo edi;
	edi.entityId = 0;
	edi.isAlive = false;
	edi.componentNames = {"Position", "Velocity"};
	REQUIRE(edi.componentNames.size() == 2);
}
