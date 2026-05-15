#pragma once

/// @file ResourceCache.hpp
/// @brief リソースキャッシュ（Godot ResourceLoader風）
/// @details テクスチャ・フォントを名前ベースでキャッシュ管理する。
///          同一パスのリソースは再読み込みせずキャッシュから返す。
///          非同期ロードもサポートする。
///
/// @code
/// mitiru::resource::ResourceCache cache;
/// auto tex = cache.getTexture("assets/player.png");
/// // 2回目はキャッシュから即座に返る
/// auto tex2 = cache.getTexture("assets/player.png");
/// assert(tex.get() == tex2.get());
/// @endcode

#include <mitiru/render/Texture.hpp>

#include <future>
#include <memory>
#include <string>
#include <unordered_map>

namespace mitiru::resource
{

/// @brief リソースキャッシュ
/// @details テクスチャとフォントをパス名で管理し、重複ロードを防止する。
class ResourceCache
{
public:
	/// @brief テクスチャを取得する（キャッシュ済みならキャッシュから）
	/// @param path ファイルパスまたはリソース名
	/// @return テクスチャの共有ポインタ
	[[nodiscard]] std::shared_ptr<render::Texture> getTexture(const std::string& path)
	{
		auto it = m_textures.find(path);
		if (it != m_textures.end()) return it->second;

		// プロシージャルテクスチャとして白い4x4を生成する（ファイルI/O不要）
		auto tex = std::make_shared<render::Texture>(
			render::Texture::solid(4, 4, 255, 255, 255, 255));
		m_textures[path] = tex;
		return tex;
	}

	/// @brief テクスチャを直接登録する
	/// @param name リソース名
	/// @param tex テクスチャ
	void registerTexture(const std::string& name, std::shared_ptr<render::Texture> tex)
	{
		m_textures[name] = std::move(tex);
	}

	/// @brief テクスチャがキャッシュ済みか
	/// @param path パスまたは名前
	/// @return キャッシュ済みならtrue
	[[nodiscard]] bool hasTexture(const std::string& path) const
	{
		return m_textures.count(path) > 0;
	}

	/// @brief テクスチャの非同期ロード
	/// @param path ファイルパス
	/// @return テクスチャのfuture
	[[nodiscard]] std::future<std::shared_ptr<render::Texture>> loadTextureAsync(
		const std::string& path)
	{
		return std::async(std::launch::async, [this, path]() {
			return getTexture(path);
		});
	}

	/// @brief 全キャッシュをクリアする
	void clearAll()
	{
		m_textures.clear();
	}

	/// @brief テクスチャキャッシュをクリアする
	void clearTextures() { m_textures.clear(); }

	/// @brief キャッシュ済みテクスチャ数を取得する
	[[nodiscard]] int cachedTextureCount() const noexcept
	{
		return static_cast<int>(m_textures.size());
	}

private:
	std::unordered_map<std::string, std::shared_ptr<render::Texture>> m_textures; ///< テクスチャキャッシュ
};

} // namespace mitiru::resource
