#pragma once

/// @file GpuCompat.hpp
/// @brief GPU 機能、アダプタの選択順、診断ファイルの書式、利用者向けの失敗文の宣言。
/// @details GPU がない CI でも選択順と書式を確認できるよう、D3D12 を呼ばない処理だけを置き、実際に問い合わせる Dx12GpuProbe.hpp と分ける。診断ファイルには機種名と性能値だけを書く。

#include <array>
#include <bitset>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::gfx::compat
{

/// @brief ゲームに必要な GPU 機能。必須の機能が欠けるアダプタは選ばない
enum class GpuCap : std::uint8_t
{
	FeatureLevel11,   ///< D3D12 デバイスを FL 11_0 で作れる
	Msaa4Rgba16f,     ///< 3D の本線 (HDR の色) の 4x MSAA
	Msaa4Rgba8,       ///< 3D の法線バッファの 4x MSAA
	Msaa4Depth32,     ///< 3D の深度の 4x MSAA
	Msaa8Rgba8,       ///< 2D の滑らかさ (無ければ 1x)
	TypedUavStore,    ///< R16G16B16A16_FLOAT の UAV への書き (SSR、大気、cluster LOD の描画)
	TypedUavLoad,     ///< R16G16B16A16 などの UAV の読み (DDGI。無ければその効果だけ切れる)
	ShaderModel66,    ///< SM 6.6 (DDGI、メッシュシェーダーのモデル。無ければその機能だけ切れる)
	MeshShader,       ///< メッシュシェーダー (drawModel の cluster LOD)
	Int64Ops,         ///< シェーダーの 64 bit 整数 (cluster LOD)
	Dxr11,            ///< DXR 1.1 (DDGI)
	BindingTier3,     ///< 記述子 heap を直接引く (DDGI)
	Tearing,          ///< 垂直同期なしの表示 (無ければ常に垂直同期)
	Count
};

constexpr std::size_t kGpuCapCount = static_cast<std::size_t>(GpuCap::Count);

struct CapInfo
{
	GpuCap id;
	const char* key;       ///< MITIRU_D3D12_FAKE_MISSING に書く名前
	const char* label;     ///< 診断ファイル用の短い説明
	bool required;         ///< 欠けたらそのアダプタでは動かない
	const char* ifMissing; ///< 欠けたときに起きること
};

inline constexpr std::array<CapInfo, kGpuCapCount> kCapTable = {{
	{GpuCap::FeatureLevel11, "fl11", "D3D12 (機能レベル 11_0)", true, "このアダプタでは動かない。WARP (ソフトウェア描画) へ落ちる"},
	{GpuCap::Msaa4Rgba16f, "msaa4_rgba16f", "4x MSAA (R16G16B16A16_FLOAT)", true, "3D の描画を作れず、このアダプタでは動かない"},
	{GpuCap::Msaa4Rgba8, "msaa4_rgba8", "4x MSAA (R8G8B8A8_UNORM)", true, "3D の描画を作れず、このアダプタでは動かない"},
	{GpuCap::Msaa4Depth32, "msaa4_depth", "4x MSAA (D32_FLOAT)", true, "3D の描画を作れず、このアダプタでは動かない"},
	{GpuCap::Msaa8Rgba8, "msaa8", "8x MSAA (R8G8B8A8_UNORM)", false, "2D の MSAA を 1x にして続ける"},
	{GpuCap::TypedUavStore, "typed_uav_store", "UAV への書き (R16G16B16A16_FLOAT)", false, "SSR、大気、cluster LOD の描画を使うゲームは、その効果の作成で止まる"},
	{GpuCap::TypedUavLoad, "typed_uav_load", "UAV の読み (追加の形式)", false, "DDGI (動く間接光) だけ切れる"},
	{GpuCap::ShaderModel66, "sm66", "シェーダーモデル 6.6", false, "DDGI と cluster LOD だけ切れる"},
	{GpuCap::MeshShader, "mesh_shader", "メッシュシェーダー", false, "drawModel の cluster LOD だけ切れる"},
	{GpuCap::Int64Ops, "int64", "シェーダーの 64 bit 整数", false, "drawModel の cluster LOD だけ切れる"},
	{GpuCap::Dxr11, "dxr", "DXR 1.1", false, "DDGI だけ切れる"},
	{GpuCap::BindingTier3, "binding_tier3", "リソース束縛 Tier 3", false, "DDGI だけ切れる"},
	{GpuCap::Tearing, "tearing", "垂直同期なしの表示", false, "垂直同期を切れない"},
}};

[[nodiscard]] inline const CapInfo& capInfo(GpuCap cap) noexcept
{
	return kCapTable[static_cast<std::size_t>(cap)];
}

using CapSet = std::bitset<kGpuCapCount>;

[[nodiscard]] inline CapSet allCaps() noexcept
{
	CapSet s;
	s.set();
	return s;
}

[[nodiscard]] inline std::vector<GpuCap> missingRequired(const CapSet& have)
{
	std::vector<GpuCap> out;
	for (const CapInfo& c : kCapTable)
	{
		if (c.required && !have.test(static_cast<std::size_t>(c.id))) { out.push_back(c.id); }
	}
	return out;
}

/// @brief MITIRU_D3D12_FAKE_MISSING の解釈。key はカンマで区切る。`warp` は WARP の作成も失敗させる
struct FakeMissing
{
	CapSet missing;
	bool warpFails = false;
	std::vector<std::string> unknown;  ///< 読めなかった語 (綴りの誤りを黙って無視しない)

	[[nodiscard]] bool has(GpuCap cap) const noexcept { return missing.test(static_cast<std::size_t>(cap)); }
	[[nodiscard]] bool any() const noexcept { return missing.any() || warpFails; }
};

[[nodiscard]] inline FakeMissing parseFakeMissing(std::string_view text)
{
	FakeMissing out;
	std::size_t pos = 0;
	while (pos <= text.size())
	{
		const std::size_t comma = text.find(',', pos);
		std::string_view tok = text.substr(pos, comma == std::string_view::npos ? std::string_view::npos : comma - pos);
		pos = (comma == std::string_view::npos) ? text.size() + 1 : comma + 1;
		while (!tok.empty() && (tok.front() == ' ' || tok.front() == '\t')) { tok.remove_prefix(1); }
		while (!tok.empty() && (tok.back() == ' ' || tok.back() == '\t')) { tok.remove_suffix(1); }
		if (tok.empty()) { continue; }
		if (tok == "warp") { out.warpFails = true; continue; }
		if (tok == "msaa4")  // 3 つの 4x MSAA をまとめて欠けさせる (実機での欠け方に近い)
		{
			out.missing.set(static_cast<std::size_t>(GpuCap::Msaa4Rgba16f));
			out.missing.set(static_cast<std::size_t>(GpuCap::Msaa4Rgba8));
			out.missing.set(static_cast<std::size_t>(GpuCap::Msaa4Depth32));
			continue;
		}
		bool found = false;
		for (const CapInfo& c : kCapTable)
		{
			if (tok == c.key)
			{
				out.missing.set(static_cast<std::size_t>(c.id));
				found = true;
				break;
			}
		}
		if (!found) { out.unknown.emplace_back(tok); }
	}
	return out;
}

/// @brief MITIRU_D3D12_FAKE_MISSING を 1 回だけ読んだ結果。Dx12GpuProbe.hpp の環境変数読み取りで設定する
[[nodiscard]] inline FakeMissing& fakeMissingSlot()
{
	static FakeMissing slot;
	return slot;
}

[[nodiscard]] inline bool fakeMissing(GpuCap cap) noexcept
{
	return fakeMissingSlot().has(cap);
}

struct AdapterInfo
{
	int index = -1;
	std::string name;
	std::uint32_t vendorId = 0;
	std::uint32_t deviceId = 0;
	std::uint32_t revision = 0;
	std::uint64_t dedicatedVramBytes = 0;
	std::uint64_t sharedMemoryBytes = 0;
	std::string driverVersion;  ///< 取れなければ空
	bool software = false;
	bool integrated = false;    ///< 専用メモリを持たず本体メモリを使う (UMA)
};

struct AdapterProbe
{
	AdapterInfo info;
	bool deviceCreated = false;
	std::int32_t hresult = 0;      ///< D3D12CreateDevice の結果
	bool probeCrashed = false;
	bool probeTimedOut = false;
	bool skippedByHistory = false;
	CapSet caps;                   ///< deviceCreated のときだけ意味がある
	std::string maxFeatureLevel;   ///< "12_1" のような表記
	std::string maxShaderModel;    ///< "6_6" のような表記
	int resourceHeapTier = 0;
	int resourceBindingTier = 0;
};

[[nodiscard]] inline std::string skipKey(const AdapterInfo& info)
{
	char b[64];
	std::snprintf(b, sizeof(b), "%04X:%04X:%s", static_cast<unsigned>(info.vendorId),
	              static_cast<unsigned>(info.deviceId), info.driverVersion.empty() ? "?" : info.driverVersion.c_str());
	return b;
}

/// 時間切れは、起動が遅いだけの PC でも起きる。1 回で永久に外すと、遅いだけの GPU を使えなくなるので、重なったときだけ飛ばす
inline constexpr const char* kSkipReasonTimeout = "検査が時間切れになった";
inline constexpr int kSkipAfterTimeouts = 2;

struct SkipList
{
	std::vector<std::string> keys;         ///< 子プロセスが落ちた。1 回で飛ばす
	std::vector<std::string> timeoutKeys;  ///< 時間切れの回数ぶん重ねて持つ

	[[nodiscard]] bool contains(std::string_view key) const
	{
		int timeouts = 0;
		for (const std::string& existing : keys)
		{
			if (existing == key) { return true; }
		}
		for (const std::string& existing : timeoutKeys)
		{
			if (existing == key) { ++timeouts; }
		}
		return timeouts >= kSkipAfterTimeouts;
	}

	void add(std::string_view key)
	{
		if (!contains(key)) { keys.emplace_back(key); }
	}

	void addTimeout(std::string_view key) { timeoutKeys.emplace_back(key); }
};

[[nodiscard]] inline SkipList parseSkipList(std::string_view text)
{
	SkipList out;
	std::size_t pos = 0;
	while (pos < text.size())
	{
		const std::size_t end = text.find('\n', pos);
		std::string_view line = text.substr(pos, end == std::string_view::npos ? text.size() - pos : end - pos);
		pos = end == std::string_view::npos ? text.size() : end + 1;
		if (!line.empty() && line.back() == '\r') { line.remove_suffix(1); }
		if (line.empty() || line.front() == '#') { continue; }
		const std::size_t tab = line.find('\t');
		const std::string_view key = line.substr(0, tab);
		if (key.empty()) { continue; }
		const bool timedOut = tab != std::string_view::npos && line.substr(tab + 1).rfind(kSkipReasonTimeout, 0) == 0;
		if (timedOut) { out.addTimeout(key); }
		else { out.add(key); }
	}
	return out;
}

[[nodiscard]] inline std::string formatSkipLine(std::string_view key, std::string_view reason)
{
	return std::string(key) + "\t" + std::string(reason) + "\n";
}

/// @brief ゲームが最初の画質を決めるための GPU の大まかな区分。host が環境変数 MITIRU_GPU_CLASS で伝える
enum class GpuClass : std::uint8_t { Unknown, Software, Integrated, Discrete };

/// 専用メモリがこれ以下の GPU は、専用でも内蔵と同じ軽さの画質から始める
inline constexpr std::uint64_t kWeakDiscreteVramBytes = 1024ull * 1024ull * 1024ull;
inline constexpr const char* kGpuClassEnv = "MITIRU_GPU_CLASS";

[[nodiscard]] inline const char* gpuClassName(GpuClass c) noexcept
{
	switch (c)
	{
	case GpuClass::Software: return "software";
	case GpuClass::Integrated: return "integrated";
	case GpuClass::Discrete: return "discrete";
	case GpuClass::Unknown: break;
	}
	return "unknown";
}

[[nodiscard]] inline GpuClass classifyAdapter(const AdapterInfo& a, bool warp) noexcept
{
	if (warp || a.software) { return GpuClass::Software; }
	if (a.integrated || (a.dedicatedVramBytes != 0 && a.dedicatedVramBytes <= kWeakDiscreteVramBytes)) { return GpuClass::Integrated; }
	return GpuClass::Discrete;
}

enum class AdapterVerdict : std::uint8_t { NotProbed, Chosen, Skipped };

struct AdapterLine
{
	AdapterInfo info;
	AdapterVerdict verdict = AdapterVerdict::NotProbed;
	std::string why;  ///< 選ばなかった理由
	CapSet lacking;   ///< 必須の機能が無くて外したときの、欠けていたもの
};

struct Selection
{
	int chosen = -1;                 ///< 選んだ候補の番号 (候補の並びの中)。無ければ -1
	std::string reason;              ///< 選んだ理由
	std::vector<AdapterLine> lines;  ///< 全アダプタの扱い
	bool overrideMissed = false;     ///< MITIRU_D3D12_ADAPTER に合うアダプタが無かった
};

/// @brief `MITIRU_D3D12_ADAPTER` が数字なら列挙番号、それ以外なら名前の部分一致で選ぶ。大文字と小文字は区別しない
[[nodiscard]] inline bool matchesOverride(const AdapterInfo& a, std::string_view spec)
{
	if (spec.empty()) { return true; }
	bool digits = true;
	for (const char c : spec) { if (c < '0' || c > '9') { digits = false; break; } }
	if (digits) { return a.index == std::atoi(std::string(spec).c_str()); }
	auto lower = [](std::string s) { for (char& c : s) { if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c - 'A' + 'a'); } } return s; };
	return lower(a.name).find(lower(std::string(spec))) != std::string::npos;
}

/// @brief 優先順の先頭から、動くアダプタを選ぶ
/// @param infos OS が高性能を優先して並べたアダプタ。EnumAdapterByGpuPreference の結果で、ソフトウェアは含まない
/// @param probe 先頭から 1 台ずつ問い合わせ、選べた時点で止める。使わないアダプタを起動しないため
/// @param overrideSpec 空でなければ、一致するアダプタだけを候補にする
[[nodiscard]] inline Selection selectAdapter(const std::vector<AdapterInfo>& infos,
                                             const std::function<AdapterProbe(const AdapterInfo&)>& probe,
                                             std::string_view overrideSpec)
{
	Selection sel;
	sel.lines.reserve(infos.size());
	for (const AdapterInfo& a : infos) { sel.lines.push_back({a, AdapterVerdict::NotProbed, {}}); }

	bool anyMatch = overrideSpec.empty();
	for (std::size_t i = 0; i < infos.size(); ++i)
	{
		AdapterLine& line = sel.lines[i];
		if (!matchesOverride(infos[i], overrideSpec))
		{
			line.why = "MITIRU_D3D12_ADAPTER の指定に合わない";
			continue;
		}
		anyMatch = true;
		const AdapterProbe p = probe(infos[i]);
		if (!p.deviceCreated)
		{
			line.verdict = AdapterVerdict::Skipped;
			if (p.skippedByHistory)
			{
				line.why = "前回の検査で落ちたので飛ばす";
			}
			else if (p.probeTimedOut)
			{
				line.why = "検査が時間内に終わらなかった (ドライバーの中で止まった可能性)";
			}
			else
			{
				char buf[128];
				if (p.probeCrashed)
				{
					std::snprintf(buf, sizeof(buf), "検査の子プロセスが落ちた (終了コード 0x%08X)。ドライバーの不具合の可能性",
					              static_cast<unsigned>(p.hresult));
				}
				else
				{
					std::snprintf(buf, sizeof(buf), "D3D12 のデバイスを作れない (0x%08X)", static_cast<unsigned>(p.hresult));
				}
				line.why = buf;
			}
			continue;
		}
		const std::vector<GpuCap> lacking = missingRequired(p.caps);
		if (!lacking.empty())
		{
			std::string why = "必須の機能が無い:";
			for (const GpuCap c : lacking)
			{
				why += std::string(" ") + capInfo(c).key;
				line.lacking.set(static_cast<std::size_t>(c));
			}
			line.verdict = AdapterVerdict::Skipped;
			line.why = std::move(why);
			continue;
		}
		line.verdict = AdapterVerdict::Chosen;
		sel.chosen = static_cast<int>(i);
		sel.reason = overrideSpec.empty() ? "高性能の優先順で、必須の機能がそろう最初のアダプタ"
		                                  : "MITIRU_D3D12_ADAPTER の指定に合うアダプタ";
		return sel;
	}
	sel.overrideMissed = !anyMatch;
	return sel;
}

enum class GpuFailure : std::uint8_t
{
	NoAdapter,        ///< D3D12 を使えるアダプタが 1 台もない
	MissingRequired,  ///< 使えそうなアダプタはあるが、必須の機能が欠けている
	OverrideMissed,   ///< MITIRU_D3D12_ADAPTER に合う名前が無い
	WarpFailed,       ///< ソフトウェア描画 (WARP) も作れない
	DeviceSetup,      ///< 選んだ後の作成 (キュー、フェンス、スワップチェーン) が失敗した
};

/// @brief 利用者向けの失敗文。足りないものと対処を書く
[[nodiscard]] inline std::string failureMessage(GpuFailure kind, const Selection& sel, std::string_view detail = {})
{
	std::string out;
	switch (kind)
	{
	case GpuFailure::NoAdapter:
		out = "このパソコンのグラフィックスでは、ゲームを動かせません (DirectX 12 を使えません)。\n"
		      "グラフィックスドライバーを最新にして、もう一度起動してください。";
		break;
	case GpuFailure::MissingRequired:
	{
		std::string name = "グラフィックス";
		std::string lack;
		for (const AdapterLine& l : sel.lines)
		{
			if (l.verdict == AdapterVerdict::Skipped && l.lacking.any())
			{
				name = l.info.name;
				for (const CapInfo& c : kCapTable)
				{
					if (l.lacking.test(static_cast<std::size_t>(c.id))) { lack += (lack.empty() ? "" : "、") + std::string(c.label); }
				}
				break;
			}
		}
		out = name + " は、ゲームに必要な機能 (" + lack + ") に対応していません。\n"
		      "グラフィックスドライバーを最新にして、もう一度起動してください。";
		break;
	}
	case GpuFailure::OverrideMissed:
		out = "MITIRU_D3D12_ADAPTER に合うグラフィックスが見つかりません。環境変数を消すか、診断ファイルの一覧にある名前か番号を指定してください。";
		break;
	case GpuFailure::WarpFailed:
		out = "グラフィックスを初期化できませんでした。\n"
		      "グラフィックスドライバーを最新にして、もう一度起動してください。";
		break;
	case GpuFailure::DeviceSetup:
		out = "グラフィックスの準備に失敗しました。\n"
		      "グラフィックスドライバーを最新にして、もう一度起動してください。";
		break;
	}
	if (!detail.empty()) { out += "\n(" + std::string(detail) + ")"; }
	return out;
}

/// @brief GPU の失敗。what() は診断ファイルの場所を含む利用者向けの日本語
class GpuCompatError : public std::runtime_error
{
public:
	GpuCompatError(GpuFailure kind, std::string message)
		: std::runtime_error(std::move(message)), m_kind(kind) {}
	[[nodiscard]] GpuFailure kind() const noexcept { return m_kind; }

private:
	GpuFailure m_kind;
};

struct ReportContext
{
	std::string engineVersion;
	std::string osVersion;
	std::string cpuFeatures;   ///< "SSE4.2 AVX AVX2" のような並び
	std::string chosenWhy;     ///< 選んだ理由 (Selection::reason か WARP の理由)
	bool usingWarp = false;
	std::string fakeMissingText;
	std::string failure;       ///< 失敗で終わった場合の文
	std::uint64_t vramBudgetBytes = 0;
	std::uint64_t vramUsageBytes = 0;
};

[[nodiscard]] inline std::string hex4(std::uint32_t v)
{
	char b[16];
	std::snprintf(b, sizeof(b), "%04X", static_cast<unsigned>(v));
	return b;
}

[[nodiscard]] inline std::string megabytes(std::uint64_t bytes)
{
	return std::to_string(bytes / (1024ull * 1024ull)) + " MB";
}

/// @brief 診断ファイルの本文。選んだ 1 台の詳細と全アダプタの一覧を含む
[[nodiscard]] inline std::string formatReport(const Selection& sel, const std::optional<AdapterProbe>& chosen, const ReportContext& ctx)
{
	std::string s;
	s += "MitiruEngine GPU 診断\n";
	s += "engine: " + (ctx.engineVersion.empty() ? std::string("unknown") : ctx.engineVersion) + "\n";
	s += "OS: " + ctx.osVersion + "\n";
	s += "CPU の命令: " + ctx.cpuFeatures + "\n";
	if (!ctx.fakeMissingText.empty()) { s += "MITIRU_D3D12_FAKE_MISSING: " + ctx.fakeMissingText + "\n"; }
	s += "\n";
	if (!ctx.failure.empty()) { s += "結果: 失敗\n" + ctx.failure + "\n\n"; }
	else { s += std::string("結果: 起動できた (") + (ctx.usingWarp ? "WARP = ソフトウェア描画" : "GPU") + ")\n"; }
	if (chosen)
	{
		const AdapterInfo& a = chosen->info;
		s += "使うアダプタ: " + a.name + "\n";
		s += "  理由: " + ctx.chosenWhy + "\n";
		s += "  ベンダー ID: " + hex4(a.vendorId) + "  デバイス ID: " + hex4(a.deviceId) + "  リビジョン: " + hex4(a.revision) + "\n";
		s += "  ドライバー: " + (a.driverVersion.empty() ? std::string("不明") : a.driverVersion) + "\n";
		s += std::string("  性能の区分: ") + gpuClassName(classifyAdapter(a, ctx.usingWarp)) + "\n";
		s += "  専用メモリ: " + megabytes(a.dedicatedVramBytes) + "  共有メモリ: " + megabytes(a.sharedMemoryBytes)
		   + (a.integrated ? "  (本体メモリを使う GPU)" : "") + "\n";
		s += "  最大の機能レベル: " + chosen->maxFeatureLevel + "  最大のシェーダーモデル: " + chosen->maxShaderModel + "\n";
		s += "  リソースヒープ Tier: " + std::to_string(chosen->resourceHeapTier)
		   + "  リソース束縛 Tier: " + std::to_string(chosen->resourceBindingTier) + "\n";
		if (ctx.vramBudgetBytes != 0)
		{
			s += "  ビデオメモリ: 使用 " + megabytes(ctx.vramUsageBytes) + " / 予算 " + megabytes(ctx.vramBudgetBytes) + "\n";
		}
		s += "\n機能 (必須は欠けると動かない):\n";
		for (const CapInfo& c : kCapTable)
		{
			const bool yes = chosen->caps.test(static_cast<std::size_t>(c.id));
			char line[200];
			std::snprintf(line, sizeof(line), "  %-16s %-8s %-3s %s\n", c.key, c.required ? "必須" : "任意", yes ? "yes" : "no", c.label);
			s += line;
		}
	}
	else if (ctx.usingWarp) { s += "使うアダプタ: WARP (ソフトウェア描画)\n  理由: " + ctx.chosenWhy + "\n"; }
	s += "\n列挙したアダプタ (優先順):\n";
	if (sel.lines.empty()) { s += "  (1 台も見つからない)\n"; }
	for (const AdapterLine& l : sel.lines)
	{
		const char* v = l.verdict == AdapterVerdict::Chosen ? "使う" : (l.verdict == AdapterVerdict::Skipped ? "外す" : "未確認");
		char line[320];
		std::snprintf(line, sizeof(line), "  [%d] %s  (%s:%s, %s)  %s%s%s\n", l.info.index, l.info.name.c_str(),
		              hex4(l.info.vendorId).c_str(), hex4(l.info.deviceId).c_str(), megabytes(l.info.dedicatedVramBytes).c_str(), v,
		              l.why.empty() ? "" : ": ", l.why.c_str());
		s += line;
	}
	return s;
}

}  // namespace mitiru::gfx::compat
