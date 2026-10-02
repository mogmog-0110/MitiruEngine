// mitiru::Engine の detail header。直接 include しないこと。core/Engine.hpp 経由で include される
#pragma once

/// @file Engine_Module_Loader.hpp
/// @brief Engine の module loader 部分の out-of-class 定義 (v0.2.0 step 2-3)
/// @details
/// `Engine::loadModule / unloadModule / reloadModule` の実装と、
/// module 状態の accessor 群、rewind 用 GameMemory ring 記録 /
/// rewind / branch を収める。
/// per-frame signal flow は Engine_Module_Adapter.hpp 側。

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <mitiru/core/detail/PhysicsQueryJob.hpp>
#include <mitiru/module/Spawner.hpp>
#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/bridge/StateStore.hpp>
#include <mitiru/core/Game.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/core/Screen.hpp>
#include <mitiru/debug/InspectorLauncher.hpp>
#include <mitiru/debug/CrashReport.hpp>
#include <mitiru/debug/DebugPrint.hpp>
#include <mitiru/module/DrawCommands.hpp>
#include <mitiru/module/ModuleHost.hpp>
#include <mitiru/module/SoundIntentRouter.hpp>
#include <mitiru/observe/BugRing.hpp>
#include <mitiru/observe/GameMemoryRing.hpp>
#include <mitiru/observe/Reflect.hpp>
#include <mitiru/observe/ReflectDiff.hpp>
#include <mitiru/observe/SeriesMarkers.hpp>
#include <mitiru/observe/SharedSnapshot.hpp>
#include <mitiru/render/SaveScreenshotPng.hpp>

// ── Free helper 群 (wire version の人間語化、H-1/H-4) ──────────────────────
namespace mitiru::module::detail
{

/// @brief wire version (数値 + build 指紋) を人間語へ (例 "v21 (VC19, CRT=dll (/MD 系), IDL=2)")。
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

/// @brief `Engine_Module_Adapter.hpp` の実体を先出し宣言 (include 順で本体がまだ見えないため。
///        `Engine_Module.hpp` が Loader→Adapter の順に include する)。`SceneViewObject` は
///        ポインタ引数でしか使わないので前方宣言のみで足りる。
struct SceneViewObject;
void drainDrawCommands(mitiru::Screen& screen, const mitiru::module::DrawCommandBuffer& buf,
                       mitiru::render::SpriteCache& spriteCache,
                       std::vector<SceneViewObject>* sceneOut) noexcept;

}  // namespace mitiru::module::detail

// ── 1 ファイル配布 (P12) ─────────────────────────────────────────────────

MITIRU_INLINE std::filesystem::path mitiru::Engine::mountModulePackIfConfigured()
{
	std::string packPath = m_config.packPath;
	if (packPath.empty())
	{
		// MITIRU_PACK (P12 の 1 ファイル配布契約: module.dll 必須) を正とする。
		// MITIRU_ASSET_PACK (host --asset-pack の既存経路、assets 専用 pack を指す) は
		// 後方互換のフォールバックとして読む (module.dll を同梱した pack をこちらの
		// env 経由で渡していた既存運用を壊さないため)。両方設定されていたら
		// MITIRU_PACK を優先し、その旨を 1 回だけ警告する (知らせずに片方を無視しない)。
		const char* pack      = std::getenv("MITIRU_PACK");
		const char* assetPack = std::getenv("MITIRU_ASSET_PACK");
		if (pack != nullptr && pack[0] != '\0')
		{
			packPath = pack;
			if (assetPack != nullptr && assetPack[0] != '\0')
			{
				debug::warnOnce("pack.env.both-set",
					"MITIRU_PACK と MITIRU_ASSET_PACK が両方設定されています。"
					"module.dll の load には MITIRU_PACK を使い、MITIRU_ASSET_PACK は無視します。");
			}
		}
		else if (assetPack != nullptr && assetPack[0] != '\0')
		{
			packPath = assetPack;
		}
	}
	if (packPath.empty()) { return {}; }

	auto pack = vfs::AssetPack::open(packPath);
	if (!pack)
	{
		std::fprintf(stderr, "[pack] %s を開けません (.mtpak として不正)\n", packPath.c_str());
		return {};
	}

	// tools/make_pack.py が書く固定レイアウト: "module.dll" が唯一の実行コード、
	// 残りは assets/**・recordings/** (どちらも readGlobal 経由で VFS mount 後に読める)。
	auto dllBytes = pack->read("module.dll");
	if (!dllBytes)
	{
		std::fprintf(stderr, "[pack] %s に module.dll がありません\n", packPath.c_str());
		return {};
	}

	const std::filesystem::path extractedPath =
		std::filesystem::temp_directory_path() /
		("mitiru_pack_" + std::filesystem::path(packPath).stem().string() + ".dll");
	std::ofstream out(extractedPath, std::ios::binary | std::ios::trunc);
	if (!out || !out.write(reinterpret_cast<const char*>(dllBytes->data()),
		static_cast<std::streamsize>(dllBytes->size())))
	{
		std::fprintf(stderr, "[pack] %s の展開に失敗しました\n", extractedPath.string().c_str());
		return {};
	}
	out.close();

	// assets/recordings をグローバル mount する (以後 vfs::readGlobal/readAsset がこの pack を優先)。
	vfs::mountGlobal(std::move(*pack));
	return extractedPath;
}

MITIRU_INLINE bool mitiru::Engine::loadModule(const std::filesystem::path& modulePathIn)
{
	// すでに module が active なら明示的 unload を要求する。
	if (m_moduleHost && m_moduleHost->isLoaded())
	{
		return false;
	}

	// P12: pack 配布時は渡された modulePath を無視し、pack 内の DLL を使う。
	const std::filesystem::path packDll = mountModulePackIfConfigured();
	const std::filesystem::path& modulePath = !packDll.empty() ? packDll : modulePathIn;

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
	if (!guardModuleCode("mitiru_module_load", m_moduleHost.get(), "load refused",
	                     [&] { loadFn(&m_moduleApi, &m_moduleMemory); }))
	{
		m_moduleMemory = memoryBefore;
		m_moduleApi    = module::ModuleApi{};
		m_moduleHost->unload();
		m_moduleHost->setLastError("mitiru_module_load の中で落ちました (報告は crash フォルダ)");
		return false;
	}

	// DLL が申告した GameMemory サイズを保持。v≤8 DLL は未設定 ⇒ zero-init の 0。
	m_moduleMemorySize = m_moduleApi.memorySize;

	// version check。拒否時は DLL が確保したばかりの memory を unloadFn で DLL に
	// 返却してから unload する (リーク解消)。返却は fresh 確保時のみ。温存 memory を
	// 渡す reload は reloadModule 側で先ロード検証されるため、ここでは触らない。
	// ABI は「数値 + build 指紋」の完全一致を要求する (H-1/H-4)。古い DLL (version < host)
	// も弾く: SoundIntent 等の配列要素が後の version で大きくなると soundIntents[] の stride =
	// 後続 FrameIntents field の offset がズレ、旧 DLL を新 host で動かすと気づかれないまま破損/
	// クラッシュするため (D1)。数値一致でも CRT 種別 / IDL / toolset 混成は Screen* (STL
	// 内包) と cross-DLL delete が気づかれないまま破損するため同様に拒否する。
	if (m_moduleApi.version != module::kWireApiVersion)
	{
		const std::uint32_t dllVersion = m_moduleApi.version;
		if (memoryBefore == nullptr && m_moduleMemory != nullptr)
		{
			if (auto unloadFn = m_moduleHost->unloadFn())
			{
				(void)guardModuleCode("mitiru_module_unload", m_moduleHost.get(), "load refused",
				                      [&] { unloadFn(m_moduleMemory); });
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

	m_moduleReflection = m_moduleHost->captureReflection();
	bindModuleSideState();
	m_sideStateRing.clear();
	observe::restartBugRing(this);

	// reflection を申告したのに memorySize=0 だと reflectToJson が bounds 外で全 skip し
	// /api/ai/state が {} を返す。原因が分かりにくいので一度だけ警告する (R-01)。
	// non-POD game でも api->memorySize = sizeof(GameMemory) を申告すれば現フレーム観測は可
	// (reflectToJson は申告した offset のスカラーしか触らない)。ring/diff/branch は flat POD 必須。
	if (m_moduleReflection.fieldCount() > 0 && m_moduleMemorySize == 0)
	{
		std::fprintf(stderr,
			"[ai] warning: MITIRU_REFLECT で %d field 申告されていますが api->memorySize が 0 です。"
			"/api/ai/state は空 {} になります。api->memorySize = sizeof(GameMemory) を申告してください。\n",
			static_cast<int>(m_moduleReflection.fieldCount()));
	}

	// pause 中も dt を通す layer mask (2-1)。宣言が無い DLL は resolveSymbol が nullptr を
	// 返すので mask=0 (従来どおり pause は全 layer 共通) のまま。
	m_pauseAlwaysLayersMask = 0;
	if (auto fn = m_moduleHost->pauseAlwaysLayersMaskFn())
	{
		m_pauseAlwaysLayersMask = fn();
	}
	// 種類別 (ingame/debug/object) の宣言があればそちら、無ければ 3 種類とも always mask。
	for (std::uint8_t k = 0; k < 4; ++k) { m_pauseLayersByKind[k] = m_pauseAlwaysLayersMask; }
	if (auto fn = m_moduleHost->pauseLayersByKindFn())
	{
		for (std::uint8_t k = 1; k < 4; ++k) { m_pauseLayersByKind[k] = fn(k); }
	}

	// 物理問い合わせ job (v37) が答える静的 world。--collision の JSON から箱を積む。無ければ world を持たない。
	m_modulePhysics.reset();
	m_pendingPhysicsQueries.clear();
	if (!m_config.collisionPath.empty())
	{
		m_modulePhysics = detail::loadCollisionWorld(m_config.collisionPath);
	}

	// 型の台帳 (HE2 の GameObjectClassRegistry 相当)。load 時に 1 回 JSON にしておき、/api/ai/types は
	// 文字列を返すだけにする (型の一覧は実行中に変わらない)。
	m_spawnerTypesJson.clear();
	if (auto fn = m_moduleHost->spawnerTypesFn())
	{
		std::vector<module::SpawnerTypeEntry> rows(module::detail::kMaxSpawnerEntries);
		const int n = fn(rows.data(), static_cast<int>(rows.size()));
		nlohmann::json types = nlohmann::json::array();
		for (int i = 0; i < n && i < static_cast<int>(rows.size()); ++i)
		{
			const auto& r = rows[static_cast<std::size_t>(i)];
			types.push_back({
				{"type", std::string(r.jsonName, ::strnlen(r.jsonName, sizeof(r.jsonName)))},
				{"caption", std::string(r.info.caption, ::strnlen(r.info.caption, sizeof(r.info.caption)))},
				{"group", std::string(r.info.group, ::strnlen(r.info.group, sizeof(r.info.group)))},
				{"placeable", r.info.placeable != 0},
				{"eternal", r.info.eternal != 0},
				{"size", r.size}});
		}
		m_spawnerTypesJson = nlohmann::json{{"supported", true}, {"types", std::move(types)}}.dump();
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

	clearModuleFault();
	debug::setCrashContextText(debug::crashContext().gameDll, debug::pathToUtf8(modulePath));
	module::setFaultDumpDirectory(debug::crashDirectory(), modulePath.stem());

	// on_init は「memory が新規確保された時」のみ呼ぶ (Game.hpp registerGame の設計意図)。
	// 温存 memory を渡された場合に呼ぶと T::init() が user 状態をリセットし得る。
	if (m_moduleApi.on_init != nullptr && memoryBefore == nullptr)
	{
		guardModuleCallback("on_init", [&] { m_moduleApi.on_init(m_moduleMemory); });
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

	releaseReloadRollback();
	if (m_moduleApi.on_shutdown != nullptr)
	{
		try { guardModuleCallback("on_shutdown", [&] { m_moduleApi.on_shutdown(m_moduleMemory); }); }
		catch (...) {}
	}

	// 落ちた game の解放処理は呼ばない。壊れた heap を delete させて host まで落とすより、
	// 終了直前の 1 割当を手放す方が安い。
	if (auto unloadFn = m_moduleHost->unloadFn(); unloadFn != nullptr && !m_moduleFaulted)
	{
		try { guardModuleCallback("mitiru_module_unload", [&] { unloadFn(m_moduleMemory); }); }
		catch (...) {}
	}

	// unloadFn は DLL 側 delete。解放済み pointer を保持し続けると次の load で
	// 「非 null なら再利用」に渡って use-after-free になる (A5)。必ず null へ戻す。
	m_moduleMemory     = nullptr;
	m_moduleMemorySize = 0;
	m_pauseAlwaysLayersMask = 0;

	m_moduleApi = module::ModuleApi{};
	m_moduleReflection = module::ModuleReflection{};
	m_sideState.unbind();          // 窓口の ctx と関数は解放する DLL の中を指す
	m_sideStateRing.clear();
	m_sideRestorePending = false;
	m_moduleHost->unload();

	// (もう使えない) DLL が所有する state を参照していた pending event は
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
	// 旧 DLL を残したまま、新 DLL を一時 host で load + API 解決 + version 検証
	// まで済ませる。途中で失敗したら旧 module / 旧 memory には一切触らず false を
	// 返す → host は「old code で継続」できる。temp copy 名が一意なので同一 source
	// でも独立 module として並走 load できる (ModuleHost の copy strategy)。
	module::ModuleHost newHost;
	if (!newHost.load(modulePath))
	{
		m_moduleHost->setLastError(newHost.lastError(), newHost.lastLoadBusy());
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
	if (!guardModuleCode("mitiru_module_load", &newHost, "reload refused; the running DLL continues",
	                     [&] { loadFn(&newApi, &memory); }))
	{
		m_moduleHost->setLastError("新 DLL の mitiru_module_load の中で落ちました (旧コードで継続)");
		return false;
	}

	if (newApi.version != module::kWireApiVersion)  // 数値 + 指紋の完全一致要求 (D1 / H-1/H-4)
	{
		// 新 DLL が fresh 確保した場合のみ新 DLL 自身に返却する。温存 memory は
		// 旧 module が継続使用するため絶対に解放しない。
		if (memoryBefore == nullptr && memory != nullptr)
		{
			if (auto unloadFn = newHost.unloadFn())
			{
				(void)guardModuleCode("mitiru_module_unload", &newHost, "reload refused",
				                      [&] { unloadFn(memory); });
			}
		}
		m_moduleHost->setLastError(
			module::detail::describeVersionMismatch(newApi.version, module::kWireApiVersion));
		return false;
	}

	// ── GameMemory サイズ / layout 変動 guard (C-1 + layout hash) ──────────
	// 温存 pointer の割当は旧 sizeof のまま。新 DLL の申告サイズが違うと旧割当の
	// 末尾を越えて read/write する heap overflow になるため、状態温存を放棄する。
	// サイズ一致でも反射記述子の hash か、反射が無ければ形の hash (mitiru_module_layout_hash) が
	// 違えば field の並べ替え / 型変更。旧 bytes を新 layout で解釈すると値がおかしくなるため、同様に放棄する。
	// 旧割当は「確保した世代の DLL」の unloadFn に返却させる (cross-CRT delete 回避)。
	// ここは全検証通過後なので、以降の失敗で旧 module へ戻る経路は無い。
	module::ModuleReflection newReflection = newHost.captureReflection();
	// 差し替える前の DLL の窓口の状態。DLL の static は差し替えで消えるので、今のうちに写して新しい DLL へ戻す。
	// 落ちて止まっている DLL は呼ばず、止めたときに GameMemory を戻したフレームの記録を使う。
	std::vector<std::uint8_t> carriedSide;
	if (m_sideRestorePending)
	{
		std::size_t len = 0;
		if (const std::uint8_t* side = m_sideStateRing.at(0, len)) { carriedSide.assign(side, side + len); }
	}
	else if (!m_moduleFaulted)
	{
		(void)captureModuleSideState(carriedSide, true);
	}
	// 新しい DLL が最初のフレームで落ちたときの戻り先。後段で旧 memory を解放することがあるので先に写す。
	std::vector<std::uint8_t> preReloadMemory;
	// 落ちて止まっている DLL は戻り先にしない (戻っても同じ所でまた落ちる)。
	if (m_config.reloadRollbackFrames > 0 && memoryBefore != nullptr && m_moduleMemorySize > 0 && !m_moduleFaulted)
	{
		preReloadMemory.assign(static_cast<const std::uint8_t*>(memoryBefore),
		                       static_cast<const std::uint8_t*>(memoryBefore) + m_moduleMemorySize);
	}
	const bool layoutChanged = module::layoutDiffers(m_moduleReflection, newReflection);
	bool stateReset = false;
	// P9: layout 変動時に丸ごと初期化する代わり、reflect の名前+型が一致する field だけ
	// 引き継ぐための旧 GameMemory の退避先 (unloadFn で旧 memory を返却する前に読み取る)。
	std::vector<std::uint8_t> oldMemorySnapshot;
	module::ModuleReflection  oldReflection;
	if (memoryBefore != nullptr
	    && (newApi.memorySize != m_moduleMemorySize || layoutChanged))
	{
		const bool canMigrate = m_moduleReflection.fieldCount() > 0 && newReflection.fieldCount() > 0;
		const char* const carry = canMigrate ? "reflect 一致 field のみ引き継ぎ"
		                                     : "反射が無いので状態は引き継がず初期化";
		if (newApi.memorySize != m_moduleMemorySize)
		{
			std::fprintf(stderr, "[module] reload: GameMemory size changed %u -> %u, %s\n",
				m_moduleMemorySize, newApi.memorySize, carry);
		}
		else
		{
			std::fprintf(stderr,
				"[module] reload: GameMemory layout changed (size %u unchanged, layout hash mismatch), %s\n",
				m_moduleMemorySize, carry);
		}
		if (canMigrate)
		{
			oldMemorySnapshot.assign(
				static_cast<const std::uint8_t*>(m_moduleMemory),
				static_cast<const std::uint8_t*>(m_moduleMemory) + m_moduleMemorySize);
			oldReflection = m_moduleReflection;
		}
		if (m_moduleApi.on_shutdown != nullptr)
		{
			try { guardModuleCallback("on_shutdown", [&] { m_moduleApi.on_shutdown(m_moduleMemory); }); }
			catch (...) {}
		}
		// 落ちた旧 DLL には解放させない (unloadModule と同じ理由)。
		if (auto oldUnloadFn = m_moduleHost->unloadFn(); oldUnloadFn != nullptr && !m_moduleFaulted)
		{
			try { guardModuleCallback("mitiru_module_unload", [&] { oldUnloadFn(m_moduleMemory); }); }
			catch (...) {}
		}
		m_moduleMemory = nullptr;
		// null slot を渡し直して新 DLL に fresh 確保させる。以降は初回 load と
		// 同じ経路 (memoryBefore=null 扱いで末尾の on_init が走り、その後 migrate で上書きする)。
		newApi         = module::ModuleApi{};
		newApi.version = module::kWireApiVersion;
		memory         = nullptr;
		memoryBefore   = nullptr;
		// 旧 memory は返却済みなので、ここで落ちたら戻る先が無い。module を外して host だけ動かし続ける。
		if (!guardModuleCode("mitiru_module_load", &newHost, "reload failed; no module is loaded",
		                     [&] { loadFn(&newApi, &memory); }))
		{
			m_moduleApi        = module::ModuleApi{};
			m_moduleMemorySize = 0;
			m_moduleHost->unload();
			m_moduleHost->setLastError("新 DLL の mitiru_module_load の中で落ちました (module は外れています)");
			return false;
		}
		newReflection = newHost.captureReflection();
		stateReset = true;
	}

	// ── 差し替え ──────────────────────────────────────────────────────────
	// 旧 DLL は unloadFn (DLL 側 delete) を呼ばず FreeLibrary のみ。GameMemory は
	// 引き続き host が所有する = 状態温存の正規化 (解放済み pointer の再利用ではない)。
	// 旧 on_shutdown も呼ばない。GameMemory は flat POD 契約で DLL 側に
	// 解放すべきリソースを持たないし、T::shutdown() が状態をおかしくする余地も残さない。
	releaseReloadRollback();
	if (!preReloadMemory.empty())
	{
		m_rollbackHost       = std::make_unique<module::ModuleHost>(std::move(*m_moduleHost));
		m_rollbackApi        = m_moduleApi;
		m_rollbackReflection = m_moduleReflection;
		m_rollbackMemory     = std::move(preReloadMemory);
		m_rollbackFramesLeft = m_config.reloadRollbackFrames;
	}
	*m_moduleHost      = std::move(newHost);  // move 代入が旧 handle を FreeLibrary する (戻り先に移した場合は空)
	m_moduleApi        = newApi;
	m_moduleReflection = std::move(newReflection);
	m_moduleMemory     = memory;
	m_moduleMemorySize = newApi.memorySize;
	bindModuleSideState();

	// 旧 DLL の code を参照しうる pending event は破棄する (unloadModule と同じ理由)。
	if (m_moduleActionEvents)
	{
		std::lock_guard lock(m_moduleActionEvents->mu);
		m_moduleActionEvents->events.clear();
	}

	// GameMemory サイズ・layout が不変なら ring を温存する (rewind → 編集 →
	// reload → resim の合流に必要)。サイズ変化 / layout hash 不一致 (state reset 済み) は
	// 旧 layout bytes へ rewind すると復元がおかしくなるため破棄する。InputRing は layout 非依存なので常に温存する。
	if (m_moduleMemoryRing.frameSize() != m_moduleMemorySize || stateReset)
	{
		m_moduleMemoryRing.clear();
		m_sideStateRing.clear();
		observe::restartBugRing(this);
		m_resimQueue.clear(); m_resimCursor = 0; m_resimSnapSize = 0;  // 進行中 resim も破棄
	}

	// sprite(id) の解決基準を新 DLL の隣へ更新する (loadModule と同じ規約、ABI v16)。
	m_spriteCache.setBaseDir(modulePath.parent_path() / "assets" / "sprites");

	const bool resumeAfterFault = m_moduleFaulted && memoryBefore != nullptr;
	clearModuleFault();
	debug::setCrashContextText(debug::crashContext().gameDll, debug::pathToUtf8(modulePath));
	module::setFaultDumpDirectory(debug::crashDirectory(), modulePath.stem());

	// 停止時に GameMemory を戻した時、落ちた DLL には場面を組み直させていない。新しい DLL に組み直させる。
	if (resumeAfterFault && newApi.on_rebuild != nullptr)
	{
		guardModuleCallback("on_rebuild", [&] { newApi.on_rebuild(memory, module::kModuleRebuildRestore); });
	}
	// 温存した GameMemory に合わせて窓口も戻す。形が変わって初期化した reload は on_init が両方を作る。
	if (!stateReset) { (void)carryModuleSideStateAcrossReload(carriedSide); }

	// memory 温存 reload では on_init を呼ばない (T::init() が user 状態をリセットし得る)。
	// 旧 memory が無く fresh 確保された時だけ初回 load と同様に呼ぶ。
	if (memoryBefore == nullptr && newApi.on_init != nullptr)
	{
		guardModuleCallback("on_init", [&] { newApi.on_init(memory); });
	}
	// P9: on_init 後の新 GameMemory (= 新 layout の初期値) へ、旧 GameMemory から
	// 名前+型が一致した field だけ上書きする。一致しない field は on_init の初期値のまま。
	if (!oldMemorySnapshot.empty() && memory != nullptr)
	{
		observe::migrateReflectedMemory(
			oldMemorySnapshot.data(), static_cast<std::uint32_t>(oldMemorySnapshot.size()),
			oldReflection.fieldsData(), oldReflection.fieldCount(),
			oldReflection.schemasData(), oldReflection.schemaCount(),
			static_cast<std::uint8_t*>(memory), newApi.memorySize,
			m_moduleReflection.fieldsData(), m_moduleReflection.fieldCount(),
			m_moduleReflection.schemasData(), m_moduleReflection.schemaCount());
	}
	return true;
}

// ── reload 直後の戻り先 ────────────────────────────────────────────────────

MITIRU_INLINE void mitiru::Engine::releaseReloadRollback() noexcept
{
	m_rollbackHost.reset();  // 差し替え前の DLL を FreeLibrary する
	m_rollbackApi        = module::ModuleApi{};
	m_rollbackReflection = module::ModuleReflection{};
	m_rollbackMemory.clear();
	m_rollbackMemory.shrink_to_fit();
	m_rollbackFramesLeft = 0;
}

MITIRU_INLINE bool mitiru::Engine::reloadRollbackArmed() const noexcept
{
	return m_rollbackHost != nullptr && m_rollbackHost->isLoaded();
}

MITIRU_INLINE bool mitiru::Engine::rollbackModuleReload()
{
	if (!reloadRollbackArmed() || !m_moduleHost) { return false; }
	const auto oldSize = static_cast<std::uint32_t>(m_rollbackMemory.size());
	void* memory = m_moduleMemory;
	const bool sameLayout = oldSize == m_moduleMemorySize
	                        && !module::layoutDiffers(m_rollbackReflection, m_moduleReflection);
	if (!sameLayout)
	{
		// 形が変わっていた reload は旧 memory を解放済み。旧 DLL に旧い形で確保し直させる (on_init は呼ばない)。
		module::ModuleApi api{};
		api.version = module::kWireApiVersion;
		void* fresh = nullptr;
		if (auto loadFn = m_rollbackHost->loadFn())
		{
			(void)guardModuleCode("mitiru_module_load", m_rollbackHost.get(), "rollback abandoned",
			                      [&] { loadFn(&api, &fresh); });
		}
		if (fresh == nullptr) { return false; }
		// 落ちたばかりの新しい DLL には解放させない (壊れた heap を触らせない)。1 割当を手放す。
		memory        = fresh;
		m_rollbackApi = api;
	}
	std::memcpy(memory, m_rollbackMemory.data(), oldSize);

	*m_moduleHost      = std::move(*m_rollbackHost);  // 落ちた新しい DLL を FreeLibrary する
	m_moduleApi        = m_rollbackApi;
	m_moduleReflection = std::move(m_rollbackReflection);
	m_moduleMemory     = memory;
	m_moduleMemorySize = oldSize;
	releaseReloadRollback();
	// 戻り先の DLL の窓口は差し替えの時点のまま残っていて、戻した GameMemory と同じ時点を指す。
	bindModuleSideState();
	m_sideStateRing.clear();
	observe::restartBugRing(this);

	m_moduleMemoryRing.clear();
	m_resimQueue.clear(); m_resimCursor = 0; m_resimSnapSize = 0;
	if (m_moduleActionEvents)
	{
		std::lock_guard lock(m_moduleActionEvents->mu);
		m_moduleActionEvents->events.clear();
	}
	// 戻り先の DLL が組み直しで落ちたら、猶予は解放済みなので通常の停止になる (報告して新しい DLL を待つ)。
	if (m_moduleApi.on_rebuild != nullptr)
	{
		guardModuleCallback("on_rebuild", [&] { m_moduleApi.on_rebuild(m_moduleMemory, module::kModuleRebuildRestore); });
	}
	std::fprintf(stderr,
		"[module] 差し替えた DLL が最初のフレームで落ちたので、差し替え前の DLL と状態へ戻しました。"
		"直して保存すればもう一度読み直します\n");
	return true;
}

MITIRU_INLINE bool mitiru::Engine::callModuleUpdate(const module::InputSnapshot* snap, module::FrameIntents* intents)
{
	const bool ok = guardModuleCallback("on_update", [&] {
		m_moduleApi.on_update(m_moduleMemory, snap->effectiveDt, snap, intents);
	});
	if (ok && m_rollbackFramesLeft > 0 && --m_rollbackFramesLeft == 0) { releaseReloadRollback(); }
	return ok;
}

MITIRU_INLINE bool mitiru::Engine::callModuleDrawCommands(const module::DrawContext* ctx, module::DrawCommandBuffer* out)
{
	return guardModuleCallback("on_draw_commands", [&] { m_moduleApi.on_draw_commands(m_moduleMemory, ctx, out); });
}

MITIRU_INLINE bool mitiru::Engine::callModuleDraw(Screen* screen)
{
	return guardModuleCallback("on_draw", [&] { m_moduleApi.on_draw(m_moduleMemory, screen); });
}

// ── moduleStateStore accessor ──────────────────────────────────────────────

MITIRU_INLINE mitiru::bridge::StateStore* mitiru::Engine::moduleStateStore() noexcept
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
	    m_moduleReflection.fieldCount() <= 0)
	{
		return "[]";  // MITIRU_REFLECT 未宣言 / 未 load
	}
	const auto ja = observe::reflectToJson(static_cast<const std::uint8_t*>(a), m_moduleMemorySize,
		m_moduleReflection.fieldsData(), m_moduleReflection.fieldCount(),
		m_moduleReflection.schemasData(), m_moduleReflection.schemaCount());
	const auto jb = observe::reflectToJson(static_cast<const std::uint8_t*>(b), m_moduleMemorySize,
		m_moduleReflection.fieldsData(), m_moduleReflection.fieldCount(),
		m_moduleReflection.schemasData(), m_moduleReflection.schemaCount());
	return observe::reflectDiff(ja, jb).dump();
}

MITIRU_INLINE const char* mitiru::Engine::queryModuleWriteBlame(std::uint32_t offset) const
{
	// game が mitiru_why_blame_at を export していれば呼ぶ (optional、host→DLL の pull)。
	if (!m_moduleHost) { return nullptr; }
	const auto fn = m_moduleHost->whyBlameAtFn();
	return (fn != nullptr) ? fn(offset) : nullptr;
}

MITIRU_INLINE const char* mitiru::Engine::queryModuleEverWrote(std::uint32_t offset) const
{
	// game が mitiru_why_everwrote_at を export していれば呼ぶ (optional、host→DLL の pull)。
	if (!m_moduleHost) { return nullptr; }
	const auto fn = m_moduleHost->whyEverWroteAtFn();
	return (fn != nullptr) ? fn(offset) : nullptr;
}

MITIRU_INLINE std::string mitiru::Engine::reflectBlobJson(const void* blob) const
{
	if (blob == nullptr || m_moduleMemorySize == 0 || m_moduleReflection.fieldCount() <= 0)
	{
		return "{}";
	}
	return observe::reflectToJson(static_cast<const std::uint8_t*>(blob), m_moduleMemorySize,
		m_moduleReflection.fieldsData(), m_moduleReflection.fieldCount(),
		m_moduleReflection.schemasData(), m_moduleReflection.schemaCount()).dump();
}

// ── rewind: GameMemory ring 記録 + rewind ──────────────────

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
		// 予算バイト数: host の --rewind-mb (明示指定時) > game の MITIRU_REWIND_BUDGET 宣言 >
		// 既定 512MB。>0 (または host 明示の 0=無制限) は XOR+RLE デルタ圧縮 ring になる。
		std::size_t budgetBytes = 512ull * 1024ull * 1024ull;
		if (m_moduleHost)
		{
			if (auto fn = m_moduleHost->rewindBudgetBytesFn())
			{
				const std::uint64_t declared = fn();
				if (declared > 0) { budgetBytes = static_cast<std::size_t>(declared); }
			}
		}
		if (m_config.timeTravelBudgetBytesExplicit)
		{
			budgetBytes = m_config.timeTravelBudgetBytes;
		}
		const std::size_t rawLimit = (m_config.timeTravelRawLimitBytes > 0)
			? m_config.timeTravelRawLimitBytes : SIZE_MAX;
		m_moduleMemoryRing.configure(m_moduleMemorySize, frames, budgetBytes, 60, rawLimit);
		// 窓口のリングも同じフレーム数で、同じ予算をもう 1 つ使う (budgetBytes 0 = 無制限は上限なしの扱い)。
		if (!m_sideState.empty())
		{
			m_sideStateRing.configure(frames, budgetBytes == 0 ? SIZE_MAX : budgetBytes, 60);
		}
	}
	m_moduleMemoryRing.push(m_moduleMemory, m_moduleMemorySize);
	recordModuleSideStateFrame();
}

MITIRU_INLINE void mitiru::Engine::recordModuleSideStateFrame()
{
	if (m_sideState.empty()) { return; }
	if (!m_sideStateRing.configured() || m_sideStateRing.capacity() != m_moduleMemoryRing.capacity())
	{
		m_sideStateRing.configure(m_moduleMemoryRing.capacity(), 512ull * 1024ull * 1024ull, 60);
	}
	// 保存できなかったフレームを飛ばして積むと、GameMemory のリングと「何フレーム前か」がずれる。
	// 窓口のリングを空にして、そのフレームから先だけを巻き戻せる範囲にする。
	if (!captureModuleSideState(m_sideScratch, true) || m_sideScratch.empty())
	{
		m_sideStateRing.clear();
		return;
	}
	m_sideStateRing.push(m_sideScratch.data(), m_sideScratch.size());
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
	if (modulePartialState())
	{
		debug::warnOnceFix("resim.partial-state",
			"resim 不可: この game の GameMemory は進行データだけ (MITIRU_GAME_OBJECTS)",
			"過去の bytes へ戻しても場面の中身 (DLL 内のオブジェクト) は戻らない",
			"全状態を巻き戻したい game は MITIRU_GAME (flat POD) で書く");
		return false;
	}
	// 窓口を持つ game は、窓口の記録が残っているフレームまでしか戻れない。
	const std::size_t memFrames = m_sideState.empty()
		? m_moduleMemoryRing.size() : (std::min)(m_moduleMemoryRing.size(), m_sideStateRing.size());
	const std::size_t inFrames  = m_moduleInputRing.size();
	if (m_moduleMemorySize == 0 || memFrames == 0 || inFrames == 0)
	{
		debug::warnOnceFix("resim.unavailable",
			"resim 不可: flat POD 未申告か、巻き戻し ring がまだ空",
			"GameMemory が trivially copyable でない、または reload 直後で ring がまだ埋まっていない",
			"GameMemory を flat POD にするか、数フレーム経過してから resim を呼ぶ");
		return false;
	}
	if (k >= memFrames || k > inFrames)
	{
		// ring の窓 (既定 5 秒) を超えた要求は窓内へ丸める
		k = static_cast<std::uint32_t>((std::min)(memFrames - 1, inFrames));
		debug::warnOnceFix("resim.clamp", "resim: 要求が ring の窓を超えたため丸めた",
			"k が rewind ring の記録済みフレーム数 (既定 5 秒分) を超えている",
			"k を memFrames-1 以下に収めるか、EngineConfig の ring サイズを増やす");
	}
	if (k == 0) { return false; }

	if (!rewindModuleFramesAgo(k)) { return false; }

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
	const bool hasSide = !m_sideState.empty();
	if (size == 0 || (hasSide ? size <= m_moduleMemorySize : size != m_moduleMemorySize))
	{
		if (hasSide && size == m_moduleMemorySize)
		{
			debug::warnOnceFix("rewind.side-missing-image",
				"GameMemory だけを書き戻す操作を断りました (GameMemory の外に持つ状態が戻らず食い違うため)",
				"窓口を持つ game へ、窓口の記録を含まない bytes (古いセーブ・録画等) を戻そうとした",
				"今の DLL でセーブ・録画し直す");
		}
		return false;  // size guard (reload 防御)
	}
	if (hasSide)
	{
		// 窓口の image が形として正しく、今の窓口の表に戻せることを先に確かめる (GameMemory だけ書いて断らない)。
		observe::SideImageView view;
		const auto* image = static_cast<const std::uint8_t*>(bytes) + m_moduleMemorySize;
		const std::size_t imageLen = size - m_moduleMemorySize;
		const std::string why = observe::parseSideImage(image, imageLen, view)
			? m_sideState.mismatch(view) : std::string("記録の形が壊れている");
		if (!why.empty())
		{
			debug::warnOnce("rewind.side-image", "書き戻しを断りました: " + why);
			return false;
		}
	}
	std::memcpy(m_moduleMemory, bytes, m_moduleMemorySize);
	observe::restartBugRing(this);
	// 場面の中身を DLL 内に持つ game (ADR 0040) は、書き戻された進行データから組み立て直す。
	if (m_moduleApi.on_rebuild != nullptr)
	{
		try
		{
			guardModuleCallback("on_rebuild", [&] {
				m_moduleApi.on_rebuild(m_moduleMemory, module::kModuleRebuildRestore);
			});
		}
		catch (...) { debug::warnOnce("rebuild.threw", "on_rebuild が例外を投げました (場面の組み立て直しに失敗)"); }
	}
	if (hasSide)
	{
		return restoreModuleSideImage(static_cast<const std::uint8_t*>(bytes) + m_moduleMemorySize,
		                              size - m_moduleMemorySize, "書き戻し");
	}
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
	if (modulePartialState())
	{
		// 分岐は本物の on_update を回してから bytes を戻す。場面の中身は戻らないので、試すだけでおかしくなる。
		debug::warnOnce("branch.partial-state",
			"分岐 (branch) は使えません: この game は MITIRU_GAME_OBJECTS (GameMemory は進行データだけ) です");
		return "{}";
	}

	// 現 GameMemory を退避 (試行後に bit-exact 復元する)。frame arena (2-1) から確保し、
	// HTTP branch endpoint 等の頻繁な呼び出しでも heap allocation を積ませない。
	// arena が溢れた稀なケースだけ heap にフォールバックする。
	std::vector<std::uint8_t> savedFallback;
	std::uint8_t* saved = static_cast<std::uint8_t*>(frameArena().alloc(m_moduleMemorySize));
	if (saved == nullptr)
	{
		savedFallback.resize(m_moduleMemorySize);
		saved = savedFallback.data();
	}
	std::memcpy(saved, m_moduleMemory, m_moduleMemorySize);
	// 窓口の状態も退避する。on_update は本物の world を進めるので、戻さないと試しただけで live が進む。
	if (!captureModuleSideState(m_sideLiveScratch, true)) { return "{}"; }
	const auto restoreLive = [&] {
		std::memcpy(m_moduleMemory, saved, m_moduleMemorySize);
		(void)restoreModuleSideImage(m_sideLiveScratch.data(), m_sideLiveScratch.size(), "分岐の後始末");
	};

	// 台本入力で on_update を frameCount 回回す。draw/present/intents drain は一切しない
	// (= sound/state push 等の副作用が外に出ない)。intents は使い捨て (~300KB なので heap)。
	// dt は snapshot の effectiveDt を渡す (v21、H-3): 記録済み入力列なら pause/hitStop の
	// dt gating も再現される。台本を合成する側 (HTTP 等) は effectiveDt を明示する契約。
	// 0 のフレームは「進まない」の意味 (pause 記録の忠実な再現)。
	auto intents = std::make_unique<module::FrameIntents>();
	for (int i = 0; i < frameCount; ++i)
	{
		std::memset(intents.get(), 0, sizeof(module::FrameIntents));
		// 試行中の落ちは live の障害ではない (止めたり巻き戻したりせず、退避した bytes へ戻すだけ)
		const bool ok = guardModuleCode("on_update (branch)", m_moduleHost.get(),
			"branch discarded; the live game keeps running", [&] {
			m_moduleApi.on_update(m_moduleMemory, inputs[i].effectiveDt, &inputs[i], intents.get());
		});
		if (!ok)
		{
			restoreLive();
			return "{}";
		}
	}

	// 試行後の state を reflected JSON に。
	nlohmann::json state = observe::reflectToJson(
		static_cast<const std::uint8_t*>(m_moduleMemory), m_moduleMemorySize,
		m_moduleReflection.fieldsData(), m_moduleReflection.fieldCount(),
		m_moduleReflection.schemasData(), m_moduleReflection.schemaCount());

	// GameMemory と窓口を試行前へ復元 (live は何も変わらなかったことになる)。
	restoreLive();
	return state.dump();
}

// ── moduleLoadError ────────────────────────────────────────────────────────
// ModuleHost は Engine.hpp では前方宣言 (pimpl) のため、ここで定義する。

MITIRU_INLINE std::string mitiru::Engine::moduleLoadError() const
{
	return m_moduleHost ? m_moduleHost->lastError() : std::string{};
}

MITIRU_INLINE bool mitiru::Engine::moduleLoadBusy() const
{
	return m_moduleHost && m_moduleHost->lastLoadBusy();
}

// ── ゴーストリプレイ (10-1) ──────────────────────────────────────────────

MITIRU_INLINE bool mitiru::Engine::loadGhostModule(const std::filesystem::path& modulePath)
{
	if (m_ghostHost && m_ghostHost->isLoaded()) { return false; }  // 既に load 済み

	if (!m_ghostHost) { m_ghostHost = std::make_unique<module::ModuleHost>(); }
	if (!m_ghostHost->load(modulePath))
	{
		std::fprintf(stderr, "[ghost] load failed: %s\n", m_ghostHost->lastError().c_str());
		return false;
	}

	const auto loadFn = m_ghostHost->loadFn();
	if (loadFn == nullptr)
	{
		m_ghostHost->unload();
		std::fprintf(stderr, "[ghost] load failed: 新 DLL に load entry symbol がありません\n");
		return false;
	}

	// live と state を共有しないため必ず null から fresh 確保する。
	module::ModuleApi api{};
	api.version = module::kWireApiVersion;
	void* memory = nullptr;
	if (!guardModuleCode("mitiru_module_load", m_ghostHost.get(), "ghost replay not started",
	                     [&] { loadFn(&api, &memory); }))
	{
		m_ghostHost->unload();
		return false;
	}

	// ABI/ビルド指紋は live と同じ完全一致要求 (loadModule と同じ理由、D1 / H-1/H-4)。
	if (api.version != module::kWireApiVersion)
	{
		if (memory != nullptr)
		{
			if (auto unloadFn = m_ghostHost->unloadFn())
			{
				(void)guardModuleCode("mitiru_module_unload", m_ghostHost.get(), "ghost replay not started",
				                      [&] { unloadFn(memory); });
			}
		}
		std::fprintf(stderr, "[ghost] load failed: %s\n",
			module::detail::describeVersionMismatch(api.version, module::kWireApiVersion).c_str());
		m_ghostHost->unload();
		return false;
	}

	m_ghostApi        = api;
	m_ghostMemory     = memory;
	m_ghostMemorySize = api.memorySize;
	if (!m_ghostIntents) { m_ghostIntents = std::make_unique<module::FrameIntents>(); }
	if (m_ghostApi.on_init != nullptr
	    && !guardModuleCode("ghost on_init", m_ghostHost.get(), "ghost replay dropped; the live game keeps running",
	                        [&] { m_ghostApi.on_init(m_ghostMemory); }))
	{
		dropGhostModule();
		return false;
	}
	return true;
}

MITIRU_INLINE void mitiru::Engine::unloadGhostModule() noexcept
{
	if (!m_ghostHost || !m_ghostHost->isLoaded()) { return; }

	const char* const action = "ghost unloaded without its own cleanup";
	const bool shutdownOk = m_ghostApi.on_shutdown == nullptr
		|| guardModuleCode("ghost on_shutdown", m_ghostHost.get(), action,
		                   [&] { m_ghostApi.on_shutdown(m_ghostMemory); });
	// on_shutdown で落ちた DLL には解放させない (unloadModule と同じ理由)。
	if (auto unloadFn = m_ghostHost->unloadFn(); unloadFn != nullptr && shutdownOk)
	{
		(void)guardModuleCode("mitiru_module_unload", m_ghostHost.get(), action,
		                      [&] { unloadFn(m_ghostMemory); });
	}
	m_ghostMemory     = nullptr;
	m_ghostMemorySize = 0;
	m_ghostApi        = module::ModuleApi{};
	m_ghostHost->unload();
}

MITIRU_INLINE void mitiru::Engine::stepGhost(const module::InputSnapshot& snapshot) noexcept
{
	if (m_ghostMemory == nullptr || m_ghostApi.on_update == nullptr || !m_ghostIntents) { return; }
	// ghost は観察専用。intents は使い捨てで drain しない (副作用を live や host state に及ぼさない)。
	std::memset(m_ghostIntents.get(), 0, sizeof(module::FrameIntents));
	module::ModuleFault fault{};
	bool ok = false;
	try
	{
		ok = module::callGuarded("ghost on_update", fault, [&] {
			m_ghostApi.on_update(m_ghostMemory, snapshot.effectiveDt, &snapshot, m_ghostIntents.get());
		});
	}
	catch (...) {}
	if (!ok) { abandonGhostModule(fault); }
}

MITIRU_INLINE bool mitiru::Engine::hasGhostModule() const noexcept
{
	return m_ghostHost && m_ghostHost->isLoaded();
}

MITIRU_INLINE void mitiru::Engine::drawGhost(Screen& screen, float alpha) noexcept
{
	if (m_ghostMemory == nullptr || m_ghostApi.on_draw == nullptr) { return; }

	// ghost 専用の独立 Screen へ焼く (SW ラスタライズのみ。live の pipeline を共有しない)。
	// サイズが live と食い違ったら (起動直後 / リサイズ後) 作り直す。
	if (!m_ghostRenderScreen
		|| m_ghostRenderScreen->width() != screen.width()
		|| m_ghostRenderScreen->height() != screen.height())
	{
		m_ghostRenderScreen = std::make_unique<Screen>(screen.width(), screen.height());
		m_ghostRenderScreen->enableSoftwareFramebuffer();
	}
	Screen& gs = *m_ghostRenderScreen;
	gs.resetDrawCallCount();
	gs.clear(sgc::Colorf{0.0f, 0.0f, 0.0f, 0.0f});
	module::ModuleFault fault{};
	bool ok = false;
	try { ok = module::callGuarded("ghost on_draw", fault, [&] { m_ghostApi.on_draw(m_ghostMemory, &gs); }); }
	catch (...) {}
	if (!ok)
	{
		abandonGhostModule(fault);
		return;
	}
	gs.present();

	// ghost が描いた画素だけ alpha を一律減衰させ、1 枚のスプライトとして live に合成する
	// (未描画= alpha 0 の画素は 0 のまま = 何も足さない)。
	m_ghostCompositeBuffer = gs.pixels();
	const auto a = static_cast<std::uint8_t>(
		std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
	for (std::size_t i = 3; i < m_ghostCompositeBuffer.size(); i += 4)
	{
		if (m_ghostCompositeBuffer[i] != 0) { m_ghostCompositeBuffer[i] = a; }
	}

	// drawSprite は「ほぼ透明はカットオフ」するため 0.5 未満の alpha が消える。
	// blitAlphaBlended はカットオフ無しでそのまま src-over 合成する。
	screen.blitAlphaBlended(
		sgc::Rectf{0.0f, 0.0f,
			static_cast<float>(screen.width()), static_cast<float>(screen.height())},
		m_ghostCompositeBuffer.data(), gs.width(), gs.height());
}

// ── 分岐候補 (O4、ADR 0035「候補レーン」) ────────────────────────────────
// ghost (上) と違い DLL は再 load しない。live で load 済みの m_moduleApi (同じ
// on_update/on_draw 関数ポインタ) をそのまま使い回し、GameMemory だけ slot ごとに
// 複製 + 上書き差分を持つ。live には一切書き込まない (branchModuleMemory と同じ契約)。

MITIRU_INLINE std::string mitiru::Engine::stepCandidateBranch(std::size_t slot,
	const std::string& overridesJson, const module::InputSnapshot* inputs, int frameCount)
{
	if (slot >= kMaxCandidateBranches) { return "{}"; }
	if (m_moduleMemory == nullptr || m_moduleMemorySize == 0 || m_moduleApi.on_update == nullptr
		|| m_moduleReflection.fieldCount() <= 0 || inputs == nullptr || frameCount <= 0)
	{ return "{}"; }
	if (modulePartialState())
	{
		debug::warnOnce("candidates.partial-state",
			"候補の並走は使えません: この game は MITIRU_GAME_OBJECTS (GameMemory は進行データだけ) です");
		return "{}";
	}

	CandidateBranch& c = m_candidateBranches[slot];
	c.memory.assign(m_moduleMemorySize, std::uint8_t{0});
	std::memcpy(c.memory.data(), m_moduleMemory, m_moduleMemorySize);

	// aiStatePut (Engine_Http.hpp) と同じ書式 ({"field": value, ...})。候補は「試して
	// 捨てる」前提の使い捨てバッファなので、1 field の失敗 (未知 field・型不一致) で全体を
	// 捨てず、書けた分だけ反映して続行する (live には触れないので安全)。
	if (!overridesJson.empty())
	{
		nlohmann::json req;
		try { req = nlohmann::json::parse(overridesJson); } catch (...) { req = nlohmann::json::object(); }
		if (req.is_object())
		{
			for (auto it = req.begin(); it != req.end(); ++it)
			{
				std::string err;
				(void)observe::reflectWriteField(c.memory.data(), m_moduleMemorySize,
					m_moduleReflection.fieldsData(), m_moduleReflection.fieldCount(),
					m_moduleReflection.schemasData(), m_moduleReflection.schemaCount(), it.key(), it.value(), err);
			}
		}
	}

	// 窓口の状態は DLL に 1 組しかない。候補は live の状態から進め、進めた後を c.side に写して live へ戻す。
	if (!captureModuleSideState(m_sideLiveScratch, true)) { return "{}"; }
	if (!c.intents) { c.intents = std::make_unique<module::FrameIntents>(); }
	bool stepped = true;
	for (int i = 0; i < frameCount && stepped; ++i)
	{
		std::memset(c.intents.get(), 0, sizeof(module::FrameIntents));
		stepped = guardModuleCode("on_update (candidate branch)", m_moduleHost.get(),
			"candidate discarded; the live game keeps running", [&] {
			m_moduleApi.on_update(c.memory.data(), inputs[i].effectiveDt, &inputs[i], c.intents.get());
		});
	}
	if (stepped && !m_sideState.empty())
	{
		std::string error;
		bool saved = false;
		stepped = guardModuleCode("side state save (candidate branch)", m_moduleHost.get(),
			"candidate discarded; the live game keeps running",
			[&] { saved = m_sideState.capture(c.memory.data(), true, c.side, &error); }) && saved;
	}
	(void)restoreModuleSideImage(m_sideLiveScratch.data(), m_sideLiveScratch.size(), "候補の後始末");
	if (!stepped) { c.active = false; return "{}"; }
	c.active = true;

	const nlohmann::json state = observe::reflectToJson(c.memory.data(), m_moduleMemorySize,
		m_moduleReflection.fieldsData(), m_moduleReflection.fieldCount(),
		m_moduleReflection.schemasData(), m_moduleReflection.schemaCount());
	return state.dump();
}

MITIRU_INLINE void mitiru::Engine::clearCandidateBranch(std::size_t slot) noexcept
{
	if (slot >= kMaxCandidateBranches) { return; }
	m_candidateBranches[slot].active = false;
}

MITIRU_INLINE bool mitiru::Engine::hasCandidateBranch(std::size_t slot) const noexcept
{
	return slot < kMaxCandidateBranches && m_candidateBranches[slot].active;
}

MITIRU_INLINE void mitiru::Engine::drawCandidateBranches(Screen& screen, float alpha) noexcept
{
	// on_draw (Screen 直描画) / on_draw_commands (Canvas/DrawCommandBuffer 経路、beko_run 含む)
	// のどちらか一方があれば候補ゴーストを描ける (docs/BRANCH_EDITOR.md「既知の制約」解消、O4 残り)。
	if (m_moduleApi.on_draw == nullptr && m_moduleApi.on_draw_commands == nullptr) { return; }

	// 色相の近似 (debug 専用の固定 4 色パレット。HSV 変換はしない): slot ごとに RGB 乗率を
	// 変え、drawGhost と同じ blitAlphaBlended で 1 案ずつ live へ重ねる。
	static constexpr float kTint[kMaxCandidateBranches][3] = {
		{1.0f, 0.45f, 0.45f},  // slot 0: 赤系
		{0.45f, 1.0f, 0.45f},  // slot 1: 緑系
		{0.45f, 0.6f, 1.0f},   // slot 2: 青系
		{1.0f, 1.0f, 0.4f},    // slot 3: 黄系
	};
	const auto a = static_cast<std::uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);

	// draw が窓口の状態 (物理の world 等) を読む game のため、候補ごとにその候補の窓口へ差し替えて描き、
	// 描き終えたら live へ戻す。
	bool sideSwapped = false;
	const auto swapInSide = [&](const CandidateBranch& c) {
		if (m_sideState.empty() || c.side.empty()) { return; }
		if (!sideSwapped && !captureModuleSideState(m_sideLiveScratch, true)) { return; }
		sideSwapped = true;
		(void)restoreModuleSideImage(c.side.data(), c.side.size(), "候補の描画");
	};
	struct RestoreLiveSide
	{
		Engine* e; bool& swapped;
		~RestoreLiveSide()
		{
			if (swapped) { (void)e->restoreModuleSideImage(e->m_sideLiveScratch.data(), e->m_sideLiveScratch.size(), "候補の描画の後始末"); }
		}
	} restoreLiveSide{this, sideSwapped};

	for (std::size_t slot = 0; slot < kMaxCandidateBranches; ++slot)
	{
		CandidateBranch& c = m_candidateBranches[slot];
		if (!c.active || c.memory.empty()) { continue; }
		swapInSide(c);

		if (!c.renderScreen || c.renderScreen->width() != screen.width()
			|| c.renderScreen->height() != screen.height())
		{
			c.renderScreen = std::make_unique<Screen>(screen.width(), screen.height());
			c.renderScreen->enableSoftwareFramebuffer();
		}
		Screen& gs = *c.renderScreen;
		gs.resetDrawCallCount();
		gs.clear(sgc::Colorf{0.0f, 0.0f, 0.0f, 0.0f});
		if (m_moduleApi.on_draw_commands != nullptr)
		{
			// Canvas 経路 (beko_run 等): live の ModuleAdapter::draw と同じ手順で
			// DrawCommandBuffer を出させ、既存の drainDrawCommands (Engine_Module_Adapter.hpp)
			// で候補専用の Screen へ再生する。tint/alpha は下の pixel post-process (drawGhost
			// と同型) で一括適用するので、ここでは色を変えずにそのまま焼く。
			module::DrawContext ctx{};
			if (m_moduleInputSnapshot)
			{
				ctx.logicalW = m_moduleInputSnapshot->logicalW;
				ctx.logicalH = m_moduleInputSnapshot->logicalH;
			}
			static thread_local module::DrawCommandBuffer buf;
			buf.count = 0;
			buf.droppedCount = 0;
			buf.textPoolUsed  = 0;
			buf.pointPoolUsed = 0;
			try
			{
				if (!guardModuleCallback("on_draw_commands", [&] {
					m_moduleApi.on_draw_commands(c.memory.data(), &ctx, &buf); })) { return; }
			}
			catch (...) { continue; }
			module::detail::drainDrawCommands(gs, buf, m_spriteCache, nullptr);
		}
		else
		{
			try
			{
				if (!guardModuleCallback("on_draw", [&] { m_moduleApi.on_draw(c.memory.data(), &gs); })) { return; }
			}
			catch (...) { continue; }
		}
		gs.present();

		c.compositeBuffer = gs.pixels();
		const auto& tint = kTint[slot];
		for (std::size_t i = 0; i + 3 < c.compositeBuffer.size(); i += 4)
		{
			if (c.compositeBuffer[i + 3] == 0) { continue; }  // 未描画画素はそのまま (0 を足さない)
			c.compositeBuffer[i + 0] = static_cast<std::uint8_t>(c.compositeBuffer[i + 0] * tint[0]);
			c.compositeBuffer[i + 1] = static_cast<std::uint8_t>(c.compositeBuffer[i + 1] * tint[1]);
			c.compositeBuffer[i + 2] = static_cast<std::uint8_t>(c.compositeBuffer[i + 2] * tint[2]);
			c.compositeBuffer[i + 3] = a;
		}

		screen.blitAlphaBlended(
			sgc::Rectf{0.0f, 0.0f,
				static_cast<float>(screen.width()), static_cast<float>(screen.height())},
			c.compositeBuffer.data(), gs.width(), gs.height());
	}
}
