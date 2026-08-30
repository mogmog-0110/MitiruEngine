#pragma once

/// @file SaveStore.hpp
/// @brief `window.mitiru.save.*` を backing する C++ save-slot file bridge (F-11)。
///
/// web 側 module `mitiru_save.js` は `window.mitiru.dispatch` 経由で 4 つの
/// action。`save.write` / `save.read` / `save.list` / `save.delete`。を
/// dispatch し、C++ handler が未登録なら `localStorage` に fallback する。
/// 本クラスは `mitiru::cef::StateStore` にこの 4 つの handler を登録し、
/// ゲームごとの save ディレクトリ下の atomic file I/O にマップする。
///
/// **使い方**
/// ```cpp
/// #include <mitiru/cef/StateStore.hpp>
/// #include <mitiru/cef/SaveStore.hpp>
///
/// auto store = ctx.makeStateStore();
/// mitiru::cef::SaveStore saves(*store, {
///     .dir       = getSaveRoot() / "my_game",
///     .maxSlots  = 10,
/// });
/// // `saves` keeps itself registered for the lifetime of the object.
/// ```
///
/// **File layout** (`Config::dir` 下):
///   - `slot_<N>.json`          commit 済み blob (data + meta + version)
///   - `slot_<N>.meta.json`     `save.list()` 用の小さな summary
///   - `slot_<N>.staging.json`  一時ファイル。write 途中だけ存在する。crash
///                              recovery がこれを拾った場合、`read()` が
///                              `slot_<N>.json` に昇格させる。
///
/// **JSON 契約** (mitiru_save.js の JSDoc と一致):
///   - `save.write`  → `{slot, payload: <full blob JSON string>}`  → `{ok:true}`
///   - `save.read`   → `{slot}`                                    → blob オブジェクト or null
///   - `save.list`   → `{}`                                         → `{slot, exists, meta}` の配列
///   - `save.delete` → `{slot}`                                    → `{ok:true}`
///
/// handler が投げた例外は StateStore の dispatchFromJson を通って
/// `{error:"..."}` レスポンスに変換され、`mitiru.save` がそれを Promise
/// reject に変える。reject 経路は web 側の localStorage fallback を起動する
/// ため、サーバー側の失敗は save を黙って捨てるのではなく gracefully に
/// degrade する。
///
/// **Thread safety。** StateStore は CEF UI thread で dispatch する。本クラスは
/// 内部 mutex でさらに直列化し、read と並行 write (script が両方を駆動した
/// 場合に起こりうる) の race がディスクを不整合状態にしないようにする。

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/cef/StateStore.hpp>

namespace mitiru::cef
{

/// @brief web の `mitiru.save` bridge の disk backing 実装。
class SaveStore
{
public:
	using json = ::nlohmann::json;

	/// @brief 構築時の設定。
	struct Config
	{
		/// 各 slot のファイルを格納するディレクトリ。無ければ作成する。
		std::filesystem::path dir;

		/// ゲームが公開する slot 数。JS 側の `mitiru.save.MAX_SLOTS` と
		/// 一致させる必要がある (default 10)。
		std::size_t maxSlots = 10;

		/// 単一 slot の payload 文字列長の上限。改竄された JS が gigabyte 級の
		/// data を送ってくるのを防ぐ。default は 16 MB で、これはまともな
		/// save file の範囲を既に大きく超えている。
		std::size_t maxFileBytes = 16ull * 1024ull * 1024ull;
	};

	SaveStore(StateStore& store, Config cfg)
		: m_store(store)
		, m_cfg(std::move(cfg))
	{
		if (m_cfg.dir.empty())
		{
			throw std::invalid_argument("SaveStore: Config::dir is empty");
		}
		if (m_cfg.maxSlots == 0)
		{
			throw std::invalid_argument("SaveStore: Config::maxSlots must be > 0");
		}

		std::error_code ec;
		std::filesystem::create_directories(m_cfg.dir, ec);
		if (ec)
		{
			throw std::runtime_error("SaveStore: cannot create " + m_cfg.dir.string()
			                       + " — " + ec.message());
		}

		registerHandlers();
	}

	SaveStore(const SaveStore&)            = delete;
	SaveStore& operator=(const SaveStore&) = delete;
	SaveStore(SaveStore&&)                 = delete;
	SaveStore& operator=(SaveStore&&)      = delete;

	~SaveStore()
	{
		// StateStore::onAction は lambda を指す std::function を保持するだけ。
		// SaveStore が破棄されると lambda 本体は std::function 内に残るが、
		// capture した `this` は dangle する。以降の dispatch が綺麗に unknown
		// 扱いになるよう handler を解除する。
		m_store.offAction("save.write");
		m_store.offAction("save.read");
		m_store.offAction("save.list");
		m_store.offAction("save.delete");
	}

	// ── accessor ──────────────────────────────────────────────

	/// @brief save ディレクトリ (絶対パス推奨、相対パスも許容)。
	[[nodiscard]] const std::filesystem::path& dir() const noexcept { return m_cfg.dir; }

	/// @brief slot 数。JS 側の `mitiru.save.MAX_SLOTS` と照合される。
	[[nodiscard]] std::size_t maxSlots() const noexcept { return m_cfg.maxSlots; }

	/// @brief write 1 回あたりのサイズ上限 (バイト)。
	[[nodiscard]] std::size_t maxFileBytes() const noexcept { return m_cfg.maxFileBytes; }

private:
	StateStore&        m_store;
	Config             m_cfg;
	mutable std::mutex m_mu;

	// ── handler 配線 ─────────────────────────────────────────

	void registerHandlers()
	{
		m_store.onAction("save.write",
			[this](const json& p) { return onWrite(p); });
		m_store.onAction("save.read",
			[this](const json& p) { return onRead(p); });
		m_store.onAction("save.list",
			[this](const json&  ) { return onList(); });
		m_store.onAction("save.delete",
			[this](const json& p) { return onDelete(p); });
	}

	// ── path helper ──────────────────────────────────────────

	[[nodiscard]] std::filesystem::path blobPath(int slot) const
	{
		return m_cfg.dir / ("slot_" + std::to_string(slot) + ".json");
	}
	[[nodiscard]] std::filesystem::path metaPath(int slot) const
	{
		return m_cfg.dir / ("slot_" + std::to_string(slot) + ".meta.json");
	}
	[[nodiscard]] std::filesystem::path stagePath(int slot) const
	{
		return m_cfg.dir / ("slot_" + std::to_string(slot) + ".staging.json");
	}

	[[nodiscard]] int requireSlot(const json& payload) const
	{
		if (!payload.is_object() || !payload.contains("slot") ||
		    !payload["slot"].is_number_integer())
		{
			throw std::runtime_error("SaveStore: missing integer 'slot' in payload");
		}
		const int slot = payload["slot"].get<int>();
		if (slot < 0 ||
		    static_cast<std::size_t>(slot) >= m_cfg.maxSlots)
		{
			throw std::out_of_range("SaveStore: slot " + std::to_string(slot)
			                      + " out of range [0, "
			                      + std::to_string(m_cfg.maxSlots - 1) + "]");
		}
		return slot;
	}

	// ── I/O プリミティブ ────────────────────────────────────────

	static std::string readFile(const std::filesystem::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
		{
			throw std::runtime_error("SaveStore: cannot open " + path.string());
		}
		return { std::istreambuf_iterator<char>(in),
		         std::istreambuf_iterator<char>() };
	}

	static void writeFileAtomic(const std::filesystem::path& staging,
	                            const std::filesystem::path& final_,
	                            const std::string& content)
	{
		{
			std::ofstream out(staging,
			                  std::ios::binary | std::ios::trunc);
			if (!out)
			{
				throw std::runtime_error("SaveStore: cannot open staging "
				                       + staging.string());
			}
			out.write(content.data(),
			          static_cast<std::streamsize>(content.size()));
			if (!out)
			{
				throw std::runtime_error("SaveStore: write to staging failed");
			}
			out.flush();
		}
		std::error_code ec;
		// 上書き rename は POSIX (default で可) と Win32 (MSVC STL では
		// std::filesystem::rename が REPLACE 挙動の MoveFileExW にマップされる)
		// の両方で必要。
		std::filesystem::rename(staging, final_, ec);
		if (ec)
		{
			// fallback: copy + remove (遅いがより広いプラットフォームで動く)。
			std::filesystem::copy_file(staging, final_,
				std::filesystem::copy_options::overwrite_existing, ec);
			if (ec)
			{
				throw std::runtime_error("SaveStore: atomic rename failed — "
				                       + ec.message());
			}
			std::filesystem::remove(staging, ec);
		}
	}

	// ── handler ──────────────────────────────────────────────

	json onWrite(const json& p)
	{
		std::lock_guard lock(m_mu);

		const int slot = requireSlot(p);
		if (!p.contains("payload") || !p["payload"].is_string())
		{
			throw std::runtime_error("SaveStore: missing 'payload' string");
		}
		const std::string payload = p["payload"].get<std::string>();
		if (payload.size() > m_cfg.maxFileBytes)
		{
			throw std::runtime_error("SaveStore: payload "
				+ std::to_string(payload.size())
				+ " bytes exceeds maxFileBytes "
				+ std::to_string(m_cfg.maxFileBytes));
		}

		// payload が parse できるか検証し、meta を先に抽出する。壊れた write を
		// ディスクに送ると次の read を汚染してしまう。
		json blob;
		try { blob = json::parse(payload); }
		catch (const std::exception& e)
		{
			throw std::runtime_error(std::string(
				"SaveStore: payload is not valid JSON: ") + e.what());
		}

		const json metaBlob = extractMeta(blob);

		// main file の atomic write。
		writeFileAtomic(stagePath(slot), blobPath(slot), payload);

		// best-effort な meta write。失敗してもこの slot の次の list() は
		// exists:true / meta:null を返すだけで、list の consumer は null-meta を
		// gracefully に扱う。
		std::ofstream metaOut(metaPath(slot),
		                      std::ios::binary | std::ios::trunc);
		if (metaOut)
		{
			const std::string dumped = metaBlob.dump();
			metaOut.write(dumped.data(),
			              static_cast<std::streamsize>(dumped.size()));
		}

		return json{{"ok", true}};
	}

	json onRead(const json& p)
	{
		std::lock_guard lock(m_mu);

		const int slot = requireSlot(p);

		// crash recovery: commit 済みファイルが無いのに staging ファイルが残って
		// いる = 前回の write が writeFileAtomic の staging 段階の後、rename の
		// 前に死んだことを意味する。これを昇格させる。
		const auto staging = stagePath(slot);
		const auto final_  = blobPath(slot);
		std::error_code ec;
		if (std::filesystem::exists(staging, ec) &&
		    !std::filesystem::exists(final_,  ec))
		{
			std::filesystem::rename(staging, final_, ec);
			// ここでは ec を握り潰す。下の存在チェックに fall through する。
		}

		if (!std::filesystem::exists(final_, ec))
		{
			return json(nullptr);
		}

		const std::string content = readFile(final_);
		try { return json::parse(content); }
		catch (const std::exception& e)
		{
			throw std::runtime_error(std::string(
				"SaveStore: slot file is corrupt JSON — ") + e.what());
		}
	}

	json onList()
	{
		std::lock_guard lock(m_mu);

		json arr = json::array();
		for (std::size_t i = 0; i < m_cfg.maxSlots; ++i)
		{
			json entry = {
				{ "slot",   static_cast<int>(i) },
				{ "exists", false                 },
				{ "meta",   nullptr               },
			};

			std::error_code ec;
			if (std::filesystem::exists(blobPath(static_cast<int>(i)), ec))
			{
				entry["exists"] = true;
				entry["meta"]   = readMeta(static_cast<int>(i));
			}
			arr.push_back(std::move(entry));
		}
		return arr;
	}

	json onDelete(const json& p)
	{
		std::lock_guard lock(m_mu);

		const int slot = requireSlot(p);
		std::error_code ec;
		std::filesystem::remove(blobPath(slot),  ec);
		std::filesystem::remove(metaPath(slot),  ec);
		std::filesystem::remove(stagePath(slot), ec);
		return json{{"ok", true}};
	}

	// ── helper ─────────────────────────────────────────────────

	static json extractMeta(const json& blob)
	{
		json m = json::object();
		if (!blob.is_object()) { return m; }

		static constexpr const char* kKeys[] = {
			"version", "timestamp", "playtimeMs",
			"title",   "description", "thumbnailDataUrl",
		};
		for (const auto* k : kKeys)
		{
			if (blob.contains(k)) { m[k] = blob[k]; }
		}
		return m;
	}

	json readMeta(int slot) const
	{
		std::error_code ec;
		const auto path = metaPath(slot);
		if (!std::filesystem::exists(path, ec)) { return json(nullptr); }
		try
		{
			return json::parse(readFile(path));
		}
		catch (...)
		{
			return json(nullptr);
		}
	}
};

} // namespace mitiru::cef
