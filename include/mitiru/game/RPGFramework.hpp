#pragma once

/// @file RPGFramework.hpp
/// @brief RPGシステムフレームワーク
/// @details ステータス、スキル、装備、インベントリ、ターン制バトルの基盤を提供する。
///
/// @code
/// mitiru::game::CharacterStats hero;
/// hero.hp = {100, 0, 100, 100};
/// hero.atk = {25, 0, 999, 25};
///
/// mitiru::game::DamageCalculator calc;
/// auto result = calc.calculate(hero, enemy, fireSkill);
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace mitiru::game
{

// ─── 基本ステータス ───

/// @brief 単一ステータス値
struct Stat
{
	float base = 0.0f;     ///< 基本値
	float modifier = 0.0f; ///< 装備・バフ等の修正値
	float max = 999.0f;    ///< 最大値
	float current = 0.0f;  ///< 現在値

	/// @brief 有効値（基本値＋修正値、最大値でクランプ）
	[[nodiscard]] float effective() const noexcept
	{
		return std::min(base + modifier, max);
	}
};

/// @brief 属性タイプ
enum class Element : std::uint8_t
{
	None = 0,
	Fire,
	Ice,
	Thunder,
	Wind,
	Earth,
	Light,
	Dark
};

/// @brief スキルの対象タイプ
enum class TargetType : std::uint8_t
{
	Single, ///< 単体
	All,    ///< 全体
	Self    ///< 自分
};

/// @brief 装備スロット
enum class EquipSlot : std::uint8_t
{
	Weapon,
	Armor,
	Accessory
};

/// @brief バトルアクションの種類
enum class BattleActionType : std::uint8_t
{
	Attack,
	Skill,
	Item,
	Defend,
	Flee
};

/// @brief バトルフェーズ
enum class BattlePhase : std::uint8_t
{
	Start,       ///< バトル開始
	PlayerTurn,  ///< プレイヤーターン
	EnemyTurn,   ///< 敵ターン
	Animation,   ///< アニメーション再生中
	Victory,     ///< 勝利
	Defeat       ///< 敗北
};

// ─── ステータス効果 ───

/// @brief ステータス効果の種類
enum class StatusEffect : std::uint8_t
{
	None = 0,
	Poison,    ///< 毒（毎ターンHP減少）
	Paralysis, ///< 麻痺（行動不能）
	Sleep,     ///< 睡眠（行動不能、被ダメージで解除）
	AtkUp,     ///< 攻撃力上昇
	DefUp,     ///< 防御力上昇
	AtkDown,   ///< 攻撃力低下
	DefDown    ///< 防御力低下
};

/// @brief キャラクターステータス
struct CharacterStats
{
	Stat hp;                  ///< ヒットポイント
	Stat mp;                  ///< マジックポイント
	Stat atk;                 ///< 攻撃力
	Stat def;                 ///< 防御力
	Stat spd;                 ///< 速度
	Stat luk;                 ///< 運
	int level = 1;            ///< レベル
	int exp = 0;              ///< 経験値
	std::string name;         ///< キャラクター名
	Element weakness = Element::None; ///< 弱点属性
	std::vector<StatusEffect> statusEffects; ///< 現在のステータス効果
};

// ─── スキル ───

/// @brief スキル定義
struct Skill
{
	std::uint32_t id = 0;    ///< スキルID
	std::string name;         ///< スキル名
	float mpCost = 0.0f;     ///< MP消費量
	float basePower = 1.0f;  ///< 基本威力倍率
	Element element = Element::None; ///< 属性
	TargetType targetType = TargetType::Single; ///< 対象タイプ
	std::vector<StatusEffect> effects; ///< 付与する状態異常
};

// ─── 装備 ───

/// @brief ステータスボーナス
struct StatBonuses
{
	float hp = 0.0f;
	float mp = 0.0f;
	float atk = 0.0f;
	float def = 0.0f;
	float spd = 0.0f;
	float luk = 0.0f;
};

/// @brief 装備品
struct Equipment
{
	std::uint32_t id = 0; ///< 装備ID
	std::string name;      ///< 装備名
	EquipSlot slot = EquipSlot::Weapon; ///< スロット
	StatBonuses bonuses;   ///< ステータスボーナス
	Element element = Element::None; ///< 属性付与
};

// ─── インベントリ ───

/// @brief インベントリシステム
struct Inventory
{
	std::map<std::uint32_t, int> items; ///< アイテムID → 所持数
	int maxSlots = 99;                  ///< 最大スロット数

	/// @brief アイテムを追加する
	/// @param itemId アイテムID
	/// @param count 個数
	/// @return 追加できたか
	[[nodiscard]] bool addItem(std::uint32_t itemId, int count = 1)
	{
		if (static_cast<int>(items.size()) >= maxSlots && items.find(itemId) == items.end())
		{
			return false; // スロット上限
		}
		items[itemId] += count;
		return true;
	}

	/// @brief アイテムを消費する
	/// @param itemId アイテムID
	/// @param count 個数
	/// @return 消費できたか
	[[nodiscard]] bool removeItem(std::uint32_t itemId, int count = 1)
	{
		auto it = items.find(itemId);
		if (it == items.end() || it->second < count)
		{
			return false;
		}
		it->second -= count;
		if (it->second <= 0)
		{
			items.erase(it);
		}
		return true;
	}

	/// @brief アイテムを所持しているか
	[[nodiscard]] bool hasItem(std::uint32_t itemId, int count = 1) const
	{
		auto it = items.find(itemId);
		return it != items.end() && it->second >= count;
	}

	/// @brief アイテムの所持数を返す
	[[nodiscard]] int getCount(std::uint32_t itemId) const
	{
		auto it = items.find(itemId);
		return (it != items.end()) ? it->second : 0;
	}
};

// ─── バトルアクション ───

/// @brief バトルアクション
struct BattleAction
{
	BattleActionType type = BattleActionType::Attack; ///< アクション種類
	int targetIndex = 0;          ///< 対象インデックス
	std::uint32_t skillId = 0;    ///< 使用スキルID
	std::uint32_t itemId = 0;     ///< 使用アイテムID
};

// ─── ダメージ計算 ───

/// @brief ダメージ計算結果
struct DamageResult
{
	float damage = 0.0f;     ///< 最終ダメージ
	bool critical = false;   ///< クリティカルか
	float elementMultiplier = 1.0f; ///< 属性倍率
	bool missed = false;     ///< ミスしたか
};

/// @brief ダメージ計算機
/// @details 公式: (atk * skill.power / def) * elementMultiplier * random(0.85, 1.15)
class DamageCalculator
{
public:
	/// @brief ダメージを計算する
	/// @param attacker 攻撃者ステータス
	/// @param defender 防御者ステータス
	/// @param skill 使用スキル
	/// @param seed 乱数シード（0の場合はランダム）
	/// @return ダメージ計算結果
	[[nodiscard]] static DamageResult calculate(
		const CharacterStats& attacker,
		const CharacterStats& defender,
		const Skill& skill,
		std::uint32_t seed = 0)
	{
		DamageResult result;

		// seed 未指定 (0) は固定既定で決定論を保つ (replay 再現性。caller は明示 seed 可)。
		static constexpr std::uint32_t kDefaultSeed = 0x9E3779B9u;
		std::mt19937 rng(seed != 0 ? seed : kDefaultSeed);
		std::uniform_real_distribution<float> randomDist(0.85f, 1.15f);
		std::uniform_real_distribution<float> critDist(0.0f, 100.0f);
		std::uniform_real_distribution<float> hitDist(0.0f, 100.0f);

		// 命中判定（95%基本命中率）
		if (hitDist(rng) > 95.0f)
		{
			result.missed = true;
			return result;
		}

		// 属性倍率
		result.elementMultiplier = calculateElementMultiplier(skill.element, defender.weakness);

		// 基本ダメージ = (攻撃力 * スキル威力) / 防御力
		const float atkVal = attacker.atk.effective();
		const float defVal = std::max(1.0f, defender.def.effective());
		float baseDamage = (atkVal * skill.basePower) / defVal;

		// クリティカル判定（運依存）
		const float critChance = std::min(50.0f, attacker.luk.effective() * 0.5f);
		if (critDist(rng) < critChance)
		{
			result.critical = true;
			baseDamage *= 1.5f;
		}

		// ランダム揺れ + 属性倍率
		result.damage = std::max(1.0f, baseDamage * result.elementMultiplier * randomDist(rng));
		return result;
	}

private:
	/// @brief 属性相性倍率を計算する
	[[nodiscard]] static float calculateElementMultiplier(Element attackElement, Element defenderWeakness) noexcept
	{
		if (attackElement == Element::None) return 1.0f;
		if (attackElement == defenderWeakness) return 2.0f;
		return 1.0f;
	}
};

// ─── ターン管理 ───

/// @brief ターン順序内の1エントリ
struct TurnEntry
{
	int index = 0;        ///< パーティ/敵リスト内インデックス
	bool isEnemy = false; ///< 敵かどうか
	float speed = 0.0f;   ///< 速度値（ソート用）
};

/// @brief ターン順序計算機
class TurnOrderCalculator
{
public:
	/// @brief パーティと敵の速度からターン順序を計算する
	/// @param party パーティメンバーのステータス
	/// @param enemies 敵のステータス
	/// @return ターン順序（速度降順）
	[[nodiscard]] static std::vector<TurnEntry> calculateOrder(
		const std::vector<CharacterStats>& party,
		const std::vector<CharacterStats>& enemies)
	{
		std::vector<TurnEntry> order;
		order.reserve(party.size() + enemies.size());

		for (std::size_t i = 0; i < party.size(); ++i)
		{
			if (party[i].hp.current > 0.0f)
			{
				order.push_back(TurnEntry{
					static_cast<int>(i), false, party[i].spd.effective()});
			}
		}

		for (std::size_t i = 0; i < enemies.size(); ++i)
		{
			if (enemies[i].hp.current > 0.0f)
			{
				order.push_back(TurnEntry{
					static_cast<int>(i), true, enemies[i].spd.effective()});
			}
		}

		// 速度降順にソート
		std::sort(order.begin(), order.end(),
			[](const TurnEntry& a, const TurnEntry& b) { return a.speed > b.speed; });

		return order;
	}
};

// ─── バトルステート ───

/// @brief バトルの状態
struct BattleState
{
	BattlePhase phase = BattlePhase::Start;      ///< 現在のフェーズ
	std::vector<TurnEntry> turnOrder;             ///< ターン順序
	int currentActorIndex = 0;                    ///< 現在行動中のアクターインデックス
	int turnCount = 0;                            ///< 経過ターン数
	std::vector<CharacterStats> party;            ///< パーティ
	std::vector<CharacterStats> enemies;          ///< 敵

	/// @brief 現在のアクターを取得する
	[[nodiscard]] const TurnEntry* currentActor() const noexcept
	{
		if (currentActorIndex < 0 || currentActorIndex >= static_cast<int>(turnOrder.size()))
		{
			return nullptr;
		}
		return &turnOrder[static_cast<std::size_t>(currentActorIndex)];
	}

	/// @brief 次のアクターに進める
	void advanceTurn()
	{
		++currentActorIndex;
		if (currentActorIndex >= static_cast<int>(turnOrder.size()))
		{
			// 全員行動完了 → 新ラウンド
			currentActorIndex = 0;
			++turnCount;
			turnOrder = TurnOrderCalculator::calculateOrder(party, enemies);
		}

		// フェーズ更新
		if (isPartyDefeated())
		{
			phase = BattlePhase::Defeat;
		}
		else if (isEnemiesDefeated())
		{
			phase = BattlePhase::Victory;
		}
		else
		{
			const auto* actor = currentActor();
			phase = (actor && actor->isEnemy) ? BattlePhase::EnemyTurn : BattlePhase::PlayerTurn;
		}
	}

	/// @brief バトルを開始する（ターン順序計算）
	void beginBattle()
	{
		phase = BattlePhase::Start;
		turnCount = 0;
		currentActorIndex = 0;
		turnOrder = TurnOrderCalculator::calculateOrder(party, enemies);

		if (!turnOrder.empty())
		{
			phase = turnOrder[0].isEnemy ? BattlePhase::EnemyTurn : BattlePhase::PlayerTurn;
		}
	}

	/// @brief パーティ全滅かどうか
	[[nodiscard]] bool isPartyDefeated() const noexcept
	{
		return std::all_of(party.begin(), party.end(),
			[](const CharacterStats& c) { return c.hp.current <= 0.0f; });
	}

	/// @brief 敵全滅かどうか
	[[nodiscard]] bool isEnemiesDefeated() const noexcept
	{
		return std::all_of(enemies.begin(), enemies.end(),
			[](const CharacterStats& c) { return c.hp.current <= 0.0f; });
	}
};

} // namespace mitiru::game
