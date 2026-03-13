#pragma once

/// @file Common.hpp
/// @brief GraphWalker コア型定義・定数・共有データ
///
/// ゲーム状態、オブジェクト種別、ゾーンID、ネオンカラー、
/// 物理/カメラ/プレイヤーの定数、およびシーン間共有データを定義する。
///
/// @code
/// mitiru::gw::SharedData data;
/// data.state = mitiru::gw::GameState::Playing;
/// data.currentZone = mitiru::gw::ZoneId::Tutorial;
/// @endcode

#include <map>
#include <set>
#include <string>

#include <sgc/math/Vec2.hpp>

namespace mitiru::gw
{

/// @brief ゲーム状態の列挙型
enum class GameState
{
	Title,      ///< タイトル画面
	Playing,    ///< プレイ中
	Paused,     ///< 一時停止
	Clear,      ///< クリア
	GameOver,   ///< ゲームオーバー
};

/// @brief オブジェクト種別
enum class ObjectType
{
	Player,             ///< プレイヤー
	StaticPlatform,     ///< 固定プラットフォーム
	MovingPlatform,     ///< 移動床（2点間を往復）
	CrumblingPlatform,  ///< 崩れる床（乗ると崩壊→再生）
	SpringPlatform,     ///< バネ床（乗ると上方に弾む）
	SpikeHazard,        ///< スパイクトラップ
	LaserBarrier,       ///< レーザー障壁
	CollectibleBlock,   ///< 収集可能な数式ブロック
	Checkpoint,         ///< チェックポイント
	NPC,                ///< NPC（会話可能）
	PatrolEnemy,        ///< 巡回する敵
	Gate,               ///< ゲート（通行制限）
	Goal,               ///< ゴール地点
	StaticLineString,   ///< 静的ライン（地形）
	FormulaGraph,       ///< 数式グラフ足場
};

/// @brief ゾーンID
enum class ZoneId : int
{
	Tutorial = 0,       ///< チュートリアル
	Linear = 1,         ///< 一次関数の平原
	AbsValue = 2,       ///< 絶対値の谷
	Trig = 3,           ///< 三角関数の海
	Parabola = 4,       ///< 放物線の丘
	Exponential = 5,    ///< 指数の塔
	Logarithm = 6,      ///< 対数の洞窟
	Chaos = 7,          ///< 合成関数の聖域
};

/// @brief ゾーン総数
inline constexpr int ZONE_COUNT = 8;

/// @brief ネオンカラー（RGBA、0.0〜1.0）
struct NeonColor
{
	float r = 0.0f;  ///< 赤
	float g = 0.0f;  ///< 緑
	float b = 0.0f;  ///< 青
	float a = 1.0f;  ///< アルファ

	/// @brief 等値比較
	[[nodiscard]] constexpr bool operator==(const NeonColor& other) const noexcept = default;

	// ─── ゾーン別プリセットカラー ───

	/// @brief 背景色（ダーク）
	[[nodiscard]] static constexpr NeonColor background() noexcept
	{
		return { 0.05f, 0.05f, 0.12f, 1.0f };
	}

	/// @brief プレイヤーカラー（ネオンシアン）
	[[nodiscard]] static constexpr NeonColor player() noexcept
	{
		return { 0.2f, 0.9f, 1.0f, 1.0f };
	}

	/// @brief プラットフォームカラー
	[[nodiscard]] static constexpr NeonColor platform() noexcept
	{
		return { 0.3f, 0.3f, 0.45f, 1.0f };
	}

	/// @brief ゴールカラー（ゴールド）
	[[nodiscard]] static constexpr NeonColor goal() noexcept
	{
		return { 1.0f, 0.85f, 0.0f, 1.0f };
	}

	/// @brief チェックポイントカラー
	[[nodiscard]] static constexpr NeonColor checkpoint() noexcept
	{
		return { 1.0f, 0.85f, 0.0f, 1.0f };
	}

	/// @brief ゲートロック時カラー
	[[nodiscard]] static constexpr NeonColor gateLocked() noexcept
	{
		return { 0.8f, 0.2f, 0.2f, 1.0f };
	}

	/// @brief ゲート解除時カラー
	[[nodiscard]] static constexpr NeonColor gateUnlocked() noexcept
	{
		return { 0.2f, 0.8f, 0.4f, 1.0f };
	}

	/// @brief ゾーンIDからプライマリカラーを取得する
	/// @param zone ゾーンID
	/// @return プライマリカラー
	[[nodiscard]] static constexpr NeonColor zonePrimary(ZoneId zone) noexcept
	{
		switch (zone)
		{
		case ZoneId::Tutorial:    return { 0.4f, 1.0f, 0.7f, 1.0f };   // ミントグリーン
		case ZoneId::Linear:      return { 0.0f, 0.9f, 1.0f, 1.0f };   // シアン
		case ZoneId::AbsValue:    return { 0.2f, 0.8f, 0.5f, 1.0f };   // エメラルド
		case ZoneId::Trig:        return { 0.8f, 0.3f, 1.0f, 1.0f };   // パープル
		case ZoneId::Parabola:    return { 1.0f, 0.0f, 0.6f, 1.0f };   // マゼンタ
		case ZoneId::Exponential: return { 1.0f, 0.5f, 0.0f, 1.0f };   // オレンジ
		case ZoneId::Logarithm:   return { 0.3f, 0.6f, 1.0f, 1.0f };   // ブルー
		case ZoneId::Chaos:       return { 1.0f, 0.2f, 0.2f, 1.0f };   // レッド
		default:                  return { 0.5f, 0.5f, 0.5f, 1.0f };
		}
	}

	/// @brief ゾーンIDからセカンダリカラーを取得する
	/// @param zone ゾーンID
	/// @return セカンダリカラー
	[[nodiscard]] static constexpr NeonColor zoneSecondary(ZoneId zone) noexcept
	{
		switch (zone)
		{
		case ZoneId::Tutorial:    return { 0.2f, 0.7f, 0.4f, 1.0f };
		case ZoneId::Linear:      return { 0.0f, 0.4f, 0.8f, 1.0f };
		case ZoneId::AbsValue:    return { 0.1f, 0.5f, 0.3f, 1.0f };
		case ZoneId::Trig:        return { 1.0f, 0.0f, 0.6f, 1.0f };
		case ZoneId::Parabola:    return { 0.6f, 0.0f, 0.4f, 1.0f };
		case ZoneId::Exponential: return { 0.8f, 0.3f, 0.0f, 1.0f };
		case ZoneId::Logarithm:   return { 0.1f, 0.3f, 0.6f, 1.0f };
		case ZoneId::Chaos:       return { 0.8f, 0.0f, 0.0f, 1.0f };
		default:                  return { 0.3f, 0.3f, 0.3f, 1.0f };
		}
	}
};

/// @brief ゾーン情報
struct ZoneInfo
{
	ZoneId id;                ///< ゾーンID
	std::string name;         ///< 表示名
	NeonColor primary;        ///< テーマカラー（メイン）
	NeonColor secondary;      ///< テーマカラー（サブ）
	float bgmTempo = 1.0f;    ///< BGMテンポ倍率
	sgc::Vec2f boundsMin;     ///< ゾーン境界の左下
	sgc::Vec2f boundsMax;     ///< ゾーン境界の右上
};

/// @brief ゲーム定数
namespace GameConstants
{
	/// @brief 重力加速度（ピクセル/秒^2）
	inline constexpr float GRAVITY = 980.0f;

	/// @brief 物理タイムステップ
	inline constexpr float PHYSICS_STEP = 1.0f / 200.0f;

	/// @brief 論理座標1単位あたりのピクセル数
	inline constexpr float PIXELS_PER_UNIT = 60.0f;

	/// @brief 画面幅
	inline constexpr int SCREEN_WIDTH = 1280;

	/// @brief 画面高さ
	inline constexpr int SCREEN_HEIGHT = 720;

	/// @brief プレイヤーの最大速度（水平）
	inline constexpr float PLAYER_MAX_SPEED = 300.0f;

	/// @brief プレイヤーのジャンプ力
	inline constexpr float PLAYER_JUMP_FORCE = 500.0f;

	/// @brief プレイヤー半径
	inline constexpr float PLAYER_RADIUS = 12.0f;

	/// @brief カメラ追従のスムージング係数
	inline constexpr float CAMERA_SMOOTHING = 5.0f;

	/// @brief カメラズームのデフォルト値
	inline constexpr float CAMERA_DEFAULT_ZOOM = 1.0f;

	/// @brief セーブスロット数
	inline constexpr int SAVE_SLOT_COUNT = 3;

	/// @brief デフォルトのスポーン位置
	inline constexpr float DEFAULT_SPAWN_X = -40.0f;
	inline constexpr float DEFAULT_SPAWN_Y = 2.0f;
}

/// @brief シーン間で共有するゲームデータ
struct SharedData
{
	/// @brief 現在のゲーム状態
	GameState state = GameState::Title;

	/// @brief 現在のセーブスロット番号
	int currentSlot = 0;

	/// @brief プレイヤーのスポーン位置（論理座標）
	sgc::Vec2f playerSpawnPos = { GameConstants::DEFAULT_SPAWN_X, GameConstants::DEFAULT_SPAWN_Y };

	/// @brief 訪問済みチェックポイントID
	std::set<std::string> visitedCheckpoints;

	/// @brief 収集済みアイテムID
	std::set<std::string> collectedItems;

	/// @brief アンロック済み電卓ボタン
	std::set<std::string> unlockedButtons;

	/// @brief アンロック済みゲートID
	std::set<std::string> unlockedGates;

	/// @brief 実績マップ（ID→達成状態）
	std::map<std::string, bool> achievements;

	/// @brief 現在のチェックポイントID
	std::string currentCheckpointId;

	/// @brief 現在のゾーン
	ZoneId currentZone = ZoneId::Tutorial;

	/// @brief プレイ時間（秒）
	float playTime = 0.0f;

	/// @brief ニューゲーム+レベル（0=初回）
	int ngPlusLevel = 0;

	/// @brief 高コントラストモード
	bool highContrast = false;

	/// @brief 色覚サポートモード（0=通常, 1=赤色盲, 2=緑赤色盲, 3=青黄色盲）
	int colorblindMode = 0;
};

} // namespace mitiru::gw
