// Renderer3D_DX12 の天候描画の実装。DX12PipelineStates.hpp から include される。
// 濡れは描画直前に材質と色を調整し、PS は変更しない (WetSurface.hpp)。
// 雲は CbAtmosphere の cloud に量と時刻を渡す。
// どちらも 0 のフレームは画素を変えない。

public:

/// @brief SceneLook::wetness (ADR 0099)。0 で従来の絵になる。drawMeshEx の入口で、以後の描画に濡れを適用する
void applyWetness(std::uint8_t wetness) noexcept
{
	m_wetness = wetAmount(wetness);
	if (wetness != 0) { features::use(features::Feature::WetSurface); }
}

/// @brief SceneLook::cloudCover (ADR 0099)。0 で雲なし。物理ベースの空 (SkyLook::enabled) にだけ描く
void applyCloudCover(std::uint8_t cloudCover) noexcept
{
	m_cloudCover = static_cast<float>(cloudCover) * (1.0f / 255.0f);
	if (cloudCover != 0) { features::use(features::Feature::CloudLayer); }
}

private:

float m_wetness    = 0.0f;
float m_cloudCover = 0.0f;

/// 雲の定数。x は量 (0..1)、y は動きの時刻 (秒)。
/// リプレイで同じ絵にするため、時刻は決定論のフレーム番号から決め、壁時計は使わない。
void fillCloudParams(float out[4]) const noexcept
{
	constexpr float kFramesPerSecond = 60.0f;
	out[0] = m_cloudCover;
	out[1] = static_cast<float>(m_colorFinishSeed) * (1.0f / kFramesPerSecond);
	out[2] = 0.0f;
	out[3] = 0.0f;
}
