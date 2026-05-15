#pragma once

/// @file TransitionEngine.hpp
/// @brief ビジュアルノベル用トランジションエンジン
/// @details シーン切り替え・背景遷移・キャラクター変化に使用する
///          多彩なビジュアルトランジション効果を提供する。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numbers>
#include <string>
#include <vector>

#include <mitiru/vn/EasingFunctions.hpp>

namespace mitiru::vn
{

/// @brief トランジションの種類
enum class TransitionType
{
	Fade,        ///< アルファフェード（黒・白・カスタムカラー）
	Dissolve,    ///< クロスディゾルブ（旧→新のブレンド）
	WipeLeft,    ///< 左方向ワイプ
	WipeRight,   ///< 右方向ワイプ
	WipeUp,      ///< 上方向ワイプ
	WipeDown,    ///< 下方向ワイプ
	WipeDiagonal,///< 対角ワイプ
	SlideIn,     ///< 新画面がスライドイン
	SlideOut,    ///< 旧画面がスライドアウト
	CurtainOpen, ///< カーテン開（中央から外側へ）
	CurtainClose,///< カーテン閉（外側から中央へ）
	RuleImage,   ///< ルール画像ベースのトランジション
	Pixelate,    ///< ピクセレート効果
	Ripple,      ///< 波紋/リプル歪み
};

/// @brief トランジションの状態
enum class TransitionState
{
	Idle,          ///< 待機中
	Transitioning, ///< 遷移中
	Complete,      ///< 完了
};

/// @brief ワイプ方向
enum class WipeDirection
{
	Left,
	Right,
	Up,
	Down,
	DiagonalTopLeft,
	DiagonalTopRight,
	DiagonalBottomLeft,
	DiagonalBottomRight,
};

/// @brief スライド方向
enum class SlideDirection
{
	Left,
	Right,
	Up,
	Down,
};

/// @brief ルール画像パターン
enum class RulePattern
{
	Circle,   ///< 円形
	Diamond,  ///< ダイアモンド形
	Blinds,   ///< ブラインド
	Mosaic,   ///< モザイク
	Clock,    ///< 時計回り
	Random,   ///< ランダム
};

/// @brief フェードカラー
struct FadeColor
{
	float r{0.0f};
	float g{0.0f};
	float b{0.0f};
	float a{1.0f};

	[[nodiscard]] static constexpr FadeColor black() noexcept
	{
		return {0.0f, 0.0f, 0.0f, 1.0f};
	}

	[[nodiscard]] static constexpr FadeColor white() noexcept
	{
		return {1.0f, 1.0f, 1.0f, 1.0f};
	}
};

// ── ルール画像生成 ─────────────────────────────────────────

/// @brief プロシージャルルール画像ジェネレータ
/// @details ファイル不要で各種トランジションパターンのルール画像を
///          グレースケール配列として生成する。
///          各ピクセルの輝度（0=最初に遷移、255=最後に遷移）で
///          トランジションの順序を定義する。
class RuleImageGenerator
{
public:
	/// @brief ルール画像を生成する
	/// @param pattern パターンの種類
	/// @param width 画像幅
	/// @param height 画像高さ
	/// @return グレースケール値の配列（サイズ = width * height）
	[[nodiscard]] static std::vector<std::uint8_t> generate(
		RulePattern pattern,
		int width,
		int height) noexcept
	{
		switch (pattern)
		{
		case RulePattern::Circle:  return generateCircle(width, height);
		case RulePattern::Diamond: return generateDiamond(width, height);
		case RulePattern::Blinds:  return generateBlinds(width, height);
		case RulePattern::Mosaic:  return generateMosaic(width, height);
		case RulePattern::Clock:   return generateClock(width, height);
		case RulePattern::Random:  return generateRandom(width, height);
		}
		return generateCircle(width, height);
	}

private:
	[[nodiscard]] static std::vector<std::uint8_t> generateCircle(
		int width, int height) noexcept
	{
		std::vector<std::uint8_t> data(static_cast<std::size_t>(width * height));
		const float cx = static_cast<float>(width) * 0.5f;
		const float cy = static_cast<float>(height) * 0.5f;
		const float maxDist = std::sqrt(cx * cx + cy * cy);

		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				const float dx = static_cast<float>(x) - cx;
				const float dy = static_cast<float>(y) - cy;
				const float dist = std::sqrt(dx * dx + dy * dy);
				const float normalized = std::clamp(dist / maxDist, 0.0f, 1.0f);
				data[static_cast<std::size_t>(y * width + x)] =
					static_cast<std::uint8_t>(normalized * 255.0f);
			}
		}
		return data;
	}

	[[nodiscard]] static std::vector<std::uint8_t> generateDiamond(
		int width, int height) noexcept
	{
		std::vector<std::uint8_t> data(static_cast<std::size_t>(width * height));
		const float cx = static_cast<float>(width) * 0.5f;
		const float cy = static_cast<float>(height) * 0.5f;
		const float maxDist = cx + cy;

		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				const float dx = std::abs(static_cast<float>(x) - cx);
				const float dy = std::abs(static_cast<float>(y) - cy);
				const float dist = dx + dy;
				const float normalized = std::clamp(dist / maxDist, 0.0f, 1.0f);
				data[static_cast<std::size_t>(y * width + x)] =
					static_cast<std::uint8_t>(normalized * 255.0f);
			}
		}
		return data;
	}

	[[nodiscard]] static std::vector<std::uint8_t> generateBlinds(
		int width, int height, int blindCount = 8) noexcept
	{
		std::vector<std::uint8_t> data(static_cast<std::size_t>(width * height));
		const float blindHeight = static_cast<float>(height) / static_cast<float>(blindCount);

		for (int y = 0; y < height; ++y)
		{
			const float posInBlind = std::fmod(static_cast<float>(y), blindHeight);
			const float normalized = std::clamp(posInBlind / blindHeight, 0.0f, 1.0f);
			const auto value = static_cast<std::uint8_t>(normalized * 255.0f);
			for (int x = 0; x < width; ++x)
			{
				data[static_cast<std::size_t>(y * width + x)] = value;
			}
		}
		return data;
	}

	[[nodiscard]] static std::vector<std::uint8_t> generateMosaic(
		int width, int height, int tileSize = 32) noexcept
	{
		std::vector<std::uint8_t> data(static_cast<std::size_t>(width * height));
		const int tilesX = (width + tileSize - 1) / tileSize;
		const int tilesY = (height + tileSize - 1) / tileSize;
		const int totalTiles = tilesX * tilesY;

		/// 簡易ハッシュでタイルごとに擬似ランダム値を割り当てる
		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				const int tx = x / tileSize;
				const int ty = y / tileSize;
				const int tileIndex = ty * tilesX + tx;
				const float normalized = static_cast<float>(tileIndex) / static_cast<float>(totalTiles);
				/// ハッシュで順序をシャッフル
				const auto hashed = static_cast<std::uint32_t>(tileIndex * 2654435761u);
				const float shuffled = static_cast<float>(hashed % 256u) / 255.0f;
				static_cast<void>(normalized);
				data[static_cast<std::size_t>(y * width + x)] =
					static_cast<std::uint8_t>(shuffled * 255.0f);
			}
		}
		return data;
	}

	[[nodiscard]] static std::vector<std::uint8_t> generateClock(
		int width, int height) noexcept
	{
		std::vector<std::uint8_t> data(static_cast<std::size_t>(width * height));
		const float cx = static_cast<float>(width) * 0.5f;
		const float cy = static_cast<float>(height) * 0.5f;

		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				const float dx = static_cast<float>(x) - cx;
				const float dy = static_cast<float>(y) - cy;
				/// 12時方向を0として時計回りに正規化
				float angle = std::atan2(dx, -dy);
				if (angle < 0.0f)
				{
					angle += 2.0f * std::numbers::pi_v<float>;
				}
				const float normalized = angle / (2.0f * std::numbers::pi_v<float>);
				data[static_cast<std::size_t>(y * width + x)] =
					static_cast<std::uint8_t>(std::clamp(normalized, 0.0f, 1.0f) * 255.0f);
			}
		}
		return data;
	}

	[[nodiscard]] static std::vector<std::uint8_t> generateRandom(
		int width, int height) noexcept
	{
		std::vector<std::uint8_t> data(static_cast<std::size_t>(width * height));

		/// 再現可能な擬似乱数（xorshift32）
		std::uint32_t state = 12345u;
		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				state ^= state << 13;
				state ^= state >> 17;
				state ^= state << 5;
				data[static_cast<std::size_t>(y * width + x)] =
					static_cast<std::uint8_t>(state & 0xFFu);
			}
		}
		return data;
	}
};

// ── トランジションエフェクト ───────────────────────────────

/// @brief トランジションエフェクトの基底
/// @details 各種トランジション効果の共通インターフェースと
///          進行状態を管理する。
struct TransitionEffect
{
	TransitionType type{TransitionType::Fade};
	float duration{1.0f};
	float elapsed{0.0f};
	EasingType easing{EasingType::Linear};

	/// @brief フェードカラー（Fade タイプ用）
	FadeColor fadeColor{FadeColor::black()};

	/// @brief ワイプ方向（Wipe タイプ用）
	WipeDirection wipeDirection{WipeDirection::Left};

	/// @brief スライド方向（SlideIn/Out タイプ用）
	SlideDirection slideDirection{SlideDirection::Left};

	/// @brief ルール画像データ（RuleImage タイプ用）
	std::vector<std::uint8_t> ruleData;
	int ruleWidth{0};
	int ruleHeight{0};

	/// @brief ルール画像のソフトネス（境界のぼかし幅）
	float ruleSoftness{0.05f};

	/// @brief ピクセレートの最大ブロックサイズ
	int pixelateMaxSize{32};

	/// @brief リプルの波紋数
	float rippleFrequency{3.0f};

	/// @brief リプルの振幅
	float rippleAmplitude{20.0f};

	/// @brief エフェクトを進行させる
	/// @param dt デルタタイム（秒）
	void update(float dt) noexcept
	{
		elapsed = std::min(elapsed + dt, duration);
	}

	/// @brief 全体の進行度を取得する
	/// @return 進行度 [0.0, 1.0]
	[[nodiscard]] float getOverallProgress() const noexcept
	{
		if (duration <= 0.0f) return 1.0f;
		const float raw = std::clamp(elapsed / duration, 0.0f, 1.0f);
		return Easing::apply(easing, raw);
	}

	/// @brief エフェクトが完了したか
	[[nodiscard]] bool isComplete() const noexcept
	{
		return elapsed >= duration;
	}

	/// @brief 指定座標でのブレンド係数を取得する（ルール画像トランジション用）
	/// @param x X 座標（正規化 [0.0, 1.0]）
	/// @param y Y 座標（正規化 [0.0, 1.0]）
	/// @return ブレンド係数 [0.0, 1.0]（0=旧画像、1=新画像）
	[[nodiscard]] float getBlendFactor(float x, float y) const noexcept
	{
		const float progress = getOverallProgress();

		switch (type)
		{
		case TransitionType::Fade:
		case TransitionType::Dissolve:
			return progress;

		case TransitionType::WipeLeft:
			return (x <= progress) ? 1.0f : 0.0f;

		case TransitionType::WipeRight:
			return (x >= 1.0f - progress) ? 1.0f : 0.0f;

		case TransitionType::WipeUp:
			return (y <= progress) ? 1.0f : 0.0f;

		case TransitionType::WipeDown:
			return (y >= 1.0f - progress) ? 1.0f : 0.0f;

		case TransitionType::WipeDiagonal:
		{
			const float diag = (x + y) * 0.5f;
			return (diag <= progress) ? 1.0f : 0.0f;
		}

		case TransitionType::SlideIn:
		case TransitionType::SlideOut:
			return progress;

		case TransitionType::CurtainOpen:
		{
			const float halfProgress = progress * 0.5f;
			const float distFromCenter = std::abs(x - 0.5f);
			return (distFromCenter >= 0.5f - halfProgress) ? 1.0f : 0.0f;
		}

		case TransitionType::CurtainClose:
		{
			const float halfProgress = progress * 0.5f;
			const float distFromCenter = std::abs(x - 0.5f);
			return (distFromCenter <= halfProgress) ? 1.0f : 0.0f;
		}

		case TransitionType::RuleImage:
			return getBlendFactorFromRule(x, y, progress);

		case TransitionType::Pixelate:
			return progress;

		case TransitionType::Ripple:
			return progress;
		}
		return progress;
	}

	/// @brief スライドオフセットを取得する（SlideIn/Out 用）
	/// @param screenWidth スクリーン幅
	/// @param screenHeight スクリーン高さ
	/// @return {offsetX, offsetY}
	[[nodiscard]] std::pair<float, float> getSlideOffset(
		float screenWidth, float screenHeight) const noexcept
	{
		const float progress = getOverallProgress();
		const float remaining = 1.0f - progress;

		float factor = remaining;
		if (type == TransitionType::SlideOut)
		{
			factor = progress;
		}

		switch (slideDirection)
		{
		case SlideDirection::Left:  return {-screenWidth * factor, 0.0f};
		case SlideDirection::Right: return { screenWidth * factor, 0.0f};
		case SlideDirection::Up:    return {0.0f, -screenHeight * factor};
		case SlideDirection::Down:  return {0.0f,  screenHeight * factor};
		}
		return {0.0f, 0.0f};
	}

	/// @brief ピクセレートのブロックサイズを取得する
	/// @return 現在のブロックサイズ（ピクセル）
	[[nodiscard]] int getPixelateBlockSize() const noexcept
	{
		const float progress = getOverallProgress();
		/// 中間で最大化し、開始/終了で1に戻る
		const float curve = 1.0f - std::abs(2.0f * progress - 1.0f);
		return std::max(1, static_cast<int>(
			static_cast<float>(pixelateMaxSize) * curve));
	}

	/// @brief リプルのオフセットを取得する
	/// @param x X 座標（正規化）
	/// @param y Y 座標（正規化）
	/// @return {offsetX, offsetY}（ピクセル単位）
	[[nodiscard]] std::pair<float, float> getRippleOffset(
		float x, float y) const noexcept
	{
		const float progress = getOverallProgress();
		const float amplitude = rippleAmplitude * (1.0f - progress);
		const float cx = 0.5f;
		const float cy = 0.5f;
		const float dx = x - cx;
		const float dy = y - cy;
		const float dist = std::sqrt(dx * dx + dy * dy);
		const float wave = std::sin(dist * rippleFrequency * 2.0f
			* std::numbers::pi_v<float> - progress * 10.0f);
		const float offsetX = (dist > 0.001f) ? (dx / dist) * wave * amplitude : 0.0f;
		const float offsetY = (dist > 0.001f) ? (dy / dist) * wave * amplitude : 0.0f;
		return {offsetX, offsetY};
	}

private:
	/// @brief ルール画像からブレンド係数を取得する
	[[nodiscard]] float getBlendFactorFromRule(
		float x, float y, float progress) const noexcept
	{
		if (ruleData.empty() || ruleWidth <= 0 || ruleHeight <= 0)
		{
			return progress;
		}

		const int px = std::clamp(
			static_cast<int>(x * static_cast<float>(ruleWidth)),
			0, ruleWidth - 1);
		const int py = std::clamp(
			static_cast<int>(y * static_cast<float>(ruleHeight)),
			0, ruleHeight - 1);

		const float threshold = static_cast<float>(
			ruleData[static_cast<std::size_t>(py * ruleWidth + px)]) / 255.0f;

		/// ソフトネスを使ったスムーズな遷移
		if (ruleSoftness <= 0.0f)
		{
			return (progress >= threshold) ? 1.0f : 0.0f;
		}
		const float lower = progress - ruleSoftness * 0.5f;
		const float upper = progress + ruleSoftness * 0.5f;
		return std::clamp((threshold - lower) / (upper - lower), 0.0f, 1.0f);
	}
};

// ── トランジションエフェクトビルダー ───────────────────────

/// @brief トランジションエフェクトを構築するビルダー
/// @details メソッドチェインでトランジションを設定する。
class TransitionBuilder
{
public:
	/// @brief フェードトランジションを作成する
	/// @param duration 時間（秒）
	/// @param color フェードカラー
	[[nodiscard]] static TransitionEffect fade(
		float duration,
		FadeColor color = FadeColor::black()) noexcept
	{
		TransitionEffect effect;
		effect.type = TransitionType::Fade;
		effect.duration = duration;
		effect.fadeColor = color;
		return effect;
	}

	/// @brief クロスディゾルブトランジションを作成する
	/// @param duration 時間（秒）
	[[nodiscard]] static TransitionEffect dissolve(float duration) noexcept
	{
		TransitionEffect effect;
		effect.type = TransitionType::Dissolve;
		effect.duration = duration;
		return effect;
	}

	/// @brief ワイプトランジションを作成する
	/// @param duration 時間（秒）
	/// @param direction ワイプ方向
	[[nodiscard]] static TransitionEffect wipe(
		float duration,
		WipeDirection direction = WipeDirection::Left) noexcept
	{
		TransitionEffect effect;
		effect.duration = duration;
		effect.wipeDirection = direction;

		switch (direction)
		{
		case WipeDirection::Left:               effect.type = TransitionType::WipeLeft; break;
		case WipeDirection::Right:              effect.type = TransitionType::WipeRight; break;
		case WipeDirection::Up:                 effect.type = TransitionType::WipeUp; break;
		case WipeDirection::Down:               effect.type = TransitionType::WipeDown; break;
		case WipeDirection::DiagonalTopLeft:
		case WipeDirection::DiagonalTopRight:
		case WipeDirection::DiagonalBottomLeft:
		case WipeDirection::DiagonalBottomRight:effect.type = TransitionType::WipeDiagonal; break;
		}
		return effect;
	}

	/// @brief スライドイントランジションを作成する
	/// @param duration 時間（秒）
	/// @param direction スライド方向
	[[nodiscard]] static TransitionEffect slideIn(
		float duration,
		SlideDirection direction = SlideDirection::Left) noexcept
	{
		TransitionEffect effect;
		effect.type = TransitionType::SlideIn;
		effect.duration = duration;
		effect.slideDirection = direction;
		return effect;
	}

	/// @brief スライドアウトトランジションを作成する
	/// @param duration 時間（秒）
	/// @param direction スライド方向
	[[nodiscard]] static TransitionEffect slideOut(
		float duration,
		SlideDirection direction = SlideDirection::Left) noexcept
	{
		TransitionEffect effect;
		effect.type = TransitionType::SlideOut;
		effect.duration = duration;
		effect.slideDirection = direction;
		return effect;
	}

	/// @brief カーテン開トランジションを作成する
	/// @param duration 時間（秒）
	[[nodiscard]] static TransitionEffect curtainOpen(float duration) noexcept
	{
		TransitionEffect effect;
		effect.type = TransitionType::CurtainOpen;
		effect.duration = duration;
		return effect;
	}

	/// @brief カーテン閉トランジションを作成する
	/// @param duration 時間（秒）
	[[nodiscard]] static TransitionEffect curtainClose(float duration) noexcept
	{
		TransitionEffect effect;
		effect.type = TransitionType::CurtainClose;
		effect.duration = duration;
		return effect;
	}

	/// @brief ルール画像トランジションを作成する
	/// @param duration 時間（秒）
	/// @param pattern プロシージャルパターン
	/// @param width ルール画像幅
	/// @param height ルール画像高さ
	/// @param softness 境界のぼかし幅
	[[nodiscard]] static TransitionEffect ruleImage(
		float duration,
		RulePattern pattern,
		int width = 256,
		int height = 256,
		float softness = 0.05f) noexcept
	{
		TransitionEffect effect;
		effect.type = TransitionType::RuleImage;
		effect.duration = duration;
		effect.ruleData = RuleImageGenerator::generate(pattern, width, height);
		effect.ruleWidth = width;
		effect.ruleHeight = height;
		effect.ruleSoftness = softness;
		return effect;
	}

	/// @brief カスタムルール画像データによるトランジションを作成する
	/// @param duration 時間（秒）
	/// @param data グレースケールルール画像データ
	/// @param width ルール画像幅
	/// @param height ルール画像高さ
	/// @param softness 境界のぼかし幅
	[[nodiscard]] static TransitionEffect ruleImageCustom(
		float duration,
		std::vector<std::uint8_t> data,
		int width,
		int height,
		float softness = 0.05f)
	{
		TransitionEffect effect;
		effect.type = TransitionType::RuleImage;
		effect.duration = duration;
		effect.ruleData = std::move(data);
		effect.ruleWidth = width;
		effect.ruleHeight = height;
		effect.ruleSoftness = softness;
		return effect;
	}

	/// @brief ピクセレートトランジションを作成する
	/// @param duration 時間（秒）
	/// @param maxBlockSize 最大ブロックサイズ
	[[nodiscard]] static TransitionEffect pixelate(
		float duration,
		int maxBlockSize = 32) noexcept
	{
		TransitionEffect effect;
		effect.type = TransitionType::Pixelate;
		effect.duration = duration;
		effect.pixelateMaxSize = maxBlockSize;
		return effect;
	}

	/// @brief リプルトランジションを作成する
	/// @param duration 時間（秒）
	/// @param frequency 波紋の周波数
	/// @param amplitude 波紋の振幅
	[[nodiscard]] static TransitionEffect ripple(
		float duration,
		float frequency = 3.0f,
		float amplitude = 20.0f) noexcept
	{
		TransitionEffect effect;
		effect.type = TransitionType::Ripple;
		effect.duration = duration;
		effect.rippleFrequency = frequency;
		effect.rippleAmplitude = amplitude;
		return effect;
	}
};

// ── トランジションマネージャ ───────────────────────────────

/// @brief トランジションのライフサイクルを管理する
/// @details トランジションの開始・更新・完了を一元管理し、
///          コールバックによる通知を提供する。
///
/// @code
/// mitiru::vn::TransitionManager manager;
///
/// auto effect = mitiru::vn::TransitionBuilder::fade(0.5f);
/// manager.startTransition(std::move(effect));
///
/// // 毎フレーム
/// manager.update(dt);
/// if (manager.state() == mitiru::vn::TransitionState::Transitioning)
/// {
///     float blend = manager.currentEffect().getBlendFactor(x, y);
/// }
/// @endcode
class TransitionManager
{
public:
	/// @brief コールバック型
	using Callback = std::function<void()>;

	/// @brief トランジションを開始する
	/// @param effect トランジションエフェクト
	void startTransition(TransitionEffect effect)
	{
		m_effect = std::move(effect);
		m_effect.elapsed = 0.0f;
		m_state = TransitionState::Transitioning;

		if (m_onStart)
		{
			m_onStart();
		}
	}

	/// @brief トランジションを更新する
	/// @param dt デルタタイム（秒）
	void update(float dt) noexcept
	{
		if (m_state != TransitionState::Transitioning)
		{
			return;
		}

		m_effect.update(dt);

		if (m_effect.isComplete())
		{
			m_state = TransitionState::Complete;
			if (m_onComplete)
			{
				m_onComplete();
			}
		}
	}

	/// @brief 現在の状態を取得する
	[[nodiscard]] TransitionState state() const noexcept
	{
		return m_state;
	}

	/// @brief トランジション中か
	[[nodiscard]] bool isTransitioning() const noexcept
	{
		return m_state == TransitionState::Transitioning;
	}

	/// @brief トランジションが完了したか
	[[nodiscard]] bool isComplete() const noexcept
	{
		return m_state == TransitionState::Complete;
	}

	/// @brief 現在のトランジションエフェクトを取得する
	[[nodiscard]] const TransitionEffect& currentEffect() const noexcept
	{
		return m_effect;
	}

	/// @brief 全体の進行度を取得する
	[[nodiscard]] float progress() const noexcept
	{
		return m_effect.getOverallProgress();
	}

	/// @brief 状態をリセットする
	void reset() noexcept
	{
		m_state = TransitionState::Idle;
		m_effect = {};
	}

	/// @brief 開始コールバックを設定する
	void onTransitionStart(Callback callback)
	{
		m_onStart = std::move(callback);
	}

	/// @brief 完了コールバックを設定する
	void onTransitionComplete(Callback callback)
	{
		m_onComplete = std::move(callback);
	}

	/// @brief 状態をJSON文字列として返す
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"state\":\"" + stateToString(m_state) + "\"";
		json += ",\"progress\":" + std::to_string(m_effect.getOverallProgress());
		json += ",\"duration\":" + std::to_string(m_effect.duration);
		json += ",\"elapsed\":" + std::to_string(m_effect.elapsed);
		json += "}";
		return json;
	}

private:
	[[nodiscard]] static std::string stateToString(TransitionState s)
	{
		switch (s)
		{
		case TransitionState::Idle:          return "Idle";
		case TransitionState::Transitioning: return "Transitioning";
		case TransitionState::Complete:      return "Complete";
		}
		return "Unknown";
	}

	TransitionEffect m_effect;                    ///< 現在のエフェクト
	TransitionState m_state{TransitionState::Idle}; ///< 現在の状態
	Callback m_onStart;                           ///< 開始コールバック
	Callback m_onComplete;                        ///< 完了コールバック
};

} // namespace mitiru::vn
