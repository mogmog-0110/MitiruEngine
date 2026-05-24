#pragma once

/// @file SpriteSheet.hpp
/// @brief スプライトシート・テクスチャアトラス管理
///
/// スプライトシートのフレーム定義、アニメーション定義、
/// フレームベースのアニメーション再生機能を提供する。
///
/// @code
/// sgc::asset::SpriteSheet sheet;
/// sheet.name = "player";
/// sheet.atlasWidth = 256;
/// sheet.atlasHeight = 256;
/// sheet.addFrame("idle_0", {0, 0, 32, 32});
/// sheet.addFrame("idle_1", {32, 0, 32, 32});
///
/// sgc::asset::SpriteAnimation anim;
/// anim.name = "idle";
/// anim.frameIndices = {0, 1};
/// anim.frameDuration = 0.15f;
/// anim.loop = true;
///
/// sgc::asset::SpriteAnimator animator(sheet);
/// animator.play(anim);
/// animator.update(0.16f);
/// const auto& frame = animator.currentFrame();
/// @endcode

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sgc::asset
{

/// @brief スプライトフレーム（アトラス内の1コマ）
struct SpriteFrame
{
	int x = 0;       ///< アトラス内X座標
	int y = 0;       ///< アトラス内Y座標
	int width = 0;   ///< フレーム幅
	int height = 0;  ///< フレーム高さ
	float pivotX = 0.5f;  ///< ピボットX（0〜1、デフォルト中央）
	float pivotY = 0.5f;  ///< ピボットY（0〜1、デフォルト中央）

	[[nodiscard]] constexpr bool operator==(const SpriteFrame&) const noexcept = default;
};

/// @brief スプライトシート（テクスチャアトラス）
class SpriteSheet
{
public:
	std::string name;      ///< シート名
	int atlasWidth = 0;    ///< アトラス幅
	int atlasHeight = 0;   ///< アトラス高さ

	/// @brief 名前付きフレームを追加する
	/// @param frameName フレーム名
	/// @param frame フレームデータ
	void addFrame(const std::string& frameName, const SpriteFrame& frame)
	{
		m_nameToIndex[frameName] = m_frames.size();
		m_frames.push_back(frame);
	}

	/// @brief インデックスでフレームを取得する
	/// @param index フレームインデックス
	/// @return フレーム参照
	[[nodiscard]] const SpriteFrame& frame(size_t index) const
	{
		return m_frames.at(index);
	}

	/// @brief 名前でフレームを取得する
	/// @param frameName フレーム名
	/// @return フレーム（見つからない場合nullopt）
	[[nodiscard]] std::optional<SpriteFrame> frameByName(const std::string& frameName) const
	{
		const auto it = m_nameToIndex.find(frameName);
		if (it != m_nameToIndex.end())
		{
			return m_frames[it->second];
		}
		return std::nullopt;
	}

	/// @brief 名前からインデックスを取得する
	/// @param frameName フレーム名
	/// @return インデックス（見つからない場合nullopt）
	[[nodiscard]] std::optional<size_t> indexByName(const std::string& frameName) const
	{
		const auto it = m_nameToIndex.find(frameName);
		if (it != m_nameToIndex.end())
		{
			return it->second;
		}
		return std::nullopt;
	}

	/// @brief フレーム数を取得する
	[[nodiscard]] size_t frameCount() const noexcept { return m_frames.size(); }

	/// @brief 全フレームを取得する
	[[nodiscard]] const std::vector<SpriteFrame>& frames() const noexcept { return m_frames; }

private:
	std::vector<SpriteFrame> m_frames;
	std::unordered_map<std::string, size_t> m_nameToIndex;
};

/// @brief スプライトアニメーション定義
struct SpriteAnimation
{
	std::string name;                     ///< アニメーション名
	std::vector<size_t> frameIndices;     ///< フレームインデックス配列
	float frameDuration = 0.1f;           ///< 1フレームの表示時間（秒）
	bool loop = true;                     ///< ループ再生するか

	/// @brief アニメーションの総時間を取得する
	[[nodiscard]] float totalDuration() const noexcept
	{
		return frameDuration * static_cast<float>(frameIndices.size());
	}
};

/// @brief スプライトアニメーション再生器
class SpriteAnimator
{
public:
	/// @brief コンストラクタ
	/// @param sheet スプライトシートへの参照
	explicit SpriteAnimator(const SpriteSheet& sheet)
		: m_sheet(&sheet)
	{
	}

	/// @brief アニメーションを再生開始する
	/// @param animation アニメーション定義
	void play(const SpriteAnimation& animation)
	{
		m_animation = animation;
		m_elapsed = 0.0f;
		m_currentIndex = 0;
		m_finished = false;
	}

	/// @brief アニメーションを更新する
	/// @param dt 経過時間（秒）
	void update(float dt)
	{
		if (!m_animation.has_value() || m_animation->frameIndices.empty() || m_finished)
		{
			return;
		}

		m_elapsed += dt;
		const float duration = m_animation->frameDuration;
		if (duration <= 0.0f)
		{
			return;
		}

		const size_t totalFrames = m_animation->frameIndices.size();
		const auto rawIndex = static_cast<size_t>(m_elapsed / duration);

		if (m_animation->loop)
		{
			m_currentIndex = rawIndex % totalFrames;
		}
		else
		{
			if (rawIndex >= totalFrames)
			{
				m_currentIndex = totalFrames - 1;
				m_finished = true;
			}
			else
			{
				m_currentIndex = rawIndex;
			}
		}
	}

	/// @brief 現在のフレームを取得する
	/// @return 現在表示すべきスプライトフレーム
	[[nodiscard]] const SpriteFrame& currentFrame() const
	{
		if (m_animation.has_value() && !m_animation->frameIndices.empty())
		{
			const size_t frameIdx = m_animation->frameIndices[m_currentIndex];
			return m_sheet->frame(frameIdx);
		}
		// フォールバック: シートの先頭フレーム
		return m_sheet->frame(0);
	}

	/// @brief 現在のフレームインデックスを取得する
	[[nodiscard]] size_t currentIndex() const noexcept { return m_currentIndex; }

	/// @brief 経過時間を取得する
	[[nodiscard]] float elapsed() const noexcept { return m_elapsed; }

	/// @brief アニメーションが完了したか（非ループ時）
	[[nodiscard]] bool isFinished() const noexcept { return m_finished; }

	/// @brief アニメーションをリセットする
	void reset()
	{
		m_elapsed = 0.0f;
		m_currentIndex = 0;
		m_finished = false;
	}

private:
	const SpriteSheet* m_sheet = nullptr;
	std::optional<SpriteAnimation> m_animation;
	float m_elapsed = 0.0f;
	size_t m_currentIndex = 0;
	bool m_finished = false;
};

} // namespace sgc::asset
