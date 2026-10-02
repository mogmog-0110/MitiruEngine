#pragma once

/// @file SpriteCache.hpp
/// @brief sprite id → Texture の遅延ロードキャッシュ (Screen::sprite(id) の resolver)
/// @details
/// host (Engine) が所有し、`Screen::setSpriteResolver(&SpriteCache::resolve, &cache)`
/// で注入する。id は `assets/sprites/<id>.png` に解決される (audio の
/// `assets/audio/<id>.wav` と同じ id 規約)。ロード失敗は id 単位で初回のみ
/// warnOnce し、以後 nullptr を返す (毎フレームのディスク再試行はしない)。
///
/// PNG のホットリロードは baseDir を asset::FileWatcher で見張り、pollReload() (Engine が ~0.5 秒ごとに呼ぶ)
/// で変わったファイルだけを読み直す。音は対応不要。SE は再生ごとにファイルを読む (既にホット)、
/// music はストリーム保持中でロック。

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <mitiru/asset/FileWatcher.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/ImageLoader.hpp>
#include <mitiru/render/Texture.hpp>

namespace mitiru::render
{

/// @brief `std::string` キーを `std::string_view` からも引ける透過ハッシュ (C1)。
///        DrawCmd 再生の毎フレーム呼び出しで、キャッシュ hit 時に `std::string` を
///        作らずに済ませるための道具 (unordered_map の透過検索には hash/equal 両方が要る)。
struct TransparentStringHash
{
	using is_transparent = void;
	[[nodiscard]] std::size_t operator()(std::string_view sv) const noexcept
	{
		return std::hash<std::string_view>{}(sv);
	}
};

/// @brief sprite id → Texture の遅延ロードキャッシュ (ホットリロード対応)
/// @details テクスチャの所有はこのキャッシュ (= host) 側。unordered_map の
///          値はノード安定 (rehash で再配置されない) なので、返した Texture* は
///          キャッシュ生存中ずっと有効。pollReload() は同じスロットの Texture を
///          上書きするためポインタは変わらず、次の描画から新ピクセルが見える。
class SpriteCache
{
public:
	/// @param backend テストは Polling を渡すと、最初の pollReload() でその場の変更を拾える
	explicit SpriteCache(asset::FileWatcher::Backend backend = asset::FileWatcher::Backend::Native)
		: m_backend(backend)
	{
		watchBaseDir();
	}

	/// @brief 解決の基準ディレクトリを設定する (loadModule が DLL 隣接 dir で上書きする)
	void setBaseDir(std::filesystem::path dir)
	{
		if (dir == m_baseDir && m_watching) { return; }
		m_baseDir = std::move(dir);
		watchBaseDir();
	}

	/// @brief 現在の基準ディレクトリ
	[[nodiscard]] const std::filesystem::path& baseDir() const noexcept
	{
		return m_baseDir;
	}

	/// @brief id の Texture を返す (初回は <baseDir>/<id>.png を遅延ロード)
	/// @details 透過ハッシュ (C1) により hit 時は `std::string` を作らない。miss (初回ロード)
	///          だけ map への挿入用に 1 回 `std::string` 化する。
	/// @return 解決できた Texture (キャッシュ所有)。失敗は warnOnce 1 回 + nullptr。
	/// @brief const char* 版。nullptr を string_view に変換すると未定義動作なので先に弾く
	[[nodiscard]] const Texture* get(const char* id)
	{
		return id != nullptr ? get(std::string_view{id}) : nullptr;
	}

	[[nodiscard]] const Texture* get(std::string_view id)
	{
		if (id.empty())
		{
			return nullptr;
		}
		if (const auto it = m_entries.find(id); it != m_entries.end())
		{
			return it->second.tex.valid() ? &it->second.tex : nullptr;
		}
		std::string key(id);
		const std::filesystem::path path = m_baseDir / (key + ".png");
		Entry entry;
		entry.tex = ImageLoader::fromFile(path.generic_string());
		if (!entry.tex.valid())
		{
			// 警告なしで表示されないと原因が分からないので、id 単位で初回のみ警告する (R-01 級)
			mitiru::debug::warnOnce("sprite.id:" + key,
				"スプライト画像が見つからない/読めない: " + path.generic_string());
		}
		// 失敗も空 Texture のままキャッシュする (毎フレームのディスク再試行を防ぐ)
		const auto it = m_entries.emplace(std::move(key), std::move(entry)).first;
		return it->second.tex.valid() ? &it->second.tex : nullptr;
	}

	/// @brief baseDir の中で変わった PNG のうち、引いたことのある id を同じスロットへ読み直す
	/// @details 失敗 id (前回 nullptr) も対象 = 後から PNG を置いたら出る。消えたファイルは通知されないので
	///          旧 Texture のまま。読めなかった id (書き込み途中など) は次の呼び出しで読み直す。
	void pollReload()
	{
		std::vector<std::string> ids = std::move(m_retry);
		m_retry.clear();
		if (m_watcher)
		{
			for (const auto& changed : m_watcher->poll()) { ids.push_back(idOf(changed)); }
		}
		for (const auto& id : ids) { reload(id); }
	}

	/// @brief Screen::setSpriteResolver へ渡す C 関数ポインタ (ctx = SpriteCache*)
	[[nodiscard]] static const Texture* resolve(void* ctx, const char* id)
	{
		return ctx != nullptr ? static_cast<SpriteCache*>(ctx)->get(id) : nullptr;
	}

private:
	struct Entry
	{
		Texture tex;   ///< 失敗時は空 Texture
	};

	/// baseDir が無いうちは見張らない (後から作られた sprites フォルダは次の setBaseDir まで拾わない)
	void watchBaseDir()
	{
		m_watcher = std::make_unique<asset::FileWatcher>(m_backend);
		m_watching = m_watcher->watchDirectory(m_baseDir, {".png"});
		std::error_code ec;
		const auto abs = std::filesystem::absolute(m_baseDir, ec);
		m_watchRoot = (ec ? m_baseDir : abs).lexically_normal();
		m_retry.clear();
	}

	/// <baseDir>/<id>.png の id。サブフォルダは / 区切りで id に含む
	[[nodiscard]] std::string idOf(const std::filesystem::path& png) const
	{
		auto rel = png.lexically_normal().lexically_relative(m_watchRoot);
		rel.replace_extension();
		return rel.generic_string();
	}

	void reload(const std::string& id)
	{
		const auto it = m_entries.find(id);
		if (it == m_entries.end()) { return; }
		Texture fresh = ImageLoader::fromFile((m_baseDir / (id + ".png")).generic_string());
		if (!fresh.valid())
		{
			std::error_code ec;
			if (std::filesystem::exists(m_baseDir / (id + ".png"), ec)) { m_retry.push_back(id); }
			return;
		}
		// 同じスロットを上書き → resolver が返した Texture* は安定。新寸法はそのまま採用。
		it->second.tex = std::move(fresh);
	}

	asset::FileWatcher::Backend          m_backend;
	std::filesystem::path                m_baseDir = "assets/sprites";  ///< 既定は cwd 相対
	std::filesystem::path                m_watchRoot;                   ///< m_baseDir の絶対パス (通知のパスと突き合わせる)
	std::unique_ptr<asset::FileWatcher>  m_watcher;
	bool                                 m_watching = false;
	std::vector<std::string>             m_retry;                       ///< 読めなかったので次の pollReload で読み直す id
	/// id → Entry (失敗は空 Texture)。透過ハッシュ (C1) で string_view のまま検索できる。
	std::unordered_map<std::string, Entry, TransparentStringHash, std::equal_to<>> m_entries;
};

} // namespace mitiru::render
