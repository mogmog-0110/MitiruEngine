#pragma once

/// @file Material.hpp
/// @brief 拡張マテリアルシステム（テクスチャスロット・シェーダーパラメータ）
///
/// IMaterial.hppの基本マテリアルを拡張し、テクスチャスロット管理、
/// シェーダーパラメータバインディング、レンダーソート比較を提供する。
///
/// @code
/// sgc::graphics3d::MaterialSystem mat;
/// mat.setProperty("shininess", 32.0f);
/// mat.setTexture(TextureSlot::Diffuse, "wood_diffuse");
/// mat.setTexture(TextureSlot::Normal, "wood_normal");
/// mat.setShaderParam("u_tiling", 2.0f);
/// @endcode

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "sgc/math/Vec4.hpp"
#include "sgc/math/Mat4.hpp"
#include "sgc/types/Color.hpp"

namespace sgc::graphics3d
{

/// @brief マテリアルの表面特性
struct MaterialProperties
{
	Colorf ambient{0.2f, 0.2f, 0.2f, 1.0f};   ///< アンビエント色
	Colorf diffuse{0.8f, 0.8f, 0.8f, 1.0f};   ///< ディフューズ色
	Colorf specular{1.0f, 1.0f, 1.0f, 1.0f};  ///< スペキュラー色
	float shininess = 32.0f;                     ///< 光沢度
	float opacity = 1.0f;                        ///< 不透明度 [0, 1]

	/// @brief 比較演算子
	[[nodiscard]] bool operator==(const MaterialProperties&) const noexcept = default;
};

/// @brief テクスチャスロットの種別
enum class TextureSlot : int32_t
{
	Diffuse = 0,    ///< ディフューズマップ
	Normal,         ///< 法線マップ
	Specular,       ///< スペキュラーマップ
	Emissive,       ///< エミッシブマップ
	Roughness,      ///< ラフネスマップ
	Metallic,       ///< メタリックマップ
	AO,             ///< アンビエントオクルージョンマップ
	Height,         ///< ハイトマップ
	Count           ///< スロット数（番兵値）
};

/// @brief シェーダーパラメータ値の型（バリアント）
using ShaderParamValue = std::variant<
	float,
	int32_t,
	Vec3f,
	Vec4f,
	Mat4f
>;

/// @brief 拡張マテリアルシステム
///
/// テクスチャスロット管理・シェーダーパラメータバインディング・
/// レンダーソート用の比較機能を提供する。
class MaterialSystem
{
public:
	/// @brief デフォルトコンストラクタ
	MaterialSystem() = default;

	/// @brief 名前を指定して構築する
	/// @param name マテリアル名
	explicit MaterialSystem(std::string name)
		: m_name{std::move(name)} {}

	/// @brief 名前と表面特性を指定して構築する
	/// @param name マテリアル名
	/// @param props 表面特性
	MaterialSystem(std::string name, const MaterialProperties& props)
		: m_name{std::move(name)}, m_properties{props} {}

	// ── 表面特性 ────────────────────────────────────────

	/// @brief 表面特性を設定する
	/// @param props 表面特性
	void setProperties(const MaterialProperties& props) { m_properties = props; }

	/// @brief 表面特性を取得する
	/// @return 表面特性の参照
	[[nodiscard]] const MaterialProperties& properties() const noexcept { return m_properties; }

	/// @brief マテリアル名を取得する
	/// @return マテリアル名
	[[nodiscard]] const std::string& name() const noexcept { return m_name; }

	// ── テクスチャスロット ──────────────────────────────

	/// @brief テクスチャをスロットに設定する
	/// @param slot テクスチャスロット
	/// @param textureId テクスチャ識別子
	void setTexture(TextureSlot slot, const std::string& textureId)
	{
		m_textures[slot] = textureId;
	}

	/// @brief テクスチャスロットの値を取得する
	/// @param slot テクスチャスロット
	/// @return テクスチャIDのoptional（未設定時はnullopt）
	[[nodiscard]] std::optional<std::string> texture(TextureSlot slot) const
	{
		const auto it = m_textures.find(slot);
		if (it == m_textures.end())
		{
			return std::nullopt;
		}
		return it->second;
	}

	/// @brief テクスチャスロットが設定されているか確認する
	/// @param slot テクスチャスロット
	/// @return 設定されていればtrue
	[[nodiscard]] bool hasTexture(TextureSlot slot) const
	{
		return m_textures.contains(slot);
	}

	/// @brief テクスチャスロットをクリアする
	/// @param slot テクスチャスロット
	void clearTexture(TextureSlot slot)
	{
		m_textures.erase(slot);
	}

	/// @brief 設定済みテクスチャスロット数を取得する
	/// @return スロット数
	[[nodiscard]] std::size_t textureCount() const noexcept
	{
		return m_textures.size();
	}

	// ── シェーダーパラメータ ────────────────────────────

	/// @brief シェーダーパラメータを設定する
	/// @param uniformName ユニフォーム名
	/// @param value パラメータ値
	void setShaderParam(const std::string& uniformName, const ShaderParamValue& value)
	{
		m_shaderParams[uniformName] = value;
	}

	/// @brief シェーダーパラメータを取得する
	/// @param uniformName ユニフォーム名
	/// @return パラメータ値のoptional（未設定時はnullopt）
	[[nodiscard]] std::optional<ShaderParamValue> shaderParam(const std::string& uniformName) const
	{
		const auto it = m_shaderParams.find(uniformName);
		if (it == m_shaderParams.end())
		{
			return std::nullopt;
		}
		return it->second;
	}

	/// @brief シェーダーパラメータが設定されているか確認する
	/// @param uniformName ユニフォーム名
	/// @return 設定されていればtrue
	[[nodiscard]] bool hasShaderParam(const std::string& uniformName) const
	{
		return m_shaderParams.contains(uniformName);
	}

	/// @brief シェーダーパラメータ数を取得する
	/// @return パラメータ数
	[[nodiscard]] std::size_t shaderParamCount() const noexcept
	{
		return m_shaderParams.size();
	}

	// ── レンダーソート用比較 ────────────────────────────

	/// @brief ソートキーを計算する
	///
	/// 不透明度とテクスチャ構成に基づくソートキーを返す。
	/// 同一マテリアルをバッチ化するために使用する。
	/// @return ソートキー値
	[[nodiscard]] uint64_t sortKey() const noexcept
	{
		uint64_t key = 0;

		// 不透明度区分（上位ビット: 不透明→半透明の順にソート）
		if (m_properties.opacity < 1.0f)
		{
			key |= (static_cast<uint64_t>(1) << 63);
		}

		// テクスチャスロット構成ハッシュ（バッチ化用）
		uint32_t texBits = 0;
		for (const auto& [slot, _] : m_textures)
		{
			texBits |= (1u << static_cast<int>(slot));
		}
		key |= (static_cast<uint64_t>(texBits) << 32);

		// マテリアル名のハッシュ（下位32ビット）
		uint32_t nameHash = 0;
		for (const char ch : m_name)
		{
			nameHash = nameHash * 31 + static_cast<uint32_t>(ch);
		}
		key |= static_cast<uint64_t>(nameHash);

		return key;
	}

	/// @brief マテリアルのソート比較
	/// @param other 比較対象
	/// @return この方が先に描画される場合true
	[[nodiscard]] bool operator<(const MaterialSystem& other) const noexcept
	{
		return sortKey() < other.sortKey();
	}

private:
	std::string m_name{"default"};                              ///< マテリアル名
	MaterialProperties m_properties;                             ///< 表面特性
	std::map<TextureSlot, std::string> m_textures;              ///< テクスチャスロット
	std::map<std::string, ShaderParamValue> m_shaderParams;     ///< シェーダーパラメータ
};

} // namespace sgc::graphics3d
