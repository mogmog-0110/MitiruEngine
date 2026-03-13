#pragma once

/// @file Zone.hpp
/// @brief GraphWalker ゾーン（エリア）定義・ルックアップ
///
/// 全8ゾーンの情報を提供する。各ゾーンは固有のカラーテーマ、
/// BGMパラメータ、および論理座標でのバウンディングボックスを持つ。
///
/// @code
/// auto info = mitiru::gw::getZoneInfo(mitiru::gw::ZoneId::Tutorial);
/// auto zone = mitiru::gw::getZoneAt({-40.0f, 5.0f});
/// @endcode

#include <optional>
#include <vector>

#include "mitiru/graphwalker/Common.hpp"

namespace mitiru::gw
{

/// @brief ゾーンIDからゾーン情報を取得する
/// @param id ゾーンID
/// @return 対応するZoneInfo
[[nodiscard]] inline ZoneInfo getZoneInfo(ZoneId id)
{
	/// ゾーン境界はworldmap.jsonの [x, y, w, h] 形式から
	/// boundsMin = (x, y), boundsMax = (x+w, y+h) に変換
	switch (id)
	{
	case ZoneId::Tutorial:
		return {
			ZoneId::Tutorial,
			"Tutorial",
			NeonColor::zonePrimary(ZoneId::Tutorial),
			NeonColor::zoneSecondary(ZoneId::Tutorial),
			0.8f,
			{ -48.0f, -5.0f },
			{ -13.0f, 13.0f }
		};
	case ZoneId::Linear:
		return {
			ZoneId::Linear,
			"Linear Plains",
			NeonColor::zonePrimary(ZoneId::Linear),
			NeonColor::zoneSecondary(ZoneId::Linear),
			1.0f,
			{ -15.0f, -10.0f },
			{ 15.0f, 15.0f }
		};
	case ZoneId::AbsValue:
		return {
			ZoneId::AbsValue,
			"Absolute Valley",
			NeonColor::zonePrimary(ZoneId::AbsValue),
			NeonColor::zoneSecondary(ZoneId::AbsValue),
			0.9f,
			{ -15.0f, -30.0f },
			{ 25.0f, -8.0f }
		};
	case ZoneId::Trig:
		return {
			ZoneId::Trig,
			"Trig Ocean",
			NeonColor::zonePrimary(ZoneId::Trig),
			NeonColor::zoneSecondary(ZoneId::Trig),
			0.8f,
			{ 13.0f, -5.0f },
			{ 48.0f, 15.0f }
		};
	case ZoneId::Parabola:
		return {
			ZoneId::Parabola,
			"Parabola Hills",
			NeonColor::zonePrimary(ZoneId::Parabola),
			NeonColor::zoneSecondary(ZoneId::Parabola),
			1.1f,
			{ 46.0f, -5.0f },
			{ 76.0f, 15.0f }
		};
	case ZoneId::Exponential:
		return {
			ZoneId::Exponential,
			"Exponential Tower",
			NeonColor::zonePrimary(ZoneId::Exponential),
			NeonColor::zoneSecondary(ZoneId::Exponential),
			1.3f,
			{ 35.0f, 13.0f },
			{ 60.0f, 43.0f }
		};
	case ZoneId::Logarithm:
		return {
			ZoneId::Logarithm,
			"Logarithm Cave",
			NeonColor::zonePrimary(ZoneId::Logarithm),
			NeonColor::zoneSecondary(ZoneId::Logarithm),
			0.7f,
			{ 46.0f, -28.0f },
			{ 76.0f, -3.0f }
		};
	case ZoneId::Chaos:
		return {
			ZoneId::Chaos,
			"Chaos Sanctum",
			NeonColor::zonePrimary(ZoneId::Chaos),
			NeonColor::zoneSecondary(ZoneId::Chaos),
			1.5f,
			{ 50.0f, -50.0f },
			{ 75.0f, -26.0f }
		};
	default:
		return {
			ZoneId::Tutorial,
			"Unknown",
			NeonColor::zonePrimary(ZoneId::Tutorial),
			NeonColor::zoneSecondary(ZoneId::Tutorial),
			1.0f,
			{ 0.0f, 0.0f },
			{ 0.0f, 0.0f }
		};
	}
}

/// @brief 全ゾーンの情報を取得する
/// @return 全8ゾーンのZoneInfoベクタ
[[nodiscard]] inline std::vector<ZoneInfo> getAllZones()
{
	std::vector<ZoneInfo> zones;
	zones.reserve(ZONE_COUNT);
	zones.push_back(getZoneInfo(ZoneId::Tutorial));
	zones.push_back(getZoneInfo(ZoneId::Linear));
	zones.push_back(getZoneInfo(ZoneId::AbsValue));
	zones.push_back(getZoneInfo(ZoneId::Trig));
	zones.push_back(getZoneInfo(ZoneId::Parabola));
	zones.push_back(getZoneInfo(ZoneId::Exponential));
	zones.push_back(getZoneInfo(ZoneId::Logarithm));
	zones.push_back(getZoneInfo(ZoneId::Chaos));
	return zones;
}

/// @brief 指定位置が属するゾーンを判定する
/// @param position 論理座標
/// @return ゾーンID（どのゾーンにも属さない場合nullopt）
[[nodiscard]] inline std::optional<ZoneId> getZoneAt(sgc::Vec2f position)
{
	const auto zones = getAllZones();
	for (const auto& zone : zones)
	{
		if (position.x >= zone.boundsMin.x &&
			position.x <= zone.boundsMax.x &&
			position.y >= zone.boundsMin.y &&
			position.y <= zone.boundsMax.y)
		{
			return zone.id;
		}
	}
	return std::nullopt;
}

} // namespace mitiru::gw
