/// @file TestSerialize.cpp
/// @brief Serialize.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/core/Serialize.hpp"
#include "sgc/types/Result.hpp"
#include "sgc/math/Vec2.hpp"
#include "sgc/math/Vec3.hpp"
#include "sgc/math/Vec4.hpp"
#include "sgc/types/Color.hpp"
#include "sgc/math/Quaternion.hpp"
#include "sgc/math/Rect.hpp"
#include "sgc/math/Mat3.hpp"
#include "sgc/math/Mat4.hpp"

#include <string>
#include <vector>

// ── テスト用のVisitable型 ──────────────────────────────────────

struct PlayerData
{
	std::string name;
	int health{0};
	float speed{0.0f};
	bool active{false};

	void visit(sgc::JsonWriter& w) const
	{
		w.write("name", name);
		w.write("health", health);
		w.write("speed", speed);
		w.write("active", active);
	}

	void visit(sgc::JsonReader& r)
	{
		r.read("name", name);
		r.read("health", health);
		r.read("speed", speed);
		r.read("active", active);
	}
};

struct NestedData
{
	int id{0};
	PlayerData player;

	void visit(sgc::JsonWriter& w) const
	{
		w.write("id", id);
		sgc::JsonWriter nested;
		player.visit(nested);
		w.write("player", nested);
	}

	void visit(sgc::JsonReader& r)
	{
		r.read("id", id);
		sgc::JsonReader nested;
		r.read("player", nested);
		player.visit(nested);
	}
};

// ── テスト ────────────────────────────────────────────────

TEST_CASE("JsonWriter produces valid JSON", "[core][serialize]")
{
	sgc::JsonWriter w;
	w.write("x", 42);
	w.write("name", std::string("test"));

	auto json = w.toString();
	REQUIRE(json.find("\"x\":42") != std::string::npos);
	REQUIRE(json.find("\"name\":\"test\"") != std::string::npos);
}

TEST_CASE("JsonWriter handles float values", "[core][serialize]")
{
	sgc::JsonWriter w;
	w.write("val", 3.14f);

	auto json = w.toString();
	REQUIRE(json.find("\"val\":") != std::string::npos);
}

TEST_CASE("JsonWriter handles bool values", "[core][serialize]")
{
	sgc::JsonWriter w;
	w.write("flag", true);

	auto json = w.toString();
	REQUIRE(json.find("\"flag\":true") != std::string::npos);
}

TEST_CASE("JsonWriter escapes strings", "[core][serialize]")
{
	sgc::JsonWriter w;
	w.write("msg", std::string("hello\nworld"));

	auto json = w.toString();
	REQUIRE(json.find("\\n") != std::string::npos);
}

TEST_CASE("JsonReader reads integer", "[core][serialize]")
{
	sgc::JsonReader r;
	r.fromString("{\"x\":42}");

	int val = 0;
	r.read("x", val);
	REQUIRE(val == 42);
}

TEST_CASE("JsonReader reads float", "[core][serialize]")
{
	sgc::JsonReader r;
	r.fromString("{\"val\":3.14}");

	float val = 0.0f;
	r.read("val", val);
	REQUIRE(val > 3.0f);
	REQUIRE(val < 3.2f);
}

TEST_CASE("JsonReader reads string", "[core][serialize]")
{
	sgc::JsonReader r;
	r.fromString("{\"name\":\"hello\"}");

	std::string val;
	r.read("name", val);
	REQUIRE(val == "hello");
}

TEST_CASE("JsonReader reads bool", "[core][serialize]")
{
	sgc::JsonReader r;
	r.fromString("{\"flag\":true}");

	bool val = false;
	r.read("flag", val);
	REQUIRE(val);
}

TEST_CASE("JsonReader has checks key existence", "[core][serialize]")
{
	sgc::JsonReader r;
	r.fromString("{\"x\":1}");

	REQUIRE(r.has("x"));
	REQUIRE_FALSE(r.has("y"));
}

TEST_CASE("JsonReader reads nested object", "[core][serialize]")
{
	sgc::JsonReader r;
	r.fromString("{\"id\":1,\"data\":{\"val\":99}}");

	int id = 0;
	r.read("id", id);
	REQUIRE(id == 1);

	sgc::JsonReader nested;
	r.read("data", nested);

	int val = 0;
	nested.read("val", val);
	REQUIRE(val == 99);
}

TEST_CASE("toJson and fromJson roundtrip", "[core][serialize]")
{
	PlayerData original;
	original.name = "hero";
	original.health = 100;
	original.speed = 5.5f;
	original.active = true;

	auto json = sgc::toJson(original);
	REQUIRE(json.find("\"name\":\"hero\"") != std::string::npos);
	REQUIRE(json.find("\"health\":100") != std::string::npos);

	auto restored = sgc::fromJson<PlayerData>(json);
	REQUIRE(restored.name == "hero");
	REQUIRE(restored.health == 100);
	REQUIRE(restored.active);
}

TEST_CASE("Nested Visitable roundtrip", "[core][serialize]")
{
	NestedData original;
	original.id = 42;
	original.player.name = "knight";
	original.player.health = 200;
	original.player.speed = 3.0f;
	original.player.active = true;

	auto json = sgc::toJson(original);

	auto restored = sgc::fromJson<NestedData>(json);
	REQUIRE(restored.id == 42);
	REQUIRE(restored.player.name == "knight");
	REQUIRE(restored.player.health == 200);
	REQUIRE(restored.player.active);
}

TEST_CASE("JsonReader handles missing keys gracefully", "[core][serialize]")
{
	sgc::JsonReader r;
	r.fromString("{\"x\":1}");

	int y = 99;
	r.read("y", y);
	REQUIRE(y == 99);  // 未変更
}

TEST_CASE("JsonWriter handles empty object", "[core][serialize]")
{
	sgc::JsonWriter w;
	REQUIRE(w.toString() == "{}");
}

// ── JsonArrayWriter ────────────────────────────────────────────

TEST_CASE("JsonArrayWriter produces valid array", "[core][serialize]")
{
	sgc::JsonArrayWriter arr;
	arr.push(1);
	arr.push(2);
	arr.push(3);
	REQUIRE(arr.toString() == "[1,2,3]");
}

TEST_CASE("JsonArrayWriter empty array", "[core][serialize]")
{
	sgc::JsonArrayWriter arr;
	REQUIRE(arr.toString() == "[]");
}

TEST_CASE("JsonArrayWriter mixed types", "[core][serialize]")
{
	sgc::JsonArrayWriter arr;
	arr.push(42);
	arr.push(true);
	arr.push(std::string("hello"));
	auto json = arr.toString();
	REQUIRE(json.find("42") != std::string::npos);
	REQUIRE(json.find("true") != std::string::npos);
	REQUIRE(json.find("\"hello\"") != std::string::npos);
}

// ── vector roundtrip ───────────────────────────────────────────

TEST_CASE("JsonWriter and JsonReader vector<int> roundtrip", "[core][serialize]")
{
	sgc::JsonWriter w;
	std::vector<int> original{10, 20, 30};
	w.write("data", original);
	auto json = w.toString();

	sgc::JsonReader r;
	r.fromString(json);
	std::vector<int> restored;
	r.read("data", restored);

	REQUIRE(restored.size() == 3);
	REQUIRE(restored[0] == 10);
	REQUIRE(restored[1] == 20);
	REQUIRE(restored[2] == 30);
}

TEST_CASE("JsonWriter and JsonReader vector<float> roundtrip", "[core][serialize]")
{
	sgc::JsonWriter w;
	std::vector<float> original{1.5f, 2.5f, 3.5f};
	w.write("vals", original);
	auto json = w.toString();

	sgc::JsonReader r;
	r.fromString(json);
	std::vector<float> restored;
	r.read("vals", restored);

	REQUIRE(restored.size() == 3);
	REQUIRE(restored[0] > 1.4f);
	REQUIRE(restored[2] > 3.4f);
}

TEST_CASE("JsonReader reads empty array", "[core][serialize]")
{
	sgc::JsonReader r;
	r.fromString("{\"arr\":[]}");

	std::vector<int> vec;
	r.read("arr", vec);
	REQUIRE(vec.empty());
}

// ── 数学型 toJson/fromJson roundtrip ───────────────────────────

TEST_CASE("Vec2f serialize roundtrip", "[core][serialize]")
{
	sgc::Vec2f original{3.0f, 7.0f};
	auto json = sgc::toJson(original);
	auto restored = sgc::fromJson<sgc::Vec2f>(json);
	REQUIRE(restored.x > 2.9f);
	REQUIRE(restored.y > 6.9f);
}

TEST_CASE("Vec3f serialize roundtrip", "[core][serialize]")
{
	sgc::Vec3f original{1.0f, 2.0f, 3.0f};
	auto json = sgc::toJson(original);
	auto restored = sgc::fromJson<sgc::Vec3f>(json);
	REQUIRE(restored.x > 0.9f);
	REQUIRE(restored.y > 1.9f);
	REQUIRE(restored.z > 2.9f);
}

TEST_CASE("Vec4f serialize roundtrip", "[core][serialize]")
{
	sgc::Vec4f original{1.0f, 2.0f, 3.0f, 4.0f};
	auto json = sgc::toJson(original);
	auto restored = sgc::fromJson<sgc::Vec4f>(json);
	REQUIRE(restored.x > 0.9f);
	REQUIRE(restored.w > 3.9f);
}

TEST_CASE("Colorf serialize roundtrip", "[core][serialize]")
{
	sgc::Colorf original{0.5f, 0.6f, 0.7f, 0.8f};
	auto json = sgc::toJson(original);
	auto restored = sgc::fromJson<sgc::Colorf>(json);
	REQUIRE(restored.r > 0.49f);
	REQUIRE(restored.a > 0.79f);
}

TEST_CASE("Quaternionf serialize roundtrip", "[core][serialize]")
{
	sgc::Quaternionf original{0.1f, 0.2f, 0.3f, 0.9f};
	auto json = sgc::toJson(original);
	auto restored = sgc::fromJson<sgc::Quaternionf>(json);
	REQUIRE(restored.x > 0.09f);
	REQUIRE(restored.w > 0.89f);
}

TEST_CASE("Rectf serialize roundtrip", "[core][serialize]")
{
	sgc::Rectf original{10.0f, 20.0f, 100.0f, 50.0f};
	auto json = sgc::toJson(original);
	auto restored = sgc::fromJson<sgc::Rectf>(json);
	REQUIRE(restored.position.x > 9.9f);
	REQUIRE(restored.size.y > 49.9f);
}

TEST_CASE("Mat3f serialize roundtrip", "[core][serialize]")
{
	auto original = sgc::Mat3f::identity();
	auto json = sgc::toJson(original);
	auto restored = sgc::fromJson<sgc::Mat3f>(json);
	REQUIRE(restored.m[0][0] > 0.9f);
	REQUIRE(restored.m[1][1] > 0.9f);
	REQUIRE(restored.m[0][1] < 0.1f);
}

TEST_CASE("Mat4f serialize roundtrip", "[core][serialize]")
{
	auto original = sgc::Mat4f::translation({5.0f, 10.0f, 15.0f});
	auto json = sgc::toJson(original);
	auto restored = sgc::fromJson<sgc::Mat4f>(json);
	REQUIRE(restored.m[0][3] > 4.9f);
	REQUIRE(restored.m[1][3] > 9.9f);
	REQUIRE(restored.m[2][3] > 14.9f);
}

// ── Visitable自動検出ネスト ──────────────────────────────────

TEST_CASE("JsonWriter auto-detects ConstVisitable for nesting", "[core][serialize]")
{
	sgc::JsonWriter w;
	sgc::Vec2f pos{1.0f, 2.0f};
	w.write("pos", pos);  // ConstVisitable<Vec2f, JsonWriter>で自動ネスト
	auto json = w.toString();
	REQUIRE(json.find("\"pos\":{") != std::string::npos);
	REQUIRE(json.find("\"x\":") != std::string::npos);
}

TEST_CASE("JsonReader auto-detects Visitable for nested reading", "[core][serialize]")
{
	sgc::JsonReader r;
	r.fromString("{\"pos\":{\"x\":5,\"y\":10}}");

	sgc::Vec2f pos;
	r.read("pos", pos);
	REQUIRE(pos.x > 4.9f);
	REQUIRE(pos.y > 9.9f);
}

// ── Error tracking ─────────────────────────────────────────────

TEST_CASE("JsonReader hasError is false on valid JSON", "[core][serialize]")
{
	sgc::JsonReader r;
	r.fromString("{\"x\":1}");
	REQUIRE_FALSE(r.hasError());
	REQUIRE(r.errorMessage().empty());
}

TEST_CASE("JsonReader hasError is true on invalid JSON", "[core][serialize]")
{
	sgc::JsonReader r;
	r.fromString("not json at all");
	REQUIRE(r.hasError());
	REQUIRE_FALSE(r.errorMessage().empty());
}

TEST_CASE("JsonReader hasError on empty string", "[core][serialize]")
{
	sgc::JsonReader r;
	r.fromString("");
	REQUIRE(r.hasError());
}

// ── Visitable vector roundtrip ─────────────────────────────────

TEST_CASE("vector<Vec2f> serialize roundtrip", "[core][serialize]")
{
	sgc::JsonWriter w;
	std::vector<sgc::Vec2f> original{{1.0f, 2.0f}, {3.0f, 4.0f}, {5.0f, 6.0f}};
	w.write("points", original);
	auto json = w.toString();

	sgc::JsonReader r;
	r.fromString(json);
	std::vector<sgc::Vec2f> restored;
	r.read("points", restored);

	REQUIRE(restored.size() == 3);
	REQUIRE(restored[0].x > 0.9f);
	REQUIRE(restored[0].y > 1.9f);
	REQUIRE(restored[2].x > 4.9f);
	REQUIRE(restored[2].y > 5.9f);
}

TEST_CASE("vector<Vec2f> empty roundtrip", "[core][serialize]")
{
	sgc::JsonWriter w;
	std::vector<sgc::Vec2f> original;
	w.write("points", original);
	auto json = w.toString();

	sgc::JsonReader r;
	r.fromString(json);
	std::vector<sgc::Vec2f> restored;
	r.read("points", restored);

	REQUIRE(restored.empty());
}

// ── fromJsonResult ─────────────────────────────────────────────

TEST_CASE("fromJsonResult succeeds on valid JSON", "[core][serialize]")
{
	auto result = sgc::fromJsonResult<PlayerData>("{\"name\":\"hero\",\"health\":100,\"speed\":5.5,\"active\":true}");
	REQUIRE(result.hasValue());
	REQUIRE(result.value().name == "hero");
	REQUIRE(result.value().health == 100);
}

TEST_CASE("fromJsonResult fails on invalid JSON", "[core][serialize]")
{
	auto result = sgc::fromJsonResult<PlayerData>("garbage input");
	REQUIRE(result.hasError());
	REQUIRE_FALSE(result.error().message.empty());
}
