# GameMemory の外に持つ状態 (side-state)

ゲームの状態を 1 個の flat POD (`MITIRU_GAME`) に置けば、巻き戻し・分岐・リプレイ・セーブは何も書かずに使える。
物理エンジンの world や群衆の agent のように flat POD に置けない状態は、保存と復元の 2 関数を持つ「窓口」として
host へ預ける。host は GameMemory と同じフレームに窓口の bytes を記録し、GameMemory を戻すたびに一緒に戻す。
設計と計測は [ADR 0054](adr/0054-side-state-channels.md)。

## 書き方

預ける物に `saveState` と `restoreState` を持たせ、static に置いて `MITIRU_SIDE_STATE` で申告する。

```cpp
#include <mitiru/module/SideState.hpp>

struct Crowd
{
    // dst へ書いて要る byte 数を返す。cap が足りなければ書かずに要る量だけ返す (host が広げて呼び直す)。
    std::uint64_t saveState(void* dst, std::uint64_t cap) const;
    // save が書いた bytes から戻す。戻せなければ false。
    bool restoreState(const void* src, std::uint64_t size);
};

static Crowd g_crowd;
MITIRU_SIDE_STATE("crowd", 1, g_crowd);   // 名前、bytes の形の番号、物
```

先頭に GameMemory の pointer を取る形 (`saveState(const void* memory, void* dst, std::uint64_t cap)`、
`restoreState(void* memory, const void* src, std::uint64_t size)`) でもよい。host は GameMemory を先に戻してから
restore を呼ぶので、何が置かれているかを GameMemory から読んで組み立て直せる。bytes の形を変えたら番号を上げる。
番号が違う記録は戻さない。

## Jolt の world を持つ

`mitiru/physics/JoltGameWorld.hpp` の `JoltGameWorld` は、DLL が持つ Jolt の world で、そのまま窓口になる。

```cpp
void buildWorld(JoltGameWorld& world, const void* state);   // 何度呼んでも同じ物を同じ順で足す
JoltGameWorld g_world(&buildWorld);
MITIRU_SIDE_STATE("jolt", 1, g_world);

void update(Input in, float dt)
{
    g_world.ensureBuilt(this);   // 無ければ作る (ホットリロード直後・restart 直後も同じ)
    g_world.step(dt);
    // 描く物は body から GameMemory へ写す (drawMeshRotationDeg で drawMesh の角度にする)
}
void init() { g_world.reset(); }   // さいしょから: world も作り直す
```

例は [`examples/physics_rewind`](../examples/physics_rewind/physics_rewind_dll.cpp)。人の形の ragdoll は
`mitiru/physics/JoltHumanoidRagdoll.hpp` の `makeHumanoidRagdollSettings` で作れる。

弾や破片のように world に出し入れする物は、組み立ての中で `addReserveBody` で作って置いておき、`spawn` で入れて
`despawn` で外す。body を消さないので ID が変わらず、保存する bytes にはどの body が world に入っているかと、外に
ある body の状態も入る。巻き戻し・分岐・セーブ・ロールバックで出し入れも戻る。

```cpp
JPH::BodyID g_shots[8];   // 組み立てで g_shots[i] = world.addReserveBody(settings) と作る

void update(Input in, float dt)
{
    g_world.ensureBuilt(this);
    if (in.pressed(Key::Space))
    {
        const JPH::BodyID id = g_world.firstFree(g_shots);   // 飛んでいない弾 (全部飛んでいれば無効な ID)
        if (!id.IsInvalid()) { g_world.spawn(id, muzzle, JPH::Quat::sIdentity(), velocity); }
    }
    g_world.step(dt);
    for (const JPH::BodyID& id : g_shots) { if (outOfArena(id)) { g_world.despawn(id); } }
}
```

## 群衆を持つ

`mitiru/nav/NavCrowd.hpp` の `NavCrowd` (DetourCrowd の群衆と、扉などの障害物で作り直すナビメッシュ) も、同じ書き方で
そのまま窓口になる。例は [`examples/crowd`](../examples/crowd/crowd_dll.cpp)、使い方は [GAME_AI.md](GAME_AI.md#群衆-navcrowd)。

## 守ること

- シミュレーションに効く状態は、GameMemory か窓口のどちらかに入れる。 DLL の static に残った状態は戻らず、
  巻き戻しやリプレイの後に食い違う。replay は窓口ごとの hash を毎フレーム比べ、食い違った窓口の名前で FAIL する。
- bytes にポインタ値を書かない。 同じ状態でも実行ごとに hash が変わる。ID か添字で書く。
- 決定論。 同じ入力から同じ bytes になること。Jolt は 1 スレッドで解き (`JoltGameWorld` はそうしてある)、body を足す順を
  変えない。`/fp:fast` を使わない。
- Jolt の body を作るのは組み立ての中だけ。 Jolt の保存は body の生成と削除を記録しない。出し入れは `spawn` / `despawn` で行う。
  組み立ての後に body を作った world や、組が違う bytes は戻さずに断る。
- draw は GameMemory だけを読むのを勧める。 位置と向きを GameMemory へ写しておけば、どの道具からも同じ絵になる。

## 使えるもの

| 機能 | 窓口を持つ game |
|---|---|
| 巻き戻し (scrub / resim)、分岐エディタ、候補の並走 | 使える (GameMemory と窓口を一緒に戻す) |
| セーブ / ロード (`hud.save` / `hud.load`) | 使える (.msav の GameMemory の後ろに窓口の bytes を書く) |
| `--record` / `--replay-test` | 使える (窓口の hash も照合する) |
| ホットリロード | 窓口を引き継ぐ。形の番号が違えば GameMemory ごと初期状態からやり直す |
| ロールバック対戦 (`mitiru_rollback`) | 使える (GekkoNet の保存枠に窓口の image を入れ、checksum にも含める。枠は `RollbackConfig::sideStateCapacity`) |
| bug ring の再生 | 使える (keyframe が GameMemory と窓口の image を持つ。巻き戻しやロードで状態が飛ぶと、そこから積み直す) |
| ツール窓 `--inspect side_state` | 窓口ごとの bytes と hash、リングの埋まり具合。replay の照合中は窓口ごとに食い違ったフレームを印で出す |

`MITIRU_GAME_OBJECTS` の game も、場面を `MITIRU_GAME_OBJECTS_SCENE_STATE` で預ければ巻き戻しと分岐を使える
(`mitiru/module/ObjectsSceneState.hpp`、[OBJECT_STYLE_GAMES.md](OBJECT_STYLE_GAMES.md))。
