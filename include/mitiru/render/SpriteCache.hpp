#pragma once

/// @file SpriteCache.hpp
/// @brief sprite id → Texture の遅延ロードキャッシュ (Screen::sprite(id) の resolver)
/// @details
/// host (Engine) が所有し、`Screen::setSpriteResolver(&SpriteCache::resolve, &cache)`
/// で注入する。id は `assets/sprites/<id>.png` に解決される (audio の
/// `assets/audio/<id>.wav` と同じ id 規約)。ロード失敗は id 単位で初回のみ
/// warnOnce し、以後 nullptr を返す (毎フレームのディスク再試行はしない)。

#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/ImageLoader.hpp>
#include <mitiru/render/Texture.hpp>

namespace mitiru::render
{

/// @brief sprite id → Texture の遅延ロードキャッシュ
/// @details テクスチャの所有はこのキャッシュ (= host) 側。unordered_map の
///          値ポインタは rehash で無効化されないため、返した Texture* は
///          キャッシュ生存中ずっと有効。
class SpriteCache
{
public:
	SpriteCache() = default;

	/// @brief 解決の基準ディレクトリを設定する (loadModule が DLL 隣接 dir で上書きする)
	void setBaseDir(std::filesystem::path dir)
	{
		m_baseDir = std::move(dir);
	}

	/// @brief 現在の基準ディレクトリ
	[[nodiscard]] const std::filesystem::path& baseDir() const noexcept
	{
		return m_baseDir;
	}

	/// @brief id の Texture を返す (初回は <baseDir>/<id>.png を遅延ロード)
	/// @return 解決できた Texture (キャッシュ所有)。失敗は warnOnce 1 回 + nullptr。
	[[nodiscard]] const Texture* get(const char* id)
	{
		if (id == nullptr || *id == '\0')
		{
			return nullptr;
		}
		std::string key(id);
		if (const auto it = m_textures.find(key); it != m_textures.end())
		{
			return it->second.valid() ? &it->second : nullptr;
		}
		const std::string path = (m_baseDir / (key + ".png")).generic_string();
		Texture tex = ImageLoader::fromFile(path);
		if (!tex.valid())
		{
			// 黙った非表示は原因不明になるので id 単位で初回のみ警告 (R-01 級)
			mitiru::debug::warnOnce("sprite.id:" + key,
				"スプライト画像が見つからない/読めない: " + path);
		}
		// 失敗も空 Texture のままキャッシュする (毎フレームのディスク再試行を防ぐ)
		const auto it = m_textures.emplace(std::move(key), std::move(tex)).first;
		return it->second.valid() ? &it->second : nullptr;
	}

	/// @brief Screen::setSpriteResolver へ渡す C 関数ポインタ (ctx = SpriteCache*)
	[[nodiscard]] static const Texture* resolve(void* ctx, const char* id)
	{
		return ctx != nullptr ? static_cast<SpriteCache*>(ctx)->get(id) : nullptr;
	}

private:
	std::filesystem::path m_baseDir = "assets/sprites";   ///< 既定は cwd 相対
	std::unordered_map<std::string, Texture> m_textures;  ///< id → Texture (失敗は空 Texture)
};

} // namespace mitiru::render
