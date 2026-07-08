#pragma once

/// @file EmbeddedAsset.hpp
/// @brief 埋め込みアセットレジストリ — バイナリアセットをexeに埋め込む

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mitiru::resource
{

/// @brief 埋め込みアセットデータ
/// @details ポインタとサイズのペア。所有権は持たない（静的配列を参照する想定）。
struct EmbeddedAssetData
{
	const uint8_t* data = nullptr;
	std::size_t size = 0;

	/// @brief データが有効かどうか
	[[nodiscard]] bool valid() const noexcept { return data != nullptr && size > 0; }

	/// @brief string_viewとして取得する（コピーなし）
	[[nodiscard]] std::string_view asStringView() const noexcept
	{
		return {reinterpret_cast<const char*>(data), size};
	}

	/// @brief stringとして取得する（コピーあり）
	[[nodiscard]] std::string asString() const
	{
		return std::string(reinterpret_cast<const char*>(data), size);
	}
};

/// @brief 埋め込みアセットレジストリ（グローバルシングルトン）
/// @details アセットをconstexprバイト配列としてexeに埋め込み、
///          名前でアクセスできるようにする。
///
/// @code
/// // 生成されたヘッダー（tools/embed_asset.pyで生成）
/// namespace my_game::assets {
/// inline constexpr uint8_t player_obj[] = { 0x23, 0x20, ... };
/// inline constexpr size_t player_obj_size = sizeof(player_obj);
/// }
///
/// // 初期化時に登録
/// mitiru::resource::EmbeddedAssets::instance().registerAsset(
///     "models/player.obj",
///     my_game::assets::player_obj,
///     my_game::assets::player_obj_size);
///
/// // 使用時
/// auto data = mitiru::resource::EmbeddedAssets::instance().get("models/player.obj");
/// if (data.valid()) {
///     auto mesh = mitiru::render::loadObjFromString(data.asStringView());
/// }
/// @endcode
class EmbeddedAssets
{
public:
	/// @brief シングルトンインスタンスを取得する
	[[nodiscard]] static EmbeddedAssets& instance()
	{
		static EmbeddedAssets s_instance;
		return s_instance;
	}

	/// @brief アセットを登録する
	/// @param name アセット名（パス形式推奨: "models/player.obj"）
	/// @param data バイトデータへのポインタ（静的寿命であること）
	/// @param size データサイズ（バイト）
	void registerAsset(const std::string& name, const uint8_t* data, std::size_t size)
	{
		m_assets[name] = {data, size};
	}

	/// @brief アセットを取得する
	/// @param name アセット名
	/// @return アセットデータ（見つからない場合は無効なデータ）
	[[nodiscard]] EmbeddedAssetData get(const std::string& name) const
	{
		auto it = m_assets.find(name);
		if (it == m_assets.end())
		{
			return {};
		}
		return it->second;
	}

	/// @brief アセットが登録されているか確認する
	/// @param name アセット名
	/// @return 登録済みならtrue
	[[nodiscard]] bool has(const std::string& name) const
	{
		return m_assets.find(name) != m_assets.end();
	}

	/// @brief 登録されたアセット数を取得する
	[[nodiscard]] std::size_t assetCount() const noexcept
	{
		return m_assets.size();
	}

	/// @brief 登録されたアセット名の一覧を取得する
	[[nodiscard]] std::vector<std::string> assetNames() const
	{
		std::vector<std::string> names;
		names.reserve(m_assets.size());
		for (const auto& [name, assetData] : m_assets)
		{
			names.push_back(name);
		}
		return names;
	}

	/// @brief 全アセットを登録解除する
	void clear() { m_assets.clear(); }

private:
	EmbeddedAssets() = default;
	EmbeddedAssets(const EmbeddedAssets&) = delete;
	EmbeddedAssets& operator=(const EmbeddedAssets&) = delete;

	std::unordered_map<std::string, EmbeddedAssetData> m_assets;
};

/// @brief 埋め込みアセット自動登録ヘルパーマクロ
/// @details 翻訳単位のstatic初期化を利用してアセットを自動登録する。
///
/// @code
/// // generated_assets.hpp
/// inline constexpr uint8_t my_model[] = { ... };
/// MITIRU_EMBED_ASSET("models/my_model.obj", my_model)
/// @endcode
// NOLINTBEGIN(bugprone-macro-parentheses,cppcoreguidelines-macro-usage)
#define MITIRU_EMBED_ASSET_CONCAT_IMPL(a, b) a##b
#define MITIRU_EMBED_ASSET_CONCAT(a, b) MITIRU_EMBED_ASSET_CONCAT_IMPL(a, b)

#define MITIRU_EMBED_ASSET(name, dataArray)                                           \
	namespace                                                                         \
	{                                                                                 \
	struct MITIRU_EMBED_ASSET_CONCAT(EmbedInit_, __LINE__)                            \
	{                                                                                 \
		MITIRU_EMBED_ASSET_CONCAT(EmbedInit_, __LINE__)()                             \
		{                                                                             \
			::mitiru::resource::EmbeddedAssets::instance().registerAsset(              \
				name, dataArray, sizeof(dataArray));                                   \
		}                                                                             \
	} MITIRU_EMBED_ASSET_CONCAT(s_embedInit_, __LINE__);                              \
	}
// NOLINTEND(bugprone-macro-parentheses,cppcoreguidelines-macro-usage)

} // namespace mitiru::resource
