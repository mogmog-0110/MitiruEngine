#pragma once

/// @file ToonStyle.hpp
/// @brief 描画ごとのトゥーンの塗りと輪郭線 (ADR 0079)。Screen::toonStyle3D で渡すと、以後の描画に効く
/// @details 段・縁の帯・つやと、その境のぼかしは陰影がトゥーンの時だけ効く。金属は bands >= 1 の描画だけに効き、bands == 0 と drawModel の .clod では無視する。
///          線と止め絵の印はどの陰影の描画にも効く。
///          色は書いた sRGB で渡す (bandTint・rimColor・glossColor は塗った色と同じく線形にしてから掛ける)。

#include <cstdint>
#include <type_traits>

namespace mitiru::render
{

/// @brief ToonStyle::flags。止め絵 (ImpactFrame) の間も、この描画の画素は色のまま残す
inline constexpr std::uint32_t kToonKeepColorInImpact = 1u;

/// @brief トゥーンの解析的な金属反射 (64 byte の POD)
struct ToonMetal
{
	/// @brief 金属の割合。0 で金属なし、1 で拡散をすべて反射に替える
	float amount = 0.0f;
	/// @brief 反射を塗った色で染める割合
	float tint = 0.8f;
	/// @brief 反射をその画素の段の色で暗くする割合
	float shadowFollow = 0.5f;
	/// @brief 環境のグラデーションを刻む段数。0 は滑らか、1..8 は段付き
	std::uint32_t bands = 3;
	/// @brief 反射ベクトルの y で空と地面を分ける高さ
	float horizonHeight = 0.0f;
	/// @brief 地平の境の幅
	float horizonSoft = 0.03f;
	float sky[3] = {0.78f, 0.86f, 1.0f};       ///< 真上の色 (sRGB)
	float horizon[3] = {1.0f, 1.0f, 1.0f};     ///< 地平の色 (sRGB)
	float ground[3] = {0.12f, 0.1f, 0.16f};    ///< 地平のすぐ下の色 (sRGB)
	float reserved = 0.0f;
};

static_assert(sizeof(ToonMetal) == 64 && std::is_trivially_copyable_v<ToonMetal> && std::is_standard_layout_v<ToonMetal>,
              "ToonMetal は 64 byte の POD");

/// @brief 1 回の描画 (と以後の描画) の段・縁の帯・つや・輪郭線・金属 (224 byte の POD)
struct ToonStyle
{
	/// @brief 段の数 1..4。0 は塗りを場面の値 (setToonRamp と toon3D) のままにし、線と flags だけを使う
	std::uint32_t bands = 2;
	/// @brief 段の境の N·L。暗い側から昇順に bands - 1 個を使う
	float thresholds[3] = {0.5f, 0.0f, 0.0f};
	/// @brief 境の幅 (N·L の単位)。0 でも画素 1 個ぶんの幅で切るので、境はぎざぎざにならない
	float softness = 0.0f;
	/// @brief 暗い段から順に、塗った色へ掛ける色
	float bandTint[4][3] = {{0.60f, 0.64f, 0.76f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}};
	/// @brief 落ちた影の中で下げる段の数。0 は一番暗い段まで落とす
	std::uint32_t shadowSteps = 0;
	/// @brief N·V がこれ未満の所を縁の帯にする。0 で帯なし
	float rimMaxNdotV = 0.0f;
	/// @brief 帯にするのは、落ちた影も含めた N·L がこれ未満の所だけ。1 で向きを問わない
	float rimMaxNdotL = 1.0f;
	float rimColor[3] = {1.0f, 1.0f, 1.0f};
	/// @brief N·H がこれを超える所をつやにする。0 でつやなし。落ちた影の中では出さない
	float glossMinNdotH = 0.0f;
	float glossColor[3] = {1.0f, 1.0f, 1.0f};
	/// @brief 線の色。画面に出る色そのもの
	float outlineColor[3] = {0.1f, 0.08f, 0.06f};
	/// @brief 線の色の割合。0.7 で下の色 30% と線の色 70% を混ぜる
	float outlineMix = 1.0f;
	/// @brief 縦 1080 画素の画面での線の太さ。0 で線を引かない。画面の高さに比例して太さを変える
	float outlineWidthPx = 0.0f;
	/// @brief outlineFarDistance より遠い線の太さ (1080p)。0 で細らせない
	float outlineFarWidthPx = 0.0f;
	/// @brief 目からこの距離 (m) より遠い線を outlineFarWidthPx にする
	float outlineFarDistance = 0.0f;
	/// @brief この距離から線を薄くし始める
	float outlineFadeStart = 0.0f;
	/// @brief この距離で線が消える。outlineFadeStart 以下なら薄くしない
	float outlineFadeEnd = 0.0f;
	std::uint32_t flags = 0;   ///< kToonKeepColorInImpact
	/// @brief つやの強さ。0..1 は塗った色との混合、1 を超えた分はつやの色を加算する
	float glossStrength = 1.0f;
	/// @brief つやの帯の縁をぼかす全幅 (N·H の単位)。0 は画素 1 個ぶんの幅を使う
	float glossSoftness = 0.0f;
	/// @brief 縁の帯の縁をぼかす全幅 (N·V の単位)。0 は画素 1 個ぶんの幅を使う
	float rimSoftness = 0.0f;
	ToonMetal metal;
};

static_assert(sizeof(ToonStyle) == 224 && std::is_trivially_copyable_v<ToonStyle> && std::is_standard_layout_v<ToonStyle>,
              "ToonStyle は 224 byte の POD");

} // namespace mitiru::render
