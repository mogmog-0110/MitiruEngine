#pragma once

/// @file MorphExpressionTypes.hpp
/// @brief 表情 (名前付きのモーフの束) の型。VRM 1.0 の `expressions` と同じ形を、モーフのバインドだけ持つ。
/// @details 型だけで JSON にも AnimAsset にも依らない。ExpressionPose は 136 byte の POD で、GameMemory に置ける。

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <mitiru/animation/AnimNameKey.hpp>

namespace mitiru::animation
{

/// 口・まばたき・視線は、ほかの表情が打ち消せる (VRM の procedural)。
enum class MorphCategory : std::uint8_t
{
	None,
	Mouth,    ///< aa ih ou ee oh
	Blink,    ///< blink blinkLeft blinkRight
	LookAt,   ///< lookUp lookDown lookLeft lookRight
};

/// その表情の重みが 0 でないとき、同じ種類の表情をどうするか。
enum class MorphOverride : std::uint8_t
{
	None,
	Block,   ///< 対象の重みを 0 にする
	Blend,   ///< 対象の重みを (1 - 重みの和) 倍に弱める
};

/// 表情が動かすモーフ。node は glTF の nodes の番号、index はそのノードの mesh の何番目のモーフか、weight は表情が最大のときの値
struct MorphBind
{
	int node = -1;
	int index = 0;
	float weight = 1.0f;
};

struct MorphExpression
{
	std::string name;
	std::uint32_t key = 0;                              ///< animNameKey(name)
	bool isBinary = false;                              ///< 0.5 を超えれば 1、それ以外は 0
	MorphCategory category = MorphCategory::None;       ///< 自分がどの種類か (プリセット名で決まる。custom は JSON の "procedural")
	MorphOverride overrideMouth = MorphOverride::None;
	MorphOverride overrideBlink = MorphOverride::None;
	MorphOverride overrideLookAt = MorphOverride::None;
	std::vector<MorphBind> binds;
};

struct MorphExpressionSet
{
	std::vector<MorphExpression> expressions;

	[[nodiscard]] int find(std::uint32_t key) const noexcept
	{
		for (std::size_t i = 0; i < expressions.size(); ++i)
		{
			if (expressions[i].key == key) { return static_cast<int>(i); }
		}
		return -1;
	}
	[[nodiscard]] int find(std::string_view name) const noexcept { return find(animNameKey(name)); }
};

inline constexpr int kExpressionMaxEntries = 16;
/// 1 体のモーフのスロットのうち、ゲームの評価が扱う数。表情の顔は 50〜80 個で、これを超えたスロットの重みは 0 のまま
inline constexpr int kMorphMaxSlots = 128;

/// ゲームが毎フレーム決める表情の重み。名前の鍵 (animNameKey) と 0..1 の値の組。同じ鍵は 1 つにまとめる
struct ExpressionEntry
{
	std::uint32_t key = 0;
	float weight = 0.0f;
};

struct ExpressionPose
{
	std::uint32_t count = 0;
	std::uint32_t reserved = 0;
	ExpressionEntry entries[kExpressionMaxEntries] = {};
};

static_assert(sizeof(ExpressionEntry) == 8);
static_assert(sizeof(ExpressionPose) == 8 + 8 * kExpressionMaxEntries);
static_assert(std::is_trivially_copyable_v<ExpressionPose>);

/// @brief 表情の重みを置く (同じ鍵があれば上書き)。満杯なら false。weight が 0 以下なら項目を消す
inline bool setExpression(ExpressionPose& pose, std::uint32_t key, float weight) noexcept
{
	const auto count = pose.count < static_cast<std::uint32_t>(kExpressionMaxEntries) ? pose.count
	                                                                                  : static_cast<std::uint32_t>(kExpressionMaxEntries);
	for (std::uint32_t i = 0; i < count; ++i)
	{
		if (pose.entries[i].key != key) { continue; }
		if (weight > 0.0f) { pose.entries[i].weight = weight; return true; }
		pose.entries[i] = pose.entries[count - 1];
		pose.entries[count - 1] = {};
		pose.count = count - 1;
		return true;
	}
	if (!(weight > 0.0f)) { return true; }
	if (count >= static_cast<std::uint32_t>(kExpressionMaxEntries)) { return false; }
	pose.entries[count] = {key, weight};
	pose.count = count + 1;
	return true;
}

inline bool setExpression(ExpressionPose& pose, std::string_view name, float weight) noexcept
{
	return setExpression(pose, animNameKey(name), weight);
}

} // namespace mitiru::animation
