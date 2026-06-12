#pragma once

/// @file SpriteCache.hpp
/// @brief sprite id → Texture の遅延ロードキャッシュ (Screen::sprite(id) の resolver)
/// @details
/// host (Engine) が所有し、`Screen::setSpriteResolver(&SpriteCache::resolve, &cache)`
/// で注入する。id は `assets/sprites/<id>.png` に解決される (audio の
/// `assets/audio/<id>.wav` と同じ id 規約)。ロード失敗は id 単位で初回のみ
/// warnOnce し、以後 nullptr を返す (毎フレームのディスク再試行はしない)。
///
/// pollReload() で PNG のホットリロードに対応する (Engine が ~0.5 秒周期で呼ぶ)。
/// 音は対応不要 — SE は再生ごとにファイルを読む (既にホット)、music はストリーム保持中でロック。

#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/ImageLoader.hpp>
#include <mitiru/render/Texture.hpp>

namespace mitiru::render
{

/// @brief sprite id → Texture の遅延ロードキャッシュ (ホットリロード対応)
/// @details テクスチャの所有はこのキャッシュ (= host) 側。unordered_map の
///          値はノード安定 (rehash で再配置されない) なので、返した Texture* は
///          キャッシュ生存中ずっと有効。pollReload() は同じスロットの Texture を
///          上書きするためポインタは変わらず、次の描画から新ピクセルが見える。
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
		if (const auto it = m_entries.find(key); it != m_entries.end())
		{
			return it->second.tex.valid() ? &it->second.tex : nullptr;
		}
		const std::filesystem::path path = m_baseDir / (key + ".png");
		Entry entry;
		entry.tex = ImageLoader::fromFile(path.generic_string());
		// 書き込み途中/欠落でも次回 poll で拾えるよう mtime を記録 (stat 失敗は既定値のまま)
		std::error_code ec;
		entry.mtime = std::filesystem::last_write_time(path, ec);
		if (!entry.tex.valid())
		{
			// 黙った非表示は原因不明になるので id 単位で初回のみ警告 (R-01 級)
			mitiru::debug::warnOnce("sprite.id:" + key,
				"スプライト画像が見つからない/読めない: " + path.generic_string());
		}
		// 失敗も空 Texture のままキャッシュする (毎フレームのディスク再試行を防ぐ)
		const auto it = m_entries.emplace(std::move(key), std::move(entry)).first;
		return it->second.tex.valid() ? &it->second.tex : nullptr;
	}

	/// @brief 全ロード済みエントリの mtime を stat し、変更があれば同じスロットへ再読込する
	/// @details 失敗 id (前回 nullptr) も再試行する = 後から PNG を置いたら出る。
	///          消失/書き込み途中で読めない瞬間は旧 Texture を維持し次回 poll へ。
	void pollReload()
	{
		for (auto& [key, entry] : m_entries)
		{
			const std::filesystem::path path = m_baseDir / (key + ".png");
			std::error_code ec;
			const auto mtime = std::filesystem::last_write_time(path, ec);
			if (ec)
			{
				continue; // 消えた/ロック中 → 旧 Texture を維持 (clobber しない)
			}
			// 失敗 id は mtime に関係なく再試行、成功済みは mtime 変化時のみ
			if (entry.tex.valid() && mtime == entry.mtime)
			{
				continue;
			}
			Texture fresh = ImageLoader::fromFile(path.generic_string());
			if (!fresh.valid())
			{
				continue; // 書き込み途中等 → mtime も据え置きで次回 poll に再試行
			}
			// 同じスロットを上書き → resolver が返した Texture* は安定。新寸法はそのまま採用。
			entry.tex = std::move(fresh);
			entry.mtime = mtime;
		}
	}

	/// @brief Screen::setSpriteResolver へ渡す C 関数ポインタ (ctx = SpriteCache*)
	[[nodiscard]] static const Texture* resolve(void* ctx, const char* id)
	{
		return ctx != nullptr ? static_cast<SpriteCache*>(ctx)->get(id) : nullptr;
	}

private:
	/// @brief キャッシュエントリ (Texture + 読込時の mtime)
	struct Entry
	{
		Texture tex;                               ///< 失敗時は空 Texture
		std::filesystem::file_time_type mtime{};   ///< 読込時のファイル更新時刻
	};

	std::filesystem::path m_baseDir = "assets/sprites";  ///< 既定は cwd 相対
	std::unordered_map<std::string, Entry> m_entries;    ///< id → Entry (失敗は空 Texture)
};

} // namespace mitiru::render
