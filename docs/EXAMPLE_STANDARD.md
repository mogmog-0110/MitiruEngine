# Example 規格

`examples/` の章 example が従う契約。目標は「小さいコードで綺麗な絵」— 1 章を 1 分で読めて、
実行した瞬間に何の機能か分かること。参照実装: [`examples/welcome/`](../examples/welcome/welcome_dll.cpp)
(歓迎デモ) / [`examples/shapes/`](../examples/shapes/shapes_dll.cpp) (カタログ形)。

## 章立て (この節が学習順の正典)

章の学習順序は dir 名ではなく **この表の並び** が持つ (examples/README.md の表も同順)。

| 帯 | 章 (学習順) |
|---|---|
| 基礎 | welcome / shapes / text / input / motion / sprites / camera / audio |
| 看板 | html_hud / html_menu / observe / rewind / restart_save / scene3d / model3d |
| 別枠 | `examples/subsys/` — renderer / audio / input / scene の単独起動 exe ([SUBSYSTEMS.md](SUBSYSTEMS.md)) |

## 契約 (MUST)

1. **1 章 = 1 機能**。見せたい API 以外を画面に出さない (アトミックツール哲学の教材版)
2. **DLL cpp 単体 ≤100 行** (コメント込み)。dir 名 `name` (snake_case、番号なし)、
   file 名 `name_dll.cpp`、target 名 = dir 名。学習順は名前でなく上の章立て表が持つ
3. **冒頭コメント 3 行固定**:
   ```cpp
   // name — <何を見せるか>
   // 実行すると: <画面に何が見えるか>
   // 関連 API: <主要 API 列挙>
   ```
   3 行目の末尾に補足を付けるなら、平易な機能名を括弧で (例 `(録画リプレイの土台)`)
4. **正典 idiom のみ**:
   - 入口は `MITIRU_GAME` / `MITIRU_GAME_SERIES` + `#include <mitiru.hpp>`
   - 状態 struct はまるごとコピーできる形 (flat POD) — `FixedVec` / `FixedString`、ポインタ・`std::vector`・`std::string` 禁止
   - 乱数は seed 固定 `mitiru::Random` (決定論 / replay 対象)
   - `draw()` は `const` — 状態変更は update、draw は描くだけ
   - テキストは `text()` / `drawTextInRect` 系のみ (生 `drawText` 禁止)
   - 手書き JS 禁止 — HTML UI 章も JavaScript を 1 行も書かず、`data-m-*` 属性だけで値を反映する
5. **全章が画面なし自動実行 (headless) の E2E fixture を兼務**。章追加は 2 行:
   - `examples/CMakeLists.txt` → `mitiru_add_example(<name>)`
   - `tests/CMakeLists.txt` → `_example_e2e_chapters` list に `<name>`
   これで `e2e_example_<name>` (headless + input-script + capture → PNG 非空) が自動登録される
6. **糖衣 API を engine に足したら、該当章の移行までが DoD**。examples が正典の書き方 — 旧 idiom を残さない
7. **サイト / docs へのサンプル掲載はこのカタログから生成する**。手書き転記は drift 源なので禁止
8. **章はゲーム仕立てにしない (得点・勝敗・目的を持ち込まない)。ただし機能名の羅列カタログにもしない** —
   画面の文字は最小限にとどめ、機能の説明はソースコメントが持つ。見た目と操作 (触ると動く) で
   「何の機能か」を伝える (Siv3D 方式)。属性名や API 名を画面に並べて説明しない
9. **デモは抽象的な信号でなく意味のある状態で駆動する** — sin 波等の中身の無い値ではなく、機能が実際に
   動いている様子を映す (HUD 章なら最小のシーンの上に重ね、シーンの本物の状態で計器を駆動する)

## 画面フォーマット (MUST)

全章の見た目をそろえる規約。実装は共通ヘッダ
[`examples/common/chapter_hud.hpp`](../examples/common/chapter_hud.hpp) の 2 関数 + `theme` パレットに
集約する — 各章で位置・サイズ・色を手書きしない (include は `"../common/chapter_hud.hpp"` の相対 1 行)。

1. **章ラベルは左上**: `chapterTitle(s, "3D シーン")` — 日本語名のみ (章番号は出さない) を 20px 濃色
   + 薄グレー半透明の下地バーで (16, 14) に描く。左上 (y < 50) には章側の要素を置かない (状態表示は y ≥ 58 から)
2. **操作説明は下端の全幅帯**: `chapterControls(s, "矢印: あるく　R: さいしょから")` — 高さ 34px の
   薄グレー半透明帯 + 濃色 18px 中央揃え。項目の区切りは全角スペース。操作の無い章 (welcome/shapes/text) は帯を出さない
3. **画面の文言は日本語**: 操作説明・状態表示は日本語で書く。ただし **API 名そのものを見せる文字列**
   は英語のまま — 整列名 "Left Top"、"flipX"、reflect field 名 "hp" / "score"、CLI コマンド行 等
4. **window タイトルは host 任せ**: `--title` 未指定なら host が DLL 名の stem (例 `scene3d`) を
   タイトルにする。章側で細工しない
5. **HTML 章 (html_hud/html_menu) も同規約**: 章ラベル / 操作帯は C++ 側 chapter_hud が描く。HTML 側の要素は
   左上バーと下端帯に重ねない。HTML 内の文言も日本語 (`data-m-*` 属性は不変)

## 外向け文言の用語規律 (MUST)

章コメント・画面文字列・examples/README.md は初心者の教材 — 平易文が主、用語は従。

1. **内部語彙禁止**: 差別化軸の番号 (軸①〜⑤ / axis N)・ADR 番号・開発 phase 名 (P2 / P3 等)・
   ABI version (v21 等) を書かない。機能を語るときは機能名で (「巻き戻し」「録画リプレイ」等)
2. **専門用語は初出 gloss**: 平易な説明が先、用語は括弧で導入する
   (例: 「まるごとコピーできる形 (flat POD)」「入力データ (InputSnapshot)」「記録ファイル (.mtrr)」)。
   同一ファイル内の 2 回目以降は用語だけでよい
3. **そのまま書いてよいもの**: API 識別子 (`MITIRU_REFLECT` / `effectiveDt` 等)、CLI コマンドと
   flag (`--inspect timetravel` / `--record run.mtrr` 等)、コード例に現れる自分の struct 名
4. **「GameMemory」は散文で使わない**: 「ゲームの全状態」と書く。エンジン再生系の説明は
   「エンジンに再生を依頼する (`hud.play()`)」のように書き手の API 視点で書く

## コードは教材そのもの (MUST)

章の **cpp / HTML のコードは web チュートリアルページに全部埋め込まれ、1 行ずつ解説される**。
つまりコード自体が読み物。初学者 (C++ もこのエンジンも初めての人) が読んで理解できることを最優先する。

1. **初学者が読めるコード**: 解読の要る密な一行・技巧的な略記・ビット詰め等を避け、
   意味の分かる変数名と素直な数行で書く。賢さより明快さ
2. **コメントは平易な「何を・なぜ」**: エンジン内部の隠語 — issue 番号 (`#34`)・ADR 番号・
   `R-NN`・内部定数名 (`kDuckSeThreshold` 等) ・開発者向けの走り書き — をコードコメントに残さない
3. **自己完結**: その章の cpp (と HTML) だけ読んで意図が取れること。他章や内部資料への参照に依存しない
4. **挙動を犠牲にしない**: 読みやすさのための書き換えは出力を変えない範囲で
   (変数名変更・式の分割は可、アルゴリズム変更は golden / e2e を壊すので不可)

## テーマ (MUST)

**白系 (Apple-light) が全章の基調。** 色は `chapter_hud.hpp` の `theme` パレットから使う。

1. **背景は紙白** `theme::kPaper` (#F5F7FA)、**文字は濃色** `theme::kInk` (#1D1D1F) /
   補助は `theme::kSubtle`。図形のアクセントは彩度のある青 / ピンク / オレンジ / 緑 (`theme::kBlue` 等)
2. **発光 (glow)・星空系の演出はダークカード内で見せる** — 白地では光らないため、明るいページの中の
   角丸ダークパネル `theme::kCard` に閉じ込める (audio の計器が参照実装)。
   ページ全体をダークにしない
3. **HTML 章 (html_hud/html_menu) も同テーマ**: 透明 overlay + 濃色文字、パネルは白地 + 薄枠 (#D8DEE9)、
   ボタン等のアクセントは #0A84FF。文字は 18px 以上
4. **3D 章の空は昼の明るい空** (skybox は空色→白のグラデ) — 白系ページとの統一感を保つ

## 検証

build → `ctest --test-dir build -C Debug -R e2e_example_` → capture PNG を目視。
ソース上の数値一致だけで「完了」を宣言しない — 実ユーザー操作経路 (headless host + input-script) の
PNG で確認するまでが完了条件。
