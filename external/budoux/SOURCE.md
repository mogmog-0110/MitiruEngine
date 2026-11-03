# BudouX の出どころ

- リポジトリ: https://github.com/google/budoux (Apache-2.0)
- 固定したバージョン: タグ `v0.9.3`、コミット `25946bbbb271dc8a53bb5b04082c6859abc5c847` (2026-10-29 に取得)
- 取った物 (同じコミットの raw URL から、本文は変えていない):

| ファイル | 元のパス | byte | SHA-256 |
|---|---|---|---|
| `LICENSE` | `LICENSE` | 11357 | `58d1e17ffe5109a7ae296caafcadfdbe6a7d176f0bc4ab01e12a689b0499d8bd` |
| `parser.py` | `budoux/parser.py` | 4832 | `f5d1d285989cd20071bff486f4e35798a579c229e4ba9f45ee9922b9cda2c9e9` |
| `ja.json` | `budoux/models/ja.json` | 20190 | `580db00a5e3e4a5b99b72cdea0db7ea70dac79599ce9d356d16bfdf0d92e7798` |

リポジトリに NOTICE ファイルは無い (同じコミットの `NOTICE` と `NOTICE.txt` が 404)。

## 使い方

- `ja.json` は日本語の文節の点数表。`python tools/gen_budoux_model.py` が C++ の表
  `include/mitiru/i18n/detail/BudouxJaData.hpp` と、参照実装の答え `tests/fixtures/budoux_ja_phrases.txt` を作る
- `parser.py` は参照実装。C++ の移植 (`include/mitiru/i18n/PhraseBreak.hpp`) が同じ境目を返すことを
  上の fixture 経由で確かめるためだけに置いてあり、エンジンのビルドでは使わない
