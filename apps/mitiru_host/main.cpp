// mitiru_host。game-as-DLL モジュール用の最小エンジンランチャ (v0.2.0 step 3-4)
//
// Argv:
//   argv[0]  = host exe のパス
//   argv[1]  = game DLL のパス (cwd 相対または絶対)
//   argv[2+] = 任意フラグ:
//                --watch         DLL ファイルの mtime を監視し変更時にリロード
//                --url <url>     CEF 起動 URL を上書き (既定: file:///./<dll_dir>/assets/scene.html)
//
// エンジンを直接見るのは host のみ。game コードは DLL 内に閉じ、
// ModuleApi.hpp の C-only シグナルフロー経由でエンジンと通信する。
//
// `--watch` は L3 ホットリロード: mtime を約 250ms ごとに監視し、変化したら
// HelloGameMemory* を生かしたまま DLL を差し替える (状態を保持)。
//
// Runtime hotkeys (Windows): F7 = step, F8 = pause/play, F9 = time-scale,
// F10 = lo-fi toggle, F12 = screenshot (file + clipboard).
//
// 将来: `mitiru-cli run [--watch]` が project/module.toml を解決して
// 等価な argv を自動構築する。

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// アンブレラ廃止 (リファクタ P2)。使うものだけ明示 include
#include <mitiru/platform/Utf8Args.hpp>
#include <mitiru/core/Engine.hpp>
#include <mitiru/module/Spawner.hpp>
#include <mitiru/debug/FrameBudget.hpp>  // FrameArena (2-1) の使用量を毎フレーム反映する
#include <mitiru/cef/CefErrorPage.hpp>  // scene.html 不在時に自前エラーページ data URI を直接開く (CEF 未使用なら空ヘッダ)
#include <mitiru/resource/AssetPath.hpp>
#include <mitiru/core/Game.hpp>
#include <mitiru/core/Config.hpp>
#include <mitiru/asset/AssetPack.hpp> // vfs: pack mount / readGlobal
#include <mitiru/audio/AudioEngine.hpp>
#include <mitiru/audio/MiniaudioEngine.hpp>
#include <mitiru/debug/InspectorLauncher.hpp>
#include <mitiru/observe/ScrubControlChannel.hpp>  // time-travel click-to-scrub
#include <mitiru/observe/DockChannel.hpp>          // 自窓矩形の broadcast (ツール窓のドッキング追従)
#include <mitiru/render/SaveScreenshotPng.hpp>
#include <mitiru/render/AsyncPngWriter.hpp>
#include <mitiru/replay/Player.hpp>
#include <mitiru/replay/Recorder.hpp>
#include <mitiru/module/ModuleHost.hpp>  // --bake: DLL の mitiru_module_bake_assets export を呼ぶだけの経路

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shellapi.h>  // ShellExecuteA (--console で既定ブラウザ起動)
#endif

#include <nlohmann/json.hpp>

namespace
{

// FileAudioEngine。論理 sound id を game の assets/audio/ 配下のファイルに解決し
// miniaudio で再生する host 側 IAudioEngine。game は触れず SoundIntents を
// 書くだけ。未知の id は無言で失敗させず stderr に出す。
class FileAudioEngine final : public mitiru::audio::IAudioEngine
{
public:
	explicit FileAudioEngine(std::filesystem::path baseDir)
		: m_baseDir(std::move(baseDir)) {}

	void playSound(std::string_view id) override { playByIdEx(id, 1.0f, 1.0f, 0.0f, /*music=*/false, false); }
	void playSound(std::string_view id, float vol) override { playByIdEx(id, vol, 1.0f, 0.0f, false, false); }
	void stopSound(std::string_view id) override { stopSoundFade(id, 0.0f); }
	void playMusic(std::string_view id) override { playByIdEx(id, 1.0f, 1.0f, 0.0f, /*music=*/true, true); }
	void playMusic(std::string_view id, float vol, bool loop) override { playByIdEx(id, vol, 1.0f, 0.0f, true, loop); }
	void stopMusic() override { m_engine.stopMusic(); }
	void setVolume(float v) override { m_engine.setMasterVolume(v); }
	[[nodiscard]] bool isPlaying(std::string_view) const override { return false; }

	// v6 拡張 (#19/#20): pitch / fade を route。
	void playSoundEx(std::string_view id, float vol, float pitch, float fadeIn) override
	{
		playByIdEx(id, vol, pitch, fadeIn, /*music=*/false, false);
	}
	void stopSoundFade(std::string_view id, float fadeOutSec) override
	{
		// 止められるのは playSoundLoop で鳴らしたものだけ。one-shot は id で覚えていない。
		const auto it = m_loopPaths.find(std::string(id));
		if (it == m_loopPaths.end()) { return; }
		m_engine.stopSoundLoop(it->second, fadeOutSec);
		m_loopPaths.erase(it);
	}
	void playSoundLoop(std::string_view id, float vol, float pitch, float fadeIn) override
	{
		playByIdEx(id, vol, pitch, fadeIn, /*music=*/false, /*loop=*/true);
	}
	void playMusicEx(std::string_view id, float vol, bool loop, float fadeIn) override
	{
		playByIdEx(id, vol, 1.0f, fadeIn, /*music=*/true, loop);
	}
	void stopMusicFade(float fadeOutSec) override { m_engine.stopMusicFade(fadeOutSec); }

	// 毎フレームの定期掃除 (終了 SE voice 回収 + fade-out 完了 music の解放、#51)。
	void update() override { m_engine.update(); }

	// 再生中 voice のメーターを miniaudio backend からそのまま中継 (mitiru_mixer 窓用)。
	[[nodiscard]] std::vector<mitiru::audio::ChannelMeter> meterChannels() const override
	{
		std::vector<mitiru::audio::ChannelMeter> out = m_engine.meterChannels();
		for (auto& m : out)
		{
			// backend は path しか知らない。game が渡した論理 id はここで足す (K1)。
			if (std::strcmp(m.kind, "voice") == 0)
			{
				mitiru::audio::ChannelMeter::copyTo(m.id, sizeof(m.id), m_voiceId.c_str());
			}
		}
		return out;
	}

	// マスター再生クロック (秒) を miniaudio から中継。game が音声クロック基準で判定するため。
	[[nodiscard]] double masterTimeSec() const noexcept override { return m_engine.masterTimeSec(); }

	// v19: 出力レイテンシ / BGM transport / サンプル精度予約を miniaudio backend へ中継。
	[[nodiscard]] double outputLatencySec() const noexcept override { return m_engine.outputLatencySec(); }
	void pauseMusic() override { m_engine.pauseMusic(); }
	void resumeMusic() override { m_engine.resumeMusic(); }
	void seekMusic(double positionSec) override { m_engine.seekMusic(positionSec); }
	void playSoundScheduled(std::string_view id, double atSec, float vol, float pitch) override
	{
		playByIdEx(id, vol, pitch, 0.0f, /*music=*/false, false, atSec);
	}

	// K1: Voice (category=2) は BGM/SE と独立した 1 本のスロット。id を覚えておき、
	// meterChannels の "voice" 行に game が渡した名前を載せる (mixer 窓の voice 一覧)。
	void playVoiceEx(std::string_view id, float vol, float pitch, float fadeIn) override
	{
		const std::string playPath = resolvePlayPath(id);
		if (playPath.empty()) { reportMissing(id); return; }
		m_voiceId = std::string(id);
		m_engine.playVoiceEx(playPath, vol, pitch, fadeIn);
	}
	void stopVoiceFade(float fadeOutSec) override { m_engine.stopVoice(fadeOutSec); }

private:
	/// 論理 id を再生に渡す実ファイルパスへ解決する。pack mount 時はパックから取り出した
	/// temp ファイル、dev (未 mount) 時は disk のファイル。見つからなければ空。
	std::string resolvePlayPath(std::string_view id)
	{
		for (const char* ext : {".wav", ".ogg", ".mp3"})
		{
			const auto        p   = m_baseDir / (std::string(id) + ext);
			const std::string key = p.generic_string();
			if (mitiru::vfs::hasGlobalMount())
			{
				const std::string packed = materializeFromPack(key, ext);
				if (!packed.empty()) { return packed; }
			}
			else if (std::filesystem::exists(p))
			{
				return p.string();
			}
		}
		return {};
	}

	void reportMissing(std::string_view id) const
	{
		std::fprintf(stderr, "[mitiru_host] sound id not found under %s: %.*s\n",
		             m_baseDir.string().c_str(),
		             static_cast<int>(id.size()), id.data());
	}

	void playByIdEx(std::string_view id, float volume, float pitch, float fadeIn,
	                bool music, bool loop, double scheduleSec = 0.0)
	{
		const std::string playPath = resolvePlayPath(id);
		if (playPath.empty()) { reportMissing(id); return; }

		if (music) { m_engine.playMusicEx(playPath, volume, loop, fadeIn); }
		else if (loop)
		{
			// 止めるときに id から実ファイルを引けるよう覚えておく。
			m_loopPaths[std::string(id)] = playPath;
			m_engine.playSoundLoop(playPath, volume, pitch, fadeIn);
		}
		else if (scheduleSec > 0.0)
		{
			// v19: サンプル精度予約 (リズムゲームの「次の拍で鳴らす」)。ducking は予約発火を
			// 先取りできないので付けない (即時 SE 用)。
			m_engine.playSoundScheduled(playPath, scheduleSec, volume, pitch);
		}
		else
		{
			m_engine.playSoundEx(playPath, volume, pitch, fadeIn);
			// #34 BGM ducking heuristic: 閾値超の大音量 SE で BGM を一瞬引っ込める。
			if (volume >= kDuckSeThreshold)
			{
				m_engine.duckMusic(kDuckMul, kDuckSec);
			}
		}
	}

	/// pack 中の音声 (key) を %TEMP% に一度だけ取り出し、その path を返す。
	/// 配布物にバラ音声を置かないための経路。pack に無ければ空。
	std::string materializeFromPack(const std::string& key, const char* ext)
	{
		if (auto it = m_audioTemp.find(key); it != m_audioTemp.end()) { return it->second; }
		const auto bytes = mitiru::vfs::readGlobal(key);
		if (!bytes) { return {}; }
		// key + pack 実体 (size/mtime) の FNV-1a で temp 名を作る。同一 pack なら
		// run 跨ぎで再利用、pack 差し替え時は名前が変わり旧バイト再生を根治する。
		std::uint64_t h = 14695981039346656037ULL;
		for (unsigned char c : key) { h = (h ^ c) * 1099511628211ULL; }
		const std::uint64_t stamp = packStamp();
		for (int i = 0; i < 8; ++i)
		{
			h = (h ^ ((stamp >> (i * 8)) & 0xFFu)) * 1099511628211ULL;
		}
		char name[64];
		std::snprintf(name, sizeof(name), "mitiru_aud_%016llx%s",
		              static_cast<unsigned long long>(h), ext);
		const auto out = std::filesystem::temp_directory_path() / name;
		std::error_code ec;
		if (!std::filesystem::exists(out, ec))
		{
			std::ofstream f(out, std::ios::binary | std::ios::trunc);
			if (!f) { return {}; }
			if (!bytes->empty())
			{
				f.write(reinterpret_cast<const char*>(bytes->data()),
				        static_cast<std::streamsize>(bytes->size()));
			}
		}
		const std::string s = out.string();
		m_audioTemp.emplace(key, s);
		return s;
	}

	/// pack ファイル (MITIRU_PACK。host が env を統一する前の名残で MITIRU_ASSET_PACK も
	/// 後方互換で見る) の size + mtime から作る指紋。temp 名に混ぜ、assets.mtpak 差し替え後に
	/// 旧 temp を掴まないようにする。初回のみ stat。
	std::uint64_t packStamp()
	{
		if (m_packStampInit) { return m_packStamp; }
		m_packStampInit = true;
		const char* packPath = std::getenv("MITIRU_PACK");
		if (packPath == nullptr || packPath[0] == '\0') { packPath = std::getenv("MITIRU_ASSET_PACK"); }
		if (packPath != nullptr && packPath[0] != '\0')
		{
			std::error_code ec;
			const std::filesystem::path p(packPath);
			if (const auto size = std::filesystem::file_size(p, ec); !ec)
			{
				m_packStamp = static_cast<std::uint64_t>(size) * 1099511628211ULL;
			}
			if (const auto mtime = std::filesystem::last_write_time(p, ec); !ec)
			{
				m_packStamp ^= static_cast<std::uint64_t>(mtime.time_since_epoch().count());
			}
		}
		return m_packStamp;
	}

	// #34 ducking パラメータ。閾値以上の SE 音量で BGM を mul 倍にし、sec で復帰。
	static constexpr float kDuckSeThreshold = 0.7f;
	static constexpr float kDuckMul         = 0.5f;
	static constexpr float kDuckSec         = 0.4f;

	std::filesystem::path                        m_baseDir;
	std::unordered_map<std::string, std::string> m_loopPaths;  ///< ループ再生中の id → 実ファイル
	std::string m_voiceId;  ///< 直近に鳴らした voice の論理 id (meterChannels 用、K1)
	mitiru::audio::MiniaudioEngine               m_engine;
	std::unordered_map<std::string, std::string> m_audioTemp;  ///< pack→temp 取り出しキャッシュ
	std::uint64_t m_packStamp     = 0;      ///< pack 指紋 (size/mtime FNV 混合)
	bool          m_packStampInit = false;  ///< packStamp 計算済みか
};

}  // namespace

namespace
{

/// プロセスの cwd を argv[0] のディレクトリに固定する。EngineConfig 内の相対パス
/// (cefStartUrl 等) を、どのシェルから起動しても解決できるようにするため。
void anchorCwdToExeDir(const char* argv0)
{
	if (argv0 == nullptr) { return; }
	std::error_code ec;
	const auto canon = std::filesystem::weakly_canonical(
		std::filesystem::path(argv0), ec);
	if (ec) { return; }
	std::filesystem::current_path(canon.parent_path(), ec);
}

/// exe と同じ場所の <exeStem>.mtargs があれば、その中身を argv[1..] 相当の
/// token 列として読む (引数なし起動 = ダブルクリック / Steam 用)。空白区切りだが
/// "..." で囲まれた部分は空白ごと 1 token になる (--title "My Game" 等)。
/// 先頭 token は通常 game DLL の相対パス。`mitiru dist` が生成する。
std::vector<std::string> readSidecarArgs(const char* argv0)
{
	std::vector<std::string> tokens;
	if (argv0 == nullptr) { return tokens; }
	std::error_code ec;
	const auto exe = std::filesystem::absolute(std::filesystem::path(argv0), ec);
	if (ec) { return tokens; }
	const auto side = exe.parent_path() / (exe.stem().string() + ".mtargs");
	std::ifstream f(side);
	if (!f) { return tokens; }
	// 簡易 quote lexer (CRT の command line 解釈と揃える。backslash-escape は非対応)。
	std::string cur;
	bool inQuote  = false;
	bool sawToken = false;   // 空 quote ("") も 1 token として残す
	char c = 0;
	while (f.get(c))
	{
		if (c == '"') { inQuote = !inQuote; sawToken = true; continue; }
		if (!inQuote && (c == ' ' || c == '\t' || c == '\r' || c == '\n'))
		{
			if (sawToken || !cur.empty()) { tokens.push_back(cur); cur.clear(); sawToken = false; }
			continue;
		}
		cur.push_back(c);
		sawToken = true;
	}
	if (sawToken || !cur.empty()) { tokens.push_back(cur); }
	return tokens;
}

#ifdef _WIN32
/// AppUserModelID を設定する (--appid)。taskbar のグループ化/ピン留めが exe パス
/// でなくこの id 単位になる。shell32 から動的に引き、無い環境では黙って継続。
void applyAppUserModelId(const std::string& id)
{
	const int wideLen = MultiByteToWideChar(CP_UTF8, 0, id.c_str(), -1, nullptr, 0);
	if (wideLen <= 0) { return; }
	std::wstring wide(static_cast<std::size_t>(wideLen), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, id.c_str(), -1, wide.data(), wideLen);
	using SetAumidFn = HRESULT(WINAPI*)(PCWSTR);
	if (HMODULE shell32 = LoadLibraryW(L"shell32.dll"))
	{
		if (auto fn = reinterpret_cast<SetAumidFn>(
				GetProcAddress(shell32, "SetCurrentProcessExplicitAppUserModelID")))
		{
			fn(wide.c_str());
		}
	}
}
#endif

/// DLL パスから "file:///./<dll_dir>/assets/scene.html" を組み立てる。
/// scene.html を DLL の隣に置いた game がそのまま動くようにするため。
std::string defaultCefUrlFor(const std::filesystem::path& dllPath)
{
	std::error_code ec;
	const auto rel = std::filesystem::relative(dllPath.parent_path(),
	                                            std::filesystem::current_path(), ec);
	std::filesystem::path under = (ec || rel.empty()) ? dllPath.parent_path() : rel;
	auto asset = under / "assets" / "scene.html";
	// 不在は従来完全沈黙だった (HUD だけ出ない画面になり原因が追えない)。起動時に 1 行だけ知らせる。
	if (!std::filesystem::exists(dllPath.parent_path() / "assets" / "scene.html", ec))
	{
		std::fprintf(stderr,
			"[mitiru_host] note: HUD 用の scene.html が見つかりません: %s\n"
			"  (HUD 無しでゲーム本体は動きます。HTML/CSS の HUD を使う場合はこの場所に置いてください)\n",
			(dllPath.parent_path() / "assets" / "scene.html").string().c_str());
#if defined(_WIN32) && defined(MITIRU_HAS_CEF)
		// 不在が確定しているので file:// を読みに行かない。native 描画だけの game
		// (章 example 等) でエラーページが画面を覆うのは「必要なものしか画面に
		// 出さない」に反するため、透明な空ページを開く。診断は上の stderr 1 行が担う。
		return "data:text/html,%3Cbody%20style%3D%22margin:0;background:transparent%22%3E%3C/body%3E";
#endif
	}
	std::string url = "file:///./";
	url += asset.generic_string();
	return url;
}

struct CliArgs
{
	std::filesystem::path dllPath;
	std::string           cefUrlOverride;
	std::string           title;               // --title <name>: window title (空=既定 DLL 名の stem)
	std::string           iconPath;            // --icon <f.ico>: window icon (空=既定 icon)
	std::string           appId;               // --appid <id>: AppUserModelID (taskbar 分離、空=設定しない)
	bool                  watch = false;
	std::string           watchAssetsDir;     // --watch-assets <dir>: 配下の .json/.baked の mtime を見て asset.reloaded を game へ通知
	bool                  configOrigins = false; // --config-origins: 設定値の由来を表で出して終了 (§3-3)
	bool                  helpRequested = false;
	bool                  helpAllRequested = false; // --help-all: printUsage() の全量版 (--help は既定 20 行に絞る, E8)
	int                   widthOverride  = 0;  // 0 = 既定 1280
	int                   heightOverride = 0;  // 0 = 既定 720
	int                   winPosX = (-2147483647 - 1);  // --window-pos X Y: 窓の初期座標 (既定=CW_USEDEFAULT)
	int                   winPosY = (-2147483647 - 1);  // 実画面に一瞬も出さず最初から指定位置へ (録画支援)
	int                   toolWinX = (-2147483647 - 1); // --tool-window-pos X Y: spawn する tool 窓の初期座標
	int                   toolWinY = (-2147483647 - 1);
	std::string           recordPath;          // --record <f>: .mtrr を書き出す
	std::string           replayPath;          // --replay-test/--replay <f>: 再実行 (ヘッドレス/GUI)
	bool                  replayGui = false;   // true なら --replay (window あり、verdict 無し、EOF で自動終了しない)
	std::string           ghostPath;           // --ghost <f>: 同じ DLL のもう1本の GameMemory に並走投入し、live の直前に半透明で描く (10-1)
	std::string           expectPath;          // --expect <f>: 最終 view.* 状態を検証
	std::string           stateDiffA;          // --state-diff <a> <b>: 2 録画の分岐 frame を報告
	std::string           stateDiffB;
	std::string           bakeInJson;          // --bake <in.json> <out.baked>: 配置 JSON を POD へ焼く (★4-1)
	std::string           bakeOutPath;
	std::string           parseError;         // 引数の形が壊れている時の説明 (空なら正常)
	bool                  backendSet = false; // --backend が指定された (既定値と同じ値でも由来は cli)
	bool                  speedSet   = false; // --speed が指定された
	std::string           fontMode;            // --font none|latin|kana|japanese (空=既定=かな)
	std::string           fontFace;            // --font-face normal|retro (空=normal=M+ Rounded)
	bool                  loFi = false;        // --lofi: 低解像+量子化+Bayerディザ
	int                   loFiW = 320, loFiH = 240; // --lofi-size WxH
	int                   loFiBitsR = 5, loFiBitsG = 6, loFiBitsB = 5; // --lofi-bits R,G,B (既定 RGB565)
	float                 loFiDither = 1.0f;   // --lofi-dither S
	bool                  loFiHard = false;    // --lofi-hard: 柔らか拡大を切りニアレストにする
	bool                  loFiVi = false;      // --lofi-vi: 映像出力段の de-dither + divot
	float                 loFiGamma = 1.0f;    // --lofi-gamma: 出力ガンマ (1 で素通し)
	int                   httpPort = 0;        // --http-port <N>: EngineHttpServer を listen 開始
	int                   cefDebugPort = 0;    // --cef-debug-port <N>: CEF remote debugging を開く (chrome-devtools / CDP で実機テスト)
	bool                  console  = false;    // --console: HTTP + default browser で console.html 自動表示
	bool                  noCef    = false;    // --no-cef: CEF を起動しない (完全ネイティブ描画の game 用、起動軽量化)
	std::string           captureDir;          // --capture-dir <d>: 毎 N フレーム PNG を吐く先 (#43)
	int                   captureEvery = 0;    // --capture-every <N>: N フレームごとに 1 枚 (0=off)
	bool                  headless = false;    // --headless: ウィンドウ無しで走らせる (AI 自動回し)
	bool                  headlessGpu3D = false; // --headless-3d: headless でも実 GPU で 3D を描く (G2、--capture-dir と併用)
	mitiru::gfx::Backend  backend = mitiru::gfx::Backend::Auto;  // --backend <name>: 明示バックエンド指定
	std::string           replayGateDir;       // --replay-gate-dir <d>: O5 決定論ゲートの .mtrr 置き場
	bool                  oracleDeterminism = false;         // --oracle-determinism [N]: P14 決定論オラクル opt-in
	std::uint32_t         oracleDeterminismEveryFrames = 0;  // N (省略時 0 = engine 既定 120)
	bool                  oracleLog = false;   // --oracle-log: N1 の機械可読 "[oracle] ..." 行を stderr へ追加出力
	bool                  synctest = false;    // --synctest [K]: GGPO SyncTest 相当。毎フレーム K フレーム前から再シミュレーション
	std::uint32_t         synctestFrames = 1;  // K (省略時 1)
	bool                  saveRoundtripTest = false; // --save-roundtrip-test: save→load→save が bit 一致するか検査
	float                 speed    = 1.0f;     // --speed <N>: time scale 倍率 (固定 dt × N で早回し)
	int                   maxFrames = 0;       // --max-frames <N>: N フレーム走ったら自動終了 (0=無制限)
	int                   rewindFrames = 0;    // --rewind-frames <N>: 巻き戻せるフレーム数 (0=既定 300)
	int                   rewindMb = -1;       // --rewind-mb <N>: 巻き戻しリングの予算 (MB、0=無制限=非圧縮)。
	                                           // -1=未指定 (ゲームの MITIRU_REWIND_BUDGET 宣言 > 既定 512MB)
	int                   recordStateEvery = 60; // --record-state-every <N>: --record で state blob を書く間隔 (0=毎フレーム)
	bool                  fixedSize = false;   // --fixed-size: ユーザのウィンドウリサイズを禁止 (#44)
	bool                  noPauseUnfocused = false; // --no-pause-unfocused: 非フォーカスでもフルレート継続 (vsync off)
	bool                  noVsync = false;     // --no-vsync: present の vsync 待ちを切る (素のフレームコスト計測, #53)
	bool                  perf    = false;     // --perf: 実フレーム時間の統計を定期表示 (#53)
	std::string           inputScript;         // --input-script <f>: in-process 入力注入 (#43-1)
	std::string           stateTrace;          // --state-trace <f>: 毎フレーム reflect 状態を JSONL 出力 (offset read = non-POD でも安全)
	std::string           inputRecordPath;     // --input-record <f>: 実入力を input-script 形式で録画 (#45)
	std::vector<mitiru::Tool> openTools;       // --inspect <name>: 起動時に開くツール独立窓
	std::vector<std::string>  openToolArgs;    // openTools と同じ並び。"scene?tab=memory" のクエリを --page ごと渡す (無ければ空)
	std::string           errorFile;           // --error-file <f>: mitiru watch のビルドエラー帯 (存在中だけ表示)
	std::string           pauseControl;        // --pause-control <f>: ファイルが "1" の間だけ pause (録画支援, フォーカス不要)
	std::string           inputFreezeControl;  // --input-freeze-control <f>: "1" の間だけ入力を無効化 (プレイヤー静止・世界は進行, 録画支援)
	bool                  noToolWindows = false; // --no-tool-windows: hud.open 等のツール窓 spawn を全無効 (録画/CI でメイン画面への割込防止)
	bool                  jsonOutput = false;  // --json: --replay-test の verdict を stdout に 1 行 JSON でも出す (CLI 側の正規表現パースを置換)
	bool                  bugRingSave = false; // --bug-ring-save: 起動直後に「昨日のバグ」リングを即保存 (P1、通常は F11 ホットキー)
	std::string           packOverride;        // --pack <file.mtpak>: 自動探索 (assets.mtpak) より優先する明示パック (P12)
	std::string           collisionPath;       // --collision <boxes.json>: 物理問い合わせ job (v37) が答える静的な箱の列
	std::string           unknownOption;       // 未知の --option (非空 = 起動拒否。typo / 廃止 flag を黙殺しない)
};

CliArgs parseArgs(int argc, char* argv[])
{
	CliArgs out;
	if (argc < 2) { out.helpRequested = true; return out; }

	for (int i = 1; i < argc; ++i)
	{
		std::string_view a{argv[i]};
		if (a == "--help" || a == "-h")
		{
			out.helpRequested = true;
		}
		else if (a == "--help-all")
		{
			out.helpRequested = true;
			out.helpAllRequested = true;
		}
		else if (a == "--watch")
		{
			out.watch = true;
		}
		else if (a == "--watch-assets")
		{
			if (i + 1 < argc) { out.watchAssetsDir = argv[++i]; }
			else { out.parseError = "--watch-assets には <dir> が必要です"; }
		}
		else if (a == "--config-origins")
		{
			// §3-3: 「この設定値はどこで決まったか」を表で出して即終了する診断フラグ。
			out.configOrigins = true;
		}
		else if (a == "--url")
		{
			if (i + 1 < argc) { out.cefUrlOverride = argv[++i]; }
		}
		else if (a == "--title")
		{
			if (i + 1 < argc) { out.title = argv[++i]; }
		}
		else if (a == "--icon")
		{
			if (i + 1 < argc) { out.iconPath = argv[++i]; }
		}
		else if (a == "--appid")
		{
			if (i + 1 < argc) { out.appId = argv[++i]; }
		}
		else if (a == "--record")
		{
			if (i + 1 < argc) { out.recordPath = argv[++i]; }
		}
		else if (a == "--replay-test")
		{
			if (i + 1 < argc) { out.replayPath = argv[++i]; out.replayGui = false; }
		}
		else if (a == "--replay")
		{
			// GUI 再生: --replay-test と同じ決定的再実行だが window あり・ツール窓も開ける・
			// --expect 判定なし・EOF で自動終了せず最後のフレームで止まる (デモ撮影用)。
			if (i + 1 < argc) { out.replayPath = argv[++i]; out.replayGui = true; }
		}
		else if (a == "--json")
		{
			out.jsonOutput = true;
		}
		else if (a == "--ghost")
		{
			if (i + 1 < argc) { out.ghostPath = argv[++i]; }
		}
		else if (a == "--expect")
		{
			if (i + 1 < argc) { out.expectPath = argv[++i]; }
		}
		else if (a == "--state-diff")
		{
			if (i + 2 < argc) { out.stateDiffA = argv[++i]; out.stateDiffB = argv[++i]; }
		}
		else if (a == "--bake")
		{
			if (i + 2 < argc) { out.bakeInJson = argv[++i]; out.bakeOutPath = argv[++i]; }
			else { out.parseError = "--bake には <in.json> <out.baked> の 2 つが必要です"; }
		}
		else if (a == "--font")
		{
			if (i + 1 < argc) { out.fontMode = argv[++i]; }
		}
		else if (a == "--font-face")
		{
			if (i + 1 < argc) { out.fontFace = argv[++i]; }
		}
		else if (a == "--http-port")
		{
			if (i + 1 < argc)
			{
				try { out.httpPort = std::stoi(argv[++i]); }
				catch (...) { out.httpPort = 0; }
			}
		}
		else if (a == "--cef-debug-port")
		{
			if (i + 1 < argc)
			{
				try { out.cefDebugPort = std::stoi(argv[++i]); }
				catch (...) { out.cefDebugPort = 0; }
			}
		}
		else if (a == "--console")
		{
			out.console = true;
		}
		else if (a == "--no-cef")
		{
			// 完全ネイティブ描画の game (HTML UI を使わない) は CEF を起動しないことで
			// Chromium コールドブートの起動スパイク + GPU/renderer サブプロセス常駐を避ける。
			out.noCef = true;
		}
		else if (a == "--capture-dir")
		{
			if (i + 1 < argc) { out.captureDir = argv[++i]; }
		}
		else if (a == "--capture-every")
		{
			if (i + 1 < argc) { try { out.captureEvery = std::stoi(argv[++i]); } catch (...) {} }
		}
		else if (a == "--headless")
		{
			out.headless = true;
		}
		else if (a == "--headless-3d")
		{
			out.headless = true;
			out.headlessGpu3D = true;
		}
		else if (a == "--backend")
		{
			out.backendSet = true;
			// --backend <auto|dx11|dx12>: --headless-3d の windowless 3D 経路 (G2、
			// gfx::createWindowlessDevice3D) が Dx11 固定になっていたのを、明示指定できるように
			// する。Vulkan/OpenGL/WebGL/WebGPU は windowless 3D 未対応 (GfxFactory.hpp 参照) の
			// ため受け付けない (綴り違い・未対応名は auto のまま、その旨を stderr に出す)。
			if (i + 1 < argc)
			{
				const std::string name{argv[++i]};
				if      (name == "auto") { out.backend = mitiru::gfx::Backend::Auto; }
				else if (name == "dx11") { out.backend = mitiru::gfx::Backend::Dx11; }
				else if (name == "dx12") { out.backend = mitiru::gfx::Backend::Dx12; }
				else
				{
					std::fprintf(stderr,
					             "[mitiru_host] --backend %s は不明です。auto のまま続行します "
					             "(auto|dx11|dx12)\n", name.c_str());
				}
			}
		}
		else if (a == "--no-pause-unfocused")
		{
			out.noPauseUnfocused = true;
		}
		else if (a == "--no-vsync")
		{
			out.noVsync = true;
		}
		else if (a == "--perf")
		{
			out.perf = true;
		}
		else if (a == "--speed")
		{
			if (i + 1 < argc) { try { out.speed = std::stof(argv[++i]); out.speedSet = true; } catch (...) {} }
		}
		else if (a == "--max-frames")
		{
			if (i + 1 < argc) { try { out.maxFrames = std::stoi(argv[++i]); } catch (...) {} }
		}
		else if (a == "--fixed-size")
		{
			out.fixedSize = true;
		}
		else if (a == "--rewind-frames")
		{
			if (i + 1 < argc) { try { out.rewindFrames = std::stoi(argv[++i]); } catch (...) {} }
		}
		else if (a == "--rewind-mb")
		{
			if (i + 1 < argc) { try { out.rewindMb = std::stoi(argv[++i]); } catch (...) {} }
		}
		else if (a == "--oracle-determinism")
		{
			// --oracle-determinism [everyFrames]: P14 の決定論オラクル (resim ring + memcmp) を
			// opt-in する。N 省略時は EngineConfig::oracleDeterminismEveryFrames=0 のまま
			// (engine 側が既定 120 フレーム間隔を使う)。
			out.oracleDeterminism = true;
			if (i + 1 < argc && argv[i + 1][0] != '-')
			{
				try
				{
					const unsigned long v = std::stoul(argv[++i]);
					if (v > 0xFFFFFFFFul) { out.parseError = "--oracle-determinism の間隔が大きすぎます (uint32 の範囲)"; }
					else { out.oracleDeterminismEveryFrames = static_cast<std::uint32_t>(v); }
				}
				catch (...) { out.parseError = "--oracle-determinism の間隔が数値ではありません"; }
			}
		}
		else if (a == "--oracle-log")
		{
			out.oracleLog = true;
		}
		else if (a == "--synctest")
		{
			// GGPO SyncTest 相当 (Developer Guide の ggpo_start_synctest)。既存の決定論オラクル
			// (resim ring + memcmp) を K=1 (毎フレーム) で回すだけなので oracle-determinism の
			// 薄い別名として実装する。K 省略時は 1。
			out.synctest = true;
			if (i + 1 < argc && argv[i + 1][0] != '-')
			{
				try { out.synctestFrames = static_cast<std::uint32_t>(std::stoul(argv[++i])); }
				catch (...) {}
			}
		}
		else if (a == "--save-roundtrip-test")
		{
			out.saveRoundtripTest = true;
		}
		else if (a == "--record-state-every")
		{
			if (i + 1 < argc) { try { out.recordStateEvery = std::stoi(argv[++i]); } catch (...) {} }
		}
		else if (a == "--input-script")
		{
			if (i + 1 < argc) { out.inputScript = argv[++i]; }
		}
		else if (a == "--state-trace")
		{
			if (i + 1 < argc) { out.stateTrace = argv[++i]; }
		}
		else if (a == "--input-record")
		{
			if (i + 1 < argc) { out.inputRecordPath = argv[++i]; }
		}
		else if (a == "--error-file")
		{
			if (i + 1 < argc) { out.errorFile = argv[++i]; }
		}
		else if (a == "--pause-control")
		{
			if (i + 1 < argc) { out.pauseControl = argv[++i]; }
		}
		else if (a == "--input-freeze-control")
		{
			if (i + 1 < argc) { out.inputFreezeControl = argv[++i]; }
		}
		else if (a == "--no-tool-windows")
		{
			out.noToolWindows = true;
		}
		else if (a == "--bug-ring-save")
		{
			out.bugRingSave = true;
		}
		else if (a == "--pack")
		{
			if (i + 1 < argc) { out.packOverride = argv[++i]; }
		}
		else if (a == "--collision")
		{
			if (i + 1 < argc) { out.collisionPath = argv[++i]; }
			else { out.parseError = "--collision には <boxes.json> が必要です"; }
		}
		else if (a == "--replay-gate-dir")
		{
			// ADR 0035 O5: POST /api/ai/commit 直後に回す決定論ゲートの録画置き場
			// (既定 tests/replay_golden、Engine_Http.hpp::runReplayGate の env 名と一致)。
			if (i + 1 < argc) { out.replayGateDir = argv[++i]; }
		}
		else if (a == "--window-pos")
		{
			// --window-pos X Y: 窓を最初からこの座標に出す (負の X = 仮想ディスプレイ等)
			if (i + 2 < argc)
			{
				try { out.winPosX = std::stoi(argv[i + 1]); out.winPosY = std::stoi(argv[i + 2]); i += 2; }
				catch (...) {}
			}
		}
		else if (a == "--tool-window-pos")
		{
			// --tool-window-pos X Y: spawn する tool 窓 (CEF) もこの座標に出す (録画で実画面に出さない)
			if (i + 2 < argc)
			{
				try { out.toolWinX = std::stoi(argv[i + 1]); out.toolWinY = std::stoi(argv[i + 2]); i += 2; }
				catch (...) {}
			}
		}
		else if (a == "--inspect")
		{
			// --inspect [inspector|input|rewind] (省略時 inspector)。
			// host を書く人が「この窓を使う」と決めた物だけ開く。
			mitiru::Tool t = mitiru::Tool::Inspector;
			std::string extraArgs;
			if (i + 1 < argc && argv[i + 1][0] != '-')
			{
				const std::string full{argv[++i]};
				// "scene?tab=memory" の ? 以降はページへのクエリ。名前解決には使わず、
				// mitiru_tool_cef へ --page ごと渡して URL に付けさせる (mitiru run --learn)。
				const std::string name = full.substr(0, full.find('?'));
				if (name.size() < full.size()) { extraArgs = "--page " + full; }
				if      (name == "input")      { t = mitiru::Tool::InputMonitor; }
				else if (name == "rewind")     { t = mitiru::Tool::Rewind; }
				else if (name == "scene")      { t = mitiru::Tool::SceneTree; }
				else if (name == "perf")       { t = mitiru::Tool::Perf; }
				else if (name == "mixer")      { t = mitiru::Tool::AudioMixer; }
				else if (name == "scene_view") { t = mitiru::Tool::SceneView; }
				else if (name == "why_view")   { t = mitiru::Tool::WhyView; }
				else if (name == "frame_view") { t = mitiru::Tool::FrameView; }
				else if (name != "inspector")
				{
					// 綴り違いを黙って既定へ落とすと、頼んだ窓と別の窓が開いたまま
					// 気づけない。開くものは変えずに、その旨だけ伝える
					std::fprintf(stderr,
					             "[mitiru_host] --inspect %s は不明な名前です。"
					             "inspector を開きます (input|rewind|scene|perf|mixer|scene_view|why_view|frame_view)\n",
					             name.c_str());
				}
			}
			out.openTools.push_back(t);
			out.openToolArgs.push_back(extraArgs);
		}
		else if (a == "--size")
		{
			// 形式: WxH 例 "800x500"。両方とも正の整数であること。
			if (i + 1 < argc)
			{
				std::string s{argv[++i]};
				auto x = s.find('x');
				if (x == std::string::npos) { x = s.find('X'); }
				if (x != std::string::npos)
				{
					try {
						int w = std::stoi(s.substr(0, x));
						int h = std::stoi(s.substr(x + 1));
						if (w > 0 && h > 0) {
							out.widthOverride  = w;
							out.heightOverride = h;
						}
					} catch (...) {}
				}
			}
		}
		else if (a == "--lofi")
		{
			out.loFi = true;
		}
		else if (a == "--lofi-hard")
		{
			out.loFiHard = true;
			out.loFi = true;
		}
		else if (a == "--lofi-vi")
		{
			out.loFiVi = true;
			out.loFi = true;
		}
		else if (a == "--lofi-gamma")
		{
			if (i + 1 < argc) { out.loFiGamma = std::strtof(argv[++i], nullptr); }
			out.loFi = true;
		}
		else if (a == "--lofi-size")
		{
			if (i + 1 < argc)
			{
				std::string s{argv[++i]};
				auto x = s.find('x'); if (x == std::string::npos) x = s.find('X');
				if (x != std::string::npos)
				{
					try {
						int w = std::stoi(s.substr(0, x)), h = std::stoi(s.substr(x + 1));
						if (w > 0 && h > 0) { out.loFiW = w; out.loFiH = h; out.loFi = true; }
					} catch (...) {}
				}
			}
		}
		else if (a == "--lofi-bits")
		{
			// 形式: R,G,B 例 "5,6,5"(RGB565) / "3,3,2"(256色相当)
			if (i + 1 < argc)
			{
				std::string s{argv[++i]};
				try {
					auto c1 = s.find(','), c2 = s.find(',', c1 + 1);
					if (c1 != std::string::npos && c2 != std::string::npos) {
						out.loFiBitsR = std::stoi(s.substr(0, c1));
						out.loFiBitsG = std::stoi(s.substr(c1 + 1, c2 - c1 - 1));
						out.loFiBitsB = std::stoi(s.substr(c2 + 1));
						out.loFi = true;
					}
				} catch (...) {}
			}
		}
		else if (a == "--lofi-dither")
		{
			if (i + 1 < argc) { try { out.loFiDither = std::stof(argv[++i]); out.loFi = true; } catch (...) {} }
		}
		else if (a.rfind("--", 0) == 0)
		{
			// 未知の --option は positional に流さず拒否する (typo / 廃止 flag の黙殺防止)。
			if (out.unknownOption.empty()) { out.unknownOption = std::string(a); }
		}
		else if (out.dllPath.empty())
		{
			out.dllPath = a;
		}
		// DLL の後ろの位置引数 = 旧式 URL スロット。
		else if (out.cefUrlOverride.empty())
		{
			out.cefUrlOverride = a;
		}
	}

	// 環境変数 MITIRU_WATCH=1 は --watch と同じ。
	if (const char* envWatch = std::getenv("MITIRU_WATCH");
	    envWatch && envWatch[0] != '\0' && std::string{envWatch} != "0")
	{
		out.watch = true;
	}
	return out;
}

/// 既定の --help (E8: 55 行は多すぎるので日常使う一部だけに絞る。全量は --help-all)。
void printUsageShort()
{
	std::fprintf(stderr,
		"usage: mitiru_host <game.dll> [options]\n"
		"\n"
		"common options:\n"
		"  --watch          poll DLL file mtime, hot-reload on change\n"
		"  --size WxH       override window size (e.g. --size 800x500)\n"
		"  --headless       ウィンドウ無しで走らせる (AI 自動プレイ/CI)\n"
		"  --record F       毎フレームの入力と state を .mtrr F に記録する\n"
		"  --replay-test F  .mtrr F をヘッドレスで再実行し bit-exact 照合 (CI ゲート)\n"
		"  --replay F       .mtrr F を window ありで GUI 再生 (デモ撮影用)\n"
		"  --input-script F 入力スクリプト F を in-process 注入 (OS 入力を経由しない)\n"
		"  --inspect [name] ツール独立窓を起動時に開く\n"
		"  --http-port N    HTTP API を 127.0.0.1:N で開始\n"
		"  --json           --replay-test の verdict を stdout に 1 行 JSON でも出す\n"
		"  --help, -h       this message\n"
		"  --help-all       全オプション・hotkeys・environment を表示\n"
		"\n"
		"docs/FIRST_TOUCH.md に最初の一歩。全オプションは --help-all で。\n");
}

void printUsage(bool full)
{
	if (!full) { printUsageShort(); return; }
	std::fprintf(stderr,
		"usage: mitiru_host <game.dll> [options]\n"
		"\n"
		"options:\n"
		"  --watch          poll DLL file mtime, hot-reload on change\n"
		"  --watch-assets D 配下の .json/.baked が変わったら game に asset.reloaded {path,hash} を届ける (録画に乗る)\n"
		"  --title <name>   window title (既定 = DLL ファイル名の stem。配布時は mitiru dist が project 名を書く)\n"
		"  --icon <f.ico>   window icon を .ico ファイルで差し替え (Windows)\n"
		"  --appid <id>     AppUserModelID を設定 (taskbar のグループ/ピン留めをゲーム単位に分離)\n"
		"  --size WxH       override window size (e.g. --size 800x500)\n"
		"  --no-pause-unfocused  keep running at full rate when window is unfocused\n"
		"  --no-vsync       present の vsync 待ちを切る (フレームコストの素を計測する用)\n"
		"  --perf           実フレーム時間の統計 (avg/p50/p95/max) を 600 フレームごとに表示\n"
		"                   GPU 実機の描画コスト計測は windowed + --perf --no-vsync で\n"
		"  --url <url>      override CEF start URL\n"
		"  --font <mode>    none|latin|kana|japanese — native draw 用フォント (既定 kana)\n"
		"  --font-face <f>  normal|retro — 普通(M+ Rounded) / レトロ(PixelMplus) (既定 normal)\n"
		"                   (既定 none = フォント skip・起動高速。日本語 native text を\n"
		"                    出すなら japanese)\n"
		"  --lofi           低解像描画+パレット量子化+Bayerディザ (DX12, DirectX5期の質感)\n"
		"  --lofi-size WxH  内部解像度 (既定 320x240)\n"
		"  --lofi-bits R,G,B  量子化ビット数 (既定 5,6,5=RGB565 / 3,3,2=256色相当)\n"
		"  --lofi-dither S  ディザ強度 (既定 1.0, 0=ディザ無し)\n"
		"  --http-port N    HTTP API を 127.0.0.1:N で開始 (実行中のゲームを外部から操作/観測)\n"
		"  --cef-debug-port N  CEF remote debugging を 127.0.0.1:N で開く (chrome-devtools / CDP 実機テスト)\n"
		"  --console        HTTP 起動 + 既定ブラウザで control panel を自動表示 (port 既定 8090)\n"
		"  --no-cef         CEF を起動しない (完全ネイティブ描画の game 用・起動軽量化)\n"
		"  --capture-dir D  毎 N フレームのフレームを PNG 連番で D に吐く (自動の見た目検証用)\n"
		"  --capture-every N  上記の間隔 (フレーム数。--capture-dir 指定時の既定 30)\n"
		"  --headless       ウィンドウ無しで走らせる (AI 自動プレイの裏回し)\n"
		"  --headless-3d    --headless でも実 GPU で 3D を描く (既定は NullDevice で 2D のみ、\n"
		"                   --capture-dir で 3D シーンの見た目を撮りたい時に使う。G2、既定 backend は DX11、\n"
		"                   --backend dx12 で DX12 windowless に切り替え可)\n"
		"  --backend NAME   グラフィックスバックエンドを明示指定する (auto|dx11|dx12。既定 auto)\n"
		"  --speed N        time scale 倍率 (固定 dt × N で早回し。長いプレイの自動回し用)\n"
		"  --max-frames N   N フレーム走ったら自動終了 (--headless 自動回しの停止条件)\n"
		"  --fixed-size     ユーザのウィンドウリサイズを禁止 (固定解像度運用)\n"
		"  --record F       毎フレームの入力と state を .mtrr F に記録する (--replay-test と排他)\n"
		"  --record-state-every N  --record 時、state blob (GameMemory 全体) を N フレームごとに\n"
		"                   記録する (既定 60。0=毎フレーム。save/load を跨ぐフレームは周期を無視して必ず記録)\n"
		"  --replay-test F  .mtrr F をヘッドレスで再実行し、記録済み GameMemory と bit-exact 照合する\n"
		"                   (CI リグレッションゲート。相違があれば非ゼロ終了)\n"
		"  --oracle-log     組込オラクル違反を機械可読な \"[oracle] ...\" 行として stderr に追加出力する\n"
		"                   (mitiru hunt の ScanOracleLines 用。既定 OFF)\n"
		"  --rewind-frames N  巻き戻せるフレーム数 (既定 300 = 約 5 秒。60fps 基準)\n"
		"  --rewind-mb N    巻き戻しリングのメモリ予算 (MB、既定 512。ゲームが\n"
		"                   MITIRU_REWIND_BUDGET を宣言していれば未指定時はそちらが優先。\n"
		"                   0=無制限=非圧縮)。\n"
		"                   予算内に収まらない古いフレームは XOR+RLE デルタ圧縮で切り詰める\n"
		"  --oracle-determinism [N]  決定論オラクル (N1e) を opt-in。K フレーム前から\n"
		"                   resim ring で再シミュレーションし、現在の GameMemory と memcmp する\n"
		"                   (N=実行間隔フレーム数、省略時 既定 120)。既定 OFF (GameMemory サイズに\n"
		"                   比例したコストがかかるため明示指定のみ有効)\n"
		"  --ghost F        .mtrr F を同じ DLL のもう1本の GameMemory に並走投入し、\n"
		"                   live の直前に半透明で描く (タイムアタックのゴースト、10-1)。\n"
		"                   --replay と併用すれば ghost vs live の同時再生になる。\n"
		"                   F の入力が尽きたら最後のフレームで静止する\n"
		"  --input-script F 入力スクリプト F を in-process 注入 (OS 入力を経由せず他アプリに漏れない)\n"
		"                   形式: 1 行 '<frame> <down|up> <KEY>' (# でコメント)。KEY=Left/Right/Up/Down/\n"
		"                   Space/Enter/Escape/英数字1字/MouseL/MouseR/MouseM/生 VK 整数。\n"
		"                   '<frame> move <dx> <dy> [frames]' でマウス delta (FPS 視線) も注入できる。\n"
		"                   実キーボード・実マウスは無視される\n"
		"  --state-trace F  MITIRU_REFLECT で申告したフィールド値を毎フレーム JSONL で F に出力。\n"
		"                   --input-script と併用し、プレイの軌跡や HP 推移を後から解析できる\n"
		"  --input-record F 実プレイの入力を input-script 形式で F に録画 (--input-script で再生可)\n"
		"  --error-file F   ビルドエラーファイル F を監視し、存在する間だけ画面上部に帯を表示\n"
		"                   (mitiru watch が自動指定。直して保存 → ビルド成功で帯が消える)\n"
		"  --pause-control F ファイル F が \"1\" の間だけ pause (dt=0, 描画継続)。フォーカス不要。\n"
		"                   録画で「編集中は静止、ビルド後に再開」を作るのに使う\n"
		"  --input-freeze-control F  ファイル F が \"1\" の間だけ入力を無効化 (--input-script と併用)。\n"
		"                   プレイヤーは静止するが engine は進む (星などは動く)。録画支援\n"
		"  --no-tool-windows  hud.open 等のツール窓 (CEF) spawn を全無効。録画/CI で\n"
		"                   望まない窓がメイン画面に出るのを防ぐ\n"
		"  --window-pos X Y ゲーム窓を最初からこの座標に出す (実画面に一瞬も出さない)。\n"
		"                   負の X = 仮想ディスプレイ等。録画支援\n"
		"  --replay F       .mtrr F を window ありで GUI 再生 (--inspect と併用可)。\n"
		"                   --replay-test と違い verdict 判定はせず、EOF で自動終了せず\n"
		"                   最後のフレームで止まる (デモ撮影・目視確認用)\n"
		"  --inspect [name] ツール独立窓を起動時に開く\n"
		"                   (name=inspector|input|rewind|scene|perf|mixer|scene_view|why_view|frame_view,\n"
		"                     既定 inspector)\n"
		"                   ※ host を書く人が main.cpp で mitiru::debug::openTool(Tool::X) と\n"
		"                     直接書けば、欲しい窓だけコードで指定できる\n"
		"  --tool-window-pos X Y  --inspect 等で spawn するツール窓の初期座標\n"
		"                   (--window-pos と別。メイン窓と重ねたくない録画で使う)\n"
		"  --bug-ring-save  起動直後に「昨日のバグ」リング (P1) を即 .mtrr 保存。\n"
		"                   通常は実行中に F11 で保存する (このフラグは自動化/CI 用)\n"
		"  --pack F         資産パック F (.mtpak) を自動探索より優先してマウントする (P12)\n"
		"  --collision F    物理問い合わせ job (hud.raycast 等) が答える静的な箱の JSON [{min,max,layer}] (v37)\n"
		"  --replay-gate-dir D  分岐エディタ「残す」直後に回す決定論ゲートの .mtrr 置き場\n"
		"                   (既定 tests/replay_golden。ADR 0035 O5)\n"
		"  --expect F       --replay-test 併用: 最終 view.* state を F (JSON) と diff し、\n"
		"                   不一致があれば非ゼロ終了 (CI リグレッションの期待値照合)\n"
		"  --state-diff A B 2 つの .mtrr A/B を比較し、GameMemory が分岐した最初の\n"
		"                   frame を報告して終了 (再生はしない)\n"
		"  --json           --replay-test の verdict (PASS/FAIL・diverged frame・diff)を\n"
		"                   stdout に 1 行 JSON でも出す (CLI 側の正規表現パース置換用)\n"
		"  --lofi-hard      --lofi の柔らか拡大を切りニアレスト拡大にする\n"
		"  --lofi-vi        --lofi の映像出力段に de-dither + divot を掛ける\n"
		"  --lofi-gamma G   --lofi 出力段のガンマ補正 (既定 1.0 = 素通し)\n"
		"  --config-origins 起動時設定 (backend/size/speed/http-port/pack/rewind/...) の値と\n"
		"                   由来 (既定/CLI/環境変数) を表で出して終了する (§3-3)\n"
		"  --help, -h       this message (既定 20 行。--help-all で全部)\n"
		"  --help-all       このヘルプの全量 (全オプション・hotkeys・environment)\n"
		"\n"
		"hotkeys (runtime, Windows):\n"
		"  F7               paused 時に 1 フレーム step\n"
		"  F8               pause / play toggle (on_update dt=0 化、描画は継続)\n"
		"  F9               time-scale cycle (1x→0.5x→0.25x→2x→4x→1x)\n"
		"  F10              lo-fi post-FX toggle (DX12)\n"
		"  F11              bug ring (P1) を今すぐ .mtrr 保存\n"
		"  F12              screenshot — ./screenshots/frame_YYYYMMDD_HHMMSS.png + clipboard\n"
		"\n"
		"environment:\n"
		"  MITIRU_WATCH=1   same as --watch\n"
		"\n"
		"The host loads the game DLL via Engine::loadModule and drives the\n"
		"main loop. The DLL must export mitiru_module_load (see\n"
		"docs/adr/0005-host-game-c-abi-signal-flow.md).\n");
}

/// @brief §3-3: 「この設定値はどこで決まったか」を追跡する 1 行。`EngineConfig` は触らず
/// (ABI/決定論の都合で Config.hpp は変更禁止)、host の引数解析側だけで由来を記録する。
struct ConfigOriginRow
{
	const char* name;    ///< EngineConfig 側のフィールド名 (表示用)
	std::string value;   ///< 実際に採用された値 (文字列化済み)
	const char* origin;  ///< "default" / "cli(--xxx)" / "env(XXX)" のいずれか
};

/// @brief `--backend` の enum を人間可読な名前へ戻す (UE の CVar 一覧のような表示のため)。
inline const char* backendDisplayName(mitiru::gfx::Backend b) noexcept
{
	switch (b)
	{
		case mitiru::gfx::Backend::Auto: return "auto";
		case mitiru::gfx::Backend::Dx11: return "dx11";
		case mitiru::gfx::Backend::Dx12: return "dx12";
		default: return "other";
	}
}

/// @brief `cfg`/`args` から代表的な設定値の「値と由来」を集めて表で stdout へ出す。
/// UE の CVar 優先順位 (console > commandline > ini > constructor) と同じ発想を、
/// Mitiru では「録画に乗らない state を増やさない」方針のもとで host 起動時設定だけに絞って示す。
void printConfigOrigins(const CliArgs& args, const mitiru::EngineConfig& cfg,
	const std::string& resolvedPackPath, bool aiOptIn)
{
	std::vector<ConfigOriginRow> rows;
	rows.push_back({ "gfxBackend", backendDisplayName(cfg.gfxBackend),
		args.backendSet ? "cli(--backend)" : "default" });
	rows.push_back({ "windowWidth/Height",
		std::to_string(cfg.windowWidth) + "x" + std::to_string(cfg.windowHeight),
		(args.widthOverride > 0 || args.heightOverride > 0) ? "cli(--size)" : "default" });
	rows.push_back({ "timeScale", std::to_string(cfg.timeScale),
		args.speedSet ? "cli(--speed)" : "default" });
	rows.push_back({ "enableHttpApi/httpApiPort",
		cfg.enableHttpApi ? std::to_string(cfg.httpApiPort) : "off",
		args.httpPort > 0 ? "cli(--http-port)"
			: (args.console ? "cli(--console)"
			: (aiOptIn ? "env(MITIRU_AI/MITIRU_AI_PORT)" : "default(off)")) });
	// host が pack を見つけられなかった時は Engine 側が MITIRU_PACK 環境変数を読む (Engine_Module_Loader)。
	const char* packEnv = std::getenv("MITIRU_PACK");
	const bool packFromEnv = resolvedPackPath.empty() && packEnv != nullptr && packEnv[0] != '\0';
	rows.push_back({ "packPath",
		packFromEnv ? std::string(packEnv) : (resolvedPackPath.empty() ? "(none)" : resolvedPackPath),
		!args.packOverride.empty() ? "cli(--pack)"
			: (packFromEnv ? "env(MITIRU_PACK)"
			: (resolvedPackPath.empty() ? "default(none)" : "auto-discovered")) });
	rows.push_back({ "timeTravelBufferFrames", std::to_string(cfg.timeTravelBufferFrames),
		args.rewindFrames > 0 ? "cli(--rewind-frames)" : "default" });
	rows.push_back({ "oracleDeterminism", cfg.oracleDeterminism ? "on" : "off",
		args.oracleDeterminism ? "cli(--oracle-determinism)"
			: (args.synctest ? "cli(--synctest)" : "default(off)") });
	rows.push_back({ "headless", cfg.headless ? "on" : "off",
		args.headless ? "cli(--headless)" : "default(off)" });

	std::fprintf(stdout, "config origin (default < env < cli, 高い方が勝つ):\n");
	for (const auto& r : rows)
	{
		std::fprintf(stdout, "  %-24s = %-20s [%s]\n", r.name, r.value.c_str(), r.origin);
	}
}

#ifdef _WIN32
/// RGBA8 トップダウン pixel buffer を Windows clipboard に CF_DIB として置く。
inline bool copyRgbaToClipboard(const std::uint8_t* rgba, int w, int h)
{
	if (!rgba || w <= 0 || h <= 0) { return false; }

	const std::size_t headerSize = sizeof(BITMAPINFOHEADER);
	const std::size_t pixelBytes = static_cast<std::size_t>(w) * h * 4;
	HGLOBAL hDib = GlobalAlloc(GMEM_MOVEABLE, headerSize + pixelBytes);
	if (!hDib) { return false; }

	auto* p = static_cast<std::uint8_t*>(GlobalLock(hDib));
	if (!p) { GlobalFree(hDib); return false; }

	BITMAPINFOHEADER hdr{};
	hdr.biSize        = static_cast<DWORD>(headerSize);
	hdr.biWidth       = w;
	hdr.biHeight      = -h;  // 負 = top-down (本 buffer の row 順と一致)
	hdr.biPlanes      = 1;
	hdr.biBitCount    = 32;
	hdr.biCompression = BI_RGB;
	hdr.biSizeImage   = static_cast<DWORD>(pixelBytes);
	std::memcpy(p, &hdr, headerSize);

	// RGBA → BGRA (DIB は B,G,R,A 順)
	auto* dst = p + headerSize;
	const std::size_t pixels = static_cast<std::size_t>(w) * h;
	for (std::size_t i = 0; i < pixels; ++i)
	{
		dst[i * 4 + 0] = rgba[i * 4 + 2];
		dst[i * 4 + 1] = rgba[i * 4 + 1];
		dst[i * 4 + 2] = rgba[i * 4 + 0];
		dst[i * 4 + 3] = rgba[i * 4 + 3];
	}
	GlobalUnlock(hDib);

	if (!OpenClipboard(nullptr)) { GlobalFree(hDib); return false; }
	EmptyClipboard();
	HANDLE set = SetClipboardData(CF_DIB, hDib);
	CloseClipboard();
	if (!set) { GlobalFree(hDib); return false; }
	// 成功時は clipboard が hDib の所有権を持つ。free しない。
	return true;
}
#endif

#ifdef _WIN32
/// 1 VK あたり、down 状態を保持して立ち下がり検出を返す。GetAsyncKeyState を毎フレーム
/// 1 回叩く前提。
inline bool justPressed(int vk)
{
	static bool wasDown[256] = {};
	if (vk < 0 || vk >= 256) { return false; }
	const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
	const bool jp = down && !wasDown[vk];
	wasDown[vk] = down;
	return jp;
}

/// F12 で window バックバッファを PNG 保存 + clipboard コピー。
inline void doScreenshot(mitiru::Engine& engine)
{
	const int w = engine.captureWidth();
	const int h = engine.captureHeight();
	if (w <= 0 || h <= 0) { return; }

	const auto rgba = engine.capture();
	if (rgba.empty()) { return; }

	const auto path = mitiru::render::saveTimestampedFrameToPng(
		rgba.data(), w, h, "screenshots", "frame");
	const bool clipOk = copyRgbaToClipboard(rgba.data(), w, h);

	if (!path.empty() && clipOk)
	{
		std::fprintf(stderr, "[mitiru_host] screenshot: %s (also on clipboard)\n", path.c_str());
	}
	else if (!path.empty())
	{
		std::fprintf(stderr, "[mitiru_host] screenshot: %s (clipboard copy failed)\n", path.c_str());
	}
	else if (clipOk)
	{
		std::fprintf(stderr, "[mitiru_host] screenshot on clipboard (file save failed)\n");
	}
	else
	{
		std::fprintf(stderr, "[mitiru_host] screenshot failed (file + clipboard)\n");
	}
}
#endif

/// onFrameStart から毎フレーム呼ぶ host hotkey 処理。Windows のみ。
inline void pollHostHotkeys(mitiru::Engine& engine)
{
#ifdef _WIN32
	// host hotkey は GetAsyncKeyState (グローバル) で読むため、自プロセスのウィンドウが
	// 前面の時だけ処理する。さもないと別アプリで作業中の F7-F12 を奪ってしまう
	// (ゲーム入力自体は WM_KEYDOWN でフォーカス限定済みだが、ここだけグローバルだった)。
	HWND fg = GetForegroundWindow();
	DWORD fgPid = 0;
	GetWindowThreadProcessId(fg, &fgPid);
	if (fgPid != GetCurrentProcessId())
	{
		return;
	}
	if (justPressed(VK_F12))
	{
		doScreenshot(engine);
	}
	if (justPressed(VK_F8))
	{
		engine.setPauseKind(mitiru::EngineConfig::kPauseKindDebug);
		engine.togglePaused();
		std::fprintf(stderr, "[mitiru_host] %s\n", engine.isPaused() ? "PAUSED" : "PLAYING");
	}
	if (justPressed(VK_F7))
	{
		if (engine.isPaused())
		{
			engine.stepOneFrame();
			std::fprintf(stderr, "[mitiru_host] step 1 frame\n");
		}
	}
	if (justPressed(VK_F9))
	{
		// 1x → 0.5x → 0.25x → 2x → 4x → 1x で巡回
		static constexpr float kScales[] = { 1.0f, 0.5f, 0.25f, 2.0f, 4.0f };
		static constexpr int N = static_cast<int>(sizeof(kScales) / sizeof(kScales[0]));
		static int idx = 0;
		idx = (idx + 1) % N;
		engine.setTimeScale(kScales[idx]);
		std::fprintf(stderr, "[mitiru_host] time-scale %.2fx\n", kScales[idx]);
	}
	if (justPressed(VK_F10))
	{
		engine.toggleLofi();
		std::fprintf(stderr, "[mitiru_host] lofi %s\n", engine.isLofiEnabled() ? "ON" : "OFF");
	}
	if (justPressed(VK_F11))
	{
		// P1: 「昨日のバグ」リングを今すぐ保存。Engine 側がワンショットで消費する。
		engine.mutableConfig().bugRingSaveRequested = true;
		std::fprintf(stderr, "[mitiru_host] bug ring save requested\n");
	}
#else
	(void)engine;  // host hotkeys は今のところ Windows 専用
#endif
}

/// ファイル監視状態。onFrameStart クロージャにキャプチャされる。
struct WatcherState
{
	std::filesystem::path           dllPath;
	std::filesystem::file_time_type lastMtime{};
	int                             pollTick   = 0;
	int                             pollEvery  = 15;       // frame 数; 60fps で約 250ms
	bool                            initialized = false;
};

/// --watch-assets の監視状態。onFrameStart から 15 フレームごとに mtime を見る (DLL の WatcherState と同じ間隔)。
struct AssetWatchState
{
	std::filesystem::path dir;
	std::unordered_map<std::string, std::filesystem::file_time_type> mtimes;
	int  pollTick = 0;
	bool initialized = false;
	static bool watched(const std::filesystem::path& p) noexcept
	{
		const auto ext = p.extension().string();
		return ext == ".json" || ext == ".baked";
	}
};

/// 変わったファイルごとに asset.reloaded を積む。初回は台帳を作るだけで通知しない。
inline void pollAssetWatch(AssetWatchState& st, mitiru::Engine& engine)
{
	if (st.dir.empty()) { return; }
	if (++st.pollTick < 15) { return; }
	st.pollTick = 0;
	std::error_code ec;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(st.dir, ec))
	{
		if (ec) { break; }
		if (!entry.is_regular_file(ec) || !AssetWatchState::watched(entry.path())) { continue; }
		const auto mtime = entry.last_write_time(ec);
		if (ec) { continue; }
		const std::string key = entry.path().generic_string();
		auto it = st.mtimes.find(key);
		if (it == st.mtimes.end())
		{
			st.mtimes.emplace(key, mtime);
			if (!st.initialized) { continue; }  // 初回は台帳を作るだけ。以後に増えたファイルは通知する
		}
		else
		{
			if (it->second == mtime) { continue; }
			it->second = mtime;
		}
		const std::string rel = std::filesystem::relative(entry.path(), st.dir, ec).generic_string();
		const std::string payload = nlohmann::json{
			{"path", ec ? key : rel},
			{"hash", mitiru::module::spawnSourceFileHash(entry.path().filename().string())}}.dump();
		if (!engine.pushModuleActionEvent("asset.reloaded", payload))
		{
			std::fprintf(stderr, "[mitiru_host] asset.reloaded を積めなかった: %s\n", key.c_str());
		}
		else
		{
			std::fprintf(stderr, "[mitiru_host] asset.reloaded -> %s\n", key.c_str());
		}
	}
	st.initialized = true;
}

/// 巻き戻し scrub の適用を Engine::IFrameListener 経由に移した実装 (1-6)。
/// 別窓 (inspector) が書く scrub command を毎フレーム読み、engine の scrub-hold へ適用する。
/// main.cpp の onFrameStart から scrub 専用ロジックを追い出し、登録だけにするのが目的。
struct ScrubApplyListener final : mitiru::IFrameListener
{
	mitiru::observe::ScrubControlReader reader;  // 自プロセス pid 宛 (inspector が host pid に書く)
	long                                 lastSeq = 0;

	void onBeforeUpdate(mitiru::Engine& engine) override
	{
		auto cmd = reader.poll();
		if (!cmd) { return; }
		const long seq = cmd->value("seq", 0L);
		if (seq <= lastSeq) { return; }
		lastSeq = seq;
		if (cmd->value("resume", 0)) { engine.clearScrubHold(); }
		else { engine.setScrubHold(static_cast<std::size_t>(cmd->value("scrubTo", 0))); }
	}
};

// ── 入力スクリプト (#43-1, in-process 注入) ────────────────────────────────
// OS 入力 (SendInput) を経由せず InputSnapshot を直接書き換えるので、他アプリにキーが
// 漏れない・headless でも効く・決定的。実キーボードはスクリプト実行中は無視される。

/// KEY 名 → 仮想キーコード。1字英数字は ASCII 大文字 / 数字、名前は主要キー、生 VK 整数も可。
inline int keyNameToVk(const std::string& s)
{
	if (s.empty()) { return -1; }
	if (s.size() == 1)
	{
		char c = s[0];
		if (c >= 'a' && c <= 'z') { c = static_cast<char>(c - 'a' + 'A'); }
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
		{
			return static_cast<int>(static_cast<unsigned char>(c));
		}
	}
	std::string u = s;
	for (auto& c : u) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
	if (u == "LEFT")  { return 0x25; }
	if (u == "UP")    { return 0x26; }
	if (u == "RIGHT") { return 0x27; }
	if (u == "DOWN")  { return 0x28; }
	if (u == "SPACE") { return 0x20; }
	if (u == "ENTER" || u == "RETURN") { return 0x0D; }
	if (u == "ESCAPE" || u == "ESC")   { return 0x1B; }
	if (u == "SHIFT") { return 0x10; }
	if (u == "TAB")   { return 0x09; }
	if (u == "CTRL" || u == "CONTROL") { return 0x11; }
	if (u == "ALT")   { return 0x12; }
	if (u == "BACK" || u == "BACKSPACE") { return 0x08; }
	try { return std::stoi(s, nullptr, 0); } catch (...) { return -1; }
}

/// 仮想キーコード → 名前（keyNameToVk の逆。--input-record の出力に使う）。
inline std::string vkToName(int vk)
{
	switch (vk)
	{
	case 0x25: return "Left";
	case 0x26: return "Up";
	case 0x27: return "Right";
	case 0x28: return "Down";
	case 0x08: return "Back";
	case 0x09: return "Tab";
	case 0x11: return "Ctrl";
	case 0x12: return "Alt";
	case 0x20: return "Space";
	case 0x0D: return "Enter";
	case 0x1B: return "Escape";
	case 0x10: return "Shift";
	default: break;
	}
	if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
	{
		return std::string(1, static_cast<char>(vk));
	}
	return std::to_string(vk);  // 名前の無いキーは生 VK 整数
}

/// PlayerError → 表示名 (--replay-test の FAIL 理由表示用)。
inline const char* playerErrorName(mitiru::replay::PlayerError e)
{
	using PE = mitiru::replay::PlayerError;
	switch (e)
	{
	case PE::None:              return "none";
	case PE::FileNotOpen:       return "file not open";
	case PE::HeaderTooShort:    return "header too short";
	case PE::MagicMismatch:     return "magic mismatch";
	case PE::VersionMismatch:   return "format version mismatch";
	case PE::FrameSizeMismatch: return "frame size mismatch";
	case PE::FrameTruncated:    return "frame truncated";
	case PE::ChecksumMismatch:  return "checksum mismatch";
	}
	return "unknown";
}

/// gfx::Backend → --record の envTag に載せる短い識別子。
inline const char* gfxBackendTag(mitiru::gfx::Backend b)
{
	using B = mitiru::gfx::Backend;
	switch (b)
	{
	case B::Auto:   return "auto";
	case B::Dx11:   return "dx11";
	case B::Dx12:   return "dx12";
	case B::Vulkan: return "vulkan";
	case B::OpenGL: return "opengl";
	case B::WebGL:  return "webgl";
	case B::WebGPU: return "webgpu";
	case B::Null:   return "null";
	}
	return "unknown";
}

/// --record の envTag を組み立てる ("backend|arch|tracy有無")。GPU adapter 名は IDevice から
/// 取り出す経路が無く、追加するには全 backend への interface 変更が要るため見送り、
/// 確実に取れる backend 種別と CPU arch だけを載せる。
/// 7-1: Tracy 計装ビルドかどうかも入れる。この録画を後から Tracy キャプチャと突き合わせる
/// (`docs/PROFILING_GUIDE.md`) とき、そもそも計装済みビルドで録ったかを envTag だけで判別できる。
inline std::string buildRecordEnvTag(mitiru::gfx::Backend backend)
{
	std::string tag = std::string(gfxBackendTag(backend)) + "|x64";
	tag += mitiru::debug::TracyHelper::isAvailable() ? "|tracy" : "|no-tracy";
	return tag;
}

struct InputScriptEvent
{
	enum Kind { Key, MouseBtn, MouseMove };
	int  frame;
	Kind kind;
	int  a;      // Key: vk / MouseBtn: 0=L 1=R 2=M / MouseMove: dx
	int  b;      // Key・MouseBtn: down=1 up=0 / MouseMove: dy
};

/// スクリプトを毎フレーム適用し InputSnapshot のキー・マウスを上書きするプレイヤ。
/// 実キーボード・実マウスは無視される (注入のみ有効 = 決定的)。
struct InputScriptPlayer
{
	std::vector<InputScriptEvent> events;  // frame 昇順
	std::size_t cursor = 0;
	int frame = 0;
	bool held[256] = {};
	bool heldBtn[3] = {};

	void apply(mitiru::module::InputSnapshot& snap)
	{
		bool prev[256];
		bool prevBtn[3];
		std::memcpy(prev, held, sizeof(prev));
		std::memcpy(prevBtn, heldBtn, sizeof(prevBtn));
		float moveX = 0.0f, moveY = 0.0f;
		while (cursor < events.size() && events[cursor].frame <= frame)
		{
			const auto& e = events[cursor++];
			switch (e.kind)
			{
			case InputScriptEvent::Key:
				if (e.a >= 0 && e.a < 256) { held[e.a] = (e.b != 0); }
				break;
			case InputScriptEvent::MouseBtn:
				if (e.a >= 0 && e.a < 3) { heldBtn[e.a] = (e.b != 0); }
				break;
			case InputScriptEvent::MouseMove:
				moveX += static_cast<float>(e.a);
				moveY += static_cast<float>(e.b);
				break;
			}
		}
		for (int v = 0; v < 256; ++v)
		{
			snap.keysDown[v]         = held[v] ? 1 : 0;
			snap.keysJustPressed[v]  = (held[v] && !prev[v]) ? 1 : 0;
			snap.keysJustReleased[v] = (!held[v] && prev[v]) ? 1 : 0;
		}
		for (int i = 0; i < 3; ++i)
		{
			snap.mouseButtonsDown[i]         = heldBtn[i] ? 1 : 0;
			snap.mouseButtonsJustPressed[i]  = (heldBtn[i] && !prevBtn[i]) ? 1 : 0;
			snap.mouseButtonsJustReleased[i] = (!heldBtn[i] && prevBtn[i]) ? 1 : 0;
		}
		snap.mouseDeltaX = moveX;
		snap.mouseDeltaY = moveY;
		++frame;
	}
};

/// '<frame> <down|up> <KEY>' / '<frame> move <dx> <dy> [frames]' 形式 (# でコメント) を読む。
/// KEY には MouseL / MouseR / MouseM も使える。move は視線用のマウス delta を
/// そのフレームに注入する ([frames] 指定で連続フレームへ同 delta を展開)。失敗時 false。
inline bool loadInputScript(const std::string& path, InputScriptPlayer& out)
{
	std::ifstream f(path);
	if (!f) { return false; }
	std::string line;
	while (std::getline(f, line))
	{
		const auto h = line.find('#');
		if (h != std::string::npos) { line = line.substr(0, h); }
		std::istringstream is(line);
		int frame = 0;
		std::string t2, t3;
		if (!(is >> frame >> t2 >> t3)) { continue; }
		auto lower = [](std::string s) {
			for (auto& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return s;
		};
		if (lower(t2) == "move")
		{
			int dx = 0, dy = 0, count = 1;
			try { dx = std::stoi(t3); } catch (...) { continue; }
			if (!(is >> dy)) { continue; }
			if (!(is >> count)) { count = 1; }
			if (count < 1) { count = 1; }
			for (int k = 0; k < count; ++k)
			{
				out.events.push_back({frame + k, InputScriptEvent::MouseMove, dx, dy});
			}
			continue;
		}
		// 両形式を許す: "<frame> <down|up> <KEY>" と "<frame> <KEY> <down|up>"。
		// (--input-record の出力は後者。#45 の例 `120 Z down` もこれ。)
		auto isAct = [](const std::string& s) {
			return s == "down" || s == "d" || s == "DOWN" || s == "up" || s == "u" || s == "UP";
		};
		std::string act, key;
		if (isAct(t2)) { act = t2; key = t3; }
		else           { key = t2; act = t3; }
		const bool down = (act == "down" || act == "d" || act == "DOWN");
		const bool up   = (act == "up" || act == "u" || act == "UP");
		if (!down && !up) { continue; }
		const std::string mk = lower(key);
		if (mk == "mousel" || mk == "mouser" || mk == "mousem")
		{
			const int idx = (mk == "mousel") ? 0 : (mk == "mouser") ? 1 : 2;
			out.events.push_back({frame, InputScriptEvent::MouseBtn, idx, down ? 1 : 0});
			continue;
		}
		const int vk = keyNameToVk(key);
		if (vk < 0) { continue; }
		out.events.push_back({frame, InputScriptEvent::Key, vk, down ? 1 : 0});
	}
	std::stable_sort(out.events.begin(), out.events.end(),
		[](const InputScriptEvent& a, const InputScriptEvent& b) { return a.frame < b.frame; });
	return true;
}

}  // namespace

int main(int argc, char* argv[])
{
#ifdef _WIN32
	// ログと警告は UTF-8 で書いている (ソースが /utf-8)。コンソールの既定は CP932 なので、
	// そのままだと日本語が全部化ける。出力コードページだけ UTF-8 へ切り替え、終了時に
	// 元へ戻す (同じ窓で続けて動く他のツールの表示を巻き込まないため)。
	static const UINT s_prevConsoleCP = GetConsoleOutputCP();
	SetConsoleOutputCP(CP_UTF8);
	std::atexit([] { SetConsoleOutputCP(s_prevConsoleCP); });
#endif
#ifdef _WIN32
	// CRT が渡す argv は ANSI (日本語環境では CP932) へ変換済みで、UTF-8 前提の
	// エンジンへ流すと日本語の引数が化ける (--title など)。UTF-16 の原本から取り直す。
	//
	// 実体は utf8Args が持つ。main の最後まで生きるので、useArgv が指し続けても安全。
	std::vector<std::string> utf8Args = mitiru::platform::commandLineUtf8Args();
	std::vector<char*>       utf8ArgPtr;
	if (!utf8Args.empty())
	{
		for (auto& s : utf8Args) { utf8ArgPtr.push_back(s.data()); }
		argc = static_cast<int>(utf8ArgPtr.size());
		argv = utf8ArgPtr.data();
	}
#endif
	anchorCwdToExeDir(argc > 0 ? argv[0] : nullptr);

	// 引数なし起動 (ダブルクリック / Steam) のときは sidecar <exe>.mtargs から
	// argv を補う。これで mitiru_host を <game>.exe にリネーム配布できる。
	std::vector<std::string> synthArgs;
	std::vector<char*>       synthPtr;
	int                      useArgc = argc;
	char**                   useArgv = argv;
	if (argc < 2)
	{
		auto side = readSidecarArgs(argc > 0 ? argv[0] : nullptr);
		if (!side.empty())
		{
			synthArgs.push_back(argc > 0 ? std::string(argv[0]) : std::string("mitiru_host"));
			for (auto& s : side) { synthArgs.push_back(s); }
			for (auto& s : synthArgs) { synthPtr.push_back(s.data()); }
			useArgc = static_cast<int>(synthPtr.size());
			useArgv = synthPtr.data();
		}
	}

	CliArgs args = parseArgs(useArgc, useArgv);
	if (!args.parseError.empty())
	{
		std::fprintf(stderr, "mitiru_host: %s\n", args.parseError.c_str());
		return 2;
	}
	if (args.helpRequested) { printUsage(args.helpAllRequested); return args.dllPath.empty() ? 1 : 0; }
	if (!args.unknownOption.empty())
	{
		std::fprintf(stderr, "mitiru_host: 未知の引数: %s (--help で一覧)\n",
		             args.unknownOption.c_str());
		return 1;
	}
	// ADR 0035 O5: Engine_Http.hpp::runReplayGate が同名 env を読む (未指定なら
	// tests/replay_golden 既定のまま)。
	if (!args.replayGateDir.empty())
	{
#ifdef _WIN32
		_putenv_s("MITIRU_REPLAY_GATE_DIR", args.replayGateDir.c_str());
#else
		setenv("MITIRU_REPLAY_GATE_DIR", args.replayGateDir.c_str(), 1);
#endif
	}

#ifdef _WIN32
	// --appid: window / CEF 生成より前 (最初期) に設定しないと taskbar 分離が効かない。
	if (!args.appId.empty()) { applyAppUserModelId(args.appId); }
#endif

	// --record と --replay-test は排他 (replay 側が record callback を上書きし、
	// 録画が黙って無効化されるため。併用の意図は成立しない)。
	if (!args.recordPath.empty() && !args.replayPath.empty())
	{
		std::fprintf(stderr,
		             "mitiru_host: --record と --replay-test は併用できません (replay 中は録画されません)\n");
		return 1;
	}

	// --state-diff A B: DLL 不要。2 つの .mtrr の GameMemory blob を byte 比較し、
	// 「同入力・異コードでどの frame から分岐したか」を 1 行 JSON で報告する。
	if (!args.stateDiffA.empty() && !args.stateDiffB.empty())
	{
		const auto d = mitiru::replay::Player::diffState(args.stateDiffA, args.stateDiffB);
		if (d.totalFrames == 0)
		{
			std::fprintf(stderr, "mitiru_host: --state-diff: 比較不能 (header/形式エラー or 空録画)\n");
			return 2;
		}
		if (d.diverged)
		{
			std::fprintf(stdout, "{\"diverged\":true,\"firstDivergentFrame\":%u,\"totalFrames\":%u}\n",
			             d.firstDivergentFrame, d.totalFrames);
			return 1;
		}
		std::fprintf(stdout, "{\"diverged\":false,\"totalFrames\":%u}\n", d.totalFrames);
		return 0;
	}

	std::error_code ec;
	if (!std::filesystem::exists(args.dllPath, ec) || ec)
	{
		// 相対パスは cwd で見つからなければ exe の隣も探す (exe と別ディレクトリから
		// 手で `mitiru_host.exe ..\build\Game.dll` を叩くケースに対応、E9)。
		bool resolved = false;
#ifdef _WIN32
		if (args.dllPath.is_relative())
		{
			wchar_t exeBuf[MAX_PATH];
			DWORD   len = GetModuleFileNameW(nullptr, exeBuf, MAX_PATH);
			if (len > 0 && len < MAX_PATH)
			{
				const auto exeDir    = std::filesystem::path(exeBuf).parent_path();
				const auto besideExe = exeDir / args.dllPath;
				if (std::filesystem::exists(besideExe, ec) && !ec)
				{
					args.dllPath = besideExe;
					resolved     = true;
				}
			}
		}
#endif
		if (!resolved)
		{
			std::fprintf(stderr, "mitiru_host: DLL not found: %s (cwd or exe ディレクトリ基準で探索済み)\n",
			             args.dllPath.string().c_str());
			return 2;
		}
	}

	// --bake <in.json> <out.baked>: DLL の reflect/spawner スキーマを使って配置 JSON を POD へ
	// 焼くだけの経路 (★4-1)。window/GPU/CEF は一切初期化しない。game 側が `MITIRU_BAKE_ASSETS()`
	// を export していない (未対応) 場合は理由を出して終了する (host 側で ad-hoc に焼くと DLL の
	// 実際の sizeof(T) とずれる恐れがあるため、必ず DLL 自身にやらせる)。
	if (!args.bakeInJson.empty() && !args.bakeOutPath.empty())
	{
		mitiru::module::ModuleHost bakeHost;
		if (!bakeHost.load(args.dllPath))
		{
			std::fprintf(stderr, "mitiru_host: --bake: DLL load 失敗: %s\n", bakeHost.lastError().c_str());
			return 2;
		}
		const auto bakeFn = bakeHost.bakeAssetsFn();
		if (bakeFn == nullptr)
		{
			std::fprintf(stderr,
				"mitiru_host: --bake: この DLL は MITIRU_BAKE_ASSETS() を export していません (bake 未対応)\n");
			return 2;
		}
		const bool ok = bakeFn(args.bakeInJson.c_str(), args.bakeOutPath.c_str());
		if (!ok)
		{
			std::fprintf(stderr, "mitiru_host: --bake: 焼き込みに失敗しました (%s → %s)\n",
				args.bakeInJson.c_str(), args.bakeOutPath.c_str());
			return 2;
		}
		std::fprintf(stdout, "[mitiru_host] baked: %s -> %s\n", args.bakeInJson.c_str(), args.bakeOutPath.c_str());
		return 0;
	}

	// dev の vfs 相対パス解決の基準 = game DLL の隣 (cwd は exe 位置に固定済みのため、
	// deploy dir と exe dir が違う配置でも "assets/..." が game の assets に届くように)。
	// pack と同じく env 共有。host / game DLL / CEF helper の各インスタンスに効く。
	{
		const auto absDll = std::filesystem::absolute(args.dllPath, ec);
		const std::string rootEnv =
			(ec ? args.dllPath : absDll).parent_path().string();
#ifdef _WIN32
		_putenv_s("MITIRU_ASSET_ROOT", rootEnv.c_str());
#else
		setenv("MITIRU_ASSET_ROOT", rootEnv.c_str(), 1);
#endif
	}

	// 秘匿配布: まず自分の exe に連結されたパックを探し、無ければ DLL の隣の
	// assets.mtpak を使う。exe 連結なら配布物からパックのファイルが消え、exe 1 つに
	// 資産が入る (CEF のランタイムは別途要る)。
	// 実際の open/mount は Engine 側 (`EngineConfig::packPath` → `mountModulePackIfConfigured`、
	// `Engine_Module_Loader.hpp`) に一本化した。host 側で AssetPack::open して
	// vfs::mountGlobal する経路は二重 mount になるため廃止し、host は「どの pack を使うか」
	// の発見 (self-exe 連結 / assets.mtpak 隣接 / --pack 明示) だけを担う。env は
	// `MITIRU_PACK` に統一 (旧 `MITIRU_ASSET_PACK` は Engine 側が後方互換で読むのみ、
	// host からはもう書かない)。
	std::string packedAppUrl;
	std::string resolvedPackPath;  // 見つかった pack の絶対パス。下で cfg.packPath へ渡す
	{
		auto packPath =
			std::filesystem::path(args.dllPath).parent_path() / "assets.mtpak";
		const auto selfExe = std::filesystem::absolute(
			std::filesystem::path(argv[0]), ec);
		if (!ec && mitiru::vfs::AssetPack::open(selfExe).has_value())
		{
			packPath = selfExe;
		}
		if (!args.packOverride.empty())
		{
			// --pack: 自動探索 (自己連結 / assets.mtpak 隣接) より優先する明示指定 (P12)。
			packPath = std::filesystem::path(args.packOverride);
		}
		if (std::filesystem::exists(packPath, ec) && !ec
			&& mitiru::vfs::AssetPack::open(packPath).has_value())
		{
			// cfg.packPath は下で mitiru::EngineConfig 構築後に設定 (このブロックの時点では
			// cfg 未構築)。ここでは発見結果だけ変数へ残す。
			const auto absPack = std::filesystem::absolute(packPath, ec);
			const std::string packEnv = (ec ? packPath : absPack).string();
			resolvedPackPath = packEnv;
#ifdef _WIN32
			_putenv_s("MITIRU_PACK", packEnv.c_str());
#else
			setenv("MITIRU_PACK", packEnv.c_str(), 1);
#endif
			// pack キーは cwd 相対の "<gameDir>/assets/...". CEF の virtualPath を
			// それに合わせるため <gameDir> を cwd からの相対で前置する。
			const auto rel = std::filesystem::relative(
				std::filesystem::path(args.dllPath).parent_path(),
				std::filesystem::current_path(), ec);
			const auto under = (ec || rel.empty())
				? std::filesystem::path(args.dllPath).parent_path() : rel;
			packedAppUrl = "app://" + under.generic_string() + "/assets/scene.html";
			std::fprintf(stdout, "[mitiru_host] asset pack found: %s (mount は Engine 側)\n",
			             packPath.string().c_str());
		}
	}

	mitiru::EngineConfig cfg;
	// P12 1 ファイル配布: 上の発見結果を Engine 側 (mountModulePackIfConfigured) へそのまま渡す。
	// host はもう自分で AssetPack::open/mountGlobal しない (二重 mount 回避、env は MITIRU_PACK に統一)。
	cfg.packPath        = resolvedPackPath;
	cfg.collisionPath   = args.collisionPath;
	cfg.gfxBackend      = args.backend;  // --backend (既定 Auto)。--headless-3d の windowless 経路にも効く
	// --title 未指定なら DLL ファイル名の stem (例 scene3d)。何のゲームか一目で分かる顔つき
	cfg.title           = args.title.empty() ? args.dllPath.stem().string() : args.title;
	cfg.windowWidth     = args.widthOverride  > 0 ? args.widthOverride  : 1280;
	cfg.windowHeight    = args.heightOverride > 0 ? args.heightOverride :  720;
	cfg.windowX         = args.winPosX;   // --window-pos (既定 INT_MIN = OS 任せ)
	cfg.windowY         = args.winPosY;
	// resize 安全: 要求サイズの半分を floor にする (極端な潰れだけ防ぎ、指定サイズは
	// 超えない)。game 窓 / launcher / 760x80 の companion bar を同じ host が起動するので、
	// 固定値でなく要求サイズ基準にして「floor > 指定」で窓が開けない事態を避ける。
	{
		const int floorW = cfg.windowWidth  / 2;
		const int floorH = cfg.windowHeight / 2;
		cfg.minWindowWidth  = floorW > 200 ? floorW : 200;
		cfg.minWindowHeight = floorH > 48  ? floorH : 48;
	}
	cfg.vsync           = true;
	if (args.noPauseUnfocused) { cfg.vsync = false; }  // 背面でもフルレート (present の vsync 待ちを回避)
	if (args.noVsync)          { cfg.vsync = false; }  // --no-vsync: 素のフレームコスト計測 (#53)
	cfg.enableCef       = !args.noCef;   // --no-cef: 完全ネイティブ game は CEF 抜きで軽量起動
	cfg.cefRemoteDebuggingPort = args.cefDebugPort;  // --cef-debug-port: 0 以外で CEF remote debugging を開く
	cfg.timeScale       = args.speed;    // --speed: 固定 dt × N 早回し (#43)
	cfg.errorBannerFile = args.errorFile; // --error-file: mitiru watch のビルドエラー帯 (空=OFF)
	cfg.bugRingSaveRequested = args.bugRingSave; // --bug-ring-save: 起動直後に即保存 (通常は F11)
	if (args.fixedSize) { cfg.windowResizable = false; }   // --fixed-size: リサイズ禁止 (#44)
	if (args.rewindFrames > 0) { cfg.timeTravelBufferFrames = static_cast<std::uint32_t>(args.rewindFrames); }   // --rewind-frames: 巻き戻しバッファ長
	// --rewind-mb: 巻き戻しリングの予算 (MB)。明示指定 (>=0、0=無制限=非圧縮) のときだけ
	// explicit フラグを立てる。未指定 (-1) は engine 側 (recordModuleMemoryFrame) が
	// game の MITIRU_REWIND_BUDGET 宣言 > 既定 512MB の順で決める。
	if (args.rewindMb >= 0)
	{
		cfg.timeTravelBudgetBytes = static_cast<std::size_t>(args.rewindMb) * 1024ull * 1024ull;
		cfg.timeTravelBudgetBytesExplicit = true;
	}
	// --oracle-determinism [N]: P14 決定論オラクル opt-in (既定 OFF、resim ring + memcmp の
	// コストが GameMemory サイズに比例するため明示指定のみ有効化)。
	if (args.oracleDeterminism)
	{
		cfg.oracleDeterminism = true;
		cfg.oracleDeterminismEveryFrames = args.oracleDeterminismEveryFrames;
	}
	// --synctest [K]: oracle-determinism の薄い別名 (K=1 既定で毎フレーム検査)。両方指定時は
	// より狭い間隔 (小さい方) を採用する。
	if (args.synctest)
	{
		cfg.oracleDeterminism = true;
		cfg.oracleDeterminismEveryFrames = cfg.oracleDeterminismEveryFrames == 0
			? args.synctestFrames
			: std::min(cfg.oracleDeterminismEveryFrames, args.synctestFrames);
	}
	cfg.oracleMachineLog = args.oracleLog;  // --oracle-log: mitiru-cli の ScanOracleLines 用
	cfg.saveRoundtripTest = args.saveRoundtripTest;  // --save-roundtrip-test: save→load→save の bit 一致検査
	if (args.headless)                   // --headless: 窓なし自動回し。vsync/CEF を切って最速で (#43)
	{
		cfg.headless  = true;
		cfg.vsync     = false;
		cfg.enableCef = false;
		cfg.deterministic = true;  // 固定 clock で run 間を決定的に (1 host frame = 1 fixed-step)
	}
	if (args.headlessGpu3D)
	{
		// G2: Engine_Init_Lifecycle.hpp の GPU device 生成が見る opt-in 環境変数。
		// EngineConfig にフィールドを足さず (ABI 非対象ファイルのため) 環境変数で渡す。
#ifdef _WIN32
		_putenv_s("MITIRU_HEADLESS_GPU3D", "1");
#else
		setenv("MITIRU_HEADLESS_GPU3D", "1", 1);
#endif
	}
	// EngineHttpServer と AI Lens: --http-port > 0 / --console / 環境変数
	// MITIRU_AI が立ってれば HTTP listen を開始 (127.0.0.1 限定)。MITIRU_AI は AI が zero-config で
	// /api/ai/state・/diff・/branch を叩けるようにする opt-in (port は MITIRU_AI_PORT、既定 8090)。
	const char* aiEnv = std::getenv("MITIRU_AI");
	const bool  aiOptIn = (aiEnv != nullptr && aiEnv[0] != '\0' && std::string{aiEnv} != "0");
	if (args.httpPort > 0 || args.console || aiOptIn)
	{
		cfg.enableHttpApi = true;
		cfg.httpApiPort   = (args.httpPort > 0) ? args.httpPort : 8090;
		if (aiOptIn && args.httpPort <= 0)
		{
			if (const char* aiPort = std::getenv("MITIRU_AI_PORT"); aiPort != nullptr && aiPort[0] != '\0')
			{
				try { cfg.httpApiPort = std::stoi(aiPort); } catch (...) { /* 既定 8090 のまま */ }
			}
		}
	}
	if (args.configOrigins)
	{
		// engine/window を作らず、ここまでに確定した値だけで即終了する (§3-3)。
		printConfigOrigins(args, cfg, resolvedPackPath, aiOptIn);
		return 0;
	}
	// --console: HTTP server は engine.run() 内で起動するため、初回フレームで listen 成功を
	// 確認してからブラウザを開く (init 失敗時は開かない、H-10 と整合)。onFrameStart で消費。
	bool consolePending  = args.console;
	const int consolePort = cfg.httpApiPort;
	// --icon: window は engine.runModule 内で生成されるため初回フレームで適用 (onFrameStart で消費)。
	bool iconPending = !args.iconPath.empty();
	// フォント: 既定で同梱の日本語フォント (PixelMplus) を読み、native draw
	// (drawTextInRect 等) でも日本語が出せる。かなは SDF atlas、漢字は TTF
	// 直描画 fallback なので起動は軽い。最速・純レトロ (8x8 ビットマップ ASCII)
	// で起動したい時は --font none。全漢字を SDF 化したい時は --font japanese。
	using FontAtlas = mitiru::EngineConfig::FontAtlas;
	if (args.fontMode == "none")
	{
		cfg.skipDefaultFont = true;
	}
	else
	{
		cfg.skipDefaultFont = false;
		if      (args.fontMode == "latin")    { cfg.fontAtlasRanges = FontAtlas::Latin; }
		else if (args.fontMode == "japanese") { cfg.fontAtlasRanges = FontAtlas::Japanese; }
		else                                   { cfg.fontAtlasRanges = FontAtlas::Kana; }  // 既定 (空 / "kana")

		// フォントフェイス: 既定 normal = M+ Rounded 1c (普通の丸ゴシック)、
		// retro = PixelMplus (ファミコン風ピクセル)。exe 隣の同梱フォントを解決する。
		const std::string faceFile = (args.fontFace == "retro")
			? "assets/fonts/PixelMplus12-Regular.ttf"
			: "assets/fonts/MPLUSRounded1c-Regular.ttf";
		cfg.fontPath = mitiru::resource::AssetPath::resolve(faceFile);
	}
	// Mitiru Saturn 標準背景。シルバーグレー (#c8c8c8)。エンジン同梱の全 surface
	// (hello_game / launcher / companion) はこのシルバー地に HUD を描き、Saturn の
	// 統一感を出す。別の背景が欲しい game はインスタンス単位で上書きできる。
	cfg.backgroundColor = sgc::Colorf{0.784f, 0.784f, 0.784f, 1.0f};

	// ローファイ・ポストFX: 低解像描画 + パレット量子化 + Bayer ディザ (DX12)
	if (args.loFi)
	{
		cfg.loFi.enabled       = true;
		cfg.loFi.internalWidth = args.loFiW;
		cfg.loFi.internalHeight= args.loFiH;
		cfg.loFi.colorBitsR    = args.loFiBitsR;
		cfg.loFi.colorBitsG    = args.loFiBitsG;
		cfg.loFi.colorBitsB    = args.loFiBitsB;
		cfg.loFi.ditherStrength= args.loFiDither;
		cfg.loFi.softUpscale   = !args.loFiHard;
		cfg.loFi.viFilter      = args.loFiVi;
		cfg.loFi.gamma         = args.loFiGamma;
	}
	cfg.cefStartUrl     = !args.cefUrlOverride.empty()
		? args.cefUrlOverride
		: (!packedAppUrl.empty() ? packedAppUrl : defaultCefUrlFor(args.dllPath));

	// MITIRU_AUTOTEST_FRAMES は autotest の猶予を延ばし、スクショ発火前に CEF が
	// scene.html を読み込む時間を確保する。Engine::run の applyAutoTestEnv() は既定
	// 120 (約 2s) だが、CEF コールドブート時はまだ scene.html が描けていないことがある。
	// HUD オーバーレイを検証するスモークテストでは 600 (約 10s) に上げる。
	if (const char* envFrames = std::getenv("MITIRU_AUTOTEST_FRAMES");
	    envFrames && envFrames[0] != '\0')
	{
		try
		{
			const int n = std::stoi(envFrames);
			if (n > 0)
			{
				cfg.autoTestMode   = true;
				cfg.autoTestFrames = n;
				cfg.autoTestExitAfter = true;
			}
		}
		catch (...) {}
	}

	// onFrameStart: 2 つの常駐ジョブを 1 つのコールバックで処理する。
	//   (1) F12 → スクリーンショット (常時 on、Windows のみ)
	//   (2) --watch 時のみ: DLL mtime を polling して L3 ホットリロード
	// キャプチャした WatcherState はこのスタックフレームに置く。Engine::runModule が
	// ループ終了までブロックするので lifetime は問題ない。
	// --capture-dir/--capture-every (#43): 既定を補完してディレクトリを作る。
	// 片方だけ指定でも有効化（dir 省略→"captures"、every 省略→30）。
	std::string captureDir = args.captureDir;
	int captureEvery = args.captureEvery;
	if (!captureDir.empty() && captureEvery <= 0) { captureEvery = 30; }
	if (captureEvery > 0 && captureDir.empty()) { captureDir = "captures"; }
	const bool captureOn = (captureEvery > 0 && !captureDir.empty());
	// capture 中は実時間 dt を使わず固定 dt にする (Engine::initialize が captureActive を
	// 見て deterministic を強制する)。描画コスト (PNG 保存等) がゲーム内時間に漏れて
	// --input-script のフレーム番号と実時刻がずれるのを防ぐ (G1)。
	cfg.captureActive = captureOn;
	// #53: headless では capture が読むフレームだけ SW ラスタライズする (観測フレーム gating)。
	// capture 無しの自動回しは on-demand のみ (HTTP screenshot 等は 1 フレーム遅れで追従)。
	// CPU ラスタライズはピクセル数比例で重く、これを省くと --speed の早回しが実時間でも速くなる。
	if (args.headless)
	{
		cfg.swRasterizeEvery = captureOn ? captureEvery : 0;
	}
	if (captureOn)
	{
		std::error_code cec;
		std::filesystem::create_directories(captureDir, cec);
		if (cec)
		{
			// 無言起動すると PNG ゼロのまま exit 0 になり得る (DoD の必須経路が消える)。
			std::fprintf(stderr, "mitiru_host: cannot create --capture-dir: %s (%s)\n",
			             captureDir.c_str(), cec.message().c_str());
			return 2;
		}
		std::fprintf(stderr, "[mitiru_host] capture: every %d frame -> %s/\n",
		             captureEvery, captureDir.c_str());
	}
	int captureFrame = 0;   // 経過フレーム数 (onFrameStart クロージャが進める)
	int captureSeq = 0;     // 保存連番
	bool captureSaveFailed = false;  // PNG 保存失敗の初回報告済みフラグ (以降は黙る)
	int totalFrame = 0;     // 総フレーム数 (--max-frames 判定用)
	// PNG エンコード (zlib 圧縮) を専用スレッドへ逃がし、host frame を待たせない (G7)。
	// スコープを抜けるとき (関数 return / 例外) にデストラクタが残キューを書き切って join。
	std::unique_ptr<mitiru::render::AsyncPngWriter> pngWriter;
	if (captureOn) { pngWriter = std::make_unique<mitiru::render::AsyncPngWriter>(); }

	// --perf (#53): onFrameStart 間隔 = 1 host frame の実時間。600 フレームごとに統計を出す。
	struct PerfStats
	{
		std::vector<double> samples;                    // 当ウィンドウのフレーム時間 (ms)
		std::chrono::steady_clock::time_point last{};
		bool hasLast = false;

		void report()
		{
			if (samples.empty()) { return; }
			std::vector<double> s = samples;
			std::sort(s.begin(), s.end());
			double sum = 0.0;
			for (const double v : s) { sum += v; }
			const auto pct = [&s](double p) {
				return s[static_cast<std::size_t>(p * static_cast<double>(s.size() - 1))];
			};
			const double avg = sum / static_cast<double>(s.size());
			std::fprintf(stderr,
				"[mitiru_host] perf: %zu frames  avg %.2f ms (%.1f fps)  p50 %.2f  p95 %.2f  max %.2f\n",
				s.size(), avg, 1000.0 / avg, pct(0.50), pct(0.95), s.back());
			samples.clear();
		}
	};
	PerfStats perfStats;
	// FrameArena (2-1) の使用量を毎フレーム反映する。オーバーレイは既定非表示
	// (F11 相当のトグルは未配線。engine.frameArena() の値を外から見えるようにする配線のみ)。
	mitiru::debug::FrameBudget frameBudget;
	if (args.perf && cfg.vsync)
	{
		std::fprintf(stderr,
			"[mitiru_host] perf: vsync ON のため present 待ちを含みます (素の描画コストは --no-vsync 併用)\n");
	}

	AssetWatchState assetWatch;
	if (!args.watchAssetsDir.empty())
	{
		std::error_code aec;
		assetWatch.dir = std::filesystem::absolute(args.watchAssetsDir, aec);
		if (aec || !std::filesystem::is_directory(assetWatch.dir, aec))
		{
			std::fprintf(stderr, "mitiru_host: --watch-assets のディレクトリが無い: %s\n", args.watchAssetsDir.c_str());
			return 2;
		}
		std::fprintf(stderr, "[mitiru_host] watch-assets: %s (.json/.baked)\n", assetWatch.dir.string().c_str());
	}

	WatcherState watcher;
	if (args.watch)
	{
		watcher.dllPath = args.dllPath;
		// 絶対パスに解決し、cwd 変更後のリロードでもファイルを見つけられるようにする。
		std::error_code rc;
		auto abs = std::filesystem::absolute(args.dllPath, rc);
		if (!rc) { watcher.dllPath = abs; }

		std::fprintf(stderr, "[mitiru_host] watch mode: polling %s\n",
		             watcher.dllPath.string().c_str());
	}

	// time-travel scrub: inspector(timetravel.html → tool_cef)が書く scrub command を
	// 毎フレーム読み、過去フレームの GameMemory へ巻き戻す (click-to-scrub)。
	// reader は host 側 = rewind は host の責務 (game DLL は pure を保つ)。適用は
	// ScrubApplyListener (IFrameListener) に移してあり、ここでは登録するだけ (1-6)。
	ScrubApplyListener scrubListener;

	// ドッキング: ツール窓 (シークバー等) が吸着・追従できるよう、自窓の画面矩形を broadcast する。
	mitiru::observe::DockWriter dockWriter{mitiru::observe::detail::scrubThisPid()};
	int dockLastX = INT_MIN, dockLastY = INT_MIN, dockLastW = 0, dockLastH = 0;

	// --pause-control (録画支援): ファイルが "1" の間だけ engine を pause (dt=0, 描画継続)。
	// フォーカス不要・scrub-control と同じ思想。自動録画で「編集中は静止」を作るのに使う。
	const std::string pauseControlFile = args.pauseControl;
	int pauseControlTick = 0;
	char pauseControlLast = '\0';   // 前回読み値 (変化時のみ setPaused = F8/HTTP pause と共存)

	// --ghost (10-1): .mtrr を開くだけここで済ませる (module load は engine 構築後、
	// 下の方で行う)。onFrameStart のクロージャがこの3つを参照で捕まえる。
	mitiru::replay::Player ghostPlayer;
	bool                   ghostActive = false;
	bool                   ghostEof    = false;
	if (!args.ghostPath.empty())
	{
		if (!ghostPlayer.open(args.ghostPath))
		{
			std::fprintf(stderr, "mitiru_host: cannot open --ghost file: %s\n  理由: %s\n",
			             args.ghostPath.c_str(), playerErrorName(ghostPlayer.lastError()));
			return 2;
		}
	}

	cfg.onFrameStart = [&watcher, watchOn = args.watch, &assetWatch,
	                    &ghostPlayer, &ghostActive, &ghostEof,
	                    captureOn, captureEvery, captureDir, &captureFrame, &captureSeq,
	                    &captureSaveFailed, &pngWriter,
	                    maxFrames = args.maxFrames, &totalFrame,
	                    perfOn = args.perf, &perfStats, &frameBudget,
	                    &pauseControlFile, &pauseControlTick, &pauseControlLast,
	                    &consolePending, consolePort,
	                    &iconPending, iconPath = args.iconPath,
	                    &dockWriter, &dockLastX, &dockLastY, &dockLastW, &dockLastH]
	                   (mitiru::Engine& engine)
	{
		pollAssetWatch(assetWatch, engine);

		// --icon: window 生成後の初回フレームで一度だけ適用 (headless では no-op)。
		if (iconPending)
		{
			iconPending = false;
			engine.setWindowIcon(iconPath);
		}

		// --perf (#53): 前回 onFrameStart からの実時間 = 1 host frame のコスト。
		if (perfOn)
		{
			const auto now = std::chrono::steady_clock::now();
			if (perfStats.hasLast)
			{
				perfStats.samples.push_back(
					std::chrono::duration<double, std::milli>(now - perfStats.last).count());
				if (perfStats.samples.size() >= 600) { perfStats.report(); }
			}
			perfStats.last = now;
			perfStats.hasLast = true;
		}

		pollHostHotkeys(engine);

		// FrameArena (2-1): 前フレームの使用量をオーバーレイ用に反映する
		// (reset は次の tickOneFrame 先頭で起きるため、ここで読むのは直前フレームの値)。
		frameBudget.setArenaUsage(engine.frameArena().used(), engine.frameArena().capacity());

		// --ghost (10-1): 1 host frame につき .mtrr から InputSnapshot を 1 件消費して進める。
		// EOF に達したら以後は読まない (ghost は直前の GameMemory のまま静止して描かれ続ける)。
		if (ghostActive && !ghostEof)
		{
			mitiru::module::InputSnapshot gsnap{};
			std::uint32_t                 gidx = 0;
			if (ghostPlayer.readNext(gsnap, gidx)) { engine.stepGhost(gsnap); }
			else { ghostEof = true; }
		}

		// --console: 初回フレームで HTTP listen を確認してからブラウザを開く (init 失敗時は開かない)。
		if (consolePending)
		{
			consolePending = false;
			if (engine.httpServer() != nullptr)
			{
				const std::string url = "http://127.0.0.1:" + std::to_string(consolePort) + "/";
				std::fprintf(stderr, "[mitiru_host] control panel: %s (opening default browser)\n",
				             url.c_str());
#ifdef _WIN32
				ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
			}
			else
			{
				std::fprintf(stderr,
				             "[mitiru_host] --console: HTTP API が起動していないためブラウザを開きません\n");
			}
		}

		// --pause-control: 数フレームごとにファイルを見て pause 状態を反映 (録画で編集中を静止させる)。
		// 値が前回読みから変化した時だけ上書きする (F8 / HTTP pause を毎 tick 潰さない)。
		if (!pauseControlFile.empty() && (++pauseControlTick % 4 == 0))
		{
			std::ifstream pf(pauseControlFile);
			if (pf)
			{
				char c = 0;
				pf.get(c);
				if (c != pauseControlLast)
				{
					pauseControlLast = c;
					engine.setPauseKind(mitiru::EngineConfig::kPauseKindDebug);
					engine.setPaused(c == '1');
				}
			}
		}

		// listener 群 (scrub 適用等) は Engine_Frame.hpp の fixed-step ループが呼ぶ
		// (1-6)。host からここで重ねて呼ぶと二重発火になるため呼ばない。

		// ドック窓の追従用: 自窓の画面矩形が変わったら broadcast する (ツール窓が読んで付いてくる)。
		{
			int wx = 0, wy = 0, ww = 0, wh = 0;
			if (engine.gameWindowRect(wx, wy, ww, wh) &&
			    (wx != dockLastX || wy != dockLastY || ww != dockLastW || wh != dockLastH))
			{
				dockLastX = wx; dockLastY = wy; dockLastW = ww; dockLastH = wh;
				const long long hwnd = engine.window()
					? static_cast<long long>(engine.window()->nativeHandle()) : 0;
				dockWriter.write({{"x", wx}, {"y", wy}, {"w", ww}, {"h", wh},
				                  {"hwnd", hwnd}, {"active", true}, {"min", false}});
			}
		}

		// --max-frames (#43): 指定フレーム数に達したら停止を要求 (headless 自動回しの終了条件)。
		// カウント自体は maxFrames 未指定でも進める (E10: headless 終了時の frames 要約に使う)。
		++totalFrame;
		if (maxFrames > 0 && totalFrame > maxFrames) { engine.requestStop(); }

		// --capture-every (#43): N フレームごとに直近フレームを PNG 連番で吐く。
		// onFrameStart は描画前なので「前フレームの提示結果」を保存する (AI 視覚検証には十分)。
		if (captureOn && (captureFrame++ % captureEvery == 0))
		{
			const int w = engine.captureWidth();
			const int h = engine.captureHeight();
			auto rgba = engine.capture();
			if (w > 0 && h > 0 && !rgba.empty() && pngWriter)
			{
				char name[32];
				std::snprintf(name, sizeof(name), "frame_%06d.png", captureSeq++);
				std::string path = captureDir + "/" + name;
				pngWriter->enqueue(path, std::move(rgba), w, h);
				// 失敗は非同期に分かるため、フレーム側では「初回検出時」に報告する
				// (エンコード自体は 1 フレーム以上遅れて終わる)。
				if (pngWriter->hadError() && !captureSaveFailed)
				{
					captureSaveFailed = true;   // 初回のみ報告 (毎フレーム spam しない)
					std::fprintf(stderr, "mitiru_host: capture PNG save failed: %s\n",
					             pngWriter->lastErrorPath().c_str());
				}
			}
		}

		if (!watchOn) { return; }
		if (++watcher.pollTick < watcher.pollEvery) { return; }
		watcher.pollTick = 0;

		std::error_code mtimeEc;
		const auto mtime =
			std::filesystem::last_write_time(watcher.dllPath, mtimeEc);
		if (mtimeEc) { return; }

		if (!watcher.initialized)
		{
			watcher.lastMtime   = mtime;
			watcher.initialized = true;
			return;
		}
		if (mtime > watcher.lastMtime)
		{
			watcher.lastMtime = mtime;
			std::fprintf(stderr, "[mitiru_host] DLL changed — reloading\n");
			const bool ok = engine.reloadModule(watcher.dllPath);
			if (!ok)
			{
				std::fprintf(stderr,
					"[mitiru_host] reload FAILED — 旧コードのまま継続します (状態は保持)。\n");
				const std::string why = engine.moduleLoadError();
				if (!why.empty())
				{
					std::fprintf(stderr, "  理由: %s\n", why.c_str());
				}
				std::fprintf(stderr,
					"  ビルドエラー等を修正して保存すれば自動で再試行します。\n");
			}
			else
			{
				std::fprintf(stderr, "[mitiru_host] reload OK\n");
			}
		}
	};

	mitiru::Engine engine;
	engine.addFrameListener(&scrubListener);
	engine.setSuppressToolWindows(args.noToolWindows);  // --no-tool-windows: 録画/CI でツール窓を出さない
	engine.setToolWindowPos(args.toolWinX, args.toolWinY);  // --tool-window-pos: 観察窓も実画面に出さない
	// 自動実行 (script 駆動 / replay / headless / capture) では game の wantMouseLock を
	// OS へ適用しない。画面外ウィンドウが実カーソルを掴む事故を防ぐ
	engine.setAllowCursorCapture(args.inputScript.empty() && args.replayPath.empty()
	                             && !args.headless && args.captureDir.empty());

	// SoundIntents を実際に鳴らすため audio engine を接続する。id は
	// game の配置先 assets/audio/ ディレクトリ (DLL の隣) に対して解決する。
	// headless (自動テスト / AI 回し) では音が不要かつ #52 の音声スレッド競合を避けるため
	// audio engine を作らない (sound intent は no-op、決定的 sim には無影響)。
	if (!args.headless)
	{
		const auto audioDir =
			std::filesystem::path(args.dllPath).parent_path() / "assets" / "audio";
		engine.setAudioEngine(std::make_shared<FileAudioEngine>(audioDir));
	}

	// ── replay-as-test (軸 4) ──────────────────────────────────────────
	// --record: 毎フレームの InputSnapshot と game が push した view.* 状態を .mtrr に
	//   追記する。--replay-test: .mtrr をヘッドレスで再投入し DLL に bit-exact 再現させ、
	//   最終 view.* 状態を検証する。
	mitiru::replay::Recorder recorder;
	mitiru::replay::Player   player;
	std::uint32_t            frameIdx = 0;

	if (!args.recordPath.empty())
	{
		// header の seed と毎フレーム snapshot の rngSeed を一致させる。
		if (!recorder.open(args.recordPath, cfg.randomSeed))
		{
			std::fprintf(stderr, "mitiru_host: cannot open record file: %s\n",
			             args.recordPath.c_str());
			return 2;
		}
		recorder.writeEnvTag(buildRecordEnvTag(cfg.gfxBackend));
		std::fprintf(stderr, "[mitiru_host] recording → %s (--record-state-every %d)\n",
		             args.recordPath.c_str(), args.recordStateEvery);
		// --record-state-every N: state blob (GameMemory 全体) は N フレームごとにだけ書く
		// (0 = 毎フレーム = 従来)。1 MB/F で 1.4ms、8 MB で 12.5ms かかる録画コストを削るため。
		// stateLen=0 のフレームは v4 format 上も合法 (既存 Player が既に許容する) なので
		// format version は上げない。save/load intent が絡むフレームだけは周期を無視して
		// 必ず state を書く (replay 側の save/load 代用が周期の谷間で失敗しないように)。
		const int stateEvery = args.recordStateEvery;
		cfg.onModuleFrameRecorded =
			[&recorder, &frameIdx, &engine, stateEvery](const mitiru::module::InputSnapshot& snap,
			                                            const mitiru::module::FrameIntents& fi)
			{
				const bool wantState = stateEvery <= 0 ||
					(static_cast<int>(frameIdx) % stateEvery) == 0 ||
					fi.loadRequest != 0 || fi.saveRequest != 0;

				const std::uint32_t memSize = engine.moduleMemorySize();
				const void*         mem     = engine.moduleMemory();
				if (!wantState)
				{
					recorder.record(frameIdx++, snap, nullptr, 0);
				}
				else if (memSize > 0 && mem != nullptr)
				{
					// GameMemory が申告されていれば「唯一の state」を opaque にそのまま記録する
					// (bit-exact diffState 用)。未申告 (v≤8) は観測 view.* JSON にフォールバック。
					recorder.record(frameIdx++, snap, mem, memSize);
				}
				else
				{
					std::string blob;
					if (auto* store = engine.moduleStateStore()) { blob = store->snapshotJson(); }
					recorder.record(frameIdx++, snap, blob.data(),
					                static_cast<std::uint32_t>(blob.size()));
				}
			};
	}

	// --input-record (#45): 実プレイの入力エッジを input-script 形式で書き出す。
	// `--input-script` で再生でき、#43 の headless+capture と組めば完全自動回帰テストになる。
	// 既存の onModuleFrameRecorded (--record) があれば chain する。lifetime は runModule 内。
	std::ofstream inputRecOut;
	std::uint32_t inputRecFrame = 0;
	if (!args.inputRecordPath.empty())
	{
		inputRecOut.open(args.inputRecordPath, std::ios::binary);
		if (!inputRecOut)
		{
			std::fprintf(stderr, "mitiru_host: cannot open --input-record file: %s\n",
			             args.inputRecordPath.c_str());
			return 2;
		}
		inputRecOut << "# mitiru input-script (--input-record). 形式: <frame> <KEY> <down|up>\n";
		std::fprintf(stderr, "[mitiru_host] input recording → %s\n", args.inputRecordPath.c_str());
		auto prev = cfg.onModuleFrameRecorded;   // --record と併用時は chain
		cfg.onModuleFrameRecorded =
			[&inputRecOut, &inputRecFrame, prev](const mitiru::module::InputSnapshot& snap,
			                                     const mitiru::module::FrameIntents& fi)
			{
				if (prev) { prev(snap, fi); }
				for (int vk = 0; vk < 256; ++vk)
				{
					if (snap.keysJustPressed[vk])
						inputRecOut << inputRecFrame << ' ' << vkToName(vk) << " down\n";
					if (snap.keysJustReleased[vk])
						inputRecOut << inputRecFrame << ' ' << vkToName(vk) << " up\n";
				}
				++inputRecFrame;
			};
	}

	// GameMemory 再現検証 (flat POD game のみ)。replay 中に on_update 後の
	// live GameMemory を記録値と byte 照合し、単一 state channel を test oracle にする。
	std::vector<std::uint8_t> recordedMem;
	std::uint32_t replayFrame     = 0;
	std::uint32_t memDivergeFrame = 0;
	bool          memDiverged     = false;
	bool          memCompared     = false;
	bool          memSizeMismatch = false;   // 録画時と GameMemory サイズが違う (struct 変更)
	std::size_t   memSizeRecorded = 0;
	std::size_t   memSizeCurrent  = 0;
	bool          frameHasRecord  = false;  // この frame に対応する記録 state を読めたか (EOF frame 除外)
	bool          keyframeRestored = false; // P1: bug_*.mtrr の frame 0 キーフレームを初回だけ復元したか
	// P1: `observe::saveBugRing` (Oracle.hpp) が書く bug_*.mtrr は frame 0 の state blob に
	// 「ring 最古の GameMemory を post-update 状態として積んだキーフレーム」を乗せる (通常の
	// --record-state-every による周期スナップショットと wire format 上は区別できないため、
	// 命名規約 (既定 prefix "bug_") で判定する)。
	bool          isBugRingReplay = !args.replayPath.empty() &&
		std::filesystem::path(args.replayPath).filename().string().rfind("bug_", 0) == 0;
	bool          loadSubstFailed = false;  // replay 中の load 代用が不能 (blob 無し録画)
	std::uint32_t loadSubstFailFrame = 0;
	std::string   memDivergeDiff;           // divergence 時の field 単位 diff JSON (MITIRU_REFLECT 済みなら)
	std::string   memDivergeBlame;          // divergence byte を最後に書いた phase 名 (`mitiru why` opt-in game のみ)
	std::string   finalReflect;             // 終端時の reflect 状態 JSON (fuzz の不変条件チェック用)
	mitiru::module::InputSnapshot lastRec{}; // --replay (GUI): EOF 後に直前入力を維持してフリーズする用

	if (!args.replayPath.empty())
	{
		const bool opened = player.open(args.replayPath);
		// リネームされた bug ring も envTag の印で見分ける (prefix は人間向けの目印でしかない)
		if (opened && player.recordedEnvTag().rfind("bugring|", 0) == 0) { isBugRingReplay = true; }
		if (!opened)
		{
			std::fprintf(stderr, "mitiru_host: cannot open replay file: %s\n  理由: %s\n",
			             args.replayPath.c_str(), playerErrorName(player.lastError()));
			if (player.lastError() == mitiru::replay::PlayerError::FrameSizeMismatch)
			{
				// 別 ABI 世代の録画は再生不能 (InputSnapshot layout が違う)。黙って
				// 途中破綻させず、記録時 ABI を添えて入口で拒否する。header の値は
				// wire version (build 指紋入り)。表示は数値 ABI 番号へ分解する。
				const std::string recAbi = player.recordedAbiVersion() > 0
					? "v" + std::to_string(mitiru::module::wireAbiNumber(
						static_cast<std::uint32_t>(player.recordedAbiVersion())))
					: std::string{"不明"};
				std::fprintf(stderr,
				             "  この録画は現在のエンジンと互換性のない世代で録られています (記録時 %s, frame %u bytes / "
				             "現 host v%u, frame %zu bytes)。\n"
				             "  対処: 現バージョンで --record して録り直してください。\n",
				             recAbi.c_str(),
				             player.recordedFrameSize(),
				             mitiru::module::kCurrentApiVersion,
				             sizeof(mitiru::module::InputSnapshot));
			}
			else if (player.lastError() == mitiru::replay::PlayerError::VersionMismatch)
			{
				std::fprintf(stderr,
				             "  この .mtrr は非対応の format version です。--record で録り直してください。\n");
			}
			return 2;
		}
		if (!args.replayGui)
		{
			cfg.enableCef = false;   // ヘッドレス決定的再実行
			cfg.headless  = true;
		}
		cfg.swRasterizeEvery = 0;  // 照合は GameMemory のみで pixels は読まない (#53)
		cfg.moduleInputOverride =
			[&player, &engine, &recordedMem, &frameHasRecord, &finalReflect, &lastRec,
			 &keyframeRestored, isBugRingReplay, guiReplay = args.replayGui]
			(mitiru::module::InputSnapshot& snap) -> bool
			{
				const auto onEof = [&]() -> bool
				{
					frameHasRecord = false;
					// module 生存中に最終 reflect 状態を捕捉 (ループ後は解放され得る)
					finalReflect = engine.reflectBlobJson(engine.moduleMemory());
					if (guiReplay)
					{
						// --replay (GUI): 自動終了せず、直前入力を維持したまま pause して
						// 最後のフレームで止まる (デモ撮影・目視確認用)。
						engine.setPaused(true);
						snap = lastRec;
						return true;
					}
					engine.requestStop();   // --replay-test: EOF → ヘッドレスループを終了
					return false;
				};

				std::uint32_t fidx = 0;
				mitiru::module::InputSnapshot rec{};
				if (!player.readNextWithState(rec, recordedMem, fidx))  // 記録 GameMemory を退避
				{
					return onEof();
				}

				// P1: bug_*.mtrr の frame 0 は「on_update 適用済み」の状態を積んだキーフレームで、
				// rec (frame 0 の入力) は既にその状態へ反映済み。素直に on_update へ回すと
				// 二重適用になるので、キーフレームを moduleMemory へ書き戻したうえで frame 0 は
				// 消費済み扱いにし、frame 1 の入力を代わりに読んで返す (以後は通常の replay と同じ)。
				if (!keyframeRestored)
				{
					keyframeRestored = true;
					if (isBugRingReplay && !recordedMem.empty() &&
						recordedMem.size() == static_cast<std::size_t>(engine.moduleMemorySize()))
					{
						engine.rewindModuleMemory(recordedMem.data(),
							static_cast<std::uint32_t>(recordedMem.size()));
						std::uint32_t fidxNext = 0;
						mitiru::module::InputSnapshot recNext{};
						if (!player.readNextWithState(recNext, recordedMem, fidxNext))
						{
							// キーフレームのみで後続入力が無い異常な録画。EOF 扱いで終了する。
							return onEof();
						}
						rec = recNext;
					}
				}

				frameHasRecord = true;
				snap = rec;
				lastRec = rec;
				return true;
			};
		// on_update 後の live GameMemory を退避した記録値と照合する (frame 整合済み)。
		// EOF frame は記録対応が無いので比較しない (stale な recordedMem との誤検出を防ぐ)。
		cfg.onModuleFrameRecorded =
			[&engine, &recordedMem, &replayFrame, &memDivergeFrame, &memDiverged, &memCompared,
			 &frameHasRecord, &memSizeMismatch, &memSizeRecorded, &memSizeCurrent, &memDivergeDiff,
			 &memDivergeBlame]
			(const mitiru::module::InputSnapshot&, const mitiru::module::FrameIntents&)
			{
				if (!frameHasRecord) { return; }
				const std::uint32_t memSize = engine.moduleMemorySize();
				const void*         mem     = engine.moduleMemory();
				if (memSize > 0 && mem != nullptr &&
				    recordedMem.size() == static_cast<std::size_t>(memSize))
				{
					memCompared = true;
					if (!memDiverged && std::memcmp(mem, recordedMem.data(), memSize) != 0)
					{
						memDiverged     = true;
						memDivergeFrame = replayFrame;
						// どの field が録画値から変わったか (divergence report)。
						memDivergeDiff  = engine.reflectDiffBlobs(recordedMem.data(), mem);
						// `mitiru why`: 最初の差異 byte を最後に書いた phase を game へ問い合わせる
						// (opt-in game のみ。mitiru_why_blame_at を export していなければ空のまま)。
						const auto* pm = static_cast<const std::uint8_t*>(mem);
						for (std::uint32_t i = 0; i < memSize; ++i)
						{
							if (pm[i] != recordedMem[i])
							{
								if (const char* ph = engine.queryModuleWriteBlame(i)) { memDivergeBlame = ph; }
								break;
							}
						}
					}
				}
				else if (memSize > 0 && !recordedMem.empty() &&
				         recordedMem.size() != static_cast<std::size_t>(memSize))
				{
					// サイズ不一致 = GameMemory struct が録画時から変更された。黙って
					// スキップすると false-green (検証ゼロで exit 0) になるため明示 FAIL へ。
					memSizeMismatch = true;
					memSizeRecorded = recordedMem.size();
					memSizeCurrent  = memSize;
				}
				++replayFrame;
			};
		// replay 中の load intent はファイルを読まず、当該フレームの記録済み GameMemory blob
		// で代用する。録画後にセーブファイルが上書きされても bit-exact が保たれる。
		// blob 無し録画 (旧 .mtrr / memorySize=0) では代用不能 → 明示 FAIL (A3 と同じ思想)。
		engine.setSaveLoadOverride(
			[&engine, &recordedMem, &replayFrame, &frameHasRecord,
			 &loadSubstFailed, &loadSubstFailFrame](const char* /*slot*/) -> bool
			{
				// EOF 後のフレーム (記録対応なし) は検証対象外。何も適用せずスキップ。
				if (!frameHasRecord) { return true; }
				const std::uint32_t memSize = engine.moduleMemorySize();
				if (memSize == 0 ||
				    recordedMem.size() != static_cast<std::size_t>(memSize))
				{
					if (!loadSubstFailed)
					{
						loadSubstFailed    = true;
						loadSubstFailFrame = replayFrame;
					}
					engine.requestStop();
					return true;  // replay 中は失敗してもファイル load にフォールバックしない
				}
				(void)engine.rewindModuleMemory(recordedMem.data(), memSize);
				return true;
			});
	}

	// record と replay はどちらも固定 dt (1/targetTps) で走らせ、dt 列を一致させる
	// → sim が bit-exact 再現する (timer 駆動の状態も含む)。
	if (!args.recordPath.empty() || !args.replayPath.empty())
	{
		cfg.deterministic = true;
	}

	// --ghost (10-1): .mtrr は既に上で開いてある。ここで module だけ load する
	// (同じ DLL のもう1本の GameMemory。--replay と組み合わせれば ghost vs live の同時再生)。
	if (ghostPlayer.isOpen())
	{
		if (!engine.loadGhostModule(args.dllPath))
		{
			std::fprintf(stderr, "mitiru_host: --ghost: モジュール load に失敗しました "
			                     "(通常の DLL load と同じ ABI 制約が適用されます)\n");
			return 2;
		}
		ghostActive = true;
		std::fprintf(stderr, "[mitiru_host] ghost replay <- %s\n", args.ghostPath.c_str());
	}

#ifdef _WIN32
	std::fprintf(stderr,
		"[mitiru_host] hotkeys: F7=step F8=pause/play F9=time-scale F10=lofi F11=bug-ring-save F12=screenshot+clipboard\n");
#endif

	// ── デバッグ独立窓のオプトイン (アトミックツール哲学) ──────────────
	// 「このデバッグ機能を使いたい」と host を書く人が決めた窓だけ開く。要らなければ
	// 何も書かない = 何も出ない (pulled UI)。game のキー入力とは無関係に host 側で制御。
	//
	//   例: 状態 inspector を常に開きたい host にするなら、ここに直接書く:
	//       mitiru::debug::openTool(mitiru::Tool::Inspector);
	//       mitiru::debug::openTool(mitiru::Tool::Rewind);
	//
	// この参照 host では CLI (--inspect <name>) で選べるようにしてある。新しいツール窓は
	// ToolRegistry.hpp の kToolTable に 1 行足せば、ここの呼び出しはそのまま使える。
	for (std::size_t k = 0; k < args.openTools.size(); ++k)
	{
		// producerPid=0 → この host プロセスを監視。extraArgs は "--page scene?tab=memory" のような上書き
		const std::string& extra = k < args.openToolArgs.size() ? args.openToolArgs[k] : std::string{};
		mitiru::debug::openTool(args.openTools[k], extra);
	}

	// --input-script (#43-1): in-process でキーを注入する。replay (--replay) 使用時は
	// そちらの moduleInputOverride が優先なので設定しない。runModule がブロックするので
	// scriptPlayer の lifetime はこのスタックフレームで足りる。
	InputScriptPlayer scriptPlayer;
	bool scriptLoaded = false;
	if (args.replayPath.empty() && !args.inputScript.empty())
	{
		scriptLoaded = loadInputScript(args.inputScript, scriptPlayer);
		if (scriptLoaded)
		{
			std::fprintf(stderr, "[mitiru_host] input-script: %zu events from %s\n",
			             scriptPlayer.events.size(), args.inputScript.c_str());
		}
		else
		{
			std::fprintf(stderr, "mitiru_host: cannot open --input-script: %s\n",
			             args.inputScript.c_str());
		}
	}

	// --state-trace: 毎フレーム reflect 状態を JSONL で追記する。reflectBlobJson は offset read
	// のみ = non-POD GameMemory (std::vector 等) でも安全 (recording/ring と違い memcpy しない)。
	// replay 時は別経路なので無効。Stage Doctor 等が「解の軌跡/HP 推移」を後で解析できる。
	std::ofstream stateTraceOut;
	if (args.replayPath.empty() && !args.stateTrace.empty())
	{
		stateTraceOut.open(args.stateTrace, std::ios::trunc);
		std::fprintf(stderr, stateTraceOut ? "[mitiru_host] state-trace -> %s\n"
		                                   : "mitiru_host: cannot open --state-trace: %s\n",
		             args.stateTrace.c_str());
	}

	// script 注入 か trace のどちらかが要るとき per-frame override を仕込む。
	if (args.replayPath.empty() && (scriptLoaded || stateTraceOut.is_open()))
	{
		const std::string freezeFile = args.inputFreezeControl;
		cfg.moduleInputOverride =
			[&scriptPlayer, &engine, &stateTraceOut, scriptLoaded, freezeFile, frzTick = 0, frozen = false]
			(mitiru::module::InputSnapshot& snap) mutable -> bool
			{
				if (scriptLoaded)
				{
					scriptPlayer.apply(snap);   // 実キーボードを上書き (注入のみ有効)
					// --input-freeze-control: "1" の間は入力を全消し → プレイヤー静止。
					if (!freezeFile.empty())
					{
						if (++frzTick % 4 == 0)
						{
							std::ifstream pf(freezeFile);
							if (pf) { char c = 0; pf.get(c); frozen = (c == '1'); }
						}
						if (frozen)
						{
							for (int v = 0; v < 256; ++v)
							{
								snap.keysDown[v] = 0; snap.keysJustPressed[v] = 0; snap.keysJustReleased[v] = 0;
							}
						}
					}
				}
				// このフレームの reflect 状態を 1 行追記 (offset read = non-POD でも安全)。
				if (stateTraceOut.is_open())
				{
					stateTraceOut << engine.reflectBlobJson(engine.moduleMemory()) << '\n';
				}
				return scriptLoaded;
			};
	}

	const auto runStart = std::chrono::steady_clock::now();
	if (!engine.runModule(args.dllPath, cfg))
	{
		// module load 失敗 (MITIRU_GAME 入口無し等)。理由は runModule が stderr に出済み。
		// 非ゼロで返すとランチャー .bat が pause してユーザがエラーを読める。
		return 3;
	}

	// --headless: hotkeys 行だけでは何フレーム動いたか分からない (E10)。終了時に要約を 1 行出す。
	if (args.headless && totalFrame > 0)
	{
		const double elapsedMs =
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - runStart).count();
		std::fprintf(stderr, "[mitiru_host] headless: %d frames, avg %.2f ms/frame\n",
		             totalFrame, elapsedMs / totalFrame);
	}

	// --perf: 端数ウィンドウの統計を出してから終了処理へ (#53)。
	if (args.perf) { perfStats.report(); }

	// Replay 検証: 観測可能な最終状態を出力する。--expect 指定時はキー単位で diff し、
	// 不一致があれば非ゼロ終了する (CI リグレッションゲート)。--replay (GUI) は verdict 対象外。
	if (!args.replayPath.empty() && !args.replayGui)
	{
		std::string finalState = "{}";
		if (auto* store = engine.moduleStateStore()) { finalState = store->snapshotJson(2); }
		// 録画環境 (envTag) を検証結果に混ぜる。他機で再現しない差分の切り分けに使う
		// (v4 録画は recordedEnvTag() が空文字を返す)。game の JSON が壊れていたら諦める。
		try
		{
			nlohmann::json j = nlohmann::json::parse(finalState);
			j["envTag"] = player.recordedEnvTag();
			finalState  = j.dump(2);
		}
		catch (const std::exception&) {}
		std::fprintf(stdout, "%s\n", finalState.c_str());

		// --json: verdict を stdout に 1 行 JSON でも出す (CLI 側の正規表現パース置換用、E4)。
		// 既存の stderr テキスト行はそのまま残す (人間が読む用途/既存ツールの後方互換)。
		auto emitJsonVerdict = [&](bool pass, const char* reason)
		{
			if (!args.jsonOutput) { return; }
			nlohmann::json v;
			v["verdict"]        = pass ? "PASS" : "FAIL";
			v["reason"]         = reason;
			v["framesCompared"] = player.framesRead();
			v["totalFrames"]    = player.totalFrames();
			if (!pass && reason == std::string_view{"diverged"})
			{
				v["divergedAtFrame"] = memDivergeFrame;
				if (!memDivergeDiff.empty() && memDivergeDiff != "[]")
				{
					try { v["diff"] = nlohmann::json::parse(memDivergeDiff); }
					catch (const std::exception&) { v["diff"] = memDivergeDiff; }
				}
				if (!memDivergeBlame.empty()) { v["blame"] = memDivergeBlame; }
			}
			std::fprintf(stdout, "%s\n", v.dump().c_str());
		};

		// replay 中の load 代用不能。検証ゼロのまま exit 0 にしない (false-green 防止)。
		if (loadSubstFailed)
		{
			std::fprintf(stderr,
			             "replay state: FAIL — load intent at frame %u: 録画にゲームの状態が"
			             "保存されておらず、セーブ読込を再現できません (古い形式の録画)。\n"
			             "  対処: game が memorySize を申告しているか確認し、現バージョンで --record して録り直してください。\n",
			             loadSubstFailFrame);
			emitJsonVerdict(false, "load_intent_unavailable");
			return 1;
		}

		// GameMemory 再現の verdict (flat POD game のみ)。bit-exact なら軸④ 構造保証の証明。
		if (memSizeMismatch)
		{
			// 比較ゼロのまま exit 0 すると「検証されてないのに成功」に見える (false-green)。
			std::fprintf(stderr,
			             "replay state: FAIL — state size mismatch (recorded %zu bytes, "
			             "current %zu bytes)\n"
			             "  ゲームの状態の形 (サイズ) が録画時から変更されています。この録画では検証できません。\n"
			             "  対処: --record で基準リプレイを録り直してください。\n",
			             memSizeRecorded, memSizeCurrent);
			emitJsonVerdict(false, "state_size_mismatch");
			return 1;
		}
		// C-2: 破損 / 切断された .mtrr を false-green にしない。clean EOF 以外の read 失敗と
		// header frame 数との不一致は、読めた分が bit-exact でも検証不成立として FAIL。
		if (player.lastError() != mitiru::replay::PlayerError::None)
		{
			std::fprintf(stderr,
			             "replay state: FAIL — replay file corrupt: %s (%llu/%u frames read)\n"
			             "  録画が切断または破損しています。--record で録り直してください。\n",
			             playerErrorName(player.lastError()),
			             static_cast<unsigned long long>(player.framesRead()),
			             player.totalFrames());
			emitJsonVerdict(false, "replay_file_corrupt");
			return 1;
		}
		if (player.framesRead() != player.totalFrames())
		{
			std::fprintf(stderr,
			             "replay state: FAIL — frame count mismatch (header %u frames, read %llu)\n"
			             "  録画が最後まで再生されていません (close 前の中断録画 / --max-frames 打ち切り等)。\n",
			             player.totalFrames(),
			             static_cast<unsigned long long>(player.framesRead()));
			emitJsonVerdict(false, "frame_count_mismatch");
			return 1;
		}
		// 比較 0 件は「検証ゼロで成功風」の false-green になるため明示 FAIL。
		if (!memCompared)
		{
			std::fprintf(stderr,
			             "replay state: FAIL — no frames compared (録画にゲームの状態が入っていません)\n"
			             "  対処: game が memorySize を申告しているか確認し、現バージョンで --record して録り直してください。\n");
			emitJsonVerdict(false, "no_frames_compared");
			return 1;
		}
		if (memCompared)
		{
			if (memDiverged)
			{
				std::fprintf(stderr,
				             "replay state: FAIL (diverged at frame %u of %u frames) — "
				             "ゲームの状態が記録からずれました\n",
				             memDivergeFrame, replayFrame);
				// どの field が変わったか (MITIRU_REFLECT 済みの game のみ。"[]" は記述子無し)
				if (!memDivergeDiff.empty() && memDivergeDiff != "[]")
				{
					std::fprintf(stderr, "replay diff: %s\n", memDivergeDiff.c_str());
				}
				// 分岐 byte を最後に書いた phase (`mitiru why` opt-in game のみ = 原因 phase)。
				if (!memDivergeBlame.empty())
				{
					std::fprintf(stderr, "replay blame: %s\n", memDivergeBlame.c_str());
				}
				emitJsonVerdict(false, "diverged");
			}
			else
			{
				std::fprintf(stderr,
				             "replay state: PASS (bit-exact, %u frames) — "
				             "全フレームでゲームの状態が記録と完全一致\n",
				             replayFrame);
				// --expect がある場合は後続の expect 判定が最終 verdict になる。
				// ここで出すと --json の stdout に 2 行 verdict が乗り、消費側がどちらを
				// 見るか一意に決まらない。
				if (args.expectPath.empty()) { emitJsonVerdict(true, "bit_exact"); }
			}
		}
		// fuzz 等が field 単位の不変条件をチェックできるよう最終 reflect 状態を出す。
		if (!finalReflect.empty() && finalReflect != "{}")
		{
			std::fprintf(stdout, "replay final: %s\n", finalReflect.c_str());
		}
		if (memDiverged) { return 1; }  // 再現失敗 = CI gate fail

		if (!args.expectPath.empty())
		{
			std::ifstream ef(args.expectPath);
			if (!ef.is_open())
			{
				std::fprintf(stderr, "mitiru_host: cannot open --expect file: %s\n",
				             args.expectPath.c_str());
				return 2;
			}
			nlohmann::json expected, actual;
			try { ef >> expected; actual = nlohmann::json::parse(finalState); }
			catch (const std::exception& e)
			{
				std::fprintf(stderr, "mitiru_host: replay assert: bad JSON: %s\n", e.what());
				return 2;
			}
			int mismatches = 0;
			nlohmann::json mismatchDetail = nlohmann::json::array();
			for (auto it = expected.begin(); it != expected.end(); ++it)
			{
				if (!actual.contains(it.key()) || actual[it.key()] != it.value())
				{
					std::fprintf(stderr, "  MISMATCH %s: expected %s, got %s\n",
					             it.key().c_str(), it.value().dump().c_str(),
					             actual.contains(it.key()) ? actual[it.key()].dump().c_str() : "(absent)");
					mismatchDetail.push_back({
					    {"field", it.key()},
					    {"expected", it.value()},
					    {"actual", actual.contains(it.key()) ? actual[it.key()] : nlohmann::json(nullptr)},
					});
					++mismatches;
				}
			}
			if (mismatches > 0)
			{
				std::fprintf(stderr, "replay assert FAILED: %d mismatch(es)\n", mismatches);
				if (args.jsonOutput)
				{
					nlohmann::json v{{"verdict", "FAIL"}, {"reason", "expect_mismatch"}, {"mismatches", mismatchDetail}};
					std::fprintf(stdout, "%s\n", v.dump().c_str());
				}
				return 1;
			}
			std::fprintf(stderr, "replay assert OK: final state matches --expect\n");
			if (args.jsonOutput)
			{
				nlohmann::json v{{"verdict", "PASS"}, {"reason", "expect_match"}};
				std::fprintf(stdout, "%s\n", v.dump().c_str());
			}
		}
	}
	return 0;
}
