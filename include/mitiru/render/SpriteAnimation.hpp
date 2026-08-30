#pragma once

/// @file SpriteAnimation.hpp
/// @brief スプライトシートアニメーションシステム
/// @details フレームベースのスプライトアニメーション機能を提供する。
///          均一グリッドのスプライトシート、Aseprite JSON形式の読み込み、
///          再生制御、コールバック、キャッシュを含む完全なアニメーションパイプライン。

#include <algorithm>
#include <cstdint>
#include <functional>
#include <numeric>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <sgc/math/Rect.hpp>

#include <mitiru/render/Texture.hpp>

namespace mitiru::render
{

// ────────────────────────────────────────────────────────────────
// SpriteFrame。アトラス上の1フレーム情報
// ────────────────────────────────────────────────────────────────

/// @brief スプライトアニメーションの1フレーム
/// @details アトラス上のソース矩形、表示時間、描画オフセットを保持する。
struct SpriteFrame
{
	sgc::Recti sourceRect{};   ///< アトラス上のソース矩形（ピクセル座標）
	float duration = 0.1f;     ///< 表示時間（秒）
	float offsetX = 0.0f;      ///< 描画オフセットX
	float offsetY = 0.0f;      ///< 描画オフセットY
};

// ────────────────────────────────────────────────────────────────
// SpriteAnimation。名前付きフレーム列
// ────────────────────────────────────────────────────────────────

/// @brief 名前付きスプライトアニメーション
/// @details 複数フレームとループ・ピンポン設定を保持する。
struct SpriteAnimation
{
	std::string name;                ///< アニメーション名（例: "walk", "idle"）
	std::vector<SpriteFrame> frames; ///< フレーム列
	bool looping = false;            ///< ループ再生
	bool pingPong = false;           ///< ピンポン再生（往復）

	/// @brief 全フレームの合計時間を返す
	[[nodiscard]] float totalDuration() const noexcept
	{
		return std::accumulate(frames.begin(), frames.end(), 0.0f,
			[](float sum, const SpriteFrame& f) { return sum + f.duration; });
	}
};

// ────────────────────────────────────────────────────────────────
// SpriteAnimationSet。エンティティ用アニメーション集合
// ────────────────────────────────────────────────────────────────

/// @brief 1エンティティ用のアニメーションセット
/// @details "idle", "walk", "attack" など複数のアニメーションを名前で管理する。
///
/// @code
/// SpriteAnimationSet set;
/// set.add(walkAnimation);
/// set.add(idleAnimation);
/// auto* anim = set.get("walk");
/// @endcode
class SpriteAnimationSet
{
public:
	/// @brief アニメーションを追加する
	/// @param animation 追加するアニメーション
	void add(const SpriteAnimation& animation)
	{
		m_animations[animation.name] = animation;
	}

	/// @brief アニメーションを追加する（ムーブ版）
	/// @param animation 追加するアニメーション
	void add(SpriteAnimation&& animation)
	{
		const auto name = animation.name;
		m_animations[name] = std::move(animation);
	}

	/// @brief 名前でアニメーションを取得する
	/// @param name アニメーション名
	/// @return アニメーションへのポインタ（未登録の場合 nullptr）
	[[nodiscard]] const SpriteAnimation* get(const std::string& name) const
	{
		const auto it = m_animations.find(name);
		if (it == m_animations.end())
		{
			return nullptr;
		}
		return &it->second;
	}

	/// @brief 登録されたアニメーション名一覧を取得する
	/// @return アニメーション名のベクタ
	[[nodiscard]] std::vector<std::string> names() const
	{
		std::vector<std::string> result;
		result.reserve(m_animations.size());
		for (const auto& [key, _] : m_animations)
		{
			result.push_back(key);
		}
		return result;
	}

	/// @brief 登録アニメーション数を返す
	[[nodiscard]] std::size_t count() const noexcept
	{
		return m_animations.size();
	}

	/// @brief アニメーションが登録されているか
	/// @param name アニメーション名
	[[nodiscard]] bool contains(const std::string& name) const
	{
		return m_animations.contains(name);
	}

private:
	std::unordered_map<std::string, SpriteAnimation> m_animations;
};

// ────────────────────────────────────────────────────────────────
// SpriteSheet。スプライトシート（テクスチャ + フレーム配列）
// ────────────────────────────────────────────────────────────────

/// @brief スプライトシート
/// @details テクスチャとフレーム配列を保持する。
///          均一グリッドまたはJSON定義からフレームを生成する。
///
/// @code
/// Texture atlas = ...;
/// auto sheet = SpriteSheet::create(atlas, 32, 32, 8);
/// auto frame = sheet.getFrame(3);
/// @endcode
class SpriteSheet
{
public:
	/// @brief デフォルトコンストラクタ（空のスプライトシート）
	SpriteSheet() = default;

	/// @brief 均一グリッドのスプライトシートを生成する
	/// @param texture テクスチャ
	/// @param frameWidth 1フレームの幅（ピクセル）
	/// @param frameHeight 1フレームの高さ（ピクセル）
	/// @param frameCount フレーム数（0 の場合はテクスチャサイズから自動計算）
	/// @return SpriteSheet
	[[nodiscard]] static SpriteSheet create(const Texture& texture,
	                                        int frameWidth,
	                                        int frameHeight,
	                                        int frameCount = 0)
	{
		SpriteSheet sheet;
		sheet.m_texture = texture;
		sheet.m_frameWidth = frameWidth;
		sheet.m_frameHeight = frameHeight;

		const int cols = texture.width() / frameWidth;
		const int rows = texture.height() / frameHeight;
		const int maxFrames = cols * rows;
		const int count = (frameCount > 0) ? std::min(frameCount, maxFrames) : maxFrames;

		sheet.m_frames.reserve(static_cast<std::size_t>(count));

		for (int i = 0; i < count; ++i)
		{
			const int col = i % cols;
			const int row = i / cols;

			SpriteFrame frame;
			frame.sourceRect = sgc::Recti{col * frameWidth, row * frameHeight,
			                              frameWidth, frameHeight};
			frame.duration = 0.1f;
			sheet.m_frames.push_back(frame);
		}

		return sheet;
	}

	/// @brief フレーム配列とテクスチャから直接構築する
	/// @param texture テクスチャ
	/// @param frames フレーム配列
	/// @return SpriteSheet
	[[nodiscard]] static SpriteSheet createFromFrames(const Texture& texture,
	                                                  std::vector<SpriteFrame> frames)
	{
		SpriteSheet sheet;
		sheet.m_texture = texture;
		sheet.m_frames = std::move(frames);

		if (!sheet.m_frames.empty())
		{
			sheet.m_frameWidth = sheet.m_frames[0].sourceRect.width();
			sheet.m_frameHeight = sheet.m_frames[0].sourceRect.height();
		}

		return sheet;
	}

	/// @brief 指定インデックスのフレームを取得する
	/// @param index フレームインデックス
	/// @return フレーム情報（範囲外の場合は先頭フレーム）
	[[nodiscard]] SpriteFrame getFrame(int index) const
	{
		if (m_frames.empty())
		{
			return {};
		}
		const auto i = static_cast<std::size_t>(
			std::clamp(index, 0, static_cast<int>(m_frames.size()) - 1));
		return m_frames[i];
	}

	/// @brief フレーム数を返す
	[[nodiscard]] int frameCount() const noexcept
	{
		return static_cast<int>(m_frames.size());
	}

	/// @brief テクスチャへの定数参照を返す
	[[nodiscard]] const Texture& texture() const noexcept
	{
		return m_texture;
	}

	/// @brief 1フレームの幅を返す
	[[nodiscard]] int frameWidth() const noexcept { return m_frameWidth; }

	/// @brief 1フレームの高さを返す
	[[nodiscard]] int frameHeight() const noexcept { return m_frameHeight; }

	/// @brief 全フレームを返す
	[[nodiscard]] const std::vector<SpriteFrame>& frames() const noexcept
	{
		return m_frames;
	}

private:
	Texture m_texture;                ///< スプライトシートテクスチャ
	std::vector<SpriteFrame> m_frames; ///< フレーム配列
	int m_frameWidth = 0;             ///< 1フレームの幅
	int m_frameHeight = 0;            ///< 1フレームの高さ
};

// ────────────────────────────────────────────────────────────────
// SpriteAnimator。アニメーション再生コントローラ
// ────────────────────────────────────────────────────────────────

/// @brief アニメーション再生状態
enum class AnimatorState
{
	Playing,   ///< 再生中
	Paused,    ///< 一時停止
	Stopped,   ///< 停止
	Finished   ///< 再生完了（非ループ時）
};

/// @brief アニメーション再生方向
enum class AnimatorDirection
{
	Forward,   ///< 順再生
	Reverse    ///< 逆再生
};

/// @brief スプライトアニメーション再生コントローラ
/// @details SpriteAnimationSetからアニメーションを選択し、
///          フレームベースの時間制御で再生を管理する。
///
/// @code
/// SpriteAnimator animator;
/// animator.setAnimationSet(animSet);
/// animator.play("walk");
///
/// // ゲームループ内
/// animator.update(deltaTime);
/// auto frame = animator.currentFrame();
/// @endcode
class SpriteAnimator
{
public:
	/// @brief コールバック型
	using AnimationCallback = std::function<void(const std::string& animationName)>;
	using FrameCallback = std::function<void(int frameIndex)>;

	/// @brief アニメーションセットを設定する
	/// @param animationSet アニメーションセット
	void setAnimationSet(const SpriteAnimationSet& animationSet)
	{
		m_animationSet = animationSet;
		stop();
	}

	/// @brief アニメーションを再生する
	/// @param animationName 再生するアニメーション名
	/// @note 同じアニメーションを指定した場合も先頭から再開する
	void play(const std::string& animationName)
	{
		const auto* anim = m_animationSet.get(animationName);
		if (!anim || anim->frames.empty())
		{
			return;
		}

		m_currentAnimationName = animationName;
		m_currentAnimation = anim;
		m_state = AnimatorState::Playing;
		m_elapsed = 0.0f;
		m_currentFrameIndex = (m_direction == AnimatorDirection::Forward)
			? 0
			: static_cast<int>(anim->frames.size()) - 1;
		m_pingPongForward = true;
	}

	/// @brief 一時停止する
	void pause() noexcept
	{
		if (m_state == AnimatorState::Playing)
		{
			m_state = AnimatorState::Paused;
		}
	}

	/// @brief 一時停止から再開する
	void resume() noexcept
	{
		if (m_state == AnimatorState::Paused)
		{
			m_state = AnimatorState::Playing;
		}
	}

	/// @brief 停止する（先頭に戻る）
	void stop() noexcept
	{
		m_state = AnimatorState::Stopped;
		m_elapsed = 0.0f;
		m_currentFrameIndex = 0;
		m_currentAnimation = nullptr;
		m_currentAnimationName.clear();
	}

	/// @brief フレームを進める
	/// @param dt 経過時間（秒）
	void update(float dt)
	{
		if (m_state != AnimatorState::Playing || !m_currentAnimation)
		{
			return;
		}

		const auto& frames = m_currentAnimation->frames;
		if (frames.empty())
		{
			return;
		}

		m_elapsed += dt * m_speed;

		const float frameDuration = frames[static_cast<std::size_t>(m_currentFrameIndex)].duration;

		while (m_elapsed >= frameDuration && m_state == AnimatorState::Playing)
		{
			m_elapsed -= frameDuration;
			advanceFrame();
		}
	}

	/// @brief 現在のフレームを取得する
	/// @return 現在のSpriteFrame（アニメーション未設定の場合は空フレーム）
	[[nodiscard]] SpriteFrame currentFrame() const
	{
		if (!m_currentAnimation || m_currentAnimation->frames.empty())
		{
			return {};
		}
		const auto idx = static_cast<std::size_t>(
			std::clamp(m_currentFrameIndex, 0,
			           static_cast<int>(m_currentAnimation->frames.size()) - 1));
		return m_currentAnimation->frames[idx];
	}

	/// @brief 現在のフレームインデックスを返す
	[[nodiscard]] int currentFrameIndex() const noexcept
	{
		return m_currentFrameIndex;
	}

	/// @brief 再生中か
	[[nodiscard]] bool isPlaying() const noexcept
	{
		return m_state == AnimatorState::Playing;
	}

	/// @brief 再生完了したか（非ループアニメーション）
	[[nodiscard]] bool isFinished() const noexcept
	{
		return m_state == AnimatorState::Finished;
	}

	/// @brief 再生状態を返す
	[[nodiscard]] AnimatorState state() const noexcept
	{
		return m_state;
	}

	/// @brief 再生速度を設定する
	/// @param multiplier 速度倍率（0.5 = 半速、2.0 = 倍速）
	void setSpeed(float multiplier) noexcept
	{
		m_speed = std::max(0.0f, multiplier);
	}

	/// @brief 再生速度を返す
	[[nodiscard]] float speed() const noexcept
	{
		return m_speed;
	}

	/// @brief 再生方向を設定する
	/// @param direction 再生方向
	void setDirection(AnimatorDirection direction) noexcept
	{
		m_direction = direction;
	}

	/// @brief 再生方向を返す
	[[nodiscard]] AnimatorDirection direction() const noexcept
	{
		return m_direction;
	}

	/// @brief 現在のアニメーション名を返す
	[[nodiscard]] const std::string& currentAnimationName() const noexcept
	{
		return m_currentAnimationName;
	}

	/// @brief アニメーション完了時コールバックを設定する
	/// @param callback コールバック関数
	void onAnimationComplete(AnimationCallback callback)
	{
		m_onComplete = std::move(callback);
	}

	/// @brief フレーム変更時コールバックを設定する
	/// @param callback コールバック関数
	void onFrameChanged(FrameCallback callback)
	{
		m_onFrameChanged = std::move(callback);
	}

private:
	/// @brief フレームを1つ進める
	void advanceFrame()
	{
		if (!m_currentAnimation)
		{
			return;
		}

		const auto frameCount = static_cast<int>(m_currentAnimation->frames.size());
		const int prevFrame = m_currentFrameIndex;

		if (m_currentAnimation->pingPong)
		{
			advancePingPong(frameCount);
		}
		else if (m_direction == AnimatorDirection::Forward)
		{
			advanceForward(frameCount);
		}
		else
		{
			advanceReverse(frameCount);
		}

		if (m_currentFrameIndex != prevFrame && m_onFrameChanged)
		{
			m_onFrameChanged(m_currentFrameIndex);
		}
	}

	/// @brief 順方向にフレームを進める
	void advanceForward(int frameCount)
	{
		++m_currentFrameIndex;
		if (m_currentFrameIndex >= frameCount)
		{
			if (m_currentAnimation->looping)
			{
				m_currentFrameIndex = 0;
			}
			else
			{
				m_currentFrameIndex = frameCount - 1;
				m_state = AnimatorState::Finished;
				notifyComplete();
			}
		}
	}

	/// @brief 逆方向にフレームを進める
	void advanceReverse(int frameCount)
	{
		--m_currentFrameIndex;
		if (m_currentFrameIndex < 0)
		{
			if (m_currentAnimation->looping)
			{
				m_currentFrameIndex = frameCount - 1;
			}
			else
			{
				m_currentFrameIndex = 0;
				m_state = AnimatorState::Finished;
				notifyComplete();
			}
		}
	}

	/// @brief ピンポン再生でフレームを進める
	void advancePingPong(int frameCount)
	{
		if (m_pingPongForward)
		{
			++m_currentFrameIndex;
			if (m_currentFrameIndex >= frameCount)
			{
				m_currentFrameIndex = frameCount - 2;
				m_pingPongForward = false;

				if (m_currentFrameIndex < 0)
				{
					m_currentFrameIndex = 0;
				}
			}
		}
		else
		{
			--m_currentFrameIndex;
			if (m_currentFrameIndex < 0)
			{
				if (m_currentAnimation->looping)
				{
					m_currentFrameIndex = 1;
					m_pingPongForward = true;

					if (frameCount < 2)
					{
						m_currentFrameIndex = 0;
					}
				}
				else
				{
					m_currentFrameIndex = 0;
					m_state = AnimatorState::Finished;
					notifyComplete();
				}
			}
		}
	}

	/// @brief 完了コールバックを呼び出す
	void notifyComplete()
	{
		if (m_onComplete)
		{
			m_onComplete(m_currentAnimationName);
		}
	}

	SpriteAnimationSet m_animationSet;                  ///< アニメーションセット
	const SpriteAnimation* m_currentAnimation = nullptr; ///< 現在のアニメーション
	std::string m_currentAnimationName;                  ///< 現在のアニメーション名
	AnimatorState m_state = AnimatorState::Stopped;      ///< 再生状態
	AnimatorDirection m_direction = AnimatorDirection::Forward; ///< 再生方向
	int m_currentFrameIndex = 0;                         ///< 現在のフレームインデックス
	float m_elapsed = 0.0f;                              ///< フレーム内経過時間
	float m_speed = 1.0f;                                ///< 再生速度倍率
	bool m_pingPongForward = true;                       ///< ピンポン再生時の方向
	AnimationCallback m_onComplete;                      ///< 完了コールバック
	FrameCallback m_onFrameChanged;                      ///< フレーム変更コールバック
};

// ────────────────────────────────────────────────────────────────
// SpriteAnimationBuilder。Fluent APIによるアニメーション構築
// ────────────────────────────────────────────────────────────────

/// @brief SpriteAnimation を Fluent API で構築するビルダー
///
/// @code
/// auto walk = SpriteAnimationBuilder("walk")
///     .addFrame({0, 0, 32, 32}, 0.1f)
///     .addFrame({32, 0, 32, 32}, 0.1f)
///     .addFrame({64, 0, 32, 32}, 0.1f)
///     .setLooping(true)
///     .build();
/// @endcode
class SpriteAnimationBuilder
{
public:
	/// @brief コンストラクタ
	/// @param name アニメーション名
	explicit SpriteAnimationBuilder(std::string name)
	{
		m_animation.name = std::move(name);
	}

	/// @brief フレームを追加する
	/// @param sourceRect ソース矩形（ピクセル座標）
	/// @param duration 表示時間（秒）
	/// @param offsetX 描画オフセットX
	/// @param offsetY 描画オフセットY
	/// @return ビルダー自身への参照
	SpriteAnimationBuilder& addFrame(const sgc::Recti& sourceRect,
	                                 float duration,
	                                 float offsetX = 0.0f,
	                                 float offsetY = 0.0f)
	{
		m_animation.frames.push_back(SpriteFrame{sourceRect, duration, offsetX, offsetY});
		return *this;
	}

	/// @brief SpriteFrameを直接追加する
	/// @param frame フレーム
	/// @return ビルダー自身への参照
	SpriteAnimationBuilder& addFrame(const SpriteFrame& frame)
	{
		m_animation.frames.push_back(frame);
		return *this;
	}

	/// @brief SpriteSheet から連続フレームを追加する
	/// @param sheet スプライトシート
	/// @param startIndex 開始フレームインデックス
	/// @param count フレーム数
	/// @param frameDuration 各フレームの表示時間（秒）
	/// @return ビルダー自身への参照
	SpriteAnimationBuilder& addFramesFromSheet(const SpriteSheet& sheet,
	                                           int startIndex,
	                                           int count,
	                                           float frameDuration)
	{
		const int end = std::min(startIndex + count, sheet.frameCount());
		for (int i = startIndex; i < end; ++i)
		{
			auto frame = sheet.getFrame(i);
			frame.duration = frameDuration;
			m_animation.frames.push_back(frame);
		}
		return *this;
	}

	/// @brief ループ設定
	/// @param looping ループするか
	/// @return ビルダー自身への参照
	SpriteAnimationBuilder& setLooping(bool looping)
	{
		m_animation.looping = looping;
		return *this;
	}

	/// @brief ピンポン再生設定
	/// @param pingPong ピンポンするか
	/// @return ビルダー自身への参照
	SpriteAnimationBuilder& setPingPong(bool pingPong)
	{
		m_animation.pingPong = pingPong;
		return *this;
	}

	/// @brief アニメーションを構築する
	/// @return 構築された SpriteAnimation
	[[nodiscard]] SpriteAnimation build() const
	{
		return m_animation;
	}

private:
	SpriteAnimation m_animation;
};

// ────────────────────────────────────────────────────────────────
// SpriteAnimationCache。アニメーションセットのキャッシュ
// ────────────────────────────────────────────────────────────────

/// @brief アニメーションセットの名前付きキャッシュ
/// @details 複数エンティティ間でアニメーション定義を共有するためのキャッシュ。
///
/// @code
/// SpriteAnimationCache cache;
/// cache.store("player", playerAnimSet);
/// auto* set = cache.retrieve("player");
/// @endcode
class SpriteAnimationCache
{
public:
	/// @brief アニメーションセットを保存する
	/// @param name キャッシュキー
	/// @param animationSet アニメーションセット
	void store(const std::string& name, const SpriteAnimationSet& animationSet)
	{
		m_cache[name] = animationSet;
	}

	/// @brief アニメーションセットを保存する（ムーブ版）
	/// @param name キャッシュキー
	/// @param animationSet アニメーションセット
	void store(const std::string& name, SpriteAnimationSet&& animationSet)
	{
		m_cache[name] = std::move(animationSet);
	}

	/// @brief アニメーションセットを取得する
	/// @param name キャッシュキー
	/// @return アニメーションセットへのポインタ（未登録の場合 nullptr）
	[[nodiscard]] const SpriteAnimationSet* retrieve(const std::string& name) const
	{
		const auto it = m_cache.find(name);
		if (it == m_cache.end())
		{
			return nullptr;
		}
		return &it->second;
	}

	/// @brief キャッシュにエントリが存在するか
	/// @param name キャッシュキー
	[[nodiscard]] bool contains(const std::string& name) const
	{
		return m_cache.contains(name);
	}

	/// @brief キャッシュからエントリを削除する
	/// @param name キャッシュキー
	/// @return 削除されたか
	bool remove(const std::string& name)
	{
		return m_cache.erase(name) > 0;
	}

	/// @brief キャッシュをクリアする
	void clear()
	{
		m_cache.clear();
	}

	/// @brief キャッシュ内のエントリ数を返す
	[[nodiscard]] std::size_t count() const noexcept
	{
		return m_cache.size();
	}

private:
	std::unordered_map<std::string, SpriteAnimationSet> m_cache;
};

} // namespace mitiru::render
