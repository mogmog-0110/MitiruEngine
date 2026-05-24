#pragma once

/// @file DxLibSpriteRenderer.hpp
/// @brief ISpriteRenderer の DxLib 実装
///
/// DxLibの描画関数を使用してスプライト描画を行う。
/// テクスチャの読み込み・解放をIDベースで管理する。
///
/// @note このファイルはDxLib SDKに依存する。
///
/// @code
/// sgc::dxlib::DxLibSpriteRenderer sprites;
/// int texId = sprites.loadTexture("player.png");
/// sprites.drawSprite(dest, src, texId, sgc::Colorf::white());
/// @endcode

#include "DxLib.h"
#include <cstdint>
#include <string>
#include <unordered_map>

#include "sgc/graphics/ISpriteRenderer.hpp"

namespace sgc::dxlib
{

/// @brief ISpriteRenderer の DxLib 実装
///
/// DxLib::LoadGraph でテクスチャを管理し、DrawRectExtendGraph 等で描画する。
class DxLibSpriteRenderer : public ISpriteRenderer
{
public:
	/// @brief デストラクタ（管理中テクスチャを全て解放する）
	~DxLibSpriteRenderer() override
	{
		for (const auto& [id, info] : m_textures)
		{
			DeleteGraph(info.handle);
		}
	}

	/// @brief テクスチャを読み込む
	/// @param path テクスチャファイルパス
	/// @return テクスチャID（失敗時は-1）
	[[nodiscard]] int loadTexture(const std::string& path) override
	{
		const int handle = LoadGraph(path.c_str());
		if (handle == -1) return -1;

		int w = 0, h = 0;
		GetGraphSize(handle, &w, &h);

		const int id = m_nextId++;
		m_textures[id] = TextureInfo{handle, static_cast<float>(w), static_cast<float>(h)};
		return id;
	}

	/// @brief テクスチャを解放する
	/// @param textureId テクスチャID
	void unloadTexture(int textureId) override
	{
		const auto it = m_textures.find(textureId);
		if (it == m_textures.end()) return;
		DeleteGraph(it->second.handle);
		m_textures.erase(it);
	}

	/// @brief テクスチャサイズを取得する
	/// @param textureId テクスチャID
	/// @return テクスチャサイズ（幅, 高さ）
	[[nodiscard]] Vec2f getTextureSize(int textureId) const override
	{
		const auto it = m_textures.find(textureId);
		if (it == m_textures.end()) return Vec2f{0.0f, 0.0f};
		return Vec2f{it->second.width, it->second.height};
	}

	/// @brief スプライトを描画する
	void drawSprite(const AABB2f& dest, const AABB2f& src,
		int textureId, const Colorf& tint = Colorf::white()) override
	{
		const auto it = m_textures.find(textureId);
		if (it == m_textures.end()) return;

		const auto srcSize = src.size();
		applyTint(tint);
		DrawRectExtendGraph(
			static_cast<int>(dest.min.x), static_cast<int>(dest.min.y),
			static_cast<int>(dest.max.x), static_cast<int>(dest.max.y),
			static_cast<int>(src.min.x), static_cast<int>(src.min.y),
			static_cast<int>(srcSize.x), static_cast<int>(srcSize.y),
			it->second.handle, TRUE
		);
		resetTint();
	}

	/// @brief 回転付きスプライトを描画する
	void drawSpriteRotated(const AABB2f& dest, const AABB2f& src,
		int textureId, float angle, const Vec2f& origin,
		const Colorf& tint = Colorf::white()) override
	{
		const auto it = m_textures.find(textureId);
		if (it == m_textures.end()) return;

		const auto destCenter = dest.center();
		const auto destSize = dest.size();
		const auto srcSize = src.size();
		const float scaleX = (srcSize.x > 0.0f) ? destSize.x / srcSize.x : 1.0f;
		const float scaleY = (srcSize.y > 0.0f) ? destSize.y / srcSize.y : 1.0f;

		applyTint(tint);
		DrawRectRotaGraph3(
			static_cast<int>(dest.min.x + origin.x),
			static_cast<int>(dest.min.y + origin.y),
			static_cast<int>(src.min.x), static_cast<int>(src.min.y),
			static_cast<int>(srcSize.x), static_cast<int>(srcSize.y),
			static_cast<int>(origin.x), static_cast<int>(origin.y),
			static_cast<double>(scaleX), static_cast<double>(scaleY),
			static_cast<double>(angle),
			it->second.handle, TRUE
		);
		resetTint();
	}

	/// @brief スケール付きスプライトを描画する
	void drawSpriteScaled(const Vec2f& pos, const Vec2f& scale,
		int textureId, const Colorf& tint = Colorf::white()) override
	{
		const auto it = m_textures.find(textureId);
		if (it == m_textures.end()) return;

		const int destX2 = static_cast<int>(pos.x + it->second.width * scale.x);
		const int destY2 = static_cast<int>(pos.y + it->second.height * scale.y);

		applyTint(tint);
		DrawExtendGraph(
			static_cast<int>(pos.x), static_cast<int>(pos.y),
			destX2, destY2,
			it->second.handle, TRUE
		);
		resetTint();
	}

	/// @brief ブレンドモードを設定する
	void setBlendMode(BlendMode mode) override
	{
		m_currentBlendMode = mode;
		switch (mode)
		{
		case BlendMode::Normal:
			SetDrawBlendMode(DX_BLENDMODE_ALPHA, 255);
			break;
		case BlendMode::Additive:
			SetDrawBlendMode(DX_BLENDMODE_ADD, 255);
			break;
		case BlendMode::Multiply:
			SetDrawBlendMode(DX_BLENDMODE_MULA, 255);
			break;
		case BlendMode::Screen:
			SetDrawBlendMode(DX_BLENDMODE_ADD, 255);
			break;
		}
	}

	/// @brief 現在のブレンドモードを取得する
	/// @return 現在のブレンドモード
	[[nodiscard]] BlendMode currentBlendMode() const noexcept
	{
		return m_currentBlendMode;
	}

private:
	/// @brief テクスチャ情報
	struct TextureInfo
	{
		int handle = -1;   ///< DxLibグラフィックハンドル
		float width = 0;   ///< テクスチャ幅
		float height = 0;  ///< テクスチャ高さ
	};

	/// @brief 色合いを適用する
	/// @param tint 色合い
	void applyTint(const Colorf& tint)
	{
		if (tint != Colorf::white())
		{
			SetDrawBright(
				static_cast<int>(tint.r * 255.0f),
				static_cast<int>(tint.g * 255.0f),
				static_cast<int>(tint.b * 255.0f)
			);
			if (tint.a < 1.0f)
			{
				SetDrawBlendMode(DX_BLENDMODE_ALPHA, static_cast<int>(tint.a * 255.0f));
			}
		}
	}

	/// @brief 色合いをリセットする
	void resetTint()
	{
		SetDrawBright(255, 255, 255);
	}

	std::unordered_map<int, TextureInfo> m_textures;  ///< テクスチャマップ
	int m_nextId = 0;                                   ///< 次のテクスチャID
	BlendMode m_currentBlendMode = BlendMode::Normal;   ///< 現在のブレンドモード
};

} // namespace sgc::dxlib
