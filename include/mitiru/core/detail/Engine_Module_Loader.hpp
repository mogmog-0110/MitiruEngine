// mitiru::Engine の detail header - 直接 include しないこと。core/Engine.hpp 経由で include される
#pragma once

/// @file Engine_Module_Loader.hpp
/// @brief Engine の module loader 部分の out-of-class 定義 (v0.2.0 step 2-3)
/// @details
/// `Engine::loadModule / unloadModule / reloadModule` の実装と、
/// module 状態の accessor 群、time-travel 用 GameMemory ring 記録 /
/// rewind / branch を収める。
/// per-frame signal flow は Engine_Module_Adapter.hpp 側。

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include <mitiru/cef/StateStore.hpp>
#include <mitiru/core/Game.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/core/Screen.hpp>
#include <mitiru/debug/InspectorLauncher.hpp>
#include <mitiru/debug/DebugPrint.hpp>
#include <mitiru/module/ModuleHost.hpp>
#include <mitiru/module/SoundIntentRouter.hpp>
#include <mitiru/observe/GameMemoryRing.hpp>
#include <mitiru/observe/Reflect.hpp>
#include <mitiru/observe/ReflectDiff.hpp>
#include <mitiru/observe/SeriesMarkers.hpp>
#include <mitiru/observe/SharedSnapshot.hpp>
#include <mitiru/render/SaveScreenshotPng.hpp>

// ── Free helper 群 (wire version の人間語化、H-1/H-4) ──────────────────────
namespace mitiru::module::detail
{

/// @brief wire version (数値 + build 指紋) を人間語へ (例 "v21 (VC19, CRT=dll (/MD系), IDL=2)")。
inline std::string describeWireVersion(std::uint32_t v)
{
	std::string s = "v" + std::to_string(wireAbiNumber(v)) + " (";
	const std::uint32_t msc = wireMscSeries(v);
	s += (msc > 0) ? ("VC" + std::to_string(msc)) : std::string{"非MSVC"};
	s += wireCrtIsDll(v) ? ", CRT=dll (/MD系)" : ", CRT=static (/MT系)";
	s += ", IDL=" + std::to_string(wireIdl(v)) + ")";
	return s;
}

/// @brief 不一致成分の指摘つき拒否メッセージ (どの成分が違うかを人間語で出す)。
inline std::string describeVersionMismatch(std::uint32_t dllV, std::uint32_t hostV)
{
	std::string why;
	if (wireAbiNumber(dllV) != wireAbiNumber(hostV)) { why += " ABI番号"; }
	if (wireIdl(dllV)       != wireIdl(hostV))       { why += " _ITERATOR_DEBUG_LEVEL"; }
	if (wireCrtIsDll(dllV)  != wireCrtIsDll(hostV))  { why += " CRT種別(/MD vs /MT)"; }
	if (wireMscSeries(dllV) != wireMscSeries(hostV)) { why += " コンパイラ系列(_MSC_VER/100)"; }
	if (why.empty()) { why = " 予約bit"; }
	return "ABI/ビルド指紋 不一致: game=" + describeWireVersion(dllV)
	     + " vs host=" + describeWireVersion(hostV)
	     + " — 相違:" + why
	     + "。game を host と同じ構成 (Debug/Release・CRT) で再ビルドしてください";
}

}  // namespace mitiru::module::detail

// ── loadModule ─────────────────────────────────────────────────────────────

MITIRU_INLINE bool mitiru::Engine::loadModule(const std::filesystem::path& modulePath)
{
	// すでに module が active なら明示的 unload を要求する。
	if (m_moduleHost && m_moduleHost->isLoaded())
	{
		return false;
	}

	if (!m_moduleHost)
	{
		m_moduleHost = std::make_unique<module::ModuleHost>();
	}

	if (!m_moduleHost->load(modulePath))
	{
		return false;
	}

	m_moduleApi = module::ModuleApi{};
	m_moduleApi.version = module::kWireApiVersion;  // 数値 + build 指紋 (H-1/H-4)

	const auto loadFn = m_moduleHost->loadFn();
	if (loadFn == nullptr)
	{
		m_moduleHost->unload();
		return false;
	}

	// loadFn 前の pointer を控える。null から確保されたか (fresh load) を後で判定する。
	void* const memoryBefore = m_moduleMemory;
	loadFn(&m_moduleApi, &m_moduleMemory);

	// DLL が申告した GameMemory サイズを保持。v≤8 DLL は未設定 ⇒ zero-init の 0。
	m_moduleMemorySize = m_moduleApi.memorySize;

	// reflection を申告したのに memorySize=0 だと reflectToJson が bounds 外で全 skip し
	// /api/ai/state が {} を返す。原因が分かりにくいので一度だけ警告する (R-01)。
	// non-POD game でも api->memorySize = sizeof(GameMemory) を申告すれば現フレーム観測は可
	// (reflectToJson は申告した offset のスカラーしか触らない)。ring/diff/branch は flat POD 必須。
	if (m_moduleApi.reflectFieldCount > 0 && m_moduleMemorySize == 0)
	{
		std::fprintf(stderr,
			"[ai] warning: MITIRU_REFLECT で %d field 申告されていますが api->memorySize が 0 です。"
			"/api/ai/state は空 {} になります。api->memorySize = sizeof(GameMemory) を申告してください。\n",
			static_cast<int>(m_moduleApi.reflectFieldCount));
	}

	// version check。拒否時は DLL が確保したばかりの memory を unloadFn で DLL に
	// 返却してから unload する (リーク解消)。返却は fresh 確保時のみ。温存 memory を
	// 渡す reload は reloadModule 側で先ロード検証されるため、ここでは触らない。
	// ABI は「数値 + build 指紋」の完全一致を要求する (H-1/H-4)。古い DLL (version < host)
	// も弾く: SoundIntent 等の配列要素が後の version で太ると soundIntents[] の stride =
	// 後続 FrameIntents field の offset がズレ、旧 DLL を新 host で動かすと silent 破損/
	// クラッシュするため (D1)。数値一致でも CRT 種別 / IDL / toolset 混成は Screen* (STL
	// 内包) と cross-DLL delete が silent 破損するため同様に拒否する。
	if (m_moduleApi.version != module::kWireApiVersion)
	{
		const std::uint32_t dllVersion = m_moduleApi.version;
		if (memoryBefore == nullptr && m_moduleMemory != nullptr)
		{
			if (auto unloadFn = m_moduleHost->unloadFn())
			{
				try { unloadFn(m_moduleMemory); }
				catch (...) {}
			}
			m_moduleMemory     = nullptr;
			m_moduleMemorySize = 0;
		}
		m_moduleHost->unload();
		m_moduleApi = module::ModuleApi{};
		m_moduleHost->setLastError(
			module::detail::describeVersionMismatch(dllVersion, module::kWireApiVersion));
		return false;
	}

	// per-frame signal flow 用の scratch buffer を遅延確保する。
	if (!m_moduleInputSnapshot)
	{
		m_moduleInputSnapshot = std::make_unique<module::InputSnapshot>();
	}
	if (!m_moduleFrameIntents)
	{
		m_moduleFrameIntents = std::make_unique<module::FrameIntents>();
	}
	if (!m_moduleActionEvents)
	{
		m_moduleActionEvents = std::make_unique<ModuleActionEventBuffer>();
	}

	// sprite(id) の解決基準を DLL 隣接の assets/sprites にする (audio の
	// assets/audio/<id>.wav と同じ「DLL の隣」規約、ABI v16)。
	m_spriteCache.setBaseDir(modulePath.parent_path() / "assets" / "sprites");

	// on_init は「memory が新規確保された時」のみ呼ぶ (Game.hpp registerGame の設計意図)。
	// 温存 memory を渡された場合に呼ぶと T::init() が user 状態をリセットし得る。
	if (m_moduleApi.on_init != nullptr && memoryBefore == nullptr)
	{
		m_moduleApi.on_init(m_moduleMemory);
	}
	return true;
}

// ── unloadModule ───────────────────────────────────────────────────────────

MITIRU_INLINE void mitiru::Engine::unloadModule() noexcept
{
	if (!m_moduleHost || !m_moduleHost->isLoaded())
	{
		return;
	}

	if (m_moduleApi.on_shutdown != nullptr)
	{
		try { m_moduleApi.on_shutdown(m_moduleMemory); }
		catch (...) {}
	}

	if (auto unloadFn = m_moduleHost->unloadFn())
	{
		try { unloadFn(m_moduleMemory); }
		catch (...) {}
	}

	// unloadFn は DLL 側 delete。解放済み pointer を保持し続けると次の load で
	// 「非 null なら再利用」に渡って use-after-free になる (A5)。必ず null へ戻す。
	m_moduleMemory     = nullptr;
	m_moduleMemorySize = 0;

	m_moduleApi = module::ModuleApi{};
	m_moduleHost->unload();

	// (今や死んでいる) DLL が所有する state を参照していた pending event は
	// action queue から全て破棄する必要がある。
	if (m_moduleActionEvents)
	{
		std::lock_guard lock(m_moduleActionEvents->mu);
		m_moduleActionEvents->events.clear();
	}
}

// ── reloadModule ───────────────────────────────────────────────────────────

MITIRU_INLINE bool mitiru::Engine::reloadModule(const std::filesystem::path& modulePath)
{
	// 旧 module が無いなら通常 load と同じ (memory は null から確保され on_init が走る)。
	if (!m_moduleHost || !m_moduleHost->isLoaded())
	{
		m_moduleMemoryRing.clear();
		return loadModule(modulePath);
	}

	// ── 先ロード・後差し替え (A1) ──────────────────────────────────────────
	// 旧 DLL を生かしたまま、新 DLL を一時 host で load + API 解決 + version 検証
	// まで済ませる。途中で失敗したら旧 module / 旧 memory には一切触らず false を
	// 返す → host は「old code で継続」できる。temp copy 名が一意なので同一 source
	// でも独立 module として並走 load できる (ModuleHost の copy strategy)。
	module::ModuleHost newHost;
	if (!newHost.load(modulePath))
	{
		m_moduleHost->setLastError(newHost.lastError());
		return false;
	}

	const auto loadFn = newHost.loadFn();
	if (loadFn == nullptr)
	{
		m_moduleHost->setLastError("新 DLL に load entry symbol がありません");
		return false;  // newHost destructor が FreeLibrary + temp 削除
	}

	// 既存 GameMemory pointer を渡す。registerGame は非 null なら再利用する (状態温存)。
	module::ModuleApi newApi{};
	newApi.version = module::kWireApiVersion;
	void* memoryBefore = m_moduleMemory;  // size 変動 guard が fresh 扱いへ倒すため非 const
	void* memory       = m_moduleMemory;
	loadFn(&newApi, &memory);

	if (newApi.version != module::kWireApiVersion)  // 数値 + 指紋の完全一致要求 (D1 / H-1/H-4)
	{
		// 新 DLL が fresh 確保した場合のみ新 DLL 自身に返却する。温存 memory は
		// 旧 module が継続使用するため絶対に解放しない。
		if (memoryBefore == nullptr && memory != nullptr)
		{
			if (auto unloadFn = newHost.unloadFn())
			{
				try { unloadFn(memory); }
				catch (...) {}
			}
		}
		m_moduleHost->setLastError(
			module::detail::describeVersionMismatch(newApi.version, module::kWireApiVersion));
		return false;
	}

	// ── GameMemory サイズ / layout 変動 guard (C-1 + layout hash) ──────────
	// 温存 pointer の割当は旧 sizeof のまま。新 DLL の申告サイズが違うと旧割当の
	// 末尾を越えて read/write する heap overflow になるため、状態温存を放棄する。
	// サイズ一致でも MITIRU_REFLECT 由来の layout hash が違えば field 並べ替え /
	// 型変更。旧 bytes を新 layout で解釈すると化けるため、同様に放棄する
	// (reflection 未宣言 game は hash=0 = 従来のサイズ照合のみ)。
	// 旧割当は「確保した世代の DLL」の unloadFn に返却させる (cross-CRT delete 回避)。
	// ここは全検証通過後なので、以降の失敗で旧 module へ戻る経路は無い。
	const std::uint64_t oldLayoutHash = module::moduleLayoutHash(m_moduleApi);
	const std::uint64_t newLayoutHash = module::moduleLayoutHash(newApi);
	const bool layoutChanged =
		(oldLayoutHash != 0 && newLayoutHash != 0 && oldLayoutHash != newLayoutHash);
	bool stateReset = false;
	if (memoryBefore != nullptr
	    && (newApi.memorySize != m_moduleMemorySize || layoutChanged))
	{
		if (newApi.memorySize != m_moduleMemorySize)
		{
			std::fprintf(stderr,
				"[module] reload: GameMemory size changed %u -> %u, state reset\n",
				m_moduleMemorySize, newApi.memorySize);
		}
		else
		{
			std::fprintf(stderr,
				"[module] reload: GameMemory layout changed (size %u unchanged, "
				"reflect hash mismatch), state reset\n",
				m_moduleMemorySize);
		}
		if (m_moduleApi.on_shutdown != nullptr)
		{
			try { m_moduleApi.on_shutdown(m_moduleMemory); }
			catch (...) {}
		}
		if (auto oldUnloadFn = m_moduleHost->unloadFn())
		{
			try { oldUnloadFn(m_moduleMemory); }
			catch (...) {}
		}
		m_moduleMemory = nullptr;
		// null slot を渡し直して新 DLL に fresh 確保させる。以降は初回 load と
		// 同じ経路 (memoryBefore=null 扱いで末尾の on_init が走る)。
		newApi         = module::ModuleApi{};
		newApi.version = module::kWireApiVersion;
		memory         = nullptr;
		memoryBefore   = nullptr;
		loadFn(&newApi, &memory);
		stateReset = true;
	}

	// ── 差し替え ──────────────────────────────────────────────────────────
	// 旧 DLL は unloadFn (DLL 側 delete) を呼ばず FreeLibrary のみ。GameMemory の
	// 所有は host が続投する = 状態温存の正規化 (解放済み pointer の再利用ではない)。
	// 旧 on_shutdown も呼ばない。GameMemory は flat POD 契約 で DLL 側に
	// 解放すべきリソースを持たないし、T::shutdown() が状態を壊す余地も残さない。
	*m_moduleHost      = std::move(newHost);  // move 代入が旧 handle を FreeLibrary する
	m_moduleApi        = newApi;
	m_moduleMemory     = memory;
	m_moduleMemorySize = newApi.memorySize;

	// 旧 DLL の code を参照しうる pending event は破棄する (unloadModule と同じ理由)。
	if (m_moduleActionEvents)
	{
		std::lock_guard lock(m_moduleActionEvents->mu);
		m_moduleActionEvents->events.clear();
	}

	// GameMemory サイズ・layout が不変なら ring を温存する (rewind → 編集 →
	// reload → resim の合流に必要)。サイズ変化 / layout hash 不一致 (state reset 済み) は
	// 旧 layout bytes への rewind が復元を壊すため破棄する。同サイズの field 並べ替えも
	// MITIRU_REFLECT 済みなら layout hash で検出される (未宣言 game は検出不能のまま)。
	// InputRing は layout 非依存なので常に温存する。
	if (m_moduleMemoryRing.frameSize() != m_moduleMemorySize || stateReset)
	{
		m_moduleMemoryRing.clear();
		m_resimQueue.clear(); m_resimCursor = 0; m_resimSnapSize = 0;  // 進行中 resim も破棄
	}

	// sprite(id) の解決基準を新 DLL の隣へ更新する (loadModule と同じ規約、ABI v16)。
	m_spriteCache.setBaseDir(modulePath.parent_path() / "assets" / "sprites");

	// memory 温存 reload では on_init を呼ばない (T::init() が user 状態をリセットし得る)。
	// 旧 memory が無く fresh 確保された時だけ初回 load と同様に呼ぶ。
	if (memoryBefore == nullptr && newApi.on_init != nullptr)
	{
		newApi.on_init(memory);
	}
	return true;
}

// ── moduleStateStore accessor ──────────────────────────────────────────────

MITIRU_INLINE mitiru::cef::StateStore* mitiru::Engine::moduleStateStore() noexcept
{
	return m_moduleStateStore.get();
}

// ── Accessors ──────────────────────────────────────────────────────────────

MITIRU_INLINE bool mitiru::Engine::hasModule() const noexcept
{
	return m_moduleHost && m_moduleHost->isLoaded();
}

MITIRU_INLINE const mitiru::module::ModuleApi& mitiru::Engine::moduleApi() const noexcept
{
	return m_moduleApi;
}

MITIRU_INLINE void* mitiru::Engine::moduleMemory() const noexcept
{
	return m_moduleMemory;
}

MITIRU_INLINE std::uint32_t mitiru::Engine::moduleMemorySize() const noexcept
{
	return m_moduleMemorySize;
}

MITIRU_INLINE std::string mitiru::Engine::reflectDiffBlobs(const void* a, const void* b) const
{
	// 2 つの GameMemory blob を field 単位で diff (replay 回帰の divergence report)。
	if (a == nullptr || b == nullptr || m_moduleMemorySize == 0 ||
	    m_moduleApi.reflectFieldCount <= 0)
	{
		return "[]";  // MITIRU_REFLECT 未宣言 / 未 load
	}
	const auto ja = observe::reflectToJson(static_cast<const std::uint8_t*>(a), m_moduleMemorySize,
		m_moduleApi.reflectFields, m_moduleApi.reflectFieldCount,
		m_moduleApi.reflectSchemas, m_moduleApi.reflectSchemaCount);
	const auto jb = observe::reflectToJson(static_cast<const std::uint8_t*>(b), m_moduleMemorySize,
		m_moduleApi.reflectFields, m_moduleApi.reflectFieldCount,
		m_moduleApi.reflectSchemas, m_moduleApi.reflectSchemaCount);
	return observe::reflectDiff(ja, jb).dump();
}

MITIRU_INLINE const char* mitiru::Engine::queryModuleWriteBlame(std::uint32_t offset) const
{
	// game が mitiru_why_blame_at を export していれば呼ぶ (optional、host→DLL の pull)。
	if (!m_moduleHost) { return nullptr; }
	const auto fn = m_moduleHost->whyBlameAtFn();
	return (fn != nullptr) ? fn(offset) : nullptr;
}

MITIRU_INLINE std::string mitiru::Engine::reflectBlobJson(const void* blob) const
{
	if (blob == nullptr || m_moduleMemorySize == 0 || m_moduleApi.reflectFieldCount <= 0)
	{
		return "{}";
	}
	return observe::reflectToJson(static_cast<const std::uint8_t*>(blob), m_moduleMemorySize,
		m_moduleApi.reflectFields, m_moduleApi.reflectFieldCount,
		m_moduleApi.reflectSchemas, m_moduleApi.reflectSchemaCount).dump();
}

// ── time-travel: GameMemory ring 記録 + rewind ──────────────────

MITIRU_INLINE void mitiru::Engine::recordModuleMemoryFrame()
{
	if (m_moduleMemorySize == 0 || m_moduleMemory == nullptr) { return; }  // 非 flat POD / 未 load
	if (m_moduleMemoryRing.frameSize() != m_moduleMemorySize)
	{
		// 巻き戻せるフレーム数: --rewind-frames (config) > game の MITIRU_REWIND_BUFFER 宣言 > 既定 300。
		std::size_t frames = 300;
		if (m_moduleHost)
		{
			if (auto fn = m_moduleHost->rewindBufferFramesFn())
			{
				const std::uint32_t declared = fn();
				if (declared > 0) { frames = declared; }
			}
		}
		if (m_config.timeTravelBufferFrames > 0)
		{
			frames = static_cast<std::size_t>(m_config.timeTravelBufferFrames);
		}
		m_moduleMemoryRing.configure(m_moduleMemorySize, frames);
	}
	m_moduleMemoryRing.push(m_moduleMemory, m_moduleMemorySize);
}

MITIRU_INLINE const std::uint8_t*
mitiru::Engine::moduleMemoryRingAt(std::size_t offsetFromNewest) const noexcept
{
	return m_moduleMemoryRing.at(offsetFromNewest);
}

// ── Rewind-Edit-Replay ──────────────────────────────────────────

MITIRU_INLINE void mitiru::Engine::recordModuleInputFrame()
{
	if (!m_moduleInputSnapshot) { return; }
	constexpr std::uint32_t kSnapSize = sizeof(module::InputSnapshot);
	if (m_moduleInputRing.frameSize() != kSnapSize)
	{
		m_moduleInputRing.configure(kSnapSize, 300);  // GameMemoryRing と同窓 (60fps × 5sec)
	}
	m_moduleInputRing.push(m_moduleInputSnapshot.get(), kSnapSize);
}

MITIRU_INLINE bool mitiru::Engine::resimFromFramesAgo(std::uint32_t k) noexcept
{
	constexpr std::uint32_t kSnapSize = sizeof(module::InputSnapshot);
	const std::size_t memFrames = m_moduleMemoryRing.size();
	const std::size_t inFrames  = m_moduleInputRing.size();
	if (m_moduleMemorySize == 0 || memFrames == 0 || inFrames == 0)
	{
		debug::warnOnce("resim.unavailable",
		                "resim 不可: flat POD 未申告か、巻き戻し ring がまだ空 (reload 直後など)");
		return false;
	}
	if (k >= memFrames || k > inFrames)
	{
		// ring の窓 (既定 5 秒) を超えた要求は窓内へ丸める
		k = static_cast<std::uint32_t>((std::min)(memFrames - 1, inFrames));
		debug::warnOnce("resim.clamp", "resim: 要求が ring の窓を超えたため丸めた");
	}
	if (k == 0) { return false; }

	const std::uint8_t* past = m_moduleMemoryRing.at(k);
	if (past == nullptr || !rewindModuleMemory(past, m_moduleMemorySize)) { return false; }

	// state[k フレーム前] から進めるための入力列 = InputRing の (k-1)〜0 フレーム前 (古い順)。
	// 再生中の push で ring が上書きされるため、ここで線形バッファへ退避する (~6KB×k)。
	try { m_resimQueue.assign(static_cast<std::size_t>(k) * kSnapSize, 0); }
	catch (...) { return false; }
	for (std::uint32_t i = 0; i < k; ++i)
	{
		const std::uint8_t* snap = m_moduleInputRing.at(k - 1 - static_cast<std::size_t>(i));
		if (snap == nullptr) { m_resimQueue.clear(); return false; }
		std::memcpy(m_resimQueue.data() + static_cast<std::size_t>(i) * kSnapSize,
		            snap, kSnapSize);
	}
	m_resimCursor   = 0;
	m_resimSnapSize = kSnapSize;
	return true;
}

MITIRU_INLINE void mitiru::Engine::applyResimInputOverride()
{
	if (m_resimSnapSize == 0 || !m_moduleInputSnapshot) { return; }
	const std::size_t total = m_resimQueue.size() / m_resimSnapSize;
	if (m_resimCursor >= total)
	{
		// 使い切り → ライブ入力へシームレス復帰
		m_resimQueue.clear(); m_resimCursor = 0; m_resimSnapSize = 0;
		return;
	}
	std::memcpy(m_moduleInputSnapshot.get(),
	            m_resimQueue.data() + m_resimCursor * m_resimSnapSize, m_resimSnapSize);
	++m_resimCursor;
}

MITIRU_INLINE std::size_t mitiru::Engine::moduleMemoryRingSize() const noexcept
{
	return m_moduleMemoryRing.size();
}

MITIRU_INLINE bool
mitiru::Engine::rewindModuleMemory(const void* bytes, std::uint32_t size) noexcept
{
	if (m_moduleMemory == nullptr || bytes == nullptr) { return false; }
	if (size == 0 || size != m_moduleMemorySize) { return false; }  // size guard (reload 防御)
	std::memcpy(m_moduleMemory, bytes, size);
	return true;
}

MITIRU_INLINE std::string
mitiru::Engine::branchModuleMemory(const module::InputSnapshot* inputs, int frameCount)
{
	if (m_moduleMemory == nullptr || m_moduleMemorySize == 0
	    || m_moduleApi.on_update == nullptr || inputs == nullptr || frameCount <= 0)
	{
		return "{}";
	}

	// 現 GameMemory を退避 (試行後に bit-exact 復元する)。
	std::vector<std::uint8_t> saved(m_moduleMemorySize);
	std::memcpy(saved.data(), m_moduleMemory, m_moduleMemorySize);

	// 台本入力で on_update を frameCount 回回す。draw/present/intents drain は一切しない
	// (= sound/state push 等の副作用が外に出ない)。intents は使い捨て (~300KB なので heap)。
	// dt は snapshot の effectiveDt を渡す (v21、H-3): 記録済み入力列なら pause/hitStop の
	// dt gating も再現される。台本を合成する側 (HTTP 等) は effectiveDt を明示する契約。
	// 0 のフレームは「進まない」の意味 (pause 記録の忠実な再現)。
	auto intents = std::make_unique<module::FrameIntents>();
	for (int i = 0; i < frameCount; ++i)
	{
		std::memset(intents.get(), 0, sizeof(module::FrameIntents));
		m_moduleApi.on_update(m_moduleMemory, inputs[i].effectiveDt, &inputs[i], intents.get());
	}

	// 試行後の state を reflected JSON に。
	nlohmann::json state = observe::reflectToJson(
		static_cast<const std::uint8_t*>(m_moduleMemory), m_moduleMemorySize,
		m_moduleApi.reflectFields, m_moduleApi.reflectFieldCount,
		m_moduleApi.reflectSchemas, m_moduleApi.reflectSchemaCount);

	// GameMemory を試行前へ復元 (live は何も変わらなかったことになる)。
	std::memcpy(m_moduleMemory, saved.data(), m_moduleMemorySize);
	return state.dump();
}

// ── moduleLoadError ────────────────────────────────────────────────────────
// ModuleHost は Engine.hpp では前方宣言 (pimpl) のため、ここで定義する。

MITIRU_INLINE std::string mitiru::Engine::moduleLoadError() const
{
	return m_moduleHost ? m_moduleHost->lastError() : std::string{};
}
