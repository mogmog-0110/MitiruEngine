#pragma once

/// @file Effects.hpp
/// @brief GraphWalkerビジュアルエフェクト（データモデル）
///
/// パーティクル・カメラシェイク・スクリーンフェード・ゾーンアナウンスの
/// データ構造と更新ロジックを提供する。描画は含まない。
///
/// @code
/// mitiru::gw::ParticleEmitter emitter;
/// emitter.emit({100.0f, 200.0f}, 10, NeonColor::player(), 150.0f, 1.0f);
/// emitter.update(dt);
/// for (const auto& p : emitter.getParticles()) { /* 描画 */ }
/// @endcode

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "mitiru/graphwalker/Common.hpp"

namespace mitiru::gw
{

/// @brief パーティクル1個分のデータ
struct Particle
{
	sgc::Vec2f position{};    ///< 位置
	sgc::Vec2f velocity{};    ///< 速度
	float lifetime = 0.0f;    ///< 残りライフタイム（秒）
	float maxLifetime = 1.0f; ///< 最大ライフタイム（秒）
	float size = 4.0f;        ///< サイズ（ピクセル）
	NeonColor color{};        ///< カラー
};

/// @brief パーティクルエミッター
///
/// パーティクルの生成・更新・死亡除去を行う。
/// 描画ロジックは含まないため、テスト可能。
class ParticleEmitter
{
public:
	/// @brief パーティクルを発射する
	/// @param pos 発生位置
	/// @param count 発生数
	/// @param color カラー
	/// @param speed 初速（ピクセル/秒）
	/// @param lifetime 寿命（秒）
	void emit(sgc::Vec2f pos, int count, NeonColor color, float speed, float lifetime)
	{
		for (int i = 0; i < count; ++i)
		{
			// 放射状に分散（均等角度）
			const float angle = (2.0f * 3.14159265f * static_cast<float>(i)) / static_cast<float>(count);
			const float vx = speed * std::cos(angle);
			const float vy = speed * std::sin(angle);

			Particle p;
			p.position = pos;
			p.velocity = sgc::Vec2f{vx, vy};
			p.lifetime = lifetime;
			p.maxLifetime = lifetime;
			p.size = 4.0f;
			p.color = color;
			m_particles.push_back(p);
		}
	}

	/// @brief フレーム更新（位置積分・死亡除去）
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		for (auto& p : m_particles)
		{
			p.position += p.velocity * dt;
			p.lifetime -= dt;
		}

		// 死亡パーティクルを除去
		m_particles.erase(
			std::remove_if(m_particles.begin(), m_particles.end(),
				[](const Particle& p) { return p.lifetime <= 0.0f; }),
			m_particles.end()
		);
	}

	/// @brief 現在のパーティクル一覧を取得する
	/// @return パーティクルのconst参照
	[[nodiscard]] const std::vector<Particle>& getParticles() const { return m_particles; }

	/// @brief 全パーティクルをクリアする
	void clear() { m_particles.clear(); }

private:
	std::vector<Particle> m_particles; ///< アクティブなパーティクル
};

/// @brief カメラエフェクトのデータ
struct CameraEffect
{
	float shakeIntensity = 0.0f;  ///< シェイク強度（ピクセル）
	float shakeDuration = 0.0f;   ///< シェイク全体の持続時間（秒）
	float shakeTimer = 0.0f;      ///< シェイク残り時間（秒）
	float zoomTarget = 1.0f;      ///< ズーム目標値
	float zoomSpeed = 2.0f;       ///< ズーム遷移速度

	/// @brief シェイクを開始する
	/// @param intensity 強度（ピクセル）
	/// @param duration 持続時間（秒）
	void startShake(float intensity, float duration)
	{
		shakeIntensity = intensity;
		shakeDuration = duration;
		shakeTimer = duration;
	}

	/// @brief シェイクを更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		if (shakeTimer > 0.0f)
		{
			shakeTimer = std::max(0.0f, shakeTimer - dt);
		}
	}

	/// @brief 現在のシェイク強度を取得する（時間に応じて減衰）
	/// @return 現在の強度
	[[nodiscard]] float getCurrentIntensity() const
	{
		if (shakeDuration <= 0.0f || shakeTimer <= 0.0f)
		{
			return 0.0f;
		}
		return shakeIntensity * (shakeTimer / shakeDuration);
	}

	/// @brief シェイク中か判定する
	/// @return シェイク中ならtrue
	[[nodiscard]] bool isShaking() const { return shakeTimer > 0.0f; }
};

/// @brief スクリーンフェードのデータ
struct ScreenFade
{
	float alpha = 0.0f;       ///< 現在のアルファ値（0=透明, 1=不透明）
	float targetAlpha = 0.0f; ///< 目標アルファ値
	float fadeSpeed = 1.0f;   ///< フェード速度（秒あたりの変化量）
	bool isFading = false;    ///< フェード中フラグ

	/// @brief フェードインを開始する（暗→明）
	/// @param speed フェード速度
	void fadeIn(float speed = 1.0f)
	{
		targetAlpha = 0.0f;
		fadeSpeed = speed;
		isFading = true;
	}

	/// @brief フェードアウトを開始する（明→暗）
	/// @param speed フェード速度
	void fadeOut(float speed = 1.0f)
	{
		targetAlpha = 1.0f;
		fadeSpeed = speed;
		isFading = true;
	}

	/// @brief フェードを更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		if (!isFading)
		{
			return;
		}

		if (alpha < targetAlpha)
		{
			alpha = std::min(alpha + fadeSpeed * dt, targetAlpha);
		}
		else if (alpha > targetAlpha)
		{
			alpha = std::max(alpha - fadeSpeed * dt, targetAlpha);
		}

		if (std::abs(alpha - targetAlpha) < 0.001f)
		{
			alpha = targetAlpha;
			isFading = false;
		}
	}
};

/// @brief ゾーンアナウンスメント表示データ
struct ZoneAnnouncement
{
	std::string zoneName;        ///< 表示するゾーン名
	NeonColor color{};           ///< 表示カラー
	float displayTime = 2.0f;    ///< 表示時間（秒）
	float timer = 0.0f;          ///< 残り時間（秒）
	bool isShowing = false;      ///< 表示中フラグ

	/// @brief アナウンスを表示開始する
	/// @param name ゾーン名
	/// @param c 表示カラー
	/// @param duration 表示時間（秒）
	void show(const std::string& name, NeonColor c, float duration = 2.0f)
	{
		zoneName = name;
		color = c;
		displayTime = duration;
		timer = duration;
		isShowing = true;
	}

	/// @brief アナウンスを更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		if (!isShowing)
		{
			return;
		}

		timer -= dt;
		if (timer <= 0.0f)
		{
			timer = 0.0f;
			isShowing = false;
		}
	}

	/// @brief 表示の進行度を取得する（1=表示開始, 0=終了）
	/// @return 進行度
	[[nodiscard]] float getProgress() const
	{
		if (displayTime <= 0.0f)
		{
			return 0.0f;
		}
		return timer / displayTime;
	}
};

} // namespace mitiru::gw
