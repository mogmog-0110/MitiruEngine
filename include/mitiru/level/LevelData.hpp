#pragma once

/// @file LevelData.hpp
/// @brief Blender から書き出したレベル (glTF) の中身。読んだあとは変わらない POD の配列
/// @details 配置 (スポーン、トリガー、敵、カメラの範囲など) は Entity、当たり判定の面と
///          ナビメッシュの元になる面は世界座標の三角形の列で持つ。座標は glTF と同じ右手系 Y-up、1 単位 1 m で、
///          drawModel に同じ glb を渡したときの見た目とそろう。
///          GameMemory には入れない。ファイルの中身だけで決まるので、読み込みとホットリロードのたびに作り直す。
///          GameMemory には Entity の添字や nameHash を持たせ、ポインタは持たせない。
///          書き方の約束は docs/LEVEL_FROM_BLENDER.md。

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sgc/math/Vec3.hpp>

namespace mitiru::level
{

inline constexpr std::size_t kNameMax = 64;   ///< 名前の最大バイト数 (終端込み)。長い名前は切り詰めて warning を出す
inline constexpr std::size_t kTypeMax = 32;
inline constexpr std::size_t kKeyMax = 32;
inline constexpr std::size_t kTextMax = 64;

/// @brief 名前と型名の比較に使う FNV-1a 32bit。constexpr なので switch の case にも書ける
[[nodiscard]] constexpr std::uint32_t nameHash(std::string_view s) noexcept
{
	std::uint32_t h = 2166136261u;
	for (const char c : s)
	{
		h ^= static_cast<std::uint8_t>(c);
		h *= 16777619u;
	}
	return h;
}

enum class PropertyKind : std::uint8_t
{
	Number,   ///< value[0]
	Bool,     ///< value[0] が 0 か 1
	Text,     ///< text
	Vector,   ///< value[0..vectorSize)
};

/// @brief Blender のカスタムプロパティ 1 つ (glTF の extras)
struct Property
{
	char key[kKeyMax] = {};
	std::uint32_t keyHash = 0;
	PropertyKind kind = PropertyKind::Number;
	std::uint8_t vectorSize = 0;
	std::uint8_t pad[2] = {};
	float value[4] = {};
	char text[kTextMax] = {};
};

/// @brief `mitiru_type` を付けたオブジェクト 1 つ。姿勢は世界座標
struct Entity
{
	char name[kNameMax] = {};
	char type[kTypeMax] = {};
	std::uint32_t nameHash = 0;
	std::uint32_t typeHash = 0;
	float position[3] = {};
	float rotation[4] = {0, 0, 0, 1};   ///< quaternion xyzw
	float scale[3] = {1, 1, 1};
	float halfExtents[3] = {1, 1, 1};   ///< scale × mitiru_size。Empty の立方体表示 (±size) の大きさで、トリガーの箱に使う
	std::int32_t parent = -1;           ///< 親をたどって最初に見つかる Entity の添字。無ければ -1
	std::uint32_t firstProperty = 0;
	std::uint32_t propertyCount = 0;
};

/// @brief 世界座標の三角形の列 (indices は vertices の添字で、3 つで 1 枚)
/// @details 頂点を点の型で持つので、当たり判定 (action::CollisionLevelBuilder::addTriangles) にも
///          ナビメッシュの焼き込みにも、並べ替えずにそのまま渡せる。
struct TriangleSoup
{
	std::vector<sgc::Vec3f> vertices;
	std::vector<std::uint32_t> indices;

	[[nodiscard]] std::size_t triangleCount() const noexcept { return indices.size() / 3; }
};

namespace detail
{
class LevelBuilder;
}

/// @brief 読み込んだレベル。作れるのはローダー (level/LevelLoader.hpp) だけで、使う側は読むだけ
class LevelData
{
public:
	[[nodiscard]] const std::vector<Entity>& entities() const noexcept { return m_entities; }
	[[nodiscard]] const TriangleSoup& collision() const noexcept { return m_collision; }
	[[nodiscard]] const TriangleSoup& navSource() const noexcept { return m_navSource; }

	[[nodiscard]] std::span<const Property> properties(const Entity& e) const noexcept
	{
		return std::span<const Property>(m_properties).subspan(e.firstProperty, e.propertyCount);
	}

	[[nodiscard]] const Entity* find(std::string_view name) const noexcept
	{
		const std::uint32_t h = nameHash(name);
		for (const auto& e : m_entities)
		{
			if (e.nameHash == h && name == e.name) { return &e; }
		}
		return nullptr;
	}

	/// @brief 型名が type の Entity を、書き出したファイルのノード順に f(entity, 添字) へ渡す
	template <typename F>
	void forEachOfType(std::string_view type, F&& f) const
	{
		const std::uint32_t h = nameHash(type);
		for (std::size_t i = 0; i < m_entities.size(); ++i)
		{
			const Entity& e = m_entities[i];
			if (e.typeHash == h && type == e.type) { f(e, i); }
		}
	}

	[[nodiscard]] const Property* property(const Entity& e, std::string_view key) const noexcept
	{
		const std::uint32_t h = nameHash(key);
		for (const auto& p : properties(e))
		{
			if (p.keyHash == h && key == p.key) { return &p; }
		}
		return nullptr;
	}

	[[nodiscard]] float number(const Entity& e, std::string_view key, float fallback = 0.0f) const noexcept
	{
		const Property* p = property(e, key);
		const bool numeric = p != nullptr && (p->kind == PropertyKind::Number || p->kind == PropertyKind::Bool);
		return numeric ? p->value[0] : fallback;
	}

	[[nodiscard]] std::string_view text(const Entity& e, std::string_view key, std::string_view fallback = {}) const noexcept
	{
		const Property* p = property(e, key);
		return (p != nullptr && p->kind == PropertyKind::Text) ? std::string_view(p->text) : fallback;
	}

private:
	friend class detail::LevelBuilder;

	std::vector<Entity> m_entities;
	std::vector<Property> m_properties;
	TriangleSoup m_collision;
	TriangleSoup m_navSource;
};

}  // namespace mitiru::level
