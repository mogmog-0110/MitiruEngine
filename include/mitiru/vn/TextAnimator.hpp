#pragma once

/// @file TextAnimator.hpp
/// @brief テキストのパーキャラクターアニメーションエフェクト
/// @details シェイク・ウェーブ・フェード・タイプライター等のエフェクトを
///          文字単位で適用するアニメーションシステム。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace mitiru::vn
{

/// @brief テキストアニメーションの種類
enum class TextEffect : std::uint8_t
{
	None,        ///< エフェクトなし
	Typewriter,  ///< タイプライター（1文字ずつ表示）
	Instant,     ///< 即座に全表示
	Shake,       ///< ランダムな振動
	Wave,        ///< 正弦波による上下揺れ
	Fade,        ///< フェードインによる文字出現
};

/// @brief アニメーションエフェクトのパラメータ
struct EffectParams
{
	float shakeAmplitude = 2.0f;   ///< シェイクの最大振幅（ピクセル）
	float shakeFrequency = 30.0f;  ///< シェイクの周波数（Hz）
	float waveAmplitude = 3.0f;    ///< ウェーブの振幅（ピクセル）
	float waveFrequency = 2.0f;    ///< ウェーブの周波数（Hz）
	float wavePhaseOffset = 0.3f;  ///< ウェーブの文字間位相差（ラジアン）
	float fadeDuration = 0.2f;     ///< フェードインの持続時間（秒）
	float revealSpeed = 20.0f;     ///< タイプライターの文字表示速度（文字/秒）
};

/// @brief 1文字分のトランスフォームオフセット
struct CharTransform
{
	float offsetX = 0.0f;   ///< X方向オフセット（ピクセル）
	float offsetY = 0.0f;   ///< Y方向オフセット（ピクセル）
	float alpha = 1.0f;     ///< アルファ値（0.0〜1.0）
	float scale = 1.0f;     ///< スケール値
	bool visible = true;     ///< 表示フラグ
};

/// @brief パーキャラクターアニメーションの状態
struct CharAnimState
{
	TextEffect effect = TextEffect::None; ///< 適用中のエフェクト
	float revealTime = 0.0f;              ///< この文字が表示された時刻
	bool revealed = false;                ///< 表示済みか
};

/// @brief テキストアニメーター
/// @details 文字列の各文字にアニメーションエフェクトを適用する。
///          update()で時間を進め、getTransform()で各文字のトランスフォームを取得する。
///
/// @code
/// mitiru::vn::TextAnimator animator;
/// animator.reset(10); // 10文字
/// animator.setEffect(mitiru::vn::TextEffect::Wave);
/// animator.setRevealMode(mitiru::vn::TextEffect::Typewriter);
///
/// // 毎フレーム更新
/// animator.update(deltaTime);
///
/// // 各文字のトランスフォームを取得
/// for (std::size_t i = 0; i < 10; ++i)
/// {
///     auto transform = animator.getTransform(i);
///     // transform.offsetX, offsetY, alpha を使って描画
/// }
/// @endcode
class TextAnimator
{
	std::vector<CharAnimState> m_charStates;
	EffectParams m_params;
	TextEffect m_revealMode = TextEffect::Typewriter;
	TextEffect m_persistentEffect = TextEffect::None;
	float m_elapsed = 0.0f;
	std::size_t m_revealedCount = 0;
	std::size_t m_totalChars = 0;
	float m_revealAccumulator = 0.0f;

	// 待機制御
	std::vector<float> m_charWaitTimes; ///< 各文字の追加待機時間
	float m_waitAccumulator = 0.0f;     ///< 現在の待機残り時間

public:
	/// @brief デフォルトコンストラクタ
	TextAnimator() = default;

	/// @brief 文字数を指定してアニメーションをリセットする
	/// @param charCount 文字数
	void reset(std::size_t charCount)
	{
		m_totalChars = charCount;
		m_revealedCount = 0;
		m_elapsed = 0.0f;
		m_revealAccumulator = 0.0f;
		m_waitAccumulator = 0.0f;

		m_charStates.clear();
		m_charStates.resize(charCount);

		m_charWaitTimes.clear();
		m_charWaitTimes.resize(charCount, 0.0f);

		if (m_revealMode == TextEffect::Instant)
		{
			revealAll();
		}
	}

	/// @brief エフェクトパラメータを設定する
	/// @param params パラメータ
	void setParams(const EffectParams& params) noexcept
	{
		m_params = params;
	}

	/// @brief エフェクトパラメータを取得する
	[[nodiscard]] const EffectParams& params() const noexcept
	{
		return m_params;
	}

	/// @brief 文字表示モードを設定する
	/// @param mode Typewriter または Instant
	void setRevealMode(TextEffect mode) noexcept
	{
		m_revealMode = mode;
	}

	/// @brief 表示後の持続エフェクトを設定する
	/// @param effect Shake, Wave, None など
	void setPersistentEffect(TextEffect effect) noexcept
	{
		m_persistentEffect = effect;
	}

	/// @brief 特定文字にエフェクトを設定する
	/// @param index 文字インデックス
	/// @param effect エフェクト
	void setCharEffect(std::size_t index, TextEffect effect)
	{
		if (index < m_charStates.size())
		{
			m_charStates[index].effect = effect;
		}
	}

	/// @brief 特定文字に待機時間を設定する
	/// @param index 文字インデックス
	/// @param waitSeconds 待機時間（秒）
	void setCharWaitTime(std::size_t index, float waitSeconds)
	{
		if (index < m_charWaitTimes.size())
		{
			m_charWaitTimes[index] = waitSeconds;
		}
	}

	/// @brief 文字表示速度を設定する
	/// @param charsPerSecond 1秒あたりの文字数
	void setRevealSpeed(float charsPerSecond) noexcept
	{
		m_params.revealSpeed = charsPerSecond;
	}

	/// @brief アニメーションを更新する
	/// @param dt デルタタイム（秒）
	void update(float dt) noexcept
	{
		m_elapsed += dt;

		// タイプライター表示の更新
		if (m_revealMode == TextEffect::Typewriter && m_revealedCount < m_totalChars)
		{
			// 待機中の場合
			if (m_waitAccumulator > 0.0f)
			{
				m_waitAccumulator -= dt;
				if (m_waitAccumulator > 0.0f)
				{
					return;
				}
				// 待機完了、残り時間をアキュムレータに加算
				dt = -m_waitAccumulator;
				m_waitAccumulator = 0.0f;
			}

			m_revealAccumulator += dt * m_params.revealSpeed;

			while (m_revealAccumulator >= 1.0f && m_revealedCount < m_totalChars)
			{
				m_revealAccumulator -= 1.0f;
				m_charStates[m_revealedCount].revealed = true;
				m_charStates[m_revealedCount].revealTime = m_elapsed;

				// 次の文字に待機時間がある場合
				if (m_revealedCount < m_charWaitTimes.size()
					&& m_charWaitTimes[m_revealedCount] > 0.0f)
				{
					m_waitAccumulator = m_charWaitTimes[m_revealedCount];
				}

				++m_revealedCount;

				// 待機が挟まった場合は即座にループを抜ける
				if (m_waitAccumulator > 0.0f)
				{
					m_revealAccumulator = 0.0f;
					break;
				}
			}
		}
	}

	/// @brief 指定文字のトランスフォームを取得する
	/// @param index 文字インデックス
	/// @return トランスフォーム情報
	[[nodiscard]] CharTransform getTransform(std::size_t index) const noexcept
	{
		CharTransform result;

		if (index >= m_totalChars)
		{
			result.visible = false;
			return result;
		}

		const auto& state = m_charStates[index];

		// 未表示の文字
		if (!state.revealed)
		{
			result.visible = false;
			result.alpha = 0.0f;
			return result;
		}

		// フェードイン計算
		const float timeSinceReveal = m_elapsed - state.revealTime;

		// 文字個別エフェクト（パーサーが設定したもの）
		const TextEffect charEffect =
			(state.effect != TextEffect::None) ? state.effect : m_persistentEffect;

		switch (charEffect)
		{
		case TextEffect::Shake:
			applyShake(result, index, m_elapsed);
			break;

		case TextEffect::Wave:
			applyWave(result, index, m_elapsed);
			break;

		case TextEffect::Fade:
			if (m_params.fadeDuration > 0.0f)
			{
				result.alpha = std::clamp(timeSinceReveal / m_params.fadeDuration, 0.0f, 1.0f);
			}
			break;

		default:
			break;
		}

		return result;
	}

	/// @brief 全文字のトランスフォームを一括取得する
	/// @return 文字数分のトランスフォーム配列
	[[nodiscard]] std::vector<CharTransform> getAllTransforms() const
	{
		std::vector<CharTransform> result;
		result.reserve(m_totalChars);
		for (std::size_t i = 0; i < m_totalChars; ++i)
		{
			result.push_back(getTransform(i));
		}
		return result;
	}

	/// @brief 表示済み文字数を取得する
	[[nodiscard]] std::size_t revealedCount() const noexcept { return m_revealedCount; }

	/// @brief 全文字数を取得する
	[[nodiscard]] std::size_t totalChars() const noexcept { return m_totalChars; }

	/// @brief 全文字が表示済みか
	[[nodiscard]] bool isComplete() const noexcept
	{
		return m_revealedCount >= m_totalChars;
	}

	/// @brief 全文字を即座に表示する
	void revealAll() noexcept
	{
		for (auto& state : m_charStates)
		{
			if (!state.revealed)
			{
				state.revealed = true;
				state.revealTime = m_elapsed;
			}
		}
		m_revealedCount = m_totalChars;
		m_waitAccumulator = 0.0f;
	}

	/// @brief 残り文字を即座にスキップして表示完了する
	void skipToEnd() noexcept
	{
		revealAll();
	}

	/// @brief 経過時間を取得する
	[[nodiscard]] float elapsed() const noexcept { return m_elapsed; }

private:
	/// @brief シェイクエフェクトを適用する
	void applyShake(CharTransform& transform, std::size_t index, float time) const noexcept
	{
		// 文字ごとに異なるシードでハッシュベースの疑似乱数
		const float seed = static_cast<float>(index) * 7.31f + time * m_params.shakeFrequency;
		const float noiseX = std::sin(seed * 13.37f) * std::cos(seed * 7.13f);
		const float noiseY = std::cos(seed * 11.29f) * std::sin(seed * 5.87f);
		transform.offsetX = noiseX * m_params.shakeAmplitude;
		transform.offsetY = noiseY * m_params.shakeAmplitude;
	}

	/// @brief ウェーブエフェクトを適用する
	void applyWave(CharTransform& transform, std::size_t index, float time) const noexcept
	{
		const float phase = static_cast<float>(index) * m_params.wavePhaseOffset;
		const float pi2 = 6.28318530718f;
		transform.offsetY = std::sin(time * m_params.waveFrequency * pi2 + phase)
			* m_params.waveAmplitude;
	}
};

} // namespace mitiru::vn
