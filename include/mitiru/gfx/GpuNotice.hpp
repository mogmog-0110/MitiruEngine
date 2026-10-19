#pragma once

/// @file GpuNotice.hpp
/// @brief 遊ぶ人が気づけない 2 つの GPU 状態（ソフトウェア描画、DX11 での 3D）を、いつ何と知らせるかを判断する宣言。
/// @details ウィンドウや環境変数に触れず、GPU のない CI でも判断と文を確かめられる。

#include <string>
#include <string_view>

#include <mitiru/core/Config.hpp>

namespace mitiru::gfx::compat
{

enum class WarpNotice : unsigned char
{
	None,     ///< 何も出さない
	Console,  ///< 端末とログにだけ出す
	Dialog,   ///< 窓で出し、続けるか終わるかを選んでもらう
};

struct WarpNoticeInput
{
	bool usingWarp = false;
	bool warpForced = false;       ///< MITIRU_D3D12_WARP を自分で立てた
	bool playerRun = false;        ///< 遊ぶ人の実行。headless や台本の実行は false
	bool dialogSuppressed = false; ///< MITIRU_NO_DIALOG
};

/// @details 強制した人は理由を知っているため、ウィンドウを出さない。ウィンドウを出せない実行でも、文は端末に残す
[[nodiscard]] constexpr WarpNotice decideWarpNotice(const WarpNoticeInput& in) noexcept
{
	if (!in.usingWarp) { return WarpNotice::None; }
	if (in.warpForced || !in.playerRun || in.dialogSuppressed) { return WarpNotice::Console; }
	return WarpNotice::Dialog;
}

namespace detail
{
[[nodiscard]] inline std::string withReportPath(std::string body, std::string_view reportPath)
{
	if (!reportPath.empty())
	{
		body += "\n動かないときは、次のファイルを送ってください。\n";
		body += reportPath;
	}
	return body;
}
}  // namespace detail

[[nodiscard]] inline std::string warpNoticeText(std::string_view reportPath)
{
	return detail::withReportPath(
		"このパソコンのグラフィックスボードかドライバーでは、ゲームを正しく動かせず、とても遅くなります。\n"
		"グラフィックスドライバーを最新にすると、直る場合があります。", reportPath);
}

/// 端末に出す知らせの書き方 (docs/CONSOLE_OUTPUT.md) に合わせた 1 文。長い説明は warpNoticeText と診断ファイルに任せる
[[nodiscard]] inline const char* warpConsoleNotice() noexcept
{
	return "GPU を使えず遅くなるので、グラフィックスドライバーを更新してください";
}

[[nodiscard]] inline const char* warpChoiceLine() noexcept
{
	return "このまま続けるときは「OK」、終わるときは「キャンセル」を押してください。";
}

/// @brief 3D の世界を描けるバックエンドか。DX11 の 3D は簡易で、モデル、地形、ゲームのメッシュを描かない（ADR 0023）
[[nodiscard]] constexpr bool backendDrawsWorld3D(Backend backend) noexcept
{
	return backend != Backend::Dx11;
}

/// @brief DX12 を始められず DX11 に切り替えたか。切り替えたときは、DX12 を始められなかったと伝える
inline bool& dx11FallbackSlot() noexcept
{
	static bool fellBack = false;
	return fellBack;
}

/// @brief ハードウェアの準備に失敗した後の再試行で、WARP を選ばせる印。Dx12Device を作り直す直前に GfxFactory が立てる
inline bool& retryWithWarpSlot() noexcept
{
	static bool retry = false;
	return retry;
}

/// @brief 直近のデバイス選びで WARP を作りに行ったか。同じ失敗を WARP で繰り返さないための印
inline bool& warpUsedSlot() noexcept
{
	static bool used = false;
	return used;
}

/// @brief 遊ぶ人の実行か。host が、ウィンドウつきで台本もリプレイも使わない起動のときだけ true にする
inline bool& playerRunSlot() noexcept
{
	static bool player = false;
	return player;
}

[[nodiscard]] inline std::string world3DUnavailableText(bool fellBackFromDx12, std::string_view reportPath)
{
	return detail::withReportPath(
		std::string(fellBackFromDx12 ? "このパソコンでは DirectX 12 を始められなかったため、ゲームの 3D の絵を描けません。\n"
		                             : "この描画方式 (DirectX 11) では、ゲームの 3D の絵を描けません。\n")
		+ (fellBackFromDx12 ? "グラフィックスドライバーを最新にして、もう一度起動してください。"
		                    : "--backend を dx12 か auto にして、もう一度起動してください。"), reportPath);
}

}  // namespace mitiru::gfx::compat
