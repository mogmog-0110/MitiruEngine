# Recipe: キーリバインドをゲーム状態(`GameMemory`)に置く

`mitiru::Binding<Act>`表(アクションマップ)をconstexpr定数ではなく、ゲームの全状態を
1個のstructにまとめたもの(型名`GameMemory`)に置くと、キー設定の変更それ自体が
記録・巻き戻し・リプレイ・セーブの対象になる。ポインタも`std::vector`も持たない
丸ごとコピーできるstruct (= flat POD)設計の追加配当を実例で示すレシピ。

## 1. 方針: いつconstexprで、いつゲームの全状態か

| 置き場所 | 向くケース |
|---|---|
| `static constexpr Binding<Act> kMap[]` | 操作が固定(リバインドUIを持たない)。表 = 操作仕様書 |
| `GameMemory`のメンバ配列 | リバインドUIを持つ。設定変更もstateの一部として扱う |

注意: 表を「非記録ソース」(設定ファイル直読み・DLL内staticの書き換え)から変更すると、
リプレイ時にその変更が再現されず録画が割れる。ゲームの全状態経由ならhostが毎フレーム
bytesごと記録するため構造的に安全で、リバインド操作を含めて1 bitも違わず
(bit-exact)再現される。

## 2. レシピ: ゲームの全状態にBinding配列

`Input::pressed`のアクションマップ版は配列参照を取る
(`bool pressed(const Binding<Act> (&map)[N], Act act)`、`Game.hpp`)。
固定長配列メンバをそのまま渡せる。`Binding<Act>`はPOD (`Act` + `Key[4]` + `Pad[2]`)
なのでflat POD制約(`MITIRU_GAME`のstatic_assert)もそのまま通る。

```cpp
enum class Act : std::uint8_t { Jump, Fire, ActCount };

struct MyGame {
    // デフォルト割当 (集成体初期化。未使用スロットは 0 のままで無害)
    mitiru::Binding<Act> bindings[2] = {
        { Act::Jump, { mitiru::Key::Space, mitiru::Key::W }, { mitiru::Pad::A } },
        { Act::Fire, { mitiru::Key::Z },                     { mitiru::Pad::X } },
    };
    int rebindTarget = -1;   // リバインド待ちの行 index (-1 = 通常プレイ)

    void update(mitiru::Input in, mitiru::Hud hud, float dt) {
        if (in.pressed(bindings, Act::Jump)) { /* jump() */ }
        if (in.down(bindings, Act::Fire))    { /* fire() */ }
    }
};
MITIRU_GAME(MyGame)
```

これだけで「Space→Shiftに変えた直後に巻き戻す」と割当もSpaceに戻り、
録画したリプレイは当時の割当で再生される。

## 3. リバインドUI

### HTML側(JavaScriptを1行も書かない: `mitiru_bind.js`のdata-m-* だけ)

```html
<div class="row">
  <span>ジャンプ: <b data-m-text="view.bind.jump">SPACE</b></span>
  <button data-m-action="rebind.jump">変更</button>
</div>
```

ボタンclickは翌フレーム、1フレーム分の入力をまとめたPOD struct (`InputSnapshot`)の
`actionEvents`に載ってDLLに届く。action eventも`InputSnapshot`の一部 = 記録対象なので、
リバインドUI操作ごと再現される。

### C++側(~15行)

リバインドモード中は`in.raw()` (生`InputSnapshot`へのescape hatch)で
次に押されたVKを全256走査して拾い、表へ書く。

```cpp
void update(mitiru::Input in, mitiru::Hud hud, float dt) {
    if (in.action("rebind.jump")) { rebindTarget = 0; }   // bindings[0] = Jump 行

    if (rebindTarget >= 0) {
        const auto* s = in.raw();
        for (int vk = 0x08; vk < 256; ++vk) {             // 0x01-0x06 (マウス) は除外
            if (s->keysJustPressed[vk] == 0) { continue; }
            if (vk == 0x1B) { rebindTarget = -1; break; }  // Esc = キャンセル
            bindings[rebindTarget].keys[0] = mitiru::Key{vk};
            rebindTarget = -1;
            break;
        }
        hud.set("view.bind.jump", "???");                  // 入力待ち表示
        return;                                            // 待機中はゲーム入力を食わない
    }

    hud.set("view.bind.jump", keyName(bindings[0].keys[0]));
    if (in.pressed(bindings, Act::Jump)) { /* jump() */ }
}
```

`rebindTarget`もゲームの全状態のメンバなので、「変更ボタンを押して入力待ちの瞬間」へ
巻き戻すことすらできる。

## 4. キー名表示はゲーム側で持つ

VK→表示名の対応はゲームの語彙(どのキーをリバインド可にするか)に依存するので、
小さな対応表をゲーム側に書く。エンジンAPIは発明しない。

```cpp
static const char* keyName(mitiru::Key k) {
    const int vk = (int)k;
    if (vk >= 'A' && vk <= 'Z') {           // 英字は static バッファで 1 文字返す
        static char buf[2] = {};
        buf[0] = (char)vk;
        return buf;
    }
    switch (k) {
    case mitiru::Key::Space: return "SPACE";
    case mitiru::Key::Up:    return "UP";
    case mitiru::Key::Down:  return "DOWN";
    case mitiru::Key::Shift: return "SHIFT";
    default:                 return "?";
    }
}
```

(表示専用helperでありgameplay stateではないので、ゲームの全状態の外で問題ない。)

## 5. セーブにも自動で乗る

セーブ = ゲームの全状態まるごとのmemcpy (`hud.save("slot0")`)なので、
リバインド結果は何もしなくてもセーブに含まれる。「キーコンフィグの保存処理」という
コードはこのレシピには存在しない。表をゲームの全状態に置いた時点で、記録・巻き戻し・
リプレイ・セーブの4つが同じ1機構(bytesのmemcpy)で片付いている。

関連: `docs/FLAT_POD.md` / `docs/REWIND.md` / `docs/BINDING.md`
