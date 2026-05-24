/// @file TestSpriteRenderer.cpp
/// @brief ISpriteRenderer mock tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/graphics/ISpriteRenderer.hpp"

#include <string>
#include <unordered_map>
#include <vector>

using namespace sgc;
using Catch::Approx;

// ── MockSpriteRenderer ──────────────────────────────────

namespace
{

/// @brief テスト用のスプライトレンダラーモック
class MockSpriteRenderer : public ISpriteRenderer
{
public:
	/// @brief 描画コール種別
	enum class CallType
	{
		DrawSprite,
		DrawSpriteRotated,
		DrawSpriteScaled,
		SetBlendMode
	};

	/// @brief 描画コール記録
	struct DrawCall
	{
		CallType type;
		AABB2f dest{};
		AABB2f src{};
		int textureId = -1;
		float angle = 0.0f;
		Vec2f origin{};
		Vec2f scale{};
		Vec2f pos{};
		Colorf tint{};
		BlendMode blendMode{};
	};

	[[nodiscard]] int loadTexture(const std::string& path) override
	{
		const int id = m_nextId++;
		m_textures[id] = TextureInfo{path, 64.0f, 64.0f};
		return id;
	}

	void unloadTexture(int textureId) override
	{
		m_textures.erase(textureId);
	}

	[[nodiscard]] Vec2f getTextureSize(int textureId) const override
	{
		const auto it = m_textures.find(textureId);
		if (it == m_textures.end()) return Vec2f{0.0f, 0.0f};
		return Vec2f{it->second.width, it->second.height};
	}

	void drawSprite(const AABB2f& dest, const AABB2f& src,
		int textureId, const Colorf& tint) override
	{
		m_calls.push_back({CallType::DrawSprite, dest, src, textureId,
			0.0f, {}, {}, {}, tint, {}});
	}

	void drawSpriteRotated(const AABB2f& dest, const AABB2f& src,
		int textureId, float angle, const Vec2f& origin,
		const Colorf& tint) override
	{
		m_calls.push_back({CallType::DrawSpriteRotated, dest, src, textureId,
			angle, origin, {}, {}, tint, {}});
	}

	void drawSpriteScaled(const Vec2f& pos, const Vec2f& scale,
		int textureId, const Colorf& tint) override
	{
		m_calls.push_back({CallType::DrawSpriteScaled, {}, {}, textureId,
			0.0f, {}, scale, pos, tint, {}});
	}

	void setBlendMode(BlendMode mode) override
	{
		m_currentBlendMode = mode;
		m_calls.push_back({CallType::SetBlendMode, {}, {}, -1,
			0.0f, {}, {}, {}, {}, mode});
	}

	/// @brief 記録された描画コールを取得する
	[[nodiscard]] const std::vector<DrawCall>& calls() const { return m_calls; }

	/// @brief 現在のブレンドモードを取得する
	[[nodiscard]] BlendMode currentBlendMode() const { return m_currentBlendMode; }

	/// @brief テクスチャ数を取得する
	[[nodiscard]] size_t textureCount() const { return m_textures.size(); }

private:
	struct TextureInfo
	{
		std::string path;
		float width;
		float height;
	};

	std::unordered_map<int, TextureInfo> m_textures;
	std::vector<DrawCall> m_calls;
	int m_nextId = 0;
	BlendMode m_currentBlendMode = BlendMode::Normal;
};

} // anonymous namespace

// ── テクスチャ管理テスト ────────────────────────────────

TEST_CASE("ISpriteRenderer - loadTexture returns valid ID", "[graphics][sprite]")
{
	MockSpriteRenderer renderer;
	const int id = renderer.loadTexture("test.png");

	REQUIRE(id >= 0);
	REQUIRE(renderer.textureCount() == 1);
}

TEST_CASE("ISpriteRenderer - loadTexture assigns unique IDs", "[graphics][sprite]")
{
	MockSpriteRenderer renderer;
	const int id1 = renderer.loadTexture("a.png");
	const int id2 = renderer.loadTexture("b.png");

	REQUIRE(id1 != id2);
	REQUIRE(renderer.textureCount() == 2);
}

TEST_CASE("ISpriteRenderer - unloadTexture removes texture", "[graphics][sprite]")
{
	MockSpriteRenderer renderer;
	const int id = renderer.loadTexture("test.png");
	renderer.unloadTexture(id);

	REQUIRE(renderer.textureCount() == 0);
}

TEST_CASE("ISpriteRenderer - getTextureSize returns valid size", "[graphics][sprite]")
{
	MockSpriteRenderer renderer;
	const int id = renderer.loadTexture("test.png");
	const auto size = renderer.getTextureSize(id);

	REQUIRE(size.x == Approx(64.0f));
	REQUIRE(size.y == Approx(64.0f));
}

TEST_CASE("ISpriteRenderer - getTextureSize returns zero for invalid ID", "[graphics][sprite]")
{
	MockSpriteRenderer renderer;
	const auto size = renderer.getTextureSize(999);

	REQUIRE(size.x == Approx(0.0f));
	REQUIRE(size.y == Approx(0.0f));
}

// ── 描画テスト ──────────────────────────────────────────

TEST_CASE("ISpriteRenderer - drawSprite records call", "[graphics][sprite]")
{
	MockSpriteRenderer renderer;
	const int id = renderer.loadTexture("test.png");

	const AABB2f dest{{0, 0}, {100, 100}};
	const AABB2f src{{0, 0}, {64, 64}};
	renderer.drawSprite(dest, src, id, Colorf::white());

	REQUIRE(renderer.calls().size() == 1);
	REQUIRE(renderer.calls()[0].type == MockSpriteRenderer::CallType::DrawSprite);
	REQUIRE(renderer.calls()[0].textureId == id);
	REQUIRE(renderer.calls()[0].tint == Colorf::white());
}

TEST_CASE("ISpriteRenderer - drawSpriteRotated records angle and origin", "[graphics][sprite]")
{
	MockSpriteRenderer renderer;
	const int id = renderer.loadTexture("test.png");

	const AABB2f dest{{10, 20}, {110, 120}};
	const AABB2f src{{0, 0}, {64, 64}};
	const Vec2f origin{50.0f, 50.0f};
	const float angle = 1.57f;

	renderer.drawSpriteRotated(dest, src, id, angle, origin, Colorf::red());

	REQUIRE(renderer.calls().size() == 1);
	const auto& call = renderer.calls()[0];
	REQUIRE(call.type == MockSpriteRenderer::CallType::DrawSpriteRotated);
	REQUIRE(call.angle == Approx(1.57f));
	REQUIRE(call.origin.x == Approx(50.0f));
	REQUIRE(call.origin.y == Approx(50.0f));
	REQUIRE(call.tint == Colorf::red());
}

TEST_CASE("ISpriteRenderer - drawSpriteScaled records scale and position", "[graphics][sprite]")
{
	MockSpriteRenderer renderer;
	const int id = renderer.loadTexture("test.png");

	const Vec2f pos{10.0f, 20.0f};
	const Vec2f scale{2.0f, 3.0f};
	renderer.drawSpriteScaled(pos, scale, id, Colorf::blue());

	REQUIRE(renderer.calls().size() == 1);
	const auto& call = renderer.calls()[0];
	REQUIRE(call.type == MockSpriteRenderer::CallType::DrawSpriteScaled);
	REQUIRE(call.pos.x == Approx(10.0f));
	REQUIRE(call.pos.y == Approx(20.0f));
	REQUIRE(call.scale.x == Approx(2.0f));
	REQUIRE(call.scale.y == Approx(3.0f));
}

// ── ブレンドモードテスト ────────────────────────────────

TEST_CASE("ISpriteRenderer - default blend mode is Normal", "[graphics][sprite]")
{
	MockSpriteRenderer renderer;
	REQUIRE(renderer.currentBlendMode() == BlendMode::Normal);
}

TEST_CASE("ISpriteRenderer - setBlendMode changes mode", "[graphics][sprite]")
{
	MockSpriteRenderer renderer;

	renderer.setBlendMode(BlendMode::Additive);
	REQUIRE(renderer.currentBlendMode() == BlendMode::Additive);

	renderer.setBlendMode(BlendMode::Multiply);
	REQUIRE(renderer.currentBlendMode() == BlendMode::Multiply);

	renderer.setBlendMode(BlendMode::Screen);
	REQUIRE(renderer.currentBlendMode() == BlendMode::Screen);

	renderer.setBlendMode(BlendMode::Normal);
	REQUIRE(renderer.currentBlendMode() == BlendMode::Normal);
}

TEST_CASE("ISpriteRenderer - setBlendMode records call", "[graphics][sprite]")
{
	MockSpriteRenderer renderer;
	renderer.setBlendMode(BlendMode::Additive);

	REQUIRE(renderer.calls().size() == 1);
	REQUIRE(renderer.calls()[0].type == MockSpriteRenderer::CallType::SetBlendMode);
	REQUIRE(renderer.calls()[0].blendMode == BlendMode::Additive);
}

// ── BlendMode列挙テスト ─────────────────────────────────

TEST_CASE("BlendMode enum values are distinct", "[graphics][sprite]")
{
	REQUIRE(static_cast<int>(BlendMode::Normal) != static_cast<int>(BlendMode::Additive));
	REQUIRE(static_cast<int>(BlendMode::Additive) != static_cast<int>(BlendMode::Multiply));
	REQUIRE(static_cast<int>(BlendMode::Multiply) != static_cast<int>(BlendMode::Screen));
}

// ── 複数操作の組み合わせテスト ──────────────────────────

TEST_CASE("ISpriteRenderer - multiple draw calls are recorded in order", "[graphics][sprite]")
{
	MockSpriteRenderer renderer;
	const int id = renderer.loadTexture("test.png");

	const AABB2f rect{{0, 0}, {64, 64}};
	renderer.drawSprite(rect, rect, id, Colorf::white());
	renderer.drawSpriteScaled(Vec2f{0, 0}, Vec2f{1, 1}, id, Colorf::white());
	renderer.drawSpriteRotated(rect, rect, id, 0.5f, Vec2f{32, 32}, Colorf::white());

	REQUIRE(renderer.calls().size() == 3);
	REQUIRE(renderer.calls()[0].type == MockSpriteRenderer::CallType::DrawSprite);
	REQUIRE(renderer.calls()[1].type == MockSpriteRenderer::CallType::DrawSpriteScaled);
	REQUIRE(renderer.calls()[2].type == MockSpriteRenderer::CallType::DrawSpriteRotated);
}

TEST_CASE("ISpriteRenderer - unload then getTextureSize returns zero", "[graphics][sprite]")
{
	MockSpriteRenderer renderer;
	const int id = renderer.loadTexture("test.png");
	renderer.unloadTexture(id);

	const auto size = renderer.getTextureSize(id);
	REQUIRE(size.x == Approx(0.0f));
	REQUIRE(size.y == Approx(0.0f));
}
