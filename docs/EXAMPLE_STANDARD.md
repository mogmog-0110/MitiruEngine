# Example規格

`examples/`の章exampleが従う契約。目標は「小さいコードで綺麗な絵」。1章を1分で読めて、
実行した瞬間に何の機能か分かること。参照実装: [`examples/welcome/`](../examples/welcome/welcome_dll.cpp)
(歓迎デモ) / [`examples/shapes/`](../examples/shapes/shapes_dll.cpp) (カタログ形)。

## 章立て(この節が学習順の正典)

章の学習順序はdir名ではなく この表の並び が持つ(examples/README.mdの表も同順)。

| 帯 | 章(学習順) |
|---|---|
| 基礎 | welcome / shapes / text / input / motion / sprites / camera / audio |
| 看板 | html_hud / html_menu / observe / rewind / restart_save / scene3d / model3d |
| 別枠 | `examples/subsys/` — renderer / audio / input / sceneの単独起動exe ([SUBSYSTEMS.md](SUBSYSTEMS.md)) |

## 契約(MUST)

1. **1章 = 1機能**。見せたいAPI以外を画面に出さない(アトミックツール哲学の教材版)
2. **DLL cpp単体 ≤100行** (コメント込み)。dir名`name` (snake_case、番号なし)、
   file名`name_dll.cpp`、target名 = dir名。学習順は名前でなく上の章立て表が持つ
3. **冒頭コメント3行固定**。
   ```cpp
   // name — <何を見せるか>
   // 実行すると: <画面に何が見えるか>
   // 関連 API: <主要 API 列挙>
   ```
   3行目の末尾に補足を付けるなら、平易な機能名を括弧で(例`(録画リプレイの土台)`)
4. **正典idiomのみ**。
   - 入口は`MITIRU_GAME` / `MITIRU_GAME_SERIES` + `#include <mitiru.hpp>`
   - 状態structはまるごとコピーできる形(flat POD)にする。`FixedVec` / `FixedString`を使い、ポインタ・`std::vector`・`std::string`は禁止
   - 乱数はseed固定`mitiru::Random` (決定論 / replay対象)
   - `draw()`は`const` — 状態変更はupdate、drawは描くだけ
   - テキストは`text()` / `drawTextInRect`系のみ(生`drawText`禁止)
   - 手書きJS禁止。HTML UI章もJavaScriptを1行も書かず、`data-m-*`属性だけで値を反映する
5. **全章が画面なし自動実行(headless)のE2E fixtureを兼務**。章追加は2行:
   - `examples/CMakeLists.txt` → `mitiru_add_example(<name>)`
   - `tests/CMakeLists.txt` → `_example_e2e_chapters` listに`<name>`
   これで`e2e_example_<name>` (headless + input-script + capture → PNG非空)が自動登録される
6. **糖衣APIをengineに足したら、該当章の移行までがDoD**。examplesが正典の書き方 — 旧idiomを残さない
7. **サイト / docsへのサンプル掲載はこのカタログから生成する**。手書き転記はdrift源なので禁止
8. **章はゲーム仕立てにしない(得点・勝敗・目的を持ち込まない)。ただし機能名の羅列カタログにもしない** —
   画面の文字は最小限にとどめ、機能の説明はソースコメントが持つ。見た目と操作(触ると動く)で
   「何の機能か」を伝える(Siv3D方式)。属性名やAPI名を画面に並べて説明しない
9. **デモは抽象的な信号でなく意味のある状態で駆動する** — sin波等の中身の無い値ではなく、機能が実際に
   動いている様子を映す(HUD章なら最小のシーンの上に重ね、シーンの本物の状態で計器を駆動する)

## 画面フォーマット(MUST)

全章の見た目をそろえる規約。実装は共通ヘッダ
[`examples/common/chapter_hud.hpp`](../examples/common/chapter_hud.hpp)の2関数 + `theme`パレットに
集約する。各章で位置・サイズ・色を手書きしない(includeは`"../common/chapter_hud.hpp"`の相対1行)。

1. **章ラベルは左上**。`chapterTitle(s, "3D シーン")` — 日本語名のみ(章番号は出さない)を20px濃色
   + 薄グレー半透明の下地バーで(16, 14)に描く。左上(y < 50)には章側の要素を置かない(状態表示はy ≥ 58から)
2. **操作説明は下端の全幅帯**。`chapterControls(s, "矢印: あるく　R: さいしょから")` — 高さ34pxの
   薄グレー半透明帯 +濃色18px中央揃え。項目の区切りは全角スペース。操作の無い章(welcome/shapes/text)は帯を出さない
3. **画面の文言は日本語**。操作説明・状態表示は日本語で書く。ただし **API名そのものを見せる文字列**
   は英語のまま。整列名 "Left Top"、"flipX"、reflect field名 "hp" / "score"、CLIコマンド行 など
4. **windowタイトルはhost任せ**。`--title`未指定ならhostがDLL名のstem (例`scene3d`)を
   タイトルにする。章側で細工しない
5. **HTML章(html_hud/html_menu)も同規約**。章ラベル / 操作帯はC++側chapter_hudが描く。HTML側の要素は
   左上バーと下端帯に重ねない。HTML内の文言も日本語(`data-m-*`属性は不変)

## 外向け文言の用語規律(MUST)

章コメント・画面文字列・examples/README.mdは初心者の教材。平易文が主、用語は従。

1. **内部語彙禁止**。差別化軸の番号(軸①〜⑤ / axis N)・ADR番号・開発phase名(P2 / P3等)・
   ABI version (v21等)を書かない。機能を語るときは機能名で(「巻き戻し」「録画リプレイ」等)
2. **専門用語は初出gloss**。平易な説明が先、用語は括弧で導入する
   (例: 「まるごとコピーできる形(flat POD)」「入力データ(InputSnapshot)」「記録ファイル(.mtrr)」)。
   同一ファイル内の2回目以降は用語だけでよい
3. **そのまま書いてよいもの**。API識別子(`MITIRU_REFLECT` / `effectiveDt`等)、CLIコマンドと
   flag (`--inspect rewind` / `--record run.mtrr`等)、コード例に現れる自分のstruct名
4. **「GameMemory」は散文で使わない**。「ゲームの全状態」と書く。エンジン再生系の説明は
   「エンジンに再生を依頼する(`hud.play()`)」のように書き手のAPI視点で書く

## コードは教材そのもの(MUST)

章のcpp / HTMLのコードはwebチュートリアルページに全部埋め込まれ、1行ずつ解説される。
つまりコード自体が読み物。初学者(C++もこのエンジンも初めての人)が読んで理解できることを最優先する。

1. **初学者が読めるコード**。解読の要る密な一行・技巧的な略記・ビット詰め等を避け、
   意味の分かる変数名と素直な数行で書く。賢さより明快さ
2. **コメントは平易な「何を・なぜ」**。エンジン内部の隠語をコードコメントに残さない。
   issue番号(`#34`)、内部定数名(`kDuckSeThreshold`等)、開発者向けの走り書きが該当する
3. **自己完結**。その章のcpp (とHTML)だけ読んで意図が取れること。他章や内部資料への参照に依存しない
4. **挙動を犠牲にしない**。読みやすさのための書き換えは出力を変えない範囲で
   (変数名変更・式の分割は可、アルゴリズム変更はgolden / e2eを壊すので不可)

## テーマ(MUST)

**白系(Apple-light)が全章の基調。** 色は`chapter_hud.hpp`の`theme`パレットから使う。

1. **背景は紙白** `theme::kPaper` (#F5F7FA)、**文字は濃色** `theme::kInk` (#1D1D1F) /
   補助は`theme::kSubtle`。図形のアクセントは彩度のある青 / ピンク / オレンジ / 緑(`theme::kBlue`等)
2. **発光(glow)・星空系の演出はダークカード内で見せる** — 白地では光らないため、明るいページの中の
   角丸ダークパネル`theme::kCard`に閉じ込める(audioの計器が参照実装)。
   ページ全体をダークにしない
3. **HTML章(html_hud/html_menu)も同テーマ**。透明overlay +濃色文字、パネルは白地 +薄枠(#D8DEE9)、
   ボタン等のアクセントは #0A84FF。文字は18px以上
4. **3D章の空は昼の明るい空** (skyboxは空色→白のグラデ) — 白系ページとの統一感を保つ

## 検証

build → `ctest --test-dir build -C Debug -R e2e_example_` → capture PNGを目視。
ソース上の数値一致だけで「完了」を宣言しない。実ユーザー操作経路(headless host + input-script)の
PNGで確認するまでが完了条件。
