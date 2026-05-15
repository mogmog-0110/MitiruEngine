#pragma once

/// @file SaveStore.hpp
/// @brief C++ save-slot file bridge backing `window.mitiru.save.*` (F-11).
///
/// The web-side module `mitiru_save.js` dispatches four actions via
/// `window.mitiru.dispatch` — `save.write` / `save.read` / `save.list` /
/// `save.delete` — and falls back to `localStorage` when no C++ handler is
/// registered.  This class registers those four handlers against a
/// `mitiru::cef::StateStore` and maps them onto atomic file I/O under a
/// per-game save directory.
///
/// **Usage**
/// ```cpp
/// #include <mitiru/cef/StateStore.hpp>
/// #include <mitiru/cef/SaveStore.hpp>
///
/// auto store = ctx.makeStateStore();
/// mitiru::cef::SaveStore saves(*store, {
///     .dir       = getSaveRoot() / "kaerucrape",
///     .maxSlots  = 10,
/// });
/// // `saves` keeps itself registered for the lifetime of the object.
/// ```
///
/// **File layout** under `Config::dir`:
///   - `slot_<N>.json`          committed blob (data + meta + version)
///   - `slot_<N>.meta.json`     small summary for `save.list()`
///   - `slot_<N>.staging.json`  ephemeral — present only mid-write; if
///                              crash recovery picks one up, `read()`
///                              promotes it to `slot_<N>.json`.
///
/// **JSON contract** (matches the JSDoc in mitiru_save.js):
///   - `save.write`  → `{slot, payload: <full blob JSON string>}`  → `{ok:true}`
///   - `save.read`   → `{slot}`                                    → blob object or null
///   - `save.list`   → `{}`                                         → array of `{slot, exists, meta}`
///   - `save.delete` → `{slot}`                                    → `{ok:true}`
///
/// Any exception thrown from a handler propagates through StateStore's
/// dispatchFromJson as an `{error:"..."}` response, which `mitiru.save` then
/// converts to a Promise rejection.  The rejection path activates the
/// web-side localStorage fallback, so a server-side failure degrades
/// gracefully rather than silently dropping saves.
///
/// **Thread safety.** StateStore dispatches on the CEF UI thread; we
/// serialise further via an internal mutex so races between a read and a
/// concurrent write (possible if a script ever drives both) do not leave
/// the disk inconsistent.

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

/// @brief Disk-backed implementation of the web `mitiru.save` bridge.
class SaveStore
{
public:
	using json = ::nlohmann::json;

	/// @brief Construction-time configuration.
	struct Config
	{
		/// Directory that holds every slot's files. Created if missing.
		std::filesystem::path dir;

		/// Number of slots the game exposes. Must match
		/// `mitiru.save.MAX_SLOTS` on the JS side (default 10).
		std::size_t maxSlots = 10;

		/// Hard cap on a single slot's payload string length.  Guard against
		/// a compromised JS sending a gigabyte of data.  Defaults to 16 MB
		/// which is already well beyond any sane save file.
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
		// StateStore::onAction only holds a std::function pointing to the
		// lambda; when SaveStore is destroyed the lambda body still exists
		// inside the std::function but its captured `this` dangles.  Remove
		// the handlers so subsequent dispatches are cleanly unknown.
		m_store.offAction("save.write");
		m_store.offAction("save.read");
		m_store.offAction("save.list");
		m_store.offAction("save.delete");
	}

	// ── accessors ──────────────────────────────────────────────

	/// @brief Save directory (absolute preferred, relative tolerated).
	[[nodiscard]] const std::filesystem::path& dir() const noexcept { return m_cfg.dir; }

	/// @brief Slot count, matched against `mitiru.save.MAX_SLOTS` on JS side.
	[[nodiscard]] std::size_t maxSlots() const noexcept { return m_cfg.maxSlots; }

	/// @brief Size cap per-write, in bytes.
	[[nodiscard]] std::size_t maxFileBytes() const noexcept { return m_cfg.maxFileBytes; }

private:
	StateStore&        m_store;
	Config             m_cfg;
	mutable std::mutex m_mu;

	// ── handler wiring ─────────────────────────────────────────

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

	// ── path helpers ──────────────────────────────────────────

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

	// ── I/O primitives ────────────────────────────────────────

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
		// Overwriting rename is required on POSIX (ok by default) and on
		// Win32 (std::filesystem::rename maps to MoveFileExW with REPLACE
		// behaviour in MSVC STL).
		std::filesystem::rename(staging, final_, ec);
		if (ec)
		{
			// Fallback: copy + remove (slower but wider platform support).
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

	// ── handlers ──────────────────────────────────────────────

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

		// Validate the payload parses, and extract meta eagerly — sending a
		// corrupt write to disk would poison the next read.
		json blob;
		try { blob = json::parse(payload); }
		catch (const std::exception& e)
		{
			throw std::runtime_error(std::string(
				"SaveStore: payload is not valid JSON: ") + e.what());
		}

		const json metaBlob = extractMeta(blob);

		// Atomic main-file write.
		writeFileAtomic(stagePath(slot), blobPath(slot), payload);

		// Best-effort meta write.  If this fails the next list() for this
		// slot still reports exists:true but with meta:null, and list
		// consumers handle null-meta gracefully.
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

		// Crash-recovery: an orphan staging file next to a missing committed
		// file implies the previous write died after writeFileAtomic's
		// staging step but before the rename.  Promote it.
		const auto staging = stagePath(slot);
		const auto final_  = blobPath(slot);
		std::error_code ec;
		if (std::filesystem::exists(staging, ec) &&
		    !std::filesystem::exists(final_,  ec))
		{
			std::filesystem::rename(staging, final_, ec);
			// ec is swallowed here — fall through to the existence check.
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

	// ── helpers ─────────────────────────────────────────────────

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
