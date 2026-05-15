#pragma once

/// @file GlobalIllumination.hpp
/// @brief グローバルイルミネーション（GI）システムインターフェース
/// @details VoxelベースGIとライトマップベイク機能の抽象インターフェースを提供する。
///          間接照明の計算手法として、リアルタイムVoxel Cone Tracing と
///          オフラインライトマップベイクの2方式をサポートする。

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mitiru::render
{

/// @brief Voxel GI設定構造体
struct VoxelGIConfig
{
	int resolution = 128;          ///< ボクセルグリッド解像度（片軸）
	float worldSize = 50.0f;       ///< ボクセルグリッドのワールド空間サイズ
	int maxBounces = 2;            ///< 最大バウンス回数
	float coneSpread = 0.1f;       ///< コーントレーシングの広がり角（ラジアン）
	float aoDistance = 5.0f;       ///< アンビエントオクルージョン距離
	float indirectIntensity = 1.0f; ///< 間接光の強度係数
	bool enableSpecular = true;    ///< スペキュラ間接光の有効フラグ
	bool enableDiffuse = true;     ///< ディフューズ間接光の有効フラグ
};

/// @brief ライトマップベイク設定構造体
struct LightmapConfig
{
	int textureSize = 1024;        ///< ライトマップテクスチャサイズ
	int samplesPerTexel = 64;      ///< テクセルあたりのサンプル数
	int maxBounces = 3;            ///< 最大バウンス回数
	float biasOffset = 0.001f;     ///< シャドウバイアスオフセット
	bool enableAO = true;          ///< AO焼き込みの有効フラグ
	bool enableDirectional = false; ///< 方向性ライトマップの有効フラグ
	float aoRadius = 1.0f;         ///< AO計算半径
};

/// @brief ライトマップベイク進捗コールバック
/// @details 進捗率（0.0-1.0）と現在のフェーズ名を通知する。
using BakeProgressCallback = std::function<void(float progress, const std::string& phase)>;

/// @brief 放射照度サンプル結果
struct IrradianceSample
{
	std::array<float, 3> color = {0.0f, 0.0f, 0.0f}; ///< RGB放射照度値
	float confidence = 0.0f;  ///< サンプルの信頼度（0.0-1.0）
};

/// @brief グローバルイルミネーションシステムインターフェース
/// @details VoxelGIとライトマップベイクの両方を統合管理する。
///          リアルタイムGIとオフラインベイクを切り替え可能。
class GISystem
{
public:
	/// @brief 仮想デストラクタ
	virtual ~GISystem() = default;

	/// コピー禁止
	GISystem(const GISystem&) = delete;
	GISystem& operator=(const GISystem&) = delete;

	// ── VoxelGI ────────────────────────────────────────────────

	/// @brief VoxelGIを初期化する
	/// @param config Voxel GI設定
	/// @return 初期化成功でtrue
	virtual bool initVoxelGI(const VoxelGIConfig& config) = 0;

	/// @brief ボクセルグリッドを更新する（毎フレーム呼び出し）
	/// @param deltaTime フレーム間隔（秒）
	virtual void updateVoxelGI(float deltaTime) = 0;

	/// @brief ボクセルグリッドを再構築する（シーン変更時）
	virtual void revoxelize() = 0;

	// ── ライトマップベイク ──────────────────────────────────────

	/// @brief ライトマップベイクを開始する
	/// @param config ライトマップ設定
	/// @param callback 進捗コールバック（省略可）
	/// @return ベイク開始成功でtrue
	virtual bool bake(const LightmapConfig& config,
	                  BakeProgressCallback callback = nullptr) = 0;

	/// @brief ベイク結果をファイルに保存する
	/// @param outputPath 出力ファイルパス
	/// @return 保存成功でtrue
	virtual bool saveLightmap(const std::string& outputPath) const = 0;

	/// @brief ベイク済みライトマップを読み込む
	/// @param inputPath 入力ファイルパス
	/// @return 読み込み成功でtrue
	virtual bool loadLightmap(const std::string& inputPath) = 0;

	// ── クエリ ─────────────────────────────────────────────────

	/// @brief ワールド座標から放射照度を取得する
	/// @param worldX ワールドX座標
	/// @param worldY ワールドY座標
	/// @param worldZ ワールドZ座標
	/// @return 放射照度サンプル結果
	[[nodiscard]] virtual IrradianceSample getIrradiance(
		float worldX, float worldY, float worldZ) const = 0;

	/// @brief GIが有効か判定する
	[[nodiscard]] virtual bool isEnabled() const noexcept = 0;

	/// @brief GIの有効・無効を切り替える
	/// @param enabled 有効フラグ
	virtual void setEnabled(bool enabled) = 0;

protected:
	/// @brief デフォルトコンストラクタ（派生クラスのみ生成可能）
	GISystem() = default;

	/// ムーブ許可（派生クラスのみ）
	GISystem(GISystem&&) noexcept = default;
	GISystem& operator=(GISystem&&) noexcept = default;
};

} // namespace mitiru::render
