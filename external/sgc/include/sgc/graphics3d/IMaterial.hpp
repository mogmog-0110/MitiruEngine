#pragma once

/// @file IMaterial.hpp
/// @brief 3Dマテリアルインターフェース
///
/// フレームワーク非依存のマテリアル（表面特性）APIを定義する。
/// PBR（Physical Based Rendering）パラメータをサポートする。
///
/// @code
/// class MyMaterial : public sgc::graphics3d::IMaterial {
///     void apply() const override { /* シェーダーにパラメータ設定 */ }
///     // ...
/// };
///
/// // MaterialData構造体でデータ駆動に使用
/// sgc::graphics3d::MaterialData mat;
/// mat.albedo = {0.8f, 0.2f, 0.1f, 1.0f};
/// mat.metallic = 0.0f;
/// mat.roughness = 0.5f;
/// @endcode

#include <cstdint>
#include <string>

#include "sgc/math/Vec3.hpp"
#include "sgc/types/Color.hpp"

namespace sgc::graphics3d
{

/// @brief マテリアルの描画モード
enum class RenderMode : int32_t
{
	Opaque,       ///< 不透明
	Transparent,  ///< 半透明
	Additive,     ///< 加算合成
	Cutout        ///< アルファカットアウト
};

/// @brief マテリアルデータ（値型）
///
/// PBRパラメータを保持するPOD的な構造体。
/// シリアライズやデータ駆動のマテリアル管理に使用する。
struct MaterialData
{
	Colorf albedo{1.0f, 1.0f, 1.0f, 1.0f};  ///< アルベド（基本色）
	Colorf emissive{0.0f, 0.0f, 0.0f, 0.0f}; ///< 自発光色
	float metallic = 0.0f;                     ///< メタリック度 [0, 1]
	float roughness = 0.5f;                    ///< ラフネス [0, 1]
	float opacity = 1.0f;                      ///< 不透明度 [0, 1]
	float alphaCutoff = 0.5f;                  ///< アルファカットオフ閾値
	RenderMode renderMode{RenderMode::Opaque}; ///< 描画モード

	/// @brief 比較演算子
	[[nodiscard]] bool operator==(const MaterialData&) const noexcept = default;
};

/// @brief 3Dマテリアルの抽象インターフェース
///
/// マテリアルの適用（シェーダーへのパラメータバインド）を抽象化する。
class IMaterial
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IMaterial() = default;

	/// @brief マテリアルを適用する（シェーダーにパラメータをバインド）
	virtual void apply() const = 0;

	/// @brief マテリアルデータを取得する
	/// @return マテリアルデータ
	[[nodiscard]] virtual MaterialData getData() const = 0;

	/// @brief マテリアルデータを設定する
	/// @param data マテリアルデータ
	virtual void setData(const MaterialData& data) = 0;

	/// @brief マテリアル名を取得する
	/// @return マテリアル名
	[[nodiscard]] virtual const std::string& name() const = 0;
};

/// @brief デフォルトマテリアル実装（データ保持のみ）
///
/// IMaterialのシンプルな実装。
/// apply()は何もしない（派生クラスでシェーダーバインドを実装する）。
class BasicMaterial : public IMaterial
{
public:
	/// @brief デフォルトコンストラクタ
	BasicMaterial() = default;

	/// @brief 名前とデータを指定して構築する
	/// @param materialName マテリアル名
	/// @param data マテリアルデータ
	BasicMaterial(std::string materialName, const MaterialData& data = {})
		: m_name{std::move(materialName)}, m_data{data} {}

	/// @brief マテリアルを適用する（基底実装は何もしない）
	void apply() const override {}

	/// @brief マテリアルデータを取得する
	[[nodiscard]] MaterialData getData() const override { return m_data; }

	/// @brief マテリアルデータを設定する
	void setData(const MaterialData& data) override { m_data = data; }

	/// @brief マテリアル名を取得する
	[[nodiscard]] const std::string& name() const override { return m_name; }

private:
	std::string m_name{"default"};
	MaterialData m_data{};
};

} // namespace sgc::graphics3d
