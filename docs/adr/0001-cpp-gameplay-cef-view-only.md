# ADR 0001 — C++ ゲームプレイ + CEF は View 専用

- **Status**: Accepted
- **Date**: 2026-05-14
- **Accepted**: 2026-05-14 (運用 1 セッション経過、P0 5 件 + Bridge view-push 統合まで完了。決定根拠が揺らぐサインなしと判定)
- **Deciders**: リードエンジニア (ユーザー)
- **Author**: Technical Director
- **Supersedes**: `docs/HYBRID_RUNTIME.md` の Mode B 中心方針 (gameplay = JS in CEF)
- **Related (parallel work)**:
  - `docs/cpp-gameplay-api-gaps.md` — C++ gameplay API 不足調査 (並行作業中)
  - `docs/BRIDGE_API_CONTRACT.md` — 新 bridge 責務定義 (並行作業中)

---

## 1. Context — なぜ変えるか

MitiruEngine は現状「**C++ が platform service を提供し、gameplay の大半は CEF
内の JS / HTML / CSS / JSON で実装する hybrid runtime**」として位置づけられている
(`docs/HYBRID_RUNTIME.md` §1, `CLAUDE.md` の基本理念)。
これは「AI フレンドリーなのは、UI + scripting 層が 95% web tech で、
LLM が得意な言語で書けるから」という前提に立っていた。

実装と運用を進めるうちに、以下の問題が顕在化した。

### 1.1 「AI が JS なら書きやすい」という前提が弱い

当初は「LLM は JS / HTML / CSS を C++ より遥かに上手に書ける」という前提だった。
しかし現実には、modern LLM は C++ ・JS いずれもほぼ同等の品質で生成できる。
gameplay logic の記述難度に **言語側の根本的な差はない**。
むしろ C++ の方が型情報・静的解析・厳密な API contract によって LLM の出力品質を
レビューしやすい局面がある。「AI フレンドリー = JS」という等式は今や成り立たない。

### 1.2 二言語依存は consumer にとってわかりづらい

KaeruCrape (現状最大の consumer) を含め、engine 利用者は「どこに何を書けばよいか」を
都度判断する必要がある。`HYBRID_RUNTIME.md` §2 の決定マトリクスは
12 行を超え、promotion / demotion ルール (同 §3) も合わせると認知負荷が高い。

`feedback-from-kaerucrape/2026-04-24.md` に挙がった F-01〜F-16 の多くは、
本質的には「**JS 側で gameplay を組むと、毎回 router / state store / novel VM /
HUD / transitions を JS で再発明する**」という、二言語境界が生む重複コストだった。
これらを engine 側 (= C++) で一度解けば JS 側の再発明は消える。

### 1.3 C++ ゲームエンジンの方が明快に勝つ軸が多い

| 軸 | C++ gameplay | JS in CEF gameplay |
|---|---|---|
| 性能 | ネイティブ実行、決定論的 | V8 GC、フレームドロップ、CEF compositor 遅延 |
| 型安全 | コンパイル時検査 | 実行時、TS でも CEF runtime には届かない |
| デバッガ | 単一 (Visual Studio / lldb) | C++ debugger と CEF DevTools の二重運用 |
| 配布 | 単一バイナリ | CEF subprocess 群 (memory: `todo_cef_subprocess_launch` のような問題が継続) |
| 移植性 | 主要 desktop / console / mobile | desktop (CEF) のみ |

これらの軸はすべて **engine の根幹** に効く。「JS で UI を素早く書ける」便益は
本物だが、それは **UI / HUD / 演出** の領域に限定される話で、gameplay logic 全体を
JS に置く根拠としては弱い。

### 1.4 CEF の価値は「綺麗な UI を簡単に作れる」点に絞られる

ImGui / RmlUi / 独自 UI フレームワークと比較した時、CEF + HTML + CSS が出せる
ビジュアル品質は依然として明確に上である:

- `filter: blur()` / `backdrop-filter` / `mix-blend-mode` / `conic-gradient`
- WAAPI による豊富なイージング、SVG `<filter>`、フォント描画品質
- Web デザイナの既存知識資産と速い iteration

しかしこれらの便益は「**画面を綺麗に作る**」局面に集中している。
gameplay state machine をこの便益と引き換えに JS 側へ置く必要はない。

### 1.5 結論

「**MitiruEngine は C++ ゲームエンジンである**」と再定義する。
CEF は **UI / HUD / 演出を綺麗に作るための view layer** として残す。
両者の境界を **薄い signal 層 (bridge)** に縮退させ、責務を再定義する。

---

## 2. Decision — 何を変えるか

以下の方針を採用する。

### 2.1 レイヤー再定義

| レイヤー | 担当 | 内容 |
|---|---|---|
| **Gameplay logic** | **C++** | scene flow / state machine / interaction / minigame / novel VM / save format / 入力解釈 / ECS / physics / 全 simulation |
| **View (UI / HUD / 演出)** | **CEF (HTML / CSS / JS)** | DOM レンダリング、アニメーション、ビジュアル表現、HTML フォーム的な入力受け取り |
| **Platform service** | **C++** | window / graphics backend / audio / disk I/O / CEF host |
| **Pure data** | **JSON** | i18n / balance / dialogue script / save blob / asset manifest |

「**JS で gameplay を書くことは新設計では非推奨**」を明文化する。
既存 JS gameplay code は段階的に C++ へ移す (移行詳細は §6)。

### 2.2 Bridge 責務の縮退

bridge は「**薄い signal 層**」として再定義する:

- **JS → C++** : 入力 / UI イベントの発火のみ
  - 例: `bridge.signal('cooking.dragDrop', { ingredientId, slotId })`
  - JS 側で state を持たない。state を更新しようとする payload は受け付けない
- **C++ → JS** : 状態通知 + DOM 更新指示のみ
  - 例: C++ が `view.notify('cooking.stateChanged', { state: 'topped', ts })` を JS に送り、JS は対応する DOM を更新する
  - JS 側は「画面に何を出すか」だけを知り、「なぜそうなったか」のロジックは持たない

詳細な API contract は `docs/BRIDGE_API_CONTRACT.md` (並行作成中) で定義する。

### 2.3 ドキュメント整合

以下を改訂する (本 ADR とは別タスクで並行進行):

- `CLAUDE.md` — 「Hybrid runtime」記述を「C++ ゲームエンジン + CEF view」に書き換え
- `docs/HYBRID_RUNTIME.md` — superseded 注記を付け、本 ADR へリンク。歴史的経緯ドキュメントとして保持
- agent memory (`feedback_hybrid_runtime_positioning.md` 他) — 新方針に合わせて更新

### 2.4 既存 consumer への扱い

KaeruCrape など既存 consumer は **engine の都合ではなく consumer 側の問題として**
段階的に migration を行う。engine 側は新方針に従って API を整備し、
consumer 側は自分のタイミングで載せ替える。engine の進化は consumer の都合で
止めない (memory: `feedback_engine_gap_fix_upstream` の「ゲーム側で回避コード書くの禁止」を
裏返すと「engine 側の方針転換でゲーム側の書き換えが発生するのは正常」)。

---

## 3. Consequences

### 3.1 Positive

- **設計の明快さ**: 「gameplay は C++、画面は CEF」という一行で説明できる。
  consumer / 新規参入者の認知負荷が下がる
- **性能・型安全・デバッガビリティの一括取り**: §1.3 の表のとおり
- **bridge の薄型化**: bridge schema は signal 名 + payload 型のみで足りる。
  現状の `mitiru.dispatch / state / event / cefQuery / executeJavaScript` のような
  多種 API 群を「signal in / signal out」二系統に整理できる
- **テスト容易性**: gameplay logic が C++ 単体テストで完結する。
  現状の RUP-S (`.claude/rules/definition-of-done.md`) は「Chrome DevTools + real
  PointerEvent」必須だが、新方針では C++ side の単体テストで gameplay correctness を
  確認でき、CEF が必要なのは「画面に正しく反映されるか」の view 検証に限定できる
- **モバイル / コンソール移植性の確保**: CEF を view から外せば、別 view backend
  (ImGui / RmlUi / native) に差し替え可能になる。Mode A (Native) が
  「gameplay 互換のまま画面だけ差し替え」できる構造になる

### 3.2 Negative — 受け入れるコスト

- **C++ gameplay API の不足**: 現在 engine は「JS が gameplay を書く」前提で
  作られているため、C++ から gameplay を書くための API (scene router / state store /
  novel VM / HUD / transitions など) が不足している。
  詳細は **[`docs/cpp-gameplay-api-gaps.md`](../cpp-gameplay-api-gaps.md)** (並行調査中) で
  優先順位とともに洗い出す
- **Bridge API の全面再定義**: 現行 `mitiru.dispatch` / `mitiru.state` /
  `mitiru.event` / `cefQuery` / `executeJavaScript` を統合・整理する必要。
  詳細は **[`docs/BRIDGE_API_CONTRACT.md`](../BRIDGE_API_CONTRACT.md)** (並行作成中) で定義
- **既存 JS gameplay code の C++ 移行コスト**: KaeruCrape の cooking state
  machine / drag-and-drop / novel UI などは現状 JS。これらの C++ 移行は
  consumer 側の作業として発生する
- **ドキュメント・memory・rules の改訂負債**:
  - `docs/HYBRID_RUNTIME.md` (superseded)
  - `CLAUDE.md` (基本理念書き換え)
  - agent memory `feedback_hybrid_runtime_positioning.md` 他
  - `.claude/rules/` 内の hybrid 前提箇所
- **CEF subprocess 問題は残る** (memory: `todo_cef_subprocess_launch`):
  CEF を view として残す限り、GPU process spawn 失敗等の subprocess 起因の
  bug は引き続き対処が必要。新方針は CEF 依存を **縮退** させるが **撤廃** はしない

### 3.3 Performance Implications

- **JS → C++ gameplay の移行で frame budget に余裕が出る**:
  現状 V8 が gameplay tick を抱えており、複雑シーンでは 1〜2 ms / frame を消費する。
  C++ 移行でこの分を取り戻せる
- **bridge 通信量の削減**: 現行 bridge は「JS が state を持ち、C++ から query する」
  双方向往復が多い。新方針では「C++ が state を持ち、JS は通知を受けて DOM を
  更新するだけ」になり、bridge 通信は単方向ストリームに近づく
- **CEF compositor overhead は変わらない**: CEF が画面に出る限り OSR (off-screen
  rendering) → C++ compositor の経路は同じ。view layer の overhead は据え置き

---

## 4. Alternatives Considered

### 4.1 Alt-1: 現状維持 (二刀流 hybrid)

**内容**: `HYBRID_RUNTIME.md` の Mode A (Native) / Mode B (Hybrid) を両方
first-class として維持し、gameplay は JS / C++ どちらにも書ける状態を続ける。

**Pros**:
- 既存 consumer (KaeruCrape) の移行コストがゼロ
- JS gameplay の iteration の速さ (hot reload) を維持できる
- ドキュメント・memory の改訂が不要

**Cons**:
- §1.2 の認知負荷問題が解決しない
- bridge API の肥大化が止まらない (F-01〜F-16 のような engine 側の追加要望が
  「JS 側で重複再発明されている問題」を解決するために増え続ける)
- 「engine は何なのか」が consumer に伝わらない問題が継続
- §1.3 の C++ 優位軸を取り逃す
- AI フレンドリー根拠の弱さ (§1.1) を抱えたまま走り続ける

**判定**: 短期コスト回避と引き換えに、engine の長期的な方向性を失う。**不採用**。

### 4.2 Alt-2: CEF 廃止 + ImGui / RmlUi 一本化

**内容**: CEF を完全に外し、UI は ImGui または RmlUi (もしくは独自 UI フレームワーク)
で実装する。gameplay も view も全て C++。

**Pros**:
- 依存ライブラリが大幅減 (CEF subprocess 問題消滅)
- 単一バイナリで配布
- モバイル / コンソール移植が直接的
- ビルド時間・配布サイズ縮小

**Cons**:
- **UI 品質で劣る** (ユーザー判断): `filter: blur()` / `backdrop-filter` /
  `mix-blend-mode` / SVG filter / web フォント描画 / WAAPI イージングなど、
  CEF が出せる「綺麗な UI」の品質は ImGui / RmlUi では再現困難
- KaeruCrape 含む既存 UI の全面書き換えが必要
- web デザイナ資産・既存知識を捨てることになる

**判定**: UI 品質が engine の差別化要素である以上、ここを切るのは過剰。**不採用**。

### 4.3 Alt-3: JS gameplay 完全廃止 + CEF view 専用 (本 ADR の決定案)

**内容**: §2 のとおり。gameplay は C++、view は CEF、bridge は薄い signal 層。

**Pros**:
- §3.1 のとおり (設計の明快さ、性能、型安全、テスト容易性、移植性)
- CEF の UI 品質を保ちながら、二言語依存の認知負荷を解消
- bridge API が薄くなることで、長期的なメンテナンスコストが下がる

**Cons**:
- §3.2 のとおり (C++ API 不足、bridge 再定義、consumer 移行、ドキュメント改訂)

**判定**: **採用**。

---

## 5. Decision Framework 照合

Technical Director の判断基準 (CLAUDE.md / agent role) に照らす:

| 基準 | 評価 |
|---|---|
| Correctness | 解く問題 (「engine の方向性が定まらない」) を正面から解く |
| Simplicity | レイヤー境界が単純化される (C++ = logic / CEF = view / JSON = data) |
| Performance | §3.3 のとおり frame budget に余裕、bridge 通信量減 |
| Maintainability | 二言語境界の認知負荷が消える。bridge API も薄型化 |
| Testability | gameplay の単体テストが C++ で完結。RUP-S は view 検証に限定 |
| Reversibility | **中程度のコスト**: 戻す場合は JS gameplay 復活 + bridge 拡張が必要。ただし「engine の哲学を据える」決定なので、頻繁に戻すべき性質ではない |

---

## 6. Migration Path (概略)

詳細は別 doc で詰める。ここでは方向性のみ示す。

### 6.1 Engine 側 (順序)

1. **本 ADR の Accept** (status: Proposed → Accepted)
2. **`docs/BRIDGE_API_CONTRACT.md` 確定** — 新 bridge の signal カタログと型
3. **`docs/cpp-gameplay-api-gaps.md` 確定** — C++ gameplay API の不足リスト + 優先順位
4. **ドキュメント整合**:
   - `CLAUDE.md` の基本理念書き換え
   - `docs/HYBRID_RUNTIME.md` に superseded ヘッダ + 本 ADR へのリンク
   - agent memory 改訂 (`feedback_hybrid_runtime_positioning.md` を新方針に置換)
   - `.claude/rules/` 内 hybrid 前提箇所の修正
5. **C++ gameplay API 整備** — Gap doc の優先順に従い段階実装
6. **Bridge 縮退** — 旧 API (mitiru.dispatch / state / event 等) を deprecated
   マークし、新 signal 層へ移行

### 6.2 Consumer 側 (KaeruCrape など)

- engine 側の新 API が揃ったタイミングで consumer 側が移行
- engine は consumer の移行完了を待たない (新 / 旧 API は一定期間並走)
- 並走期間中の互換性責任は engine 側にあるが、新規 feature は新 API のみ提供

### 6.3 Mode A / Mode B 表記の扱い

`HYBRID_RUNTIME.md` の Mode A (Native) / Mode B (Hybrid) という二分は廃止する。
新しい表現は単に「C++ gameplay + CEF view が標準。CEF を外したい platform
(mobile / console) では view backend を差し替える」。

---

## 7. Validation Criteria — この決定が正しかったとわかる指標

- **engine docs の最上位 1 段落で engine を説明できる** (現状は HYBRID_RUNTIME.md
  §1 + §5 + §6 を読まないと説明できない)
- **新規 consumer が「どこに何を書けばよいか」で迷う頻度が下がる**
  (feedback-from-kaerucrape の F-01〜F-16 のような「engine 側で吸収すべき重複」が
  半年以内に半減)
- **bridge API のシグネチャ数が縮退する** (現状の dispatch / state / event /
  cefQuery / executeJavaScript の 5 系統が、signal in / signal out の 2 系統に整理される)
- **C++ 単体テストでカバーできる gameplay の割合が増える**
  (RUP-S が必要な範囲が「view の見た目検証」に限定される)
- **「JS で gameplay を書きたい」という engine への要望が、新規には来なくなる**

逆にこの決定が **間違っていた** とわかるサイン:

- C++ gameplay API の整備コストが予想を大幅に超え、半年経っても F-04 (novel VM)
  クラスの基本要素が C++ 側に揃わない
- consumer (KaeruCrape) の migration が完全に止まり、engine と consumer の API
  が長期間乖離したまま固定化する
- CEF view との bridge が「signal だけでは足りない」局面が頻出し、結局
  双方向 RPC に戻る圧力が強まる

これらが起きたら本 ADR を再評価し、新 ADR で supersede する。

---

## 8. References

- `docs/HYBRID_RUNTIME.md` — 旧方針 (本 ADR で superseded 予定)
- `docs/feedback-from-kaerucrape/2026-04-24.md` — F-01〜F-16 (二言語境界が生む
  重複コストの実例)
- `docs/cpp-gameplay-api-gaps.md` — C++ API 不足調査 (並行作業中)
- `docs/BRIDGE_API_CONTRACT.md` — 新 bridge 責務 (並行作業中)
- `CLAUDE.md` — engine 基本理念 (改訂対象)
- agent memory `feedback_hybrid_runtime_positioning.md` — 旧 positioning メモ (改訂対象)
- agent memory `todo_cef_subprocess_launch` — CEF subprocess 問題 (引き続き対処)
