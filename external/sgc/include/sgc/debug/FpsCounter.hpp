#pragma once

/// @file FpsCounter.hpp
/// @brief FPSカウンター
///
/// フレーム毎にupdate()を呼ぶことで、FPSを計算する。
/// 指定間隔ごとに平均FPSを更新する。
///
/// @code
/// sgc::debug::FpsCounter fps;
/// // ゲームループ内で毎フレーム呼ぶ
/// fps.update(deltaTime);
/// float currentFps = fps.fps();
/// float msPerFrame = fps.frameTimeMs();
/// @endcode

namespace sgc::debug
{

/// @brief FPSカウンター
///
/// フレーム毎にupdate()を呼ぶことで、指定間隔ごとに
/// 平均FPSとフレーム時間を計算する。
class FpsCounter
{
public:
	/// @brief コンストラクタ
	/// @param updateInterval FPS更新間隔（秒、デフォルト: 0.5秒）
	explicit constexpr FpsCounter(float updateInterval = 0.5f) noexcept
		: m_updateInterval{updateInterval}
	{}

	/// @brief フレーム更新（毎フレーム呼ぶ）
	/// @param deltaTime フレーム経過時間（秒）
	void update(float deltaTime) noexcept
	{
		++m_frameCount;
		m_elapsed += deltaTime;

		if (m_elapsed >= m_updateInterval)
		{
			m_fps = static_cast<float>(m_frameCount) / m_elapsed;
			m_frameTime = m_elapsed / static_cast<float>(m_frameCount);
			m_frameCount = 0;
			m_elapsed = 0.0f;
		}
	}

	/// @brief 現在のFPSを取得する
	/// @return FPS値
	[[nodiscard]] float fps() const noexcept { return m_fps; }

	/// @brief 平均フレーム時間を取得する（秒）
	/// @return フレーム時間（秒）
	[[nodiscard]] float frameTime() const noexcept { return m_frameTime; }

	/// @brief 平均フレーム時間をミリ秒で取得する
	/// @return フレーム時間（ミリ秒）
	[[nodiscard]] float frameTimeMs() const noexcept { return m_frameTime * 1000.0f; }

	/// @brief リセットする
	void reset() noexcept
	{
		m_fps = 0.0f;
		m_frameTime = 0.0f;
		m_frameCount = 0;
		m_elapsed = 0.0f;
	}

private:
	float m_updateInterval;       ///< FPS更新間隔（秒）
	float m_fps{0.0f};            ///< 現在のFPS
	float m_frameTime{0.0f};      ///< 平均フレーム時間（秒）
	int m_frameCount{0};          ///< 現在の計測期間内のフレーム数
	float m_elapsed{0.0f};        ///< 現在の計測期間の経過時間（秒）
};

} // namespace sgc::debug
