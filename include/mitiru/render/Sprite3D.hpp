#pragma once

/// @file Sprite3D.hpp
/// @brief 2.5D スプライト描画システム（ダンガンロンパスタイル）
/// @details 3D空間内に2Dの板ポリゴン(Quad)を配置し、ビルボード・ポップアップ演出・
///          アルファクリッピングを行う。WebGL2/GLES3.0対応。
///
/// 3つのコア機能:
/// 1. **円柱ビルボード**。カメラのY軸のみに追従して回転（X軸は固定）
/// 2. **ポップアップ演出**。足元ピボットでX軸回転、イージングで「パタン」と起き上がる
/// 3. **アルファクリッピング**。discard でZバッファを透過ピクセルが汚さない

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/math/Vec3.hpp>

namespace mitiru::render
{

// ============================================================================
// Quad メッシュ生成
// ============================================================================

/// @brief 板ポリゴン頂点（3D空間用）
struct Vertex3DSprite
{
	sgc::Vec3f position;  ///< ローカル座標
	sgc::Vec2f texCoord;  ///< UV座標
};

/// @brief ピボット位置
enum class SpritePivot : std::uint8_t
{
	Center,       ///< 中央（通常ビルボード）
	BottomCenter  ///< 下辺中央（ポップアップ演出用）
};

/// @brief 板ポリゴンQuadメッシュを生成する
/// @param width  幅
/// @param height 高さ
/// @param pivot  ピボット位置
/// @return 頂点4つ + インデックス6つ
struct QuadMesh
{
	std::array<Vertex3DSprite, 4> vertices;
	std::array<std::uint32_t, 6> indices;

	/// @brief Quadメッシュを生成する
	/// @param width  幅（ワールド単位）
	/// @param height 高さ（ワールド単位）
	/// @param pivot  ピボット位置
	/// @return 生成されたQuadMesh
	[[nodiscard]] static QuadMesh create(float width, float height,
	                                     SpritePivot pivot = SpritePivot::Center) noexcept
	{
		QuadMesh mesh;
		const float hw = width * 0.5f;

		float yBottom = 0.0f;
		float yTop = 0.0f;

		if (pivot == SpritePivot::BottomCenter)
		{
			/// 原点 = 足元（下辺中央）
			/// ポップアップ演出: X軸回転で足元を軸に起き上がる
			yBottom = 0.0f;
			yTop = height;
		}
		else
		{
			/// 原点 = 中央
			yBottom = -height * 0.5f;
			yTop = height * 0.5f;
		}

		/// 頂点配置（ローカルXY平面、Z=0）
		///   3---2
		///   |   |
		///   0---1
		mesh.vertices[0] = {{-hw, yBottom, 0.0f}, {0.0f, 1.0f}}; // 左下
		mesh.vertices[1] = {{ hw, yBottom, 0.0f}, {1.0f, 1.0f}}; // 右下
		mesh.vertices[2] = {{ hw, yTop,    0.0f}, {1.0f, 0.0f}}; // 右上
		mesh.vertices[3] = {{-hw, yTop,    0.0f}, {0.0f, 0.0f}}; // 左上

		/// 三角形2枚（CCW）
		mesh.indices = {0, 1, 2, 0, 2, 3};

		return mesh;
	}
};

// ============================================================================
// 円柱ビルボード（Y軸のみ追従）
// ============================================================================

/// @brief 円柱ビルボード行列を計算する
/// @details カメラのY軸のみに追従し、X軸の傾きは固定。
///          キャラクターが常にカメラを「横向きに」見るが、倒れない。
///
/// @param objectPos  オブジェクトのワールド位置
/// @param cameraPos  カメラのワールド位置
/// @return ビルボード回転行列（Y軸回転のみ）
[[nodiscard]] inline sgc::Mat4f cylindricalBillboardMatrix(
	const sgc::Vec3f& objectPos,
	const sgc::Vec3f& cameraPos) noexcept
{
	/// カメラへの方向ベクトル（Y成分を無視 → XZ平面上の角度のみ）
	const float dx = cameraPos.x - objectPos.x;
	const float dz = cameraPos.z - objectPos.z;

	/// atan2 でY軸回りの回転角を求める
	const float angle = std::atan2(dx, dz);

	const float cosA = std::cos(angle);
	const float sinA = std::sin(angle);

	/// Y軸回転 × 平行移動
	/// sgc::Mat4f は row-major だが GL_TRUE で転置送信するため、
	/// row-major のまま構築する
	const auto rotation = sgc::Mat4f{
		cosA,  0.0f, sinA,  0.0f,
		0.0f,  1.0f, 0.0f,  0.0f,
		-sinA, 0.0f, cosA,  0.0f,
		0.0f,  0.0f, 0.0f,  1.0f
	};
	const auto translation = sgc::Mat4f::translation(objectPos);
	const auto result = translation * rotation;

	return result;
}

// ============================================================================
// ポップアップ演出（イージング + X軸回転）
// ============================================================================

/// @brief イージング関数群
namespace easing
{
	/// @brief バウンス付きイーズアウト
	/// @param t 正規化時間 [0, 1]
	/// @return イージング適用後の値
	[[nodiscard]] inline float bounceOut(float t) noexcept
	{
		t = std::clamp(t, 0.0f, 1.0f);
		if (t < 1.0f / 2.75f)
		{
			return 7.5625f * t * t;
		}
		if (t < 2.0f / 2.75f)
		{
			t -= 1.5f / 2.75f;
			return 7.5625f * t * t + 0.75f;
		}
		if (t < 2.5f / 2.75f)
		{
			t -= 2.25f / 2.75f;
			return 7.5625f * t * t + 0.9375f;
		}
		t -= 2.625f / 2.75f;
		return 7.5625f * t * t + 0.984375f;
	}

	/// @brief エラスティック（弾性）イーズアウト
	/// @param t 正規化時間 [0, 1]
	/// @return イージング適用後の値
	[[nodiscard]] inline float elasticOut(float t) noexcept
	{
		t = std::clamp(t, 0.0f, 1.0f);
		if (t <= 0.0f || t >= 1.0f)
		{
			return t;
		}
		constexpr float kPi = 3.14159265358979323846f;
		constexpr float p = 0.3f;
		return std::pow(2.0f, -10.0f * t)
			* std::sin((t - p / 4.0f) * (2.0f * kPi) / p) + 1.0f;
	}

	/// @brief 滑らかなイーズアウト（キュービック）
	/// @param t 正規化時間 [0, 1]
	/// @return イージング適用後の値
	[[nodiscard]] inline float cubicOut(float t) noexcept
	{
		t = std::clamp(t, 0.0f, 1.0f);
		const float f = t - 1.0f;
		return f * f * f + 1.0f;
	}

	/// @brief オーバーシュート付きイーズアウト（バック）
	/// @param t 正規化時間 [0, 1]
	/// @param overshoot オーバーシュート量（デフォルト1.70158）
	/// @return イージング適用後の値
	[[nodiscard]] inline float backOut(float t, float overshoot = 1.70158f) noexcept
	{
		t = std::clamp(t, 0.0f, 1.0f) - 1.0f;
		return t * t * ((overshoot + 1.0f) * t + overshoot) + 1.0f;
	}
} // namespace easing

/// @brief ポップアップ演出の状態
/// @details X軸回転で足元を軸に「パタン」と起き上がるアニメーション。
///          t=0で地面に伏せた状態（-90度）、t=1で直立（0度）。
struct PopupAnimation
{
	float duration = 0.4f;        ///< アニメーション時間（秒）
	float elapsed = 0.0f;         ///< 経過時間（秒）
	bool active = false;          ///< アニメーション中か
	bool finished = false;        ///< 完了したか

	/// @brief アニメーションを開始する
	/// @param dur アニメーション時間（秒）
	void start(float dur = 0.4f) noexcept
	{
		duration = dur;
		elapsed = 0.0f;
		active = true;
		finished = false;
	}

	/// @brief フレーム更新
	/// @param dt デルタタイム（秒）
	void update(float dt) noexcept
	{
		if (!active)
		{
			return;
		}
		elapsed += dt;
		if (elapsed >= duration)
		{
			elapsed = duration;
			active = false;
			finished = true;
		}
	}

	/// @brief 正規化進捗 [0, 1]
	[[nodiscard]] float progress() const noexcept
	{
		if (duration <= 0.0f)
		{
			return 1.0f;
		}
		return std::clamp(elapsed / duration, 0.0f, 1.0f);
	}

	/// @brief 現在のX軸回転角（ラジアン）
	/// @details t=0 → -PI/2（地面に伏せ）、t=1 → 0（直立）
	///          elasticOutで弾むような起き上がり
	[[nodiscard]] float rotationX() const noexcept
	{
		constexpr float kHalfPi = 3.14159265358979323846f * 0.5f;
		const float t = easing::elasticOut(progress());
		/// -90度 → 0度 の補間
		return -kHalfPi * (1.0f - t);
	}
};

/// @brief ポップアップ演出用のX軸回転行列を生成する
/// @param angle X軸回転角（ラジアン）
/// @return X軸回転行列
[[nodiscard]] inline sgc::Mat4f popupRotationMatrix(float angle) noexcept
{
	const float cosA = std::cos(angle);
	const float sinA = std::sin(angle);

	return sgc::Mat4f{
		1.0f, 0.0f,  0.0f,  0.0f,
		0.0f, cosA,  -sinA, 0.0f,
		0.0f, sinA,  cosA,  0.0f,
		0.0f, 0.0f,  0.0f,  1.0f
	};
}

/// @brief スプライト3Dのワールド行列を構築する
/// @details ポップアップ回転 → ビルボード回転 → 平行移動 の順で合成
/// @param position ワールド位置
/// @param cameraPos カメラ位置
/// @param popupAngle ポップアップX軸回転角（ラジアン、0=直立）
/// @param scale スケール（デフォルト1.0）
/// @return ワールド変換行列
[[nodiscard]] inline sgc::Mat4f sprite3DWorldMatrix(
	const sgc::Vec3f& position,
	const sgc::Vec3f& cameraPos,
	float popupAngle = 0.0f,
	float scale = 1.0f) noexcept
{
	/// 1. ポップアップ回転（ローカル空間でX軸回転）
	const auto popup = popupRotationMatrix(popupAngle);

	/// 2. 円柱ビルボード（Y軸のみカメラ追従）
	const auto billboard = cylindricalBillboardMatrix(position, cameraPos);

	/// 3. スケール
	const auto scaleMatrix = sgc::Mat4f::scaling({scale, scale, scale});

	/// 合成: Billboard * Popup * Scale
	/// （スケール → ポップアップ回転 → ビルボード回転+位置）
	return billboard * popup * scaleMatrix;
}

// ============================================================================
// アルファクリッピングシェーダー（GLSL ES 3.0）
// ============================================================================

/// @brief 2.5Dスプライト用頂点シェーダー
/// @details MVP変換を適用し、UV座標をフラグメントシェーダーに渡す。
constexpr const char* SPRITE3D_VERTEX_SHADER = R"glsl(#version 300 es
precision highp float;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;

uniform mat4 uMVP;

out vec2 vTexCoord;

void main()
{
    gl_Position = uMVP * vec4(aPos, 1.0);
    vTexCoord = aTexCoord;
}
)glsl";

/// @brief 2.5Dスプライト用フラグメントシェーダー
/// @details アルファクリッピングにより透過ピクセルがZバッファを汚さない。
///          - uAlphaClip 以下のアルファ値を持つピクセルは discard
///          - テクスチャと uTint 色を乗算して最終色を決定
constexpr const char* SPRITE3D_FRAGMENT_SHADER = R"glsl(#version 300 es
precision highp float;

in vec2 vTexCoord;

uniform sampler2D uTexture;
uniform vec4 uTint;       // スプライトのティント色（デフォルト白）
uniform float uAlphaClip; // アルファクリッピング閾値（通常0.1〜0.5）

out vec4 fragColor;

void main()
{
    vec4 texel = texture(uTexture, vTexCoord) * uTint;

    // アルファクリッピング:
    // 透過ピクセル(α < threshold)を完全に破棄し、
    // Zバッファへの書き込みを防ぐ。
    // これにより半透明の重なり順が正しく処理される。
    if (texel.a < uAlphaClip)
    {
        discard;
    }

    fragColor = texel;
}
)glsl";

/// @brief テクスチャなし版フラグメントシェーダー（単色スプライト用）
/// @details 開発中やプレースホルダーとして、テクスチャなしで色付き板ポリを描画する。
constexpr const char* SPRITE3D_FLAT_FRAGMENT_SHADER = R"glsl(#version 300 es
precision highp float;

in vec2 vTexCoord;

uniform vec4 uTint;
uniform float uAlphaClip;

out vec4 fragColor;

void main()
{
    if (uTint.a < uAlphaClip)
    {
        discard;
    }

    fragColor = uTint;
}
)glsl";

// ============================================================================
// Sprite3D インスタンスデータ
// ============================================================================

/// @brief 3D空間内のスプライト1枚分のデータ
struct Sprite3DInstance
{
	sgc::Vec3f position{0.0f, 0.0f, 0.0f}; ///< ワールド位置（足元）
	float width = 1.0f;                      ///< 幅（ワールド単位）
	float height = 1.5f;                     ///< 高さ（ワールド単位）
	float scale = 1.0f;                      ///< 追加スケール
	sgc::Vec4f tint{1.0f, 1.0f, 1.0f, 1.0f}; ///< ティント色
	float alphaClip = 0.1f;                  ///< アルファクリッピング閾値
	SpritePivot pivot = SpritePivot::BottomCenter; ///< ピボット位置
	PopupAnimation popup;                    ///< ポップアップアニメーション
	bool billboard = true;                   ///< ビルボード有効

	/// @brief ワールド行列を計算する
	/// @param cameraPos カメラ位置
	/// @param cameraRight カメラ右方向（完全ビルボード用、nullptrで円筒ビルボード）
	/// @param cameraUp カメラ上方向（完全ビルボード用）
	/// @return 変換行列
	[[nodiscard]] sgc::Mat4f worldMatrix(
		const sgc::Vec3f& cameraPos,
		const sgc::Vec3f* cameraRight = nullptr,
		const sgc::Vec3f* cameraUp = nullptr) const noexcept
	{
		if (billboard && cameraRight && cameraUp)
		{
			/// 完全ビルボード: カメラの回転を逆転してスプライトが完全に正対
			/// アスペクト比が保たれる（パース圧縮なし）
			const auto& r = *cameraRight;
			const auto& u = *cameraUp;
			/// カメラの前方 = right × up
			const sgc::Vec3f f{
				r.y * u.z - r.z * u.y,
				r.z * u.x - r.x * u.z,
				r.x * u.y - r.y * u.x};

			/// ビルボード回転行列（カメラ座標系の軸をワールド軸にマッピング）
			auto rot = sgc::Mat4f{
				r.x,  r.y,  r.z,  0.0f,
				u.x,  u.y,  u.z,  0.0f,
				f.x,  f.y,  f.z,  0.0f,
				0.0f, 0.0f, 0.0f, 1.0f};

			const auto trans = sgc::Mat4f::translation(position);
			const auto scl = sgc::Mat4f::scaling({scale, scale, scale});
			const auto pop = popupRotationMatrix(popup.rotationX());
			return trans * rot * pop * scl;
		}

		if (billboard)
		{
			return sprite3DWorldMatrix(position, cameraPos, popup.rotationX(), scale);
		}

		/// ビルボード無効時は単純なTRS
		auto result = sgc::Mat4f::translation(position);
		result = result * popupRotationMatrix(popup.rotationX());
		result = result * sgc::Mat4f::scaling({scale, scale, scale});
		return result;
	}
};

} // namespace mitiru::render
