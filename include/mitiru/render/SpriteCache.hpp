#pragma once

/// @file SpriteCache.hpp
/// @brief sprite id → Texture の遅延ロードキャッシュ (Screen::sprite(id) の resolver)
/// @details
/// host (Engine) が所有し、`Screen::setSpriteResolver(&SpriteCache::resolve, &cache)`
/// で注入する。id は `assets/sprites/<id>.png` に解決される (audio の
/// `assets/audio/<id>.wav` と同じ id 規約)。ロード失敗は ImageLoader がパス単位で初回のみ
/// 知らせ、以後 nullptr を返す (毎フレームのディスク再試行はしない)。
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
	/// @return 解決できた Texture (キャッシュ所有)。失敗は nullptr (知らせは ImageLoader が 1 回出す)。
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
		// 読めなかったときは ImageLoader::fromFile がパスを添えて 1 回だけ知らせる
		entry.tex = ImageLoader::fromFile(path.generic_string());
		// 失敗も空 Texture のままキャッシュする (毎フレームのディスク再試行を防ぐ)
		const auto it = m_entries.emplace(std::move(key), std::move(entry)).first;
		return it->second.tex.valid() ? &it->second.tex : nullptr;
	}

	/// @brief id の法線マップ (<baseDir>/<id>_n.png)。無ければ nullptr。2D ライトが有効なときだけ引く
	/// @details 法線マップは任意なので、無い id は知らせない。結果は id ごとに 1 回だけ作り、以後は文字列を作らない。
	///          _n の画像も通常の id として m_entries に入るので、PNG のホットリロードにそのまま乗る
	[[nodiscard]] const Texture* normalFor(std::string_view id)
	{
		if (const auto it = m_normalSlots.find(id); it != m_normalSlots.end())
		{
			return it->second->tex.valid() ? &it->second->tex : nullptr;
		}
		if (id.empty()) { return nullptr; }
		std::string key(id);
		key += "_n";
		const std::filesystem::path path = m_baseDir / (key + ".png");
		Entry entry;
		std::error_code ec;
		if (std::filesystem::exists(path, ec)) { entry.tex = ImageLoader::fromFile(path.generic_string()); }
		Entry* slot = &m_entries.try_emplace(std::move(key), std::move(entry)).first->second;
		m_normalSlots.emplace(std::string(id), slot);
		return slot->tex.valid() ? &slot->tex : nullptr;
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
	/// @details 先頭が 0x01 の id は動画の頼み (Screen::drawVideo、video/VideoTypes.hpp)。ゲームの DLL は host の
	///          オブジェクトを持てないので、Screen に既にある host 注入の口を通して動画の host へ回す
	[[nodiscard]] static const Texture* resolve(void* ctx, const char* id)
	{
		if (ctx == nullptr) { return nullptr; }
		if (id != nullptr && id[0] == '') { return resolveVideo(id); }
		return static_cast<SpriteCache*>(ctx)->get(id);
	}

	/// @brief 動画の頼み (id の先頭が 0x01) を受ける関数。動画を使う host だけが付ける
	/// @details 置き場は host の process に 1 つの static にした。SpriteCache は Engine の値メンバーなので、ここへメンバーを足すと
	///          host と別のコミットで組んだ DLL の間で Engine の大きさが食い違う
	using VideoResolveFunc = const Texture* (*)(void* ctx, const char* id);
	static void setVideoResolver(VideoResolveFunc fn, void* ctx) noexcept
	{
		videoSlot() = VideoSlot{fn, ctx};
	}
	/// 登録した相手 (ctx) が消えるときに外す。別の Engine が後から登録していたら触らない
	static void clearVideoResolver(void* ctx) noexcept
	{
		if (videoSlot().ctx == ctx) { videoSlot() = VideoSlot{}; }
	}

private:
	struct Entry
	{
		Texture tex;   ///< 失敗時は空 Texture
	};

	struct VideoSlot
	{
		VideoResolveFunc fn = nullptr;
		void*            ctx = nullptr;
	};
	static VideoSlot& videoSlot() noexcept
	{
		static VideoSlot slot;
		return slot;
	}

	[[nodiscard]] static const Texture* resolveVideo(const char* id)
	{
		const VideoSlot& slot = videoSlot();
		if (slot.fn == nullptr)
		{
			debug::warnOnce("video.feature", "mitiru.toml の [engine] features に \"video\" がありません (動画を描けません)");
			return nullptr;
		}
		return slot.fn(slot.ctx, id);
	}

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
	/// id → <id>_n の Entry (m_entries の要素への番地。unordered_map の要素は動かない)
	std::unordered_map<std::string, Entry*, TransparentStringHash, std::equal_to<>> m_normalSlots;
};

} // namespace mitiru::render
