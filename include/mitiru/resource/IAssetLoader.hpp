#pragma once

/// @file IAssetLoader.hpp
/// @brief アセットローダーコンセプト/インターフェース
/// @details アセットのロードと対応判定を行うコンセプト定義。
///          C++20 concepts を使用して型安全なローダーを制約する。

#include <any>
#include <concepts>
#include <memory>
#include <string_view>

namespace mitiru::resource
{

/// @brief アセットローダーコンセプト
/// @tparam T ローダー型
/// @details load() と canLoad() を持つ型を制約する。
///          load() は AssetType の shared_ptr を返す。
///
/// @code
/// struct TextureLoader {
///     using AssetType = Texture;
///     std::shared_ptr<Texture> load(std::string_view path) { ... }
///     bool canLoad(std::string_view path) const { ... }
/// };
/// static_assert(AssetLoader<TextureLoader>);
/// @endcode
template <typename T>
concept AssetLoader = requires(T loader, std::string_view path)
{
	typename T::AssetType;
	{ loader.load(path) } -> std::same_as<std::shared_ptr<typename T::AssetType>>;
	{ loader.canLoad(path) } -> std::same_as<bool>;
};

/// @brief 型消去されたアセットローダーインターフェース
/// @details AssetLoaderコンセプトを満たす具象型を型消去で保持するための基底。
class IAssetLoaderBase
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IAssetLoaderBase() = default;

	/// @brief 指定パスを読み込めるか判定する
	/// @param path ファイルパス
	/// @return 読み込み可能なら true
	[[nodiscard]] virtual bool canLoad(std::string_view path) const = 0;

	/// @brief 型消去されたロードを実行する
	/// @param path ファイルパス
	/// @return ロード結果（std::any に格納された shared_ptr）
	[[nodiscard]] virtual std::any loadAny(std::string_view path) = 0;
};

/// @brief 型付きアセットローダーラッパー
/// @tparam LoaderT AssetLoader コンセプトを満たす型
template <typename LoaderT>
	requires AssetLoader<LoaderT>
class TypedAssetLoader : public IAssetLoaderBase
{
public:
	using AssetType = typename LoaderT::AssetType;

	/// @brief コンストラクタ
	/// @param loader ローダーインスタンス
	explicit TypedAssetLoader(LoaderT loader)
		: m_loader(std::move(loader))
	{
	}

	/// @brief 指定パスを読み込めるか判定する
	/// @param path ファイルパス
	/// @return 読み込み可能なら true
	[[nodiscard]] bool canLoad(std::string_view path) const override
	{
		return m_loader.canLoad(path);
	}

	/// @brief アセットをロードする
	/// @param path ファイルパス
	/// @return アセットの shared_ptr
	[[nodiscard]] std::shared_ptr<AssetType> load(std::string_view path)
	{
		return m_loader.load(path);
	}

	/// @brief 型消去されたロードを実行する
	/// @param path ファイルパス
	/// @return ロード結果（std::any に shared_ptr<AssetType> を格納）
	[[nodiscard]] std::any loadAny(std::string_view path) override
	{
		return std::any(std::shared_ptr<AssetType>(m_loader.load(path)));
	}

private:
	LoaderT m_loader;  ///< 具象ローダー
};

} // namespace mitiru::resource
