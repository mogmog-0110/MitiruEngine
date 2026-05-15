#pragma once

/// @file CharacterManager.hpp
/// @brief ビジュアルノベル用キャラクタースプライトマネージャ
/// @details 立ち絵の表示・非表示・表情切り替え・移動・ディミング・
///          シェイクエフェクトなど、VNシーンにおけるキャラクター演出を
///          一元管理する。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numbers>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <mitiru/vn/EasingFunctions.hpp>

namespace mitiru::vn
{

// ── 位置コンフィグ ──────────────────────────────────────────

/// @brief プリセット位置の座標設定
struct CharacterPositionConfig
{
	float leftX = 0.15f;              ///< Left プリセットの X 座標
	float centerLeftX = 0.30f;        ///< CenterLeft プリセットの X 座標
	float centerX = 0.50f;            ///< Center プリセットの X 座標
	float centerRightX = 0.70f;       ///< CenterRight プリセットの X 座標
	float rightX = 0.85f;             ///< Right プリセットの X 座標
	float defaultY = 0.8f;            ///< プリセット位置のデフォルト Y 座標
	float slideOffscreenLeft = -0.3f;  ///< 左方向スライドの画面外座標
	float slideOffscreenRight = 1.3f;  ///< 右方向スライドの画面外座標
};

// ── 定数・列挙 ─────────────────────────────────────────────

/// @brief 定義済みキャラクター位置
enum class CharacterPosition
{
	Left,         ///< 左端
	CenterLeft,   ///< 中央やや左
	Center,       ///< 中央
	CenterRight,  ///< 中央やや右
	Right,        ///< 右端
	Custom,       ///< カスタム位置
};

/// @brief キャラクター表示アニメーション
enum class ShowAnimation
{
	Instant,   ///< 即座に表示
	FadeIn,    ///< フェードイン（alpha 0→1）
	SlideIn,   ///< 画面外からスライドイン
};

/// @brief キャラクター非表示アニメーション
enum class HideAnimation
{
	Instant,   ///< 即座に非表示
	FadeOut,   ///< フェードアウト（alpha 1→0）
	SlideOut,  ///< 画面外へスライドアウト
};

/// @brief キャラクタースライド方向
enum class CharacterSlideDirection
{
	Left,
	Right,
	Up,
	Down,
};

// ── キャラクター定義 ───────────────────────────────────────

/// @brief キャラクター定義
/// @details 名前と表情テクスチャのマップを保持する。
struct CharacterDefinition
{
	std::string id;        ///< キャラクター識別子
	std::string name;      ///< 表示名
	std::unordered_map<std::string, std::uint32_t> expressions; ///< 表情名→テクスチャID
};

// ── キャラクター状態 ───────────────────────────────────────

/// @brief キャラクターのランタイム状態
struct CharacterState
{
	std::string id;               ///< キャラクター識別子
	std::string expression;       ///< 現在の表情名
	float posX{0.5f};             ///< X 位置（正規化 0.0-1.0）
	float posY{0.8f};             ///< Y 位置（正規化 0.0-1.0）
	float alpha{1.0f};            ///< 透明度
	float scale{1.0f};            ///< スケール
	float brightness{1.0f};       ///< 明るさ（ディミング用、1.0=通常、0.6=暗い）
	bool visible{false};          ///< 表示中か
	bool dimmed{false};           ///< ディミング中か
	bool flipped{false};          ///< 左右反転か
	int zOrder{0};                ///< 描画順（大きいほど手前）
	CharacterPosition preset{CharacterPosition::Center};
};

// ── アニメーション ─────────────────────────────────────────

/// @brief キャラクターアニメーションの種類
enum class CharAnimType
{
	Show,             ///< 表示アニメーション
	Hide,             ///< 非表示アニメーション
	Move,             ///< 移動アニメーション
	ExpressionChange, ///< 表情変更（クロスディゾルブ）
	Shake,            ///< シェイクエフェクト
};

/// @brief 実行中のキャラクターアニメーション
struct CharacterAnimation
{
	std::string characterId;    ///< 対象キャラクター
	CharAnimType type;          ///< アニメーション種類
	float duration{0.3f};       ///< 時間（秒）
	float elapsed{0.0f};        ///< 経過時間
	EasingType easing{EasingType::EaseOutCubic};

	/// Show/Hide 用
	ShowAnimation showAnim{ShowAnimation::FadeIn};
	HideAnimation hideAnim{HideAnimation::FadeOut};
	CharacterSlideDirection slideDir{CharacterSlideDirection::Left};

	/// Move 用
	float fromX{0.0f};
	float fromY{0.0f};
	float toX{0.0f};
	float toY{0.0f};

	/// ExpressionChange 用
	std::string newExpression;
	std::uint32_t oldTextureId{0};

	/// Shake 用
	float shakeIntensity{5.0f};
	float shakeFrequency{30.0f};

	/// @brief 進行度を取得する
	[[nodiscard]] float getProgress() const noexcept
	{
		if (duration <= 0.0f) return 1.0f;
		const float raw = std::clamp(elapsed / duration, 0.0f, 1.0f);
		return Easing::apply(easing, raw);
	}

	/// @brief 完了したか
	[[nodiscard]] bool isComplete() const noexcept
	{
		return elapsed >= duration;
	}
};

// ── 描画情報 ───────────────────────────────────────────────

/// @brief キャラクター描画情報
struct CharacterRenderInfo
{
	std::string id;            ///< キャラクターID
	std::uint32_t textureId{0};///< 現在のテクスチャID
	float posX{0.0f};         ///< 描画 X 位置（正規化）
	float posY{0.0f};         ///< 描画 Y 位置（正規化）
	float alpha{1.0f};        ///< 透明度
	float scale{1.0f};        ///< スケール
	float brightness{1.0f};   ///< 明るさ
	bool flipped{false};      ///< 左右反転
	int zOrder{0};             ///< 描画順

	/// @brief クロスディゾルブ中の旧テクスチャ
	std::uint32_t oldTextureId{0};
	float crossDissolve{1.0f}; ///< 新表情へのブレンド率（1.0=完全に新）

	/// @brief シェイクオフセット
	float shakeOffsetX{0.0f};
	float shakeOffsetY{0.0f};
};

// ── キャラクターマネージャ ─────────────────────────────────

/// @brief ビジュアルノベル用キャラクタースプライトマネージャ
/// @details 立ち絵の登録・表示・非表示・表情切り替え・移動・ディミングなどを管理する。
///
/// @code
/// mitiru::vn::CharacterManager characters;
///
/// // キャラクター登録
/// mitiru::vn::CharacterDefinition def;
/// def.id = "sakura";
/// def.name = "さくら";
/// def.expressions = {
///     {"normal", 100}, {"happy", 101}, {"sad", 102}, {"angry", 103}
/// };
/// characters.registerCharacter(def);
///
/// // 表示
/// characters.show("sakura", "normal",
///     mitiru::vn::CharacterPosition::Center,
///     mitiru::vn::ShowAnimation::FadeIn, 0.3f);
///
/// // 表情変更
/// characters.changeExpression("sakura", "happy", 0.2f);
///
/// // 移動
/// characters.moveTo("sakura", mitiru::vn::CharacterPosition::Left, 0.5f);
///
/// // 毎フレーム
/// characters.update(dt);
/// auto renderList = characters.getRenderList();
/// @endcode
class CharacterManager
{
public:
	// ── キャラクター登録 ──────────────────────────────────────

	/// @brief キャラクターを登録する
	/// @param definition キャラクター定義
	void registerCharacter(CharacterDefinition definition)
	{
		const auto id = definition.id;
		m_definitions[id] = std::move(definition);

		/// 状態が無ければ初期化
		if (m_states.find(id) == m_states.end())
		{
			CharacterState state;
			state.id = id;
			m_states[id] = std::move(state);
		}
	}

	/// @brief キャラクターの登録を解除する
	/// @param id キャラクターID
	void unregisterCharacter(const std::string& id)
	{
		m_definitions.erase(id);
		m_states.erase(id);
		removeAnimationsFor(id);
	}

	/// @brief キャラクターが登録されているか
	[[nodiscard]] bool isRegistered(const std::string& id) const noexcept
	{
		return m_definitions.find(id) != m_definitions.end();
	}

	// ── 位置コンフィグ ───────────────────────────────────────

	/// @brief プリセット位置の設定を変更する
	/// @param config 新しい位置設定
	void setPositionConfig(const CharacterPositionConfig& config) noexcept
	{
		m_positionConfig = config;
	}

	/// @brief 現在のプリセット位置設定を取得する
	[[nodiscard]] const CharacterPositionConfig& positionConfig() const noexcept
	{
		return m_positionConfig;
	}

	// ── 表示/非表示 ──────────────────────────────────────────

	/// @brief キャラクターを表示する
	/// @param id キャラクターID
	/// @param expression 表情名
	/// @param position 表示位置
	/// @param animation 表示アニメーション
	/// @param duration アニメーション時間（秒）
	/// @param slideDir スライド方向（SlideIn 時のみ）
	void show(const std::string& id,
	          const std::string& expression,
	          CharacterPosition position = CharacterPosition::Center,
	          ShowAnimation animation = ShowAnimation::FadeIn,
	          float duration = 0.3f,
	          CharacterSlideDirection slideDir = CharacterSlideDirection::Left)
	{
		auto it = m_states.find(id);
		if (it == m_states.end()) return;

		auto& state = it->second;
		state.expression = expression;
		state.preset = position;
		setPositionFromPreset(state, position);
		state.visible = true;
		state.zOrder = m_nextZOrder++;

		if (animation == ShowAnimation::Instant)
		{
			state.alpha = 1.0f;
			return;
		}

		state.alpha = 0.0f;

		CharacterAnimation anim;
		anim.characterId = id;
		anim.type = CharAnimType::Show;
		anim.showAnim = animation;
		anim.duration = duration;
		anim.slideDir = slideDir;
		anim.fromX = state.posX;
		anim.fromY = state.posY;
		anim.toX = state.posX;
		anim.toY = state.posY;

		if (animation == ShowAnimation::SlideIn)
		{
			switch (slideDir)
			{
			case CharacterSlideDirection::Left:  anim.fromX = m_positionConfig.slideOffscreenLeft; break;
			case CharacterSlideDirection::Right: anim.fromX = m_positionConfig.slideOffscreenRight; break;
			case CharacterSlideDirection::Up:    anim.fromY = m_positionConfig.slideOffscreenLeft; break;
			case CharacterSlideDirection::Down:  anim.fromY = m_positionConfig.slideOffscreenRight; break;
			}
			state.posX = anim.fromX;
			state.posY = anim.fromY;
		}

		m_animations.push_back(std::move(anim));
	}

	/// @brief キャラクターを表示する（カスタム位置）
	/// @param id キャラクターID
	/// @param expression 表情名
	/// @param x X 位置（正規化 0.0-1.0）
	/// @param y Y 位置（正規化 0.0-1.0）
	/// @param animation 表示アニメーション
	/// @param duration アニメーション時間（秒）
	void showAt(const std::string& id,
	            const std::string& expression,
	            float x, float y,
	            ShowAnimation animation = ShowAnimation::FadeIn,
	            float duration = 0.3f)
	{
		auto it = m_states.find(id);
		if (it == m_states.end()) return;

		it->second.posX = x;
		it->second.posY = y;
		it->second.preset = CharacterPosition::Custom;

		show(id, expression, CharacterPosition::Custom, animation, duration);
		it->second.posX = x;
		it->second.posY = y;
	}

	/// @brief キャラクターを非表示にする
	/// @param id キャラクターID
	/// @param animation 非表示アニメーション
	/// @param duration アニメーション時間（秒）
	/// @param slideDir スライド方向（SlideOut 時のみ）
	void hide(const std::string& id,
	          HideAnimation animation = HideAnimation::FadeOut,
	          float duration = 0.3f,
	          CharacterSlideDirection slideDir = CharacterSlideDirection::Left)
	{
		auto it = m_states.find(id);
		if (it == m_states.end()) return;

		if (animation == HideAnimation::Instant)
		{
			it->second.visible = false;
			it->second.alpha = 0.0f;
			return;
		}

		CharacterAnimation anim;
		anim.characterId = id;
		anim.type = CharAnimType::Hide;
		anim.hideAnim = animation;
		anim.duration = duration;
		anim.slideDir = slideDir;
		anim.fromX = it->second.posX;
		anim.fromY = it->second.posY;
		anim.toX = it->second.posX;
		anim.toY = it->second.posY;

		if (animation == HideAnimation::SlideOut)
		{
			switch (slideDir)
			{
			case CharacterSlideDirection::Left:  anim.toX = m_positionConfig.slideOffscreenLeft; break;
			case CharacterSlideDirection::Right: anim.toX = m_positionConfig.slideOffscreenRight; break;
			case CharacterSlideDirection::Up:    anim.toY = m_positionConfig.slideOffscreenLeft; break;
			case CharacterSlideDirection::Down:  anim.toY = m_positionConfig.slideOffscreenRight; break;
			}
		}

		m_animations.push_back(std::move(anim));
	}

	/// @brief 全キャラクターを非表示にする
	/// @param animation 非表示アニメーション
	/// @param duration アニメーション時間（秒）
	void hideAll(HideAnimation animation = HideAnimation::FadeOut,
	             float duration = 0.3f)
	{
		for (auto& [id, state] : m_states)
		{
			if (state.visible)
			{
				hide(id, animation, duration);
			}
		}
	}

	// ── 表情変更 ──────────────────────────────────────────────

	/// @brief 表情を変更する
	/// @param id キャラクターID
	/// @param expression 新しい表情名
	/// @param crossDissolveDuration クロスディゾルブ時間（0 で即座変更）
	void changeExpression(const std::string& id,
	                      const std::string& expression,
	                      float crossDissolveDuration = 0.0f)
	{
		auto stateIt = m_states.find(id);
		if (stateIt == m_states.end()) return;

		if (crossDissolveDuration <= 0.0f)
		{
			stateIt->second.expression = expression;
			return;
		}

		const auto defIt = m_definitions.find(id);
		if (defIt == m_definitions.end()) return;

		/// 旧テクスチャIDを取得
		std::uint32_t oldTexId = 0;
		const auto oldExprIt = defIt->second.expressions.find(stateIt->second.expression);
		if (oldExprIt != defIt->second.expressions.end())
		{
			oldTexId = oldExprIt->second;
		}

		CharacterAnimation anim;
		anim.characterId = id;
		anim.type = CharAnimType::ExpressionChange;
		anim.duration = crossDissolveDuration;
		anim.newExpression = expression;
		anim.oldTextureId = oldTexId;
		anim.easing = EasingType::Linear;

		stateIt->second.expression = expression;
		m_animations.push_back(std::move(anim));
	}

	// ── 移動 ──────────────────────────────────────────────────

	/// @brief キャラクターをプリセット位置へ移動する
	/// @param id キャラクターID
	/// @param position 目標位置
	/// @param duration アニメーション時間（秒）
	/// @param easing イージング
	void moveTo(const std::string& id,
	            CharacterPosition position,
	            float duration = 0.5f,
	            EasingType easing = EasingType::EaseInOutCubic)
	{
		auto it = m_states.find(id);
		if (it == m_states.end()) return;

		float targetX = 0.0f;
		float targetY = it->second.posY;
		getPresetPosition(position, targetX, targetY);

		moveToPosition(id, targetX, targetY, duration, easing);
		it->second.preset = position;
	}

	/// @brief キャラクターをカスタム位置へ移動する
	/// @param id キャラクターID
	/// @param x 目標 X 位置（正規化）
	/// @param y 目標 Y 位置（正規化）
	/// @param duration アニメーション時間（秒）
	/// @param easing イージング
	void moveToPosition(const std::string& id,
	                    float x, float y,
	                    float duration = 0.5f,
	                    EasingType easing = EasingType::EaseInOutCubic)
	{
		auto it = m_states.find(id);
		if (it == m_states.end()) return;

		if (duration <= 0.0f)
		{
			it->second.posX = x;
			it->second.posY = y;
			it->second.preset = CharacterPosition::Custom;
			return;
		}

		CharacterAnimation anim;
		anim.characterId = id;
		anim.type = CharAnimType::Move;
		anim.duration = duration;
		anim.easing = easing;
		anim.fromX = it->second.posX;
		anim.fromY = it->second.posY;
		anim.toX = x;
		anim.toY = y;

		m_animations.push_back(std::move(anim));
	}

	// ── ディミング ────────────────────────────────────────────

	/// @brief 指定キャラクター以外をディミングする
	/// @param activeSpeakerId アクティブな話者のID
	/// @param dimBrightness ディミング時の明るさ（0.0-1.0）
	void dimInactive(const std::string& activeSpeakerId,
	                 float dimBrightness = 0.6f) noexcept
	{
		for (auto& [id, state] : m_states)
		{
			if (!state.visible) continue;
			if (id == activeSpeakerId)
			{
				state.dimmed = false;
				state.brightness = 1.0f;
			}
			else
			{
				state.dimmed = true;
				state.brightness = dimBrightness;
			}
		}
	}

	/// @brief 全キャラクターのディミングを解除する
	void clearDimming() noexcept
	{
		for (auto& [id, state] : m_states)
		{
			state.dimmed = false;
			state.brightness = 1.0f;
		}
	}

	// ── Z順序 ─────────────────────────────────────────────────

	/// @brief キャラクターを最前面に移動する
	/// @param id キャラクターID
	void bringToFront(const std::string& id) noexcept
	{
		auto it = m_states.find(id);
		if (it != m_states.end())
		{
			it->second.zOrder = m_nextZOrder++;
		}
	}

	/// @brief キャラクターを最背面に移動する
	/// @param id キャラクターID
	void sendToBack(const std::string& id) noexcept
	{
		auto it = m_states.find(id);
		if (it == m_states.end()) return;

		/// 現在の最小 zOrder を取得
		int minZ = 0;
		for (const auto& [cid, state] : m_states)
		{
			if (state.visible)
			{
				minZ = std::min(minZ, state.zOrder);
			}
		}
		it->second.zOrder = minZ - 1;
	}

	// ── 反転/スケール ─────────────────────────────────────────

	/// @brief キャラクターの左右反転を設定する
	/// @param id キャラクターID
	/// @param flipped 反転するか
	void setFlipped(const std::string& id, bool flipped) noexcept
	{
		auto it = m_states.find(id);
		if (it != m_states.end())
		{
			it->second.flipped = flipped;
		}
	}

	/// @brief キャラクターのスケールを設定する
	/// @param id キャラクターID
	/// @param scale スケール値
	void setScale(const std::string& id, float scale) noexcept
	{
		auto it = m_states.find(id);
		if (it != m_states.end())
		{
			it->second.scale = scale;
		}
	}

	// ── シェイクエフェクト ────────────────────────────────────

	/// @brief キャラクターにシェイクエフェクトを適用する
	/// @param id キャラクターID
	/// @param duration 時間（秒）
	/// @param intensity 強度（ピクセル）
	/// @param frequency 周波数
	void shake(const std::string& id,
	           float duration = 0.3f,
	           float intensity = 5.0f,
	           float frequency = 30.0f)
	{
		auto it = m_states.find(id);
		if (it == m_states.end()) return;

		CharacterAnimation anim;
		anim.characterId = id;
		anim.type = CharAnimType::Shake;
		anim.duration = duration;
		anim.shakeIntensity = intensity;
		anim.shakeFrequency = frequency;
		anim.easing = EasingType::Linear;

		m_animations.push_back(std::move(anim));
	}

	// ── 更新 ──────────────────────────────────────────────────

	/// @brief 全アニメーションを更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		/// アニメーションを進行させる
		for (auto& anim : m_animations)
		{
			anim.elapsed = std::min(anim.elapsed + dt, anim.duration);
			applyAnimation(anim);
		}

		/// 完了したアニメーションを削除する
		m_animations.erase(
			std::remove_if(m_animations.begin(), m_animations.end(),
				[this](const CharacterAnimation& anim)
				{
					if (anim.isComplete())
					{
						finalizeAnimation(anim);
						return true;
					}
					return false;
				}),
			m_animations.end());
	}

	// ── 描画情報取得 ──────────────────────────────────────────

	/// @brief 描画用キャラクター一覧を取得する（Z順ソート済み）
	/// @return CharacterRenderInfo のベクター
	[[nodiscard]] std::vector<CharacterRenderInfo> getRenderList() const
	{
		std::vector<CharacterRenderInfo> result;

		for (const auto& [id, state] : m_states)
		{
			if (!state.visible) continue;

			const auto defIt = m_definitions.find(id);
			if (defIt == m_definitions.end()) continue;

			CharacterRenderInfo info;
			info.id = id;
			info.posX = state.posX;
			info.posY = state.posY;
			info.alpha = state.alpha;
			info.scale = state.scale;
			info.brightness = state.brightness;
			info.flipped = state.flipped;
			info.zOrder = state.zOrder;

			/// テクスチャID取得
			const auto exprIt = defIt->second.expressions.find(state.expression);
			if (exprIt != defIt->second.expressions.end())
			{
				info.textureId = exprIt->second;
			}

			/// アニメーション中のエフェクト適用
			for (const auto& anim : m_animations)
			{
				if (anim.characterId != id) continue;

				if (anim.type == CharAnimType::ExpressionChange)
				{
					info.oldTextureId = anim.oldTextureId;
					info.crossDissolve = anim.getProgress();
				}
				else if (anim.type == CharAnimType::Shake)
				{
					const float progress = anim.getProgress();
					const float decay = 1.0f - progress;
					const float phase = anim.elapsed * anim.shakeFrequency;
					info.shakeOffsetX = std::sin(phase * 2.0f * std::numbers::pi_v<float>)
						* anim.shakeIntensity * decay;
					info.shakeOffsetY = std::cos(phase * 1.7f * std::numbers::pi_v<float>)
						* anim.shakeIntensity * decay * 0.5f;
				}
			}

			result.push_back(std::move(info));
		}

		/// Z 順序でソート
		std::sort(result.begin(), result.end(),
			[](const CharacterRenderInfo& a, const CharacterRenderInfo& b)
			{
				return a.zOrder < b.zOrder;
			});

		return result;
	}

	// ── クエリ ────────────────────────────────────────────────

	/// @brief キャラクターが表示中か
	[[nodiscard]] bool isVisible(const std::string& id) const noexcept
	{
		const auto it = m_states.find(id);
		return it != m_states.end() && it->second.visible;
	}

	/// @brief キャラクターの現在状態を取得する
	[[nodiscard]] const CharacterState* getState(const std::string& id) const noexcept
	{
		const auto it = m_states.find(id);
		if (it == m_states.end()) return nullptr;
		return &it->second;
	}

	/// @brief 表示中のキャラクター数を取得する
	[[nodiscard]] std::size_t visibleCount() const noexcept
	{
		std::size_t count = 0;
		for (const auto& [id, state] : m_states)
		{
			if (state.visible) ++count;
		}
		return count;
	}

	/// @brief アニメーション中か
	[[nodiscard]] bool isAnimating() const noexcept
	{
		return !m_animations.empty();
	}

	/// @brief 特定キャラクターがアニメーション中か
	[[nodiscard]] bool isAnimating(const std::string& id) const noexcept
	{
		return std::any_of(m_animations.begin(), m_animations.end(),
			[&id](const CharacterAnimation& a) { return a.characterId == id; });
	}

	/// @brief 登録済みキャラクター数
	[[nodiscard]] std::size_t registeredCount() const noexcept
	{
		return m_definitions.size();
	}

	// ── シリアライズ ──────────────────────────────────────────

	/// @brief 状態をJSON文字列として返す
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{\"characters\":[";

		bool first = true;
		for (const auto& [id, state] : m_states)
		{
			if (!first) json += ",";
			json += "{\"id\":\"" + id + "\"";
			json += ",\"expression\":\"" + state.expression + "\"";
			json += ",\"posX\":" + std::to_string(state.posX);
			json += ",\"posY\":" + std::to_string(state.posY);
			json += ",\"alpha\":" + std::to_string(state.alpha);
			json += ",\"scale\":" + std::to_string(state.scale);
			json += ",\"brightness\":" + std::to_string(state.brightness);
			json += ",\"visible\":" + std::string(state.visible ? "true" : "false");
			json += ",\"dimmed\":" + std::string(state.dimmed ? "true" : "false");
			json += ",\"flipped\":" + std::string(state.flipped ? "true" : "false");
			json += ",\"zOrder\":" + std::to_string(state.zOrder);
			json += "}";
			first = false;
		}

		json += "],\"animationCount\":" + std::to_string(m_animations.size());
		json += "}";
		return json;
	}

private:
	// ── プリセット位置の解決 ──────────────────────────────────

	/// @brief プリセット位置から座標を取得する
	void getPresetPosition(CharacterPosition position,
	                       float& outX, float& outY) const noexcept
	{
		outY = m_positionConfig.defaultY;
		switch (position)
		{
		case CharacterPosition::Left:        outX = m_positionConfig.leftX; break;
		case CharacterPosition::CenterLeft:  outX = m_positionConfig.centerLeftX; break;
		case CharacterPosition::Center:      outX = m_positionConfig.centerX; break;
		case CharacterPosition::CenterRight: outX = m_positionConfig.centerRightX; break;
		case CharacterPosition::Right:       outX = m_positionConfig.rightX; break;
		case CharacterPosition::Custom:      break; // 変更しない
		}
	}

	/// @brief 状態にプリセット位置を適用する
	void setPositionFromPreset(CharacterState& state,
	                           CharacterPosition position) const noexcept
	{
		if (position != CharacterPosition::Custom)
		{
			getPresetPosition(position, state.posX, state.posY);
		}
	}

	// ── アニメーション適用 ────────────────────────────────────

	/// @brief アニメーションを状態に適用する
	void applyAnimation(const CharacterAnimation& anim)
	{
		auto it = m_states.find(anim.characterId);
		if (it == m_states.end()) return;

		auto& state = it->second;
		const float progress = anim.getProgress();

		switch (anim.type)
		{
		case CharAnimType::Show:
			state.alpha = progress;
			if (anim.showAnim == ShowAnimation::SlideIn)
			{
				state.posX = anim.fromX + (anim.toX - anim.fromX) * progress;
				state.posY = anim.fromY + (anim.toY - anim.fromY) * progress;
			}
			break;

		case CharAnimType::Hide:
			state.alpha = 1.0f - progress;
			if (anim.hideAnim == HideAnimation::SlideOut)
			{
				state.posX = anim.fromX + (anim.toX - anim.fromX) * progress;
				state.posY = anim.fromY + (anim.toY - anim.fromY) * progress;
			}
			break;

		case CharAnimType::Move:
			state.posX = anim.fromX + (anim.toX - anim.fromX) * progress;
			state.posY = anim.fromY + (anim.toY - anim.fromY) * progress;
			break;

		case CharAnimType::ExpressionChange:
			// クロスディゾルブは getRenderList で処理
			break;

		case CharAnimType::Shake:
			// シェイクオフセットは getRenderList で処理
			break;
		}
	}

	/// @brief アニメーション完了時の後処理
	void finalizeAnimation(const CharacterAnimation& anim) noexcept
	{
		auto it = m_states.find(anim.characterId);
		if (it == m_states.end()) return;

		auto& state = it->second;

		switch (anim.type)
		{
		case CharAnimType::Show:
			state.alpha = 1.0f;
			state.posX = anim.toX;
			state.posY = anim.toY;
			break;

		case CharAnimType::Hide:
			state.alpha = 0.0f;
			state.visible = false;
			state.posX = anim.fromX; // 元の位置に戻す
			state.posY = anim.fromY;
			break;

		case CharAnimType::Move:
			state.posX = anim.toX;
			state.posY = anim.toY;
			break;

		case CharAnimType::ExpressionChange:
		case CharAnimType::Shake:
			break;
		}
	}

	/// @brief 特定キャラクターのアニメーションを削除する
	void removeAnimationsFor(const std::string& id) noexcept
	{
		m_animations.erase(
			std::remove_if(m_animations.begin(), m_animations.end(),
				[&id](const CharacterAnimation& a)
				{
					return a.characterId == id;
				}),
			m_animations.end());
	}

	// ── メンバ変数 ────────────────────────────────────────────

	std::unordered_map<std::string, CharacterDefinition> m_definitions; ///< キャラクター定義
	std::unordered_map<std::string, CharacterState> m_states;           ///< キャラクター状態
	std::vector<CharacterAnimation> m_animations;                       ///< 実行中アニメーション
	int m_nextZOrder{0};                                                ///< 次のZ順序値
	CharacterPositionConfig m_positionConfig;                           ///< プリセット位置設定
};

} // namespace mitiru::vn
