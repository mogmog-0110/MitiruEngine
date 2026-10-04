#pragma once

/// @file StageLoader.hpp
/// @brief ステージの資産を頼み、読み終わるまでロード画面を出す手助け。表示はエンジン同梱の mitiru:loading.rml。
/// @details GameMemory に置く 4 バイトの POD。最初の update で hud.stage に一覧を渡し、次のフレームから
///          in.preloadPending() が 0 になるまで false を返す。毎フレーム UI へ view.loading を送る。
///          進みは GameMemory と録画される preloadPending だけで決まるので、巻き戻しても再生しても同じ所で読み終わる。
///          使い方は 3 行で、GameMemory にメンバーを置き、update の頭で `if (!loader.update(in, hud, kAssets)) return;`、
///          draw の頭で `if (!loader.ready()) return;` と書く。別のステージへ替えるときは restart() を呼ぶ。

#include <cstdint>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/module/Game.hpp>

namespace mitiru
{

struct StageLoader
{
	std::uint8_t phase = 0;   ///< 0 = まだ頼んでいない、1 = 読み終わり待ち、2 = 読み終わった
	std::uint8_t _pad[3]{};

	/// @brief 毎フレーム update の頭で呼ぶ。読み終わったフレームから true
	/// @param paths hud.stage と同じ一覧 (`model:` `clod:` `sound:` の印を付けられる)。16 件まで
	bool update(Input in, Hud hud, const char* const* paths, int count)
	{
		if (phase == 0) { request(hud, paths, count); }
		else if (phase == 1 && in.preloadPending() == 0) { phase = 2; }
		hud.set("view.loading", phase != 2);
		return phase == 2;
	}

	template <int N>
	bool update(Input in, Hud hud, const char* const (&paths)[N]) { return update(in, hud, paths, N); }

	[[nodiscard]] bool ready() const noexcept { return phase == 2; }

	/// @brief 次の update で一覧を頼み直す (ステージを替えるとき)
	void restart() noexcept { phase = 0; }

private:
	void request(Hud hud, const char* const* paths, int count)
	{
		// 空の一覧を hud.stage に渡すと「全部を手放す」になる。読むものが無ければ待たずに終える
		if (count <= 0) { phase = 2; return; }
		if (count > module::kMaxPreloadIntents)
		{
			debug::warnOnce("stage_loader.too_many",
			                "StageLoader に渡した資産が 16 件を超えている。16 件までに分け、残りは hud.preload で頼む。");
			return;
		}
		// 同じフレームにほかの先読みが積まれて入りきらないときは、次のフレームに頼み直す
		if (hud.stage(paths, count)) { phase = 1; }
	}
};

}  // namespace mitiru
