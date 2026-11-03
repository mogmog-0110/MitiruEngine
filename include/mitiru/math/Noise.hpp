#pragma once

/// @file Noise.hpp
/// @brief FastNoiseLite (external/FastNoiseLite、MIT) の薄い包み。simplex、cellular、value、domain warp。
/// @details 純関数。同じ設定と同じ座標なら、同じビルドの中では bit 単位で同じ値を返す (TestNoise.cpp)。
///          float の計算なので、別のコンパイラや別の CPU との一致は保証しない。シミュレーションの状態に
///          使うときは、replay と同じ「同じビルド」の範囲でだけ再現すると考える (docs/NOISE.md)。
///          従来の PerlinNoise.hpp はそのまま残す (値も実装も別物)。
///
/// @code
/// mitiru::noise::Noise n({.kind = mitiru::noise::Kind::OpenSimplex2, .seed = 42, .frequency = 0.05f});
/// float v = n.at(x, y);       // [-1, 1]
/// float h = n.at01(x, y, z);  // [0, 1]
/// @endcode

#include <FastNoiseLite/FastNoiseLite.h>

#include <algorithm>
#include <cstdint>

namespace mitiru::noise
{

enum class Kind : std::uint8_t { OpenSimplex2, OpenSimplex2S, Cellular, Perlin, ValueCubic, Value };
enum class Fractal : std::uint8_t { None, FBm, Ridged, PingPong };
enum class CellularDistance : std::uint8_t { Euclidean, EuclideanSq, Manhattan, Hybrid };
enum class CellularReturn : std::uint8_t
{
	CellValue, Distance, Distance2, Distance2Add, Distance2Sub, Distance2Mul, Distance2Div
};
enum class WarpKind : std::uint8_t { OpenSimplex2, OpenSimplex2Reduced, BasicGrid };
enum class WarpFractal : std::uint8_t { None, Progressive, Independent };

struct Settings
{
	Kind kind = Kind::OpenSimplex2;
	int seed = 1337;
	float frequency = 0.01f;
	Fractal fractal = Fractal::None;
	int octaves = 3;
	float lacunarity = 2.0f;
	float gain = 0.5f;
	float weightedStrength = 0.0f;  // 0 で従来の fBm。上げると低いオクターブが高いオクターブを弱める
	float pingPongStrength = 2.0f;
	CellularDistance cellularDistance = CellularDistance::EuclideanSq;
	CellularReturn cellularReturn = CellularReturn::Distance;
	float cellularJitter = 1.0f;
};

struct WarpSettings
{
	WarpKind kind = WarpKind::OpenSimplex2;
	int seed = 1337;
	float frequency = 0.01f;
	float amplitude = 1.0f;
	WarpFractal fractal = WarpFractal::None;
	int octaves = 3;
	float lacunarity = 2.0f;
	float gain = 0.5f;
};

class Noise
{
public:
	Noise() : Noise(Settings{}) {}

	explicit Noise(const Settings& s)
	{
		m_impl.SetSeed(s.seed);
		m_impl.SetFrequency(s.frequency);
		m_impl.SetNoiseType(static_cast<FastNoiseLite::NoiseType>(s.kind));
		m_impl.SetFractalType(static_cast<FastNoiseLite::FractalType>(s.fractal));
		m_impl.SetFractalOctaves(std::max(1, s.octaves));
		m_impl.SetFractalLacunarity(s.lacunarity);
		m_impl.SetFractalGain(s.gain);
		m_impl.SetFractalWeightedStrength(s.weightedStrength);
		m_impl.SetFractalPingPongStrength(s.pingPongStrength);
		m_impl.SetCellularDistanceFunction(static_cast<FastNoiseLite::CellularDistanceFunction>(s.cellularDistance));
		m_impl.SetCellularReturnType(static_cast<FastNoiseLite::CellularReturnType>(s.cellularReturn));
		m_impl.SetCellularJitter(s.cellularJitter);
	}

	/// @return ほぼ [-1, 1]。丸めで 1 ulp ほど外へ出ることがある (Ridged の原点で 1.00000012)
	[[nodiscard]] float at(float x, float y) const noexcept { return m_impl.GetNoise(x, y); }
	[[nodiscard]] float at(float x, float y, float z) const noexcept { return m_impl.GetNoise(x, y, z); }
	/// @return [0, 1] に収める
	[[nodiscard]] float at01(float x, float y) const noexcept { return to01(at(x, y)); }
	[[nodiscard]] float at01(float x, float y, float z) const noexcept { return to01(at(x, y, z)); }

private:
	static float to01(float v) noexcept { return std::clamp((v + 1.0f) * 0.5f, 0.0f, 1.0f); }

	FastNoiseLite m_impl;
};

/// @brief 座標をずらす (domain warp)。ずらした座標を Noise::at へ渡すと、流れるような模様になる。
class Warp
{
public:
	Warp() : Warp(WarpSettings{}) {}

	explicit Warp(const WarpSettings& s)
	{
		m_impl.SetSeed(s.seed);
		m_impl.SetFrequency(s.frequency);
		m_impl.SetDomainWarpType(static_cast<FastNoiseLite::DomainWarpType>(s.kind));
		m_impl.SetDomainWarpAmp(s.amplitude);
		m_impl.SetFractalType(fractalOf(s.fractal));
		m_impl.SetFractalOctaves(std::max(1, s.octaves));
		m_impl.SetFractalLacunarity(s.lacunarity);
		m_impl.SetFractalGain(s.gain);
	}

	void apply(float& x, float& y) const noexcept { m_impl.DomainWarp(x, y); }
	void apply(float& x, float& y, float& z) const noexcept { m_impl.DomainWarp(x, y, z); }

private:
	// warp の fractal は FastNoiseLite の FractalType の後ろ 2 つに割り当てられている
	static FastNoiseLite::FractalType fractalOf(WarpFractal f) noexcept
	{
		switch (f)
		{
		case WarpFractal::Progressive: return FastNoiseLite::FractalType_DomainWarpProgressive;
		case WarpFractal::Independent: return FastNoiseLite::FractalType_DomainWarpIndependent;
		default: return FastNoiseLite::FractalType_None;
		}
	}

	FastNoiseLite m_impl;
};

} // namespace mitiru::noise
