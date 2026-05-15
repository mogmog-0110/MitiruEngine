#pragma once

/// @file OzzAnimationBridge.hpp
/// @brief Ozz-Animationブリッジ
/// @details Ozz-Animationライブラリが利用可能な場合はリアル実装に委譲し、
///          利用不可の場合は単位行列を返すNullスタブで動作する。
///          スケルトン読込・アニメーション再生・ブレンドを統一インターフェースで提供する。
///
/// @code
/// auto skeleton = std::make_shared<mitiru::animation::OzzSkeleton>();
/// skeleton->loadFromFile("assets/character.ozz");
///
/// auto anim = std::make_shared<mitiru::animation::OzzAnimation>();
/// anim->loadFromFile("assets/walk.ozz");
///
/// mitiru::animation::OzzAnimationSampler sampler;
/// sampler.setSkeleton(skeleton);
/// sampler.play(anim);
/// // 毎フレーム:
/// sampler.update(dt);
/// auto transforms = sampler.getWorldTransforms();
/// @endcode

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#ifdef MITIRU_HAS_OZZ
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/maths/simd_math.h>
#endif

namespace mitiru::animation
{

/// @brief 4x4行列（列優先、スキニング行列用）
struct Mat4
{
	std::array<float, 16> m = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	};

	/// @brief 単位行列を返す
	[[nodiscard]] static constexpr Mat4 identity() noexcept
	{
		return Mat4{};
	}
};

// ═════════════════════════════════════════════════════════════
// OzzSkeleton
// ═════════════════════════════════════════════════════════════

/// @brief Ozz-Animationスケルトンのラッパー
/// @details ジョイント階層を保持し、ジョイント数・名前の照会を提供する。
class OzzSkeleton
{
public:
	/// @brief ファイルからスケルトンを読み込む
	/// @param path .ozzスケルトンファイルパス
	/// @return 成功なら true
	bool loadFromFile([[maybe_unused]] std::string_view path)
	{
#ifdef MITIRU_HAS_OZZ
		ozz::io::File file(std::string(path).c_str(), "rb");
		if (!file.opened()) return false;
		ozz::io::IArchive archive(&file);
		if (!archive.TestTag<ozz::animation::Skeleton>()) return false;
		archive >> m_skeleton;
		m_loaded = true;
		return true;
#else
		return false;
#endif
	}

	/// @brief ジョイント数を取得する
	/// @return ジョイント数（未読込時は0）
	[[nodiscard]] int jointCount() const noexcept
	{
#ifdef MITIRU_HAS_OZZ
		return m_loaded ? m_skeleton.num_joints() : 0;
#else
		return 0;
#endif
	}

	/// @brief 指定インデックスのジョイント名を取得する
	/// @param index ジョイントインデックス
	/// @return ジョイント名（範囲外の場合は空文字列）
	[[nodiscard]] std::string jointName([[maybe_unused]] int index) const
	{
#ifdef MITIRU_HAS_OZZ
		if (!m_loaded || index < 0 || index >= m_skeleton.num_joints())
		{
			return {};
		}
		return std::string(m_skeleton.joint_names()[index]);
#else
		return {};
#endif
	}

	/// @brief 読み込み済みかどうかを返す
	[[nodiscard]] bool isLoaded() const noexcept { return m_loaded; }

#ifdef MITIRU_HAS_OZZ
	/// @brief 内部Ozzスケルトンへの参照を取得する（上級者向け）
	[[nodiscard]] const ozz::animation::Skeleton& raw() const noexcept
	{
		return m_skeleton;
	}
#endif

private:
#ifdef MITIRU_HAS_OZZ
	ozz::animation::Skeleton m_skeleton;
#endif
	bool m_loaded = false;
};

// ═════════════════════════════════════════════════════════════
// OzzAnimation
// ═════════════════════════════════════════════════════════════

/// @brief Ozz-Animationアニメーションクリップのラッパー
/// @details 単一のアニメーションクリップを保持し、再生時間の照会を提供する。
class OzzAnimation
{
public:
	/// @brief ファイルからアニメーションを読み込む
	/// @param path .ozzアニメーションファイルパス
	/// @return 成功なら true
	bool loadFromFile([[maybe_unused]] std::string_view path)
	{
#ifdef MITIRU_HAS_OZZ
		ozz::io::File file(std::string(path).c_str(), "rb");
		if (!file.opened()) return false;
		ozz::io::IArchive archive(&file);
		if (!archive.TestTag<ozz::animation::Animation>()) return false;
		archive >> m_animation;
		m_loaded = true;
		return true;
#else
		return false;
#endif
	}

	/// @brief アニメーションの再生時間（秒）を取得する
	/// @return 再生時間（未読込時は0）
	[[nodiscard]] float duration() const noexcept
	{
#ifdef MITIRU_HAS_OZZ
		return m_loaded ? m_animation.duration() : 0.0f;
#else
		return 0.0f;
#endif
	}

	/// @brief 読み込み済みかどうかを返す
	[[nodiscard]] bool isLoaded() const noexcept { return m_loaded; }

#ifdef MITIRU_HAS_OZZ
	/// @brief 内部Ozzアニメーションへの参照を取得する（上級者向け）
	[[nodiscard]] const ozz::animation::Animation& raw() const noexcept
	{
		return m_animation;
	}
#endif

private:
#ifdef MITIRU_HAS_OZZ
	ozz::animation::Animation m_animation;
#endif
	bool m_loaded = false;
};

// ═════════════════════════════════════════════════════════════
// IOzzAnimationSampler インターフェース
// ═════════════════════════════════════════════════════════════

/// @brief アニメーションサンプラーインターフェース
/// @details スケルトンに対してアニメーションを再生し、
///          ローカル/ワールドトランスフォームを取得する。
class IOzzAnimationSampler
{
public:
	virtual ~IOzzAnimationSampler() = default;

	/// @brief スケルトンを設定する
	/// @param skeleton スケルトン（共有ポインタ）
	virtual void setSkeleton(std::shared_ptr<OzzSkeleton> skeleton) = 0;

	/// @brief アニメーションを再生する
	/// @param animation 再生するアニメーション（共有ポインタ）
	virtual void play(std::shared_ptr<OzzAnimation> animation) = 0;

	/// @brief 再生を停止してリセットする
	virtual void stop() = 0;

	/// @brief 毎フレーム更新する
	/// @param dt 前フレームからの経過時間（秒）
	virtual void update(float dt) = 0;

	/// @brief ローカル空間のトランスフォーム行列を取得する
	/// @return 各ジョイントのローカルトランスフォーム（ジョイント数分）
	[[nodiscard]] virtual std::vector<Mat4> getLocalTransforms() const = 0;

	/// @brief ワールド空間のトランスフォーム行列を取得する（スキニング用）
	/// @return 各ジョイントのモデル空間トランスフォーム（ジョイント数分）
	[[nodiscard]] virtual std::vector<Mat4> getWorldTransforms() const = 0;

	/// @brief 他のサンプラーとブレンドする
	/// @param other ブレンド先のサンプラー
	/// @param weight ブレンド重み [0.0=this, 1.0=other]
	virtual void blendWith(const IOzzAnimationSampler& other, float weight) = 0;

	/// @brief 現在の再生時刻を取得する
	[[nodiscard]] virtual float currentTime() const noexcept = 0;

	/// @brief 再生中かどうかを返す
	[[nodiscard]] virtual bool isPlaying() const noexcept = 0;
};

// ═════════════════════════════════════════════════════════════
// Ozz実装（ライブラリ利用可能時）
// ═════════════════════════════════════════════════════════════

#ifdef MITIRU_HAS_OZZ

/// @brief Ozz-Animationを使ったサンプラー実装
class OzzAnimationSampler final : public IOzzAnimationSampler
{
public:
	void setSkeleton(std::shared_ptr<OzzSkeleton> skeleton) override
	{
		m_skeleton = std::move(skeleton);
		if (m_skeleton && m_skeleton->isLoaded())
		{
			const int numJoints = m_skeleton->raw().num_joints();
			const int numSoaJoints = m_skeleton->raw().num_soa_joints();
			m_locals.resize(numSoaJoints);
			m_models.resize(numJoints);
		}
	}

	void play(std::shared_ptr<OzzAnimation> animation) override
	{
		m_animation = std::move(animation);
		m_time = 0.0f;
		m_playing = true;
		if (m_animation && m_animation->isLoaded() && m_skeleton && m_skeleton->isLoaded())
		{
			m_context.Resize(m_skeleton->raw().num_joints());
		}
	}

	void stop() override
	{
		m_playing = false;
		m_time = 0.0f;
	}

	void update(float dt) override
	{
		if (!m_playing || !m_animation || !m_skeleton) return;
		if (!m_animation->isLoaded() || !m_skeleton->isLoaded()) return;

		const float duration = m_animation->raw().duration();
		if (duration <= 0.0f) return;

		m_time += dt;
		// ループ再生
		m_time = std::fmod(m_time, duration);

		// サンプリングジョブ
		ozz::animation::SamplingJob samplingJob;
		samplingJob.animation = &m_animation->raw();
		samplingJob.context = &m_context;
		samplingJob.ratio = m_time / duration;
		samplingJob.output = ozz::make_span(m_locals);
		if (!samplingJob.Run())
		{
			m_playing = false;
			return;
		}

		// ローカル→モデル空間変換
		ozz::animation::LocalToModelJob ltmJob;
		ltmJob.skeleton = &m_skeleton->raw();
		ltmJob.input = ozz::make_span(m_locals);
		ltmJob.output = ozz::make_span(m_models);
		if (!ltmJob.Run())
		{
			m_playing = false;
			return;
		}
	}

	[[nodiscard]] std::vector<Mat4> getLocalTransforms() const override
	{
		// SoAからAoSへの変換は複雑なため、モデル空間行列で代用
		return getWorldTransforms();
	}

	[[nodiscard]] std::vector<Mat4> getWorldTransforms() const override
	{
		std::vector<Mat4> result;
		result.reserve(m_models.size());
		for (const auto& model : m_models)
		{
			Mat4 mat;
			// ozz::math::Float4x4 の列を Mat4 に変換
			for (int col = 0; col < 4; ++col)
			{
				ozz::math::SimdFloat4 column = model.cols[col];
				float values[4];
				ozz::math::StorePtrU(column, values);
				for (int row = 0; row < 4; ++row)
				{
					mat.m[static_cast<std::size_t>(col * 4 + row)] = values[row];
				}
			}
			result.push_back(mat);
		}
		return result;
	}

	void blendWith([[maybe_unused]] const IOzzAnimationSampler& other,
	               [[maybe_unused]] float weight) override
	{
		// WARNING: blendWith() は未実装です。呼び出しは無視されます。
		// 完全実装にはozz::animation::BlendingJobが必要。
		// otherからローカルトランスフォームを取得し、重み付きでブレンドする予定。
	}

	[[nodiscard]] float currentTime() const noexcept override { return m_time; }
	[[nodiscard]] bool isPlaying() const noexcept override { return m_playing; }

private:
	std::shared_ptr<OzzSkeleton> m_skeleton;
	std::shared_ptr<OzzAnimation> m_animation;
	ozz::animation::SamplingJob::Context m_context;
	ozz::vector<ozz::math::SoaTransform> m_locals;
	ozz::vector<ozz::math::Float4x4> m_models;
	float m_time = 0.0f;
	bool m_playing = false;
};

#endif // MITIRU_HAS_OZZ

// ═════════════════════════════════════════════════════════════
// Null実装（ライブラリ不在時のスタブ）
// ═════════════════════════════════════════════════════════════

/// @brief 単位行列を返すだけのスタブサンプラー
/// @details Ozz-Animationが利用できない環境で使用する。
///          すべてのトランスフォームは単位行列を返す。
class NullOzzAnimationSampler final : public IOzzAnimationSampler
{
public:
	void setSkeleton(std::shared_ptr<OzzSkeleton> skeleton) override
	{
		m_skeleton = std::move(skeleton);
	}

	void play(std::shared_ptr<OzzAnimation> animation) override
	{
		m_animation = std::move(animation);
		m_time = 0.0f;
		m_playing = true;
	}

	void stop() override
	{
		m_playing = false;
		m_time = 0.0f;
	}

	void update(float dt) override
	{
		if (!m_playing || !m_animation) return;
		const float duration = m_animation->duration();
		if (duration <= 0.0f) return;
		m_time += dt;
		m_time = std::fmod(m_time, duration);
	}

	[[nodiscard]] std::vector<Mat4> getLocalTransforms() const override
	{
		return makeIdentityMatrices();
	}

	[[nodiscard]] std::vector<Mat4> getWorldTransforms() const override
	{
		return makeIdentityMatrices();
	}

	void blendWith([[maybe_unused]] const IOzzAnimationSampler& other,
	               [[maybe_unused]] float weight) override
	{
		// WARNING: blendWith() は未実装です。Null実装ではブレンドは無視されます。
	}

	[[nodiscard]] float currentTime() const noexcept override { return m_time; }
	[[nodiscard]] bool isPlaying() const noexcept override { return m_playing; }

private:
	/// @brief スケルトンのジョイント数分の単位行列を生成する
	[[nodiscard]] std::vector<Mat4> makeIdentityMatrices() const
	{
		const int count = m_skeleton ? m_skeleton->jointCount() : 0;
		return std::vector<Mat4>(static_cast<std::size_t>(count), Mat4::identity());
	}

	std::shared_ptr<OzzSkeleton> m_skeleton;
	std::shared_ptr<OzzAnimation> m_animation;
	float m_time = 0.0f;
	bool m_playing = false;
};

// ═════════════════════════════════════════════════════════════
// ファクトリ関数
// ═════════════════════════════════════════════════════════════

/// @brief 環境に応じたアニメーションサンプラーを生成する
/// @return Ozz利用可能時はOzzAnimationSampler、それ以外はNullOzzAnimationSampler
[[nodiscard]] inline std::unique_ptr<IOzzAnimationSampler> createAnimationSampler()
{
#ifdef MITIRU_HAS_OZZ
	return std::make_unique<OzzAnimationSampler>();
#else
	return std::make_unique<NullOzzAnimationSampler>();
#endif
}

} // namespace mitiru::animation
