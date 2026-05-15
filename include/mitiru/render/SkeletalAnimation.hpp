#pragma once
/// @file SkeletalAnimation.hpp
/// @brief スケルタルアニメーション（ボーン階層・キーフレーム・スキニング）

#include <sgc/math/Vec3.hpp>
#include <sgc/math/Mat4.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::render {

/// @brief ボーン（関節）
struct Bone {
    std::string name;
    int parentIndex = -1;      ///< 親ボーンのインデックス (-1=ルート)
    sgc::Mat4f bindPose;       ///< バインドポーズ（初期姿勢）の逆行列
    sgc::Mat4f localTransform; ///< ローカル変換
};

/// @brief キーフレーム
struct AnimationKeyframe {
    float time = 0.0f;
    sgc::Vec3f position{};
    sgc::Vec3f rotation{};   // Euler angles
    sgc::Vec3f scale{1,1,1};
};

/// @brief ボーン1本分のアニメーショントラック
struct BoneTrack {
    int boneIndex = -1;
    std::vector<AnimationKeyframe> keyframes;
};

/// @brief アニメーションクリップ
struct AnimationClip {
    std::string name;
    float duration = 0.0f;
    bool looping = true;
    std::vector<BoneTrack> tracks;
};

/// @brief モーフターゲット（ブレンドシェイプ）
struct MorphTarget {
    std::string name;
    std::vector<sgc::Vec3f> positionDeltas; ///< 頂点位置の差分
    float weight = 0.0f;                     ///< 現在のウェイト (0-1)
};

/// @brief スケルトン（ボーン階層）
class Skeleton {
public:
    void addBone(const Bone& bone) {
        m_nameToIndex[bone.name] = static_cast<int>(m_bones.size());
        m_bones.push_back(bone);
    }

    [[nodiscard]] int boneCount() const noexcept { return static_cast<int>(m_bones.size()); }

    [[nodiscard]] const Bone& bone(int index) const { return m_bones[static_cast<size_t>(index)]; }
    Bone& bone(int index) { return m_bones[static_cast<size_t>(index)]; }

    [[nodiscard]] int findBone(const std::string& name) const {
        auto it = m_nameToIndex.find(name);
        return (it != m_nameToIndex.end()) ? it->second : -1;
    }

    /// @brief 全ボーンのワールド変換行列を計算する
    [[nodiscard]] std::vector<sgc::Mat4f> computeWorldTransforms() const {
        std::vector<sgc::Mat4f> world(m_bones.size(), sgc::Mat4f::identity());
        for (size_t i = 0; i < m_bones.size(); ++i) {
            if (m_bones[i].parentIndex >= 0) {
                world[i] = world[static_cast<size_t>(m_bones[i].parentIndex)] * m_bones[i].localTransform;
            } else {
                world[i] = m_bones[i].localTransform;
            }
        }
        return world;
    }

    /// @brief スキニング行列（ワールド x バインドポーズ逆行列）を計算する
    [[nodiscard]] std::vector<sgc::Mat4f> computeSkinningMatrices() const {
        auto world = computeWorldTransforms();
        for (size_t i = 0; i < m_bones.size(); ++i) {
            world[i] = world[i] * m_bones[i].bindPose;
        }
        return world;
    }

private:
    std::vector<Bone> m_bones;
    std::unordered_map<std::string, int> m_nameToIndex;
};

/// @brief アニメーションプレイヤー
class AnimationPlayer3D {
public:
    void setSkeleton(Skeleton* skeleton) noexcept { m_skeleton = skeleton; }

    void play(const AnimationClip& clip) {
        m_clip = &clip;
        m_time = 0.0f;
        m_playing = true;
    }

    void stop() { m_playing = false; }

    void update(float dt) {
        if (!m_playing || !m_clip || !m_skeleton) return;

        m_time += dt;
        if (m_time > m_clip->duration) {
            if (m_clip->looping) {
                m_time = std::fmod(m_time, m_clip->duration);
            } else {
                m_time = m_clip->duration;
                m_playing = false;
            }
        }

        // Apply keyframes to skeleton
        for (const auto& track : m_clip->tracks) {
            if (track.boneIndex < 0 || track.boneIndex >= m_skeleton->boneCount()) continue;

            auto kf = interpolateKeyframe(track, m_time);
            auto& boneRef = m_skeleton->bone(track.boneIndex);
            boneRef.localTransform =
                sgc::Mat4f::translation(kf.position) *
                sgc::Mat4f::rotationY(kf.rotation.y) *
                sgc::Mat4f::rotationX(kf.rotation.x) *
                sgc::Mat4f::rotationZ(kf.rotation.z) *
                sgc::Mat4f::scaling(kf.scale);
        }
    }

    [[nodiscard]] bool isPlaying() const noexcept { return m_playing; }
    [[nodiscard]] float currentTime() const noexcept { return m_time; }

private:
    Skeleton* m_skeleton = nullptr;
    const AnimationClip* m_clip = nullptr;
    float m_time = 0.0f;
    bool m_playing = false;

    static AnimationKeyframe interpolateKeyframe(const BoneTrack& track, float time) {
        if (track.keyframes.empty()) return {};
        if (track.keyframes.size() == 1) return track.keyframes[0];

        // Find surrounding keyframes
        size_t next = 0;
        for (size_t i = 0; i < track.keyframes.size(); ++i) {
            if (track.keyframes[i].time >= time) { next = i; break; }
            next = i;
        }
        size_t prev = (next > 0) ? next - 1 : 0;

        if (prev == next) return track.keyframes[prev];

        const auto& kfA = track.keyframes[prev];
        const auto& kfB = track.keyframes[next];
        float range = kfB.time - kfA.time;
        float t = (range > 0.0001f) ? (time - kfA.time) / range : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);

        AnimationKeyframe result;
        result.time = time;
        result.position = lerpVec(kfA.position, kfB.position, t);
        result.rotation = lerpVec(kfA.rotation, kfB.rotation, t);
        result.scale = lerpVec(kfA.scale, kfB.scale, t);
        return result;
    }

    static sgc::Vec3f lerpVec(const sgc::Vec3f& a, const sgc::Vec3f& b, float t) {
        return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
    }
};

} // namespace mitiru::render
