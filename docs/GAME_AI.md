# 敵の AI の部品 (mitiru/gameai/)

game DLL が自分でリンクし、同じフレームの中で使う header-only のライブラリ。行動を決める木、同時に攻める数の制限、
間合いの取り方、視界と物音、ナビメッシュの経路、敵どうしがぶつからない歩き方からなる。host は呼ばない (ADR 0005)。
ABI も変えない。見本は `examples/enemy_ai/` (8 体の敵がプレイヤーを囲む)。

## データの置き場所

| データ | 置き場所 | 理由 |
|---|---|---|
| 木の形 (`BtTree`)、ナビメッシュ (`nav::NavMesh`)、地形 | DLL の static | 作ったら変えない。読み込むたび (ホットリロードを含む) に同じファイルから作り直す |
| 木の実行状態 (`BtState`)、黒板、知覚の記憶、攻撃の時間割、経路の角 | GameMemory (敵ごと) | すべて固定長の POD。巻き戻しと記録／再生がそのまま効く |
| 攻撃トークン (`AttackTokenPool`)、物音の板 (`NoiseBoard`) | GameMemory (標的ごと、場に 1 つ) | 同上 |

乱数も時刻も読まない。同じ入力なら同じバイト列になる (`TestGameAiSquad` が 2 回の実行と途中からの再開で確かめる)。

## ビヘイビアツリー (BehaviorTree.hpp)

ノードは前順に並んだ配列で、敵ごとの状態はノードごとの結果と記憶 (`status[64]`、`mem[64]`) だけ。時間は tick の整数。
葉の中身はゲームが書く。`tickBehaviorTree(tree, state, frame, leaves)` の `leaves` が `BtLeafCall` を受け、番号で
switch して `BtStatus` を返す。打ち切られた Action には `event = Halt` で 1 度知らせる (トークンを返す、経路を捨てる)。

| 種類 | JSON の欄 | 意味 |
|---|---|---|
| `sequence` / `selector` | `children` | 走っている子から続ける。前の子は見直さない |
| `reactiveSequence` / `reactiveSelector` | `children` | 毎 tick 先頭から見直す。条件が崩れたら、後ろで走っていた子を打ち切る |
| `parallel` | `children`、`success` | 子を全部回し、`success` 個 (省くと全部) 成功で成功 |
| `inverter` / `succeeder` | `child` | 結果を反転 / 終われば成功 |
| `cooldown` | `frames`、`child` | 子が成功したら `frames` の間は失敗。打ち切っても記録は残る |
| `repeat` | `count`、`child` | 成功を `count` 回 (省くと失敗するまで)。1 tick に 1 回まで |
| `timeout` | `frames`、`child` | 入ってから `frames` を過ぎたら子を打ち切って失敗 |
| `wait` | `frames` | 待って成功 |
| `action` / `condition` | `name`、`param` | ゲームの葉。condition が Running を返したら失敗 |

JSON は `loadBehaviorTreeJsonFile(path, names)` で読み込み時に 1 度だけ読む。`names` は名前と葉の番号の表で、表に無い名前は
`$.children[2]: 知らない葉の名前 "fly"` のように場所付きの誤りになる。C++ で組むなら `BtBuilder` で
`b.open(BtKind::Sequence).condition(kSees).action(kChase).end()` と書く。どちらも同じバイト列の `BtTree` になる。

`BtTree::hash` は木の形の指紋で、`BtState` は自分を作った木の指紋を覚えている。ホットリロードで JSON の木が変わると、
次の tick で状態を最初からにする (古いノード番号の記憶を新しい木に当てはめない)。古い木で走っていた葉には Halt が届かないので、
葉が外に持つもの (攻撃トークン) は `releaseExpired` の期限で回収する。

BehaviorTree.CPP は取り込まない。木を XML で読み、ノードをヒープに持ち、黒板が `std::any` なので、敵の状態を
GameMemory の flat POD に置けず、巻き戻しもバイト比較の再生も効かない。ここでは木の形と実行状態を分け、状態を固定長にした。

## 戦闘の部品

- `AttackTokenPool` は同時に攻める量の上限 (`capacity`、重い技は `cost` を大きく)。`tryAcquire` はその場で取る。
  `request` と `arbitrate` は 1 tick の希望を集め、点数の高い順 (同点は id の小さい順) に配るので、敵の更新順に依らない。
  `releaseExpired` は倒れた敵が返し忘れたトークンを回収する。
- `spacingVelocity` は不感帯の外で近づくか下がる。`circleVelocity` は間合いを保って横歩きする (dir = +1 は atan2(z, x) が増える向き)。
  `assignRingSlots` と `ringSlotPosition` は待つ敵を輪の上に等間隔に並べる。
- `AttackTimeline` は構え (予告)、判定、硬直を tick で数える。`entered` は区間の最初の tick に判定を出す合図。
- `scoreUtility` と `chooseUtility` は 0..1 に揃えた入力を応答曲線に通して技を選ぶ。条件の数による目減りは補正する。

## 知覚 (Perception.hpp)

`checkSight` は距離、視野の円錐、壁を見る。壁は mitiru/action/ の `CollisionWorld::raycast` なので答えは同じフレームに出る。
`NoiseBoard` に物音を積み、`listen` が半径と経過で弱め、壁越しは `occlusion` を掛ける。`updatePerceptionMemory` は見えたか
聞こえた位置を「最後に知った位置」にし、気づき (0..1) を Unaware、Suspicious、Alert に分ける。Alert は suspiciousAt を割るまで
保つので、境目で行き来しない。

## 経路と回避

- `requestPath(navMesh, follower, from, to, frame)` は `NavMesh::findPath` の結果を `PathFollower` に写す。findPath は
  Detour の `findStraightPath` でポリゴン列の縁に糸を張った折れ線 (角だけ) を返す。
- `followPath` は経路の上を `lookAhead` だけ先の点へ向かい、角で急に曲がらない。終点に届かない経路 (Detour の Partial) は、端に着くと `Arrived` でなく `Partial` になる。`skipVisibleCorners` は球の掃引で壁に
  当たらない先の角があれば手前の角を飛ばす。床の穴は掃引に映らないので、穴や崖のあるレベルでは `NavQuery` を渡す版
  (`NavPath.hpp`) を使う。ナビメッシュの `raycast` が縁に当たる角は飛ばさない。
- `computeAvoidVelocity` は ORCA (van den Berg ほか 2011) で、近い相手と当たらない速度のうち望む速度に一番近いものを選ぶ。
  `yield = 0` の相手 (プレイヤー) は避けないので、敵が避けを全部受け持つ。向き合ったまま釣り合って止まらないよう、
  近くに相手がいるときは全員が同じ回り方へ `sideBias` だけずれる。キャラクターの掃引で少し重なったら `resolveOverlapsXZ`。

ORCA の回避は敵の状態を GameMemory に置けるので、十数体までの分隊に向く。50 体を超える群れは次の `NavCrowd` を使う。

## 群衆 (NavCrowd)

`mitiru/nav/NavCrowd.hpp` の `NavCrowd` は DetourCrowd で 50〜200 体を歩かせる。互いの回避、ナビメッシュの縁で止まること、
扉などの障害物 (`DynamicNavMesh`) を回り込むことまでを受け持つ。状態は GameMemory の外に持ち、窓口
(`MITIRU_SIDE_STATE`、[SIDE_STATE.md](SIDE_STATE.md)) で巻き戻す。見本は `examples/crowd/` (120 体と扉)。

```cpp
void build(nav::NavCrowd& crowd, const void*)   // レベルを読んで agent を足す。何度呼んでも同じ物を同じ順で
{
	crowd.loadFile("mygame/assets/level.navcache");
	crowd.navMesh().setObstacle(0, nav::NavObstacle::box(doorCenter, doorHalf));
	for (int i = 0; i < 120; ++i) { crowd.addAgent(spawn[i]); }
}
nav::NavCrowd g_crowd(&build);
MITIRU_SIDE_STATE("crowd", 1, g_crowd);

void update(Input in, float dt)
{
	g_crowd.ensureBuilt(this);
	g_crowd.setTarget(id, goal);                             // その場で経路を探す
	g_crowd.navMesh().setObstacleEnabled(0, doorClosed);
	g_crowd.update(dt);                                      // 障害物を反映してから全 agent を進める
	// 描く物は g_crowd.position(id) から GameMemory へ写す
}
```

DetourCrowd は経路探索を待ち行列に積み、何フレームかに分けて解く。この待ち行列は private で bytes にできない。
そこで `update` は dtCrowd に渡す前に、dtCrowd が探し直しを頼む条件 (通路の面が無効、目的地の面が無効、通路が短く
終点に届いていない) を自分で調べ、当てはまる agent の経路を最後まで探して通路に入れる。dtCrowd は一度も
待ち行列を使わず、`update` の結果は agent の欄 (全部書き出す) とナビメッシュ (障害物の枠とタイルの世代で決まる)
だけで決まる。1 回の `update` で探す数は `maxReplansPerUpdate` (既定 16) までで、残りは次へ回す。
待ち行列が使われていないことは毎回確かめ、`invariantBreaks()` が数える。

次の表は `TestNavCrowd` の `[bench]` と同じ手順で測った時間である。扉を 40 フレーム目に閉め、220 フレーム目に開ける 600 フレームを回した。

| | Release | Debug |
|---|---|---|
| 120 体の `update` 平均 / p99 | 0.40 / 1.0 ms | 1.5 / 4.4 ms |
| 200 体の `update` 平均 / p99 | 0.8 / 2.3 ms | 2.7 / 5.4 ms |
| 窓口の保存 / 復元 (120 体、約 78 KB) | 0.02 / 0.01 ms | 0.05 / 0.05 ms |

飛び降りや梯子 (off-mesh connection) を渡る途中の状態は dtCrowd の外から戻せないので、焼かない。

## 見る

敵の木の状態・知覚・トークンを `mitiru/module/ReflectEngineTypes.hpp` を通して `MITIRU_REFLECT` に載せると、
`mitiru_host enemy_ai.dll --inspect ai` の窓で敵ごとに見られる。木の JSON と焼いたナビメッシュは `MITIRU_INSPECT_ASSETS` で
host に渡すと、ai の窓にノードの種類と葉の名前、nav の窓に床の形が出る (ABI v49、[TOOL_WINDOWS.md](TOOL_WINDOWS.md))。群衆は agent を
`FixedVec<nav::CrowdAgentView, N>` で GameMemory へ写せば `--inspect nav` の地図に出る ([TOOL_WINDOWS.md](TOOL_WINDOWS.md))。

## 境界 (ABI) への要望

無い。木、知覚、経路、回避、群衆はすべて DLL の中で解き、地形とナビメッシュは DLL が読み込み時に作る。群衆の状態は
既存の窓口 (ADR 0054) で host に預ける。host に頼むことが無い。
