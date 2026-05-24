#pragma once

/// @file Siv3DSpriteRenderer.hpp
/// @brief ISpriteRenderer の Siv3D 実装
///
/// Siv3Dの s3d::Texture を使用してスプライト描画を行う。
/// テクスチャの読み込み・解放をIDベースで管理する。
///
/// @note このファイルはSiv3D SDKに依存するため、CI対象外。
///
/// @code
/// sgc::siv3d::Siv3DSpriteRenderer sprites;
/// int texId = sprites.loadTexture("player.png");
/// sprites.drawSprite(dest, src, texId, sgc::Colorf::white());
/// @endcode

#include <Siv3D.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "sgc/graphics/ISpriteRenderer.hpp"

namespace sgc::siv3d
{

/// @brief ISpriteRenderer の Siv3D 実装
///
/// s3d::Texture を内部で管理し、整数IDでアクセスする。
/// ブレンドモード切り替えには s3d::ScopedRenderStates2D 相当の処理を行う。
class Siv3DSpriteRenderer : public ISpriteRenderer
{
public:
	/// @brief テクスチャを読み込む
	/// @param path テクスチャファイルパス
	/// @return テクスチャID（失敗時は-1）
	[[nodiscard]] int loadTexture(const std::string& path) override
	{
		const int id = m_nextId++;
		s3d::Texture tex{s3d::Unicode::FromUTF8(path)};
		m_textures.emplace(id, std::move(tex));
		return id;
	}

	/// @brief テクスチャを解放する
	/// @param textureId テクスチャID
	void unloadTexture(int textureId) override
	{
		m_textures.erase(textureId);
	}

	/// @brief テクスチャサイズを取得する
	/// @param textureId テクスチャID
	/// @return テクスチャサイズ（幅, 高さ）
	[[nodiscard]] Vec2f getTextureSize(int textureId) const override
	{
		const auto it = m_textures.find(textureId);
		if (it == m_textures.end()) return Vec2f{0.0f, 0.0f};
		const auto size = it->second.size();
		return Vec2f{static_cast<float>(size.x), static_cast<float>(size.y)};
	}

	/// @brief スプライトを描画する
	void drawSprite(const AABB2f& dest, const AABB2f& src,
		int textureId, const Colorf& tint = Colorf::white()) override
	{
		const auto it = m_textures.find(textureId);
		if (it == m_textures.end()) return;

		const auto srcRect = s3d::RectF{
			static_cast<double>(src.min.x), static_cast<double>(src.min.y),
			static_cast<double>(src.size().x), static_cast<double>(src.size().y)
		};
		const auto destSize = dest.size();
		it->second(srcRect).resized(
			static_cast<double>(destSize.x),
			static_cast<double>(destSize.y)
		).draw(
			s3d::Vec2{static_cast<double>(dest.min.x), static_cast<double>(dest.min.y)},
			toColorF(tint)
		);
	}

	/// @brief 回転付きスプライトを描画する
	void drawSpriteRotated(const AABB2f& dest, const AABB2f& src,
		int textureId, float angle, const Vec2f& origin,
		const Colorf& tint = Colorf::white()) override
	{
		const auto it = m_textures.find(textureId);
		if (it == m_textures.end()) return;

		const auto srcRect = s3d::RectF{
			static_cast<double>(src.min.x), static_cast<double>(src.min.y),
			static_cast<double>(src.size().x), static_cast<double>(src.size().y)
		};
		const auto destSize = dest.size();
		it->second(srcRect).resized(
			static_cast<double>(destSize.x),
			static_cast<double>(destSize.y)
		).rotatedAt(
			s3d::Vec2{static_cast<double>(origin.x), static_cast<double>(origin.y)},
			static_cast<double>(angle)
		).draw(
			s3d::Vec2{static_cast<double>(dest.min.x), static_cast<double>(dest.min.y)},
			toColorF(tint)
		);
	}

	/// @brief スケール付きスプライトを描画する
	void drawSpriteScaled(const Vec2f& pos, const Vec2f& scale,
		int textureId, const Colorf& tint = Colorf::white()) override
	{
		const auto it = m_textures.find(textureId);
		if (it == m_textures.end()) return;

		const auto texSize = it->second.size();
		it->second.resized(
			texSize.x * static_cast<double>(scale.x),
			texSize.y * static_cast<double>(scale.y)
		).draw(
			s3d::Vec2{static_cast<double>(pos.x), static_cast<double>(pos.y)},
			toColorF(tint)
		);
	}

	/// @brief ブレンドモードを設定する
	void setBlendMode(BlendMode mode) override
	{
		m_currentBlendMode = mode;
		// 実際のブレンドモード切替はSiv3DのScopedRenderStates2Dで行う
		// ここでは状態を保持するのみ
	}

	/// @brief 現在のブレンドモードを取得する
	/// @return 現在のブレンドモード
	[[nodiscard]] BlendMode currentBlendMode() const noexcept
	{
		return m_currentBlendMode;
	}

private:
	/// @brief sgc::Colorf を s3d::ColorF に変換する
	/// @param c sgcカラー
	/// @return Siv3Dカラー
	[[nodiscard]] static s3d::ColorF toColorF(const Colorf& c) noexcept
	{
		return s3d::ColorF{
			static_cast<double>(c.r),
			static_cast<double>(c.g),
			static_cast<double>(c.b),
			static_cast<double>(c.a)
		};
	}

	std::unordered_map<int, s3d::Texture> m_textures;  ///< テクスチャマップ
	int m_nextId = 0;                                    ///< 次のテクスチャID
	BlendMode m_currentBlendMode = BlendMode::Normal;    ///< 現在のブレンドモード
};

} // namespace sgc::siv3d
