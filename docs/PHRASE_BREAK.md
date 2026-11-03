# 日本語を文節で折り返す

日本語の文には空白が無いので、折り返しは「幅に入るだけ詰めて、字の間で折る」のが既定で、「名前はまだ無い」が
「名前はま」「だ無い」に割れる。文節で折りたい文だけ、印を打つか `<phrase>` で包む (opt-in)。使わない文は何も変わらない。

文節は [BudouX](https://github.com/google/budoux) (Google、Apache-2.0) の日本語の表で決める。点数の和は整数だけなので、
同じ文はどのビルド・どの機種でも同じ所で切れる (`tests/fixtures/budoux_ja_phrases.txt` の 1000 文で上流の Python と一致)。

## ゲームの文字 (`drawTextWrapped`)

```cpp
#include <mitiru/i18n/PhraseBreak.hpp>

const auto& ja = mitiru::i18n::PhraseModel::japanese();
std::string line = ja.marked("吾輩は猫である。名前はまだ無い。");   // 文節の境目に U+200B
screen.drawTextWrapped(rect, line, color, 20.0f);
```

- `marked` は境目に U+200B (ZERO WIDTH SPACE) を挟む。字送り 0 で画面には出ない (HarfBuzz が空白の字形に置き換える)
- `drawTextWrapped` は、収まる範囲に印があれば最後の印の直後で折る。1 文節も入らない幅では、今までどおり字の間で折る
- 印は文が変わったときに 1 度打つ (毎フレーム呼ばない)。`drawTextInRect` / `drawTextClipped` は 1 行なので印があっても変わらない
- `--font none` の 8x8 のビットマップ文字は日本語を描けないので対象外

文節そのものが要るときは `boundaries` (バイト位置) か `forEachPhrase`:

```cpp
ja.forEachPhrase(text, [](std::string_view phrase) { /* 吾輩は / 猫である。 / 名前は / まだ / 無い。 */ });
```

## UI (RML)

```html
<phrase>{{ talk_text }}</phrase>
<phrase>吾輩は猫である。名前はまだ無い。</phrase>
```

`<phrase>` は文を文節ごとの `<span class="m-phrase-part">` に置き直し、span を `display: inline-block` にする。RmlUi は空白でしか
行を折れず、U+200B は折り目にならない (書体に無い字として豆腐で描く)。inline の span のままでは、エンジンの既定
(`base.rcss` の `word-break: break-word`) が行の途中から始まる span の字も割ってしまうので、箱にして「入らなければ次の行」へ送る。
行に収まらない長い文節だけは、箱の中で字の間に折れる (枠からはみ出さない)。折れるのは文書が `word-break: break-word` のとき
(`base.rcss` の既定) で、`word-break: normal` の文書では長い文節が枠からはみ出す (`<phrase>` を使わない日本語の文は、そもそも折れない)。

- 中に置けるのは文字だけ (`{{ }}` と `[key]` の訳はそのまま効く)。`<b>` などの子要素は後ろに回るので使わない
- 既定は `display: inline`。会話ウィンドウのように枠にするときは `phrase { display: block; }` と書く
- span の `display` は C++ が書き込む (inline-block)。RCSS の `.m-phrase-part` では上書きできない
- 値が変わると同じフレームの中で写し直す (data model の反映の後に動く)
- 同梱の `mitiru:talk.rml` は変えていない。文節で折る会話ウィンドウにしたいときは、自分の RML の `.m-talk__line` の中を
  `<phrase>{{ talk_text }}</phrase>` にする

## 別の表

BudouX の `models/*.json` (中国語、タイ語、自分で学習した日本語) は `PhraseModel::fromJson` で読む。
同梱しているのは日本語だけ (`external/budoux/ja.json`。`python tools/gen_budoux_model.py` が C++ の表に写す)。

```cpp
std::string error;
auto zh = mitiru::i18n::PhraseModel::fromJson(jsonText, error);
```

## 縦書き

`drawTextVertical` は印を折り目として優先しない (印は幅 0 の字として通る)。縦書きの文節の折り返しは未対応。

関連: TEXT_RENDERING.md (折り返しと禁則の仕組み)、[UI_RMLUI.md](UI_RMLUI.md)、
設計は ADR 0119。
