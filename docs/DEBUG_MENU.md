# デバッグメニュー

どのゲームにも付けられる開発用のメニュー。ゲームは「今の項目」を返す関数を 1 つ書くだけで、見た目・開閉・キーボードとマウスとパッドの操作はエンジンが持つ。
設計の理由は [ADR 0085](adr/0085-debug-menu.md)。

デフォルトでは何も開かない。**F1** かパッドの**左右のスティックの押し込み (2 つ押し)** で開き、もう一度押すか Esc / B で閉じる。
開いている間はゲームから入力が隠れ、ゲームは止まる (dt=0 で update は呼ばれ続ける)。

## ゲームに付ける

```cpp
#include <mitiru/module/Game.hpp>

enum Dbg : std::int32_t { kRestart = 1, kGod, kGravity, kAssist, kStage0 = 100 };

void describeDebug(const Game& g, mitiru::DebugMenu& m)
{
	m.section("Cheats");
	m.button(kRestart, "Restart stage", /*closeAfter=*/true);   // 選んだらメニューを閉じる
	m.toggle(kGod, "Invincible", g.god);
	m.number(kGravity, "Gravity", g.gravity, 0.0f, 30.0f, 0.5f);
	m.choice(kAssist, "Assist", g.assist, "Easy|Normal|Mixed");
	m.text("Stage", g.stageId, /*pinned=*/true);                // 閉じていても FPS の脇に出る
}
MITIRU_DEBUG_MENU(Game, describeDebug)                          // ファイルスコープに 1 回
```

選ばれた項目は次の update に届く。

```cpp
void Game::update(mitiru::Input in, mitiru::Hud hud, float dt)
{
	mitiru::DebugMenuEvent e;
	if (mitiru::debugMenuEvent(in, e))
	{
		switch (e.id)
		{
		case kRestart: hud.requestRestart(); break;
		case kGod: god = e.value != 0.0f; break;
		case kGravity: gravity = e.value; break;
		case kAssist: assist = static_cast<int>(e.value); break;
		}
	}
}
```

| 項目 | 関数 | 届く値 (`e.value`) |
|---|---|---|
| 見出し | `section(label)` | 届かない |
| ボタン | `button(id, label, closeAfter = false, enabled = true)` | 0 |
| トグル | `toggle(id, label, on)` | 反転した 0 / 1 |
| 数値 | `number(id, label, value, min, max, step = 1)` | 動かした後の値 (min..max に収まる) |
| 選択肢 | `choice(id, label, index, "A\|B\|C")` | 選んだ番号 (0 始まり) |
| 1 行 | `text(label, value, pinned = false)` | 届かない (読むだけ) |

- 項目の名前は 63 byte まで、選択肢の文字列と 1 行の値は 39 byte まで。項目は 256 件まで。超えた分は黙って捨てる (`m.full()` で分かる)
- id はゲームが一意に決める。エンジンの項目とは別の空間なので、重なる心配は無い
- 値はメニューの側に残らない。ゲームが操作を無視すれば、メニューの値も変わらない
- 関数は const の GameMemory を読むだけにする。開いている間は毎フレーム呼ばれる
- ゲームの文字は UTF-8 で、日本語も出る

## エンジンが最初から持つ項目

ゲームの項目の後ろに付く。

| 組 | 項目 | 動き |
|---|---|---|
| Engine | Show FPS and info | 閉じていても FPS と pinned の 1 行を画面の右上に出す (既定は切) |
| Engine | Pause while open | 切ると、ゲームを動かしたまま数値を調整できる (既定は入) |
| Engine | Step one frame | 止まっている間に 1 フレーム進める (F7 と同じ) |
| Engine | Speed | 0.25x / 0.5x / 1x / 2x / 4x (F9 と同じ) |
| Engine | Lo-fi look | F10 と同じ |
| Engine | Save screenshot / Save bug record | F12 / F11 と同じ |
| Engine | Open log folder / Open save folder | エクスプローラーで開く |
| Tools | Inspector / Performance / Rewind / Input monitor / Audio mixer / Scene tree | ツール窓を開く ([TOOL_WINDOWS.md](TOOL_WINDOWS.md))。`mitiru_tool.exe` が無いと無効 |

引数が要る窓 (`stage_edit` / `motion_tune`) は、ゲームのボタンを選んだときに `hud.open("tool", args)` で頼む。

## 操作

| 操作 | キーボード | マウス | パッド |
|---|---|---|---|
| 項目を選ぶ | 上下 | ホバー、ホイール | 十字、左スティック |
| ページ / 端 | PageUp / PageDown、Home / End | | LB / RB |
| 実行 / 切り替え | Enter | クリック | A |
| 値を動かす | 左右 (押し続けると繰り返す) | 数値と選択肢の < > | 十字の左右、左スティック |
| 絞り込み | 文字 (A-Z、0-9、`-`、`_`、`.`)、Backspace で消す | | |
| 戻る / 閉じる | Esc (絞り込み中は絞り込みを消す) | | B、Back |

## 配布版で切る

提出版や配布版では、メニューと host のキー (F7〜F12) を 1 か所の指定で切れる。`m.devToolsOff(提出版か)` をゲームの項目の関数に書くか、`ship.json` の `"devKeys": false`、起動の引数 `--no-dev-keys`。
どれか 1 つで全部が切れ、切るとメニューの画面も組み込まれない。F12 だけ残すこともできる。詳しくは [HOST_KEYS.md](HOST_KEYS.md)。

## 作りと検査

- 純ロジック: `include/mitiru/debug/DebugMenuModel.hpp` (行・選び・絞り込み・窓)、`DebugMenuInput.hpp` (snapshot の読みと入力を隠す)、`DebugMenuController.hpp` (開閉と命令)。単体試験は `ctest -R debug_menu` (タグ `[debug_menu]`)
- host: `apps/mitiru_host/HostDebugMenu.hpp` が Engine と RmlUi に繋ぐ。見た目は `assets/ui/debug_menu.rml` / `debug_info.rml` / `debug_menu.rcss`
- 実経路: `ctest -R e2e_debug_menu` が、台本の F1 から項目の選択・ゲームへの操作・再生の bit-exact までを通す
- 絵: `mitiru_host --headless-3d --capture-dir <dir> --input-script <台本>` で、メニューを開いたフレームの PNG が撮れる
- 録画の再生 (`--replay`) ではメニューは組み込まれない。録画中は操作が録画に乗る
